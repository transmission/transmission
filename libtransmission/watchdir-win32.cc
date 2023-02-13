// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstddef> // for offsetof
#include <memory>
#include <utility>

#include <process.h> // for _beginthreadex()

#include <windows.h>

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>

#include <fmt/core.h>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"
#include "log.h"
#include "net.h"
#include "tr-assert.h"
#include "utils.h"
#include "watchdir-base.h"

namespace libtransmission
{
namespace
{

BOOL tr_get_overlapped_result_ex(
    HANDLE handle,
    LPOVERLAPPED overlapped,
    LPDWORD bytes_transferred,
    DWORD timeout,
    BOOL alertable)
{
    using impl_t = BOOL(WINAPI*)(HANDLE, LPOVERLAPPED, LPDWORD, DWORD, BOOL);

    static impl_t real_impl = nullptr;
    static bool is_real_impl_valid = false;

    if (!is_real_impl_valid)
    {
        real_impl = (impl_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetOverlappedResultEx");
        is_real_impl_valid = true;
    }

    if (real_impl != nullptr)
    {
        return (*real_impl)(handle, overlapped, bytes_transferred, timeout, alertable);
    }

    DWORD const wait_result = WaitForSingleObjectEx(handle, timeout, alertable);

    if (wait_result == WAIT_FAILED)
    {
        return FALSE;
    }

    if (wait_result == WAIT_IO_COMPLETION || wait_result == WAIT_TIMEOUT)
    {
        SetLastError(wait_result);
        return FALSE;
    }

    TR_ASSERT(wait_result == WAIT_OBJECT_0);

    return GetOverlappedResult(handle, overlapped, bytes_transferred, FALSE);
}

class Win32Watchdir final : public impl::BaseWatchdir
{
public:
    Win32Watchdir(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        struct event_base* event_base)
        : BaseWatchdir{ dirname, std::move(callback), timer_maker }
    {
        init(event_base);
        scan();
    }

    Win32Watchdir(Win32Watchdir&&) = delete;
    Win32Watchdir(Win32Watchdir const&) = delete;
    Win32Watchdir& operator=(Win32Watchdir&&) = delete;
    Win32Watchdir& operator=(Win32Watchdir const&) = delete;

    ~Win32Watchdir() override
    {
        if (fd_ != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(fd_, &overlapped_);
        }

        if (thread_ != nullptr)
        {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
        }

        if (event_ != nullptr)
        {
            bufferevent_free(event_);
        }

        if (notify_pipe_[0] != TR_BAD_SOCKET)
        {
            evutil_closesocket(notify_pipe_[0]);
        }

        if (notify_pipe_[1] != TR_BAD_SOCKET)
        {
            evutil_closesocket(notify_pipe_[1]);
        }

        if (fd_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(fd_);
        }
    }

private:
    static auto constexpr Win32WatchMask =
        (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);

    void init(struct event_base* event_base)
    {
        tr_net_init();

        auto const path = dirname();
        auto const wide_path = tr_win32_utf8_to_native(path);
        if (std::empty(wide_path))
        {
            tr_logAddError(fmt::format(_("Couldn't convert '{path}' to native path"), fmt::arg("path", path)));
            return;
        }

        if ((fd_ = CreateFileW(
                 wide_path.c_str(),
                 FILE_LIST_DIRECTORY,
                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                 nullptr,
                 OPEN_EXISTING,
                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                 nullptr)) == INVALID_HANDLE_VALUE)
        {
            tr_logAddError(fmt::format(_("Couldn't read '{path}'"), fmt::arg("path", path)));
            return;
        }

        overlapped_.Pointer = this;

        if (!ReadDirectoryChangesW(fd_, buffer_, sizeof(buffer_), false, Win32WatchMask, nullptr, &overlapped_, nullptr))
        {
            tr_logAddError(fmt::format(_("Couldn't read '{path}'"), fmt::arg("path", path)));
            return;
        }

        if (evutil_socketpair(AF_INET, SOCK_STREAM, 0, std::data(notify_pipe_)) == -1)
        {
            auto const error_code = errno;
            tr_logAddError(fmt::format(
                _("Couldn't create pipe: {error} ({error_code})"),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
            return;
        }

        event_ = bufferevent_socket_new(event_base, notify_pipe_[0], 0);
        if (event_ == nullptr)
        {
            auto const error_code = errno;
            tr_logAddError(fmt::format(
                _("Couldn't create event: {error} ({error_code})"),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
            return;
        }

        bufferevent_setwatermark(event_, EV_READ, sizeof(FILE_NOTIFY_INFORMATION), 0);
        bufferevent_setcb(event_, &Win32Watchdir::onBufferEvent, nullptr, nullptr, this);
        bufferevent_enable(event_, EV_READ);

        thread_ = (HANDLE)_beginthreadex(nullptr, 0, Win32Watchdir::staticThreadFunc, this, 0, nullptr);
        if (thread_ == nullptr)
        {
            tr_logAddError(_("Couldn't create thread"));
            return;
        }
    }

    static unsigned int __stdcall staticThreadFunc(void* vself)
    {
        return static_cast<Win32Watchdir*>(vself)->threadFunc();
    }

    unsigned int threadFunc()
    {
        DWORD bytes_transferred;

        while (tr_get_overlapped_result_ex(fd_, &overlapped_, &bytes_transferred, INFINITE, FALSE))
        {
            PFILE_NOTIFY_INFORMATION info = (PFILE_NOTIFY_INFORMATION)buffer_;

            while (info->NextEntryOffset != 0)
            {
                *((BYTE**)&info) += info->NextEntryOffset;
            }

            info->NextEntryOffset = bytes_transferred - ((BYTE*)info - (BYTE*)buffer_);

            send(notify_pipe_[1], (char const*)buffer_, bytes_transferred, 0);

            if (!ReadDirectoryChangesW(fd_, buffer_, sizeof(buffer_), FALSE, Win32WatchMask, nullptr, &overlapped_, nullptr))
            {
                tr_logAddError(_("Couldn't read directory changes"));
                return 0;
            }
        }

        if (GetLastError() != ERROR_OPERATION_ABORTED)
        {
            tr_logAddError(_("Couldn't wait for directory changes"));
        }

        return 0;
    }

    static void onFirstScan(evutil_socket_t /*unused*/, short /*unused*/, void* vself)
    {
        static_cast<Win32Watchdir*>(vself)->scan();
    }

    static void onBufferEvent(struct bufferevent* event, void* vself)
    {
        static_cast<Win32Watchdir*>(vself)->processBufferEvent(event);
    }

    void processBufferEvent(struct bufferevent* event)
    {
        size_t name_size = MAX_PATH * sizeof(WCHAR);

        auto buffer = std::vector<char>{};
        buffer.resize(sizeof(FILE_NOTIFY_INFORMATION) + name_size);
        PFILE_NOTIFY_INFORMATION ev = (PFILE_NOTIFY_INFORMATION)std::data(buffer);

        size_t const header_size = offsetof(FILE_NOTIFY_INFORMATION, FileName);

        // Read the size of the struct excluding name into buf.
        // Guaranteed to have at least sizeof(*ev) available
        for (;;)
        {
            auto nread = bufferevent_read(event, ev, header_size);
            if (nread == 0)
            {
                break;
            }

            if (nread == (size_t)-1)
            {
                auto const error_code = errno;
                tr_logAddError(fmt::format(
                    _("Couldn't read event: {error} ({error_code})"),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
                break;
            }

            if (nread != header_size)
            {
                tr_logAddError(fmt::format(
                    _("Couldn't read event: expected {expected_size}, got {actual_size}"),
                    fmt::arg("expected_size", header_size),
                    fmt::arg("actual_size", nread)));
                break;
            }

            size_t const nleft = ev->NextEntryOffset - nread;

            TR_ASSERT(ev->FileNameLength % sizeof(WCHAR) == 0);
            TR_ASSERT(ev->FileNameLength > 0);
            TR_ASSERT(ev->FileNameLength <= nleft);

            if (nleft > name_size)
            {
                name_size = nleft;
                buffer.resize(sizeof(FILE_NOTIFY_INFORMATION) + name_size);
                ev = (PFILE_NOTIFY_INFORMATION)std::data(buffer);
            }

            // consume entire name into buffer
            nread = bufferevent_read(event, &buffer[header_size], nleft);
            if (nread == (size_t)-1)
            {
                auto const error_code = errno;
                tr_logAddError(fmt::format(
                    _("Couldn't read filename: {error} ({error_code})"),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
                break;
            }

            if (nread != nleft)
            {
                tr_logAddError(fmt::format(
                    _("Couldn't read filename: expected {expected_size}, got {actual_size}"),
                    fmt::arg("expected_size", nleft),
                    fmt::arg("actual_size", nread)));
                break;
            }

            if (ev->Action == FILE_ACTION_ADDED || ev->Action == FILE_ACTION_MODIFIED ||
                ev->Action == FILE_ACTION_RENAMED_NEW_NAME)
            {
                if (auto const name = tr_win32_native_to_utf8({ ev->FileName, ev->FileNameLength / sizeof(WCHAR) });
                    !std::empty(name))
                {
                    processFile(name);
                }
            }
        }
    }

    HANDLE fd_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_ = {};
    DWORD buffer_[8 * 1024 / sizeof(DWORD)];
    std::array<evutil_socket_t, 2> notify_pipe_{ static_cast<evutil_socket_t>(-1), static_cast<evutil_socket_t>(-1) };
    struct bufferevent* event_ = nullptr;
    HANDLE thread_ = {};
};

} // namespace

std::unique_ptr<Watchdir> Watchdir::create(
    std::string_view dirname,
    Callback callback,
    TimerMaker& timer_maker,
    struct event_base* event_base)
{
    return std::make_unique<Win32Watchdir>(dirname, std::move(callback), timer_maker, event_base);
}

} // namespace libtransmission
