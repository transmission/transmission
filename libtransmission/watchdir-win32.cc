// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> /* offsetof */
#include <cstdlib> /* realloc() */
#include <errno.h>

#include <process.h> /* _beginthreadex() */

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
#include "watchdir.h"
#include "watchdir-common.h"

/***
****
***/

struct tr_watchdir_win32
{
    tr_watchdir_backend base;

    HANDLE fd;
    OVERLAPPED overlapped;
    DWORD buffer[8 * 1024 / sizeof(DWORD)];
    evutil_socket_t notify_pipe[2];
    struct bufferevent* event;
    HANDLE thread;
};

#define BACKEND_UPCAST(b) ((tr_watchdir_win32*)(b))

#define WIN32_WATCH_MASK (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE)

/***
****
***/

static BOOL tr_get_overlapped_result_ex(
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

static unsigned int __stdcall tr_watchdir_win32_thread(void* context)
{
    auto const handle = static_cast<tr_watchdir_t>(context);
    tr_watchdir_win32* const backend = BACKEND_UPCAST(tr_watchdir_get_backend(handle));
    DWORD bytes_transferred;

    while (tr_get_overlapped_result_ex(backend->fd, &backend->overlapped, &bytes_transferred, INFINITE, FALSE))
    {
        PFILE_NOTIFY_INFORMATION info = (PFILE_NOTIFY_INFORMATION)backend->buffer;

        while (info->NextEntryOffset != 0)
        {
            *((BYTE**)&info) += info->NextEntryOffset;
        }

        info->NextEntryOffset = bytes_transferred - ((BYTE*)info - (BYTE*)backend->buffer);

        send(backend->notify_pipe[1], (char const*)backend->buffer, bytes_transferred, 0);

        if (!ReadDirectoryChangesW(
                backend->fd,
                backend->buffer,
                sizeof(backend->buffer),
                FALSE,
                WIN32_WATCH_MASK,
                nullptr,
                &backend->overlapped,
                nullptr))
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

static void tr_watchdir_win32_on_first_scan(evutil_socket_t /*fd*/, short /*type*/, void* context)
{
    auto const handle = static_cast<tr_watchdir_t>(context);

    tr_watchdir_scan(handle, nullptr);
}

static void tr_watchdir_win32_on_event(struct bufferevent* event, void* context)
{
    auto const handle = static_cast<tr_watchdir_t>(context);
    size_t nread;
    size_t name_size = MAX_PATH * sizeof(WCHAR);
    auto* buffer = static_cast<char*>(tr_malloc(sizeof(FILE_NOTIFY_INFORMATION) + name_size));
    PFILE_NOTIFY_INFORMATION ev = (PFILE_NOTIFY_INFORMATION)buffer;
    size_t const header_size = offsetof(FILE_NOTIFY_INFORMATION, FileName);

    /* Read the size of the struct excluding name into buf. Guaranteed to have at
       least sizeof(*ev) available */
    while ((nread = bufferevent_read(event, ev, header_size)) != 0)
    {
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
            buffer = static_cast<char*>(tr_realloc(buffer, sizeof(FILE_NOTIFY_INFORMATION) + name_size));
            ev = (PFILE_NOTIFY_INFORMATION)buffer;
        }

        /* Consume entire name into buffer */
        if ((nread = bufferevent_read(event, buffer + header_size, nleft)) == (size_t)-1)
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

        if (ev->Action == FILE_ACTION_ADDED || ev->Action == FILE_ACTION_MODIFIED || ev->Action == FILE_ACTION_RENAMED_NEW_NAME)
        {
            char* name = tr_win32_native_to_utf8(ev->FileName, ev->FileNameLength / sizeof(WCHAR));

            if (name != nullptr)
            {
                tr_watchdir_process(handle, name);
                tr_free(name);
            }
        }
    }

    tr_free(buffer);
}

static void tr_watchdir_win32_free(tr_watchdir_backend* backend_base)
{
    tr_watchdir_win32* const backend = BACKEND_UPCAST(backend_base);

    if (backend == nullptr)
    {
        return;
    }

    TR_ASSERT(backend->base.free_func == &tr_watchdir_win32_free);

    if (backend->fd != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(backend->fd, &backend->overlapped);
    }

    if (backend->thread != nullptr)
    {
        WaitForSingleObject(backend->thread, INFINITE);
        CloseHandle(backend->thread);
    }

    if (backend->event != nullptr)
    {
        bufferevent_free(backend->event);
    }

    if (backend->notify_pipe[0] != TR_BAD_SOCKET)
    {
        evutil_closesocket(backend->notify_pipe[0]);
    }

    if (backend->notify_pipe[1] != TR_BAD_SOCKET)
    {
        evutil_closesocket(backend->notify_pipe[1]);
    }

    if (backend->fd != INVALID_HANDLE_VALUE)
    {
        CloseHandle(backend->fd);
    }

    tr_free(backend);
}

tr_watchdir_backend* tr_watchdir_win32_new(tr_watchdir_t handle)
{
    char const* const path = tr_watchdir_get_path(handle);

    auto* const backend = tr_new0(tr_watchdir_win32, 1);
    backend->base.free_func = &tr_watchdir_win32_free;
    backend->fd = INVALID_HANDLE_VALUE;
    backend->notify_pipe[0] = backend->notify_pipe[1] = TR_BAD_SOCKET;

    wchar_t* wide_path = tr_win32_utf8_to_native(path, -1);
    if (wide_path == nullptr)
    {
        tr_logAddError(fmt::format(_("Couldn't convert '{path}' to native path"), fmt::arg("path", path)));
        goto fail;
    }

    if ((backend->fd = CreateFileW(
             wide_path,
             FILE_LIST_DIRECTORY,
             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
             nullptr,
             OPEN_EXISTING,
             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
             nullptr)) == INVALID_HANDLE_VALUE)
    {
        tr_logAddError(fmt::format(_("Couldn't read '{path}'"), fmt::arg("path", path)));
        goto fail;
    }

    tr_free(wide_path);
    wide_path = nullptr;

    backend->overlapped.Pointer = handle;

    if (!ReadDirectoryChangesW(
            backend->fd,
            backend->buffer,
            sizeof(backend->buffer),
            FALSE,
            WIN32_WATCH_MASK,
            nullptr,
            &backend->overlapped,
            nullptr))
    {
        tr_logAddError(fmt::format(_("Couldn't read '{path}'"), fmt::arg("path", path)));
        goto fail;
    }

    if (evutil_socketpair(AF_INET, SOCK_STREAM, 0, backend->notify_pipe) == -1)
    {
        auto const error_code = errno;
        tr_logAddError(fmt::format(
            _("Couldn't create pipe: {error} ({error_code})"),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code)));
        goto fail;
    }

    if ((backend->event = bufferevent_socket_new(tr_watchdir_get_event_base(handle), backend->notify_pipe[0], 0)) == nullptr)
    {
        auto const error_code = errno;
        tr_logAddError(fmt::format(
            _("Couldn't create event: {error} ({error_code})"),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code)));
        goto fail;
    }

    bufferevent_setwatermark(backend->event, EV_READ, sizeof(FILE_NOTIFY_INFORMATION), 0);
    bufferevent_setcb(backend->event, &tr_watchdir_win32_on_event, nullptr, nullptr, handle);
    bufferevent_enable(backend->event, EV_READ);

    if ((backend->thread = (HANDLE)_beginthreadex(nullptr, 0, &tr_watchdir_win32_thread, handle, 0, nullptr)) == nullptr)
    {
        tr_logAddError(_("Couldn't create thread"));
        goto fail;
    }

    /* Perform an initial scan on the directory */
    if (event_base_once(
            tr_watchdir_get_event_base(handle),
            -1,
            EV_TIMEOUT,
            &tr_watchdir_win32_on_first_scan,
            handle,
            nullptr) == -1)
    {
        auto const error_code = errno;
        tr_logAddError(fmt::format(
            _("Couldn't scan '{path}': {error} ({error_code})"),
            fmt::arg("path", path),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code)));
    }

    return BACKEND_DOWNCAST(backend);

fail:
    tr_watchdir_win32_free(BACKEND_DOWNCAST(backend));
    tr_free(wide_path);
    return nullptr;
}
