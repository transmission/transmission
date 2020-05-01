/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <stddef.h> /* offsetof */
#include <stdlib.h> /* realloc() */

#include <process.h> /* _beginthreadex() */

#include <windows.h>

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>

#define __LIBTRANSMISSION_WATCHDIR_MODULE__

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

#define log_error(...) (!tr_logLevelIsActive(TR_LOG_ERROR) ? (void)0 : \
    tr_logAddMessage(__FILE__, __LINE__, TR_LOG_ERROR, "watchdir:win32", __VA_ARGS__))

/***
****
***/

typedef struct tr_watchdir_win32
{
    tr_watchdir_backend base;

    HANDLE fd;
    OVERLAPPED overlapped;
    DWORD buffer[8 * 1024 / sizeof(DWORD)];
    evutil_socket_t notify_pipe[2];
    struct bufferevent* event;
    HANDLE thread;
}
tr_watchdir_win32;

#define BACKEND_UPCAST(b) ((tr_watchdir_win32*)(b))

#define WIN32_WATCH_MASK (FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE)

/***
****
***/

static BOOL tr_get_overlapped_result_ex(HANDLE handle, LPOVERLAPPED overlapped, LPDWORD bytes_transferred, DWORD timeout,
    BOOL alertable)
{
    typedef BOOL (WINAPI* impl_t)(HANDLE, LPOVERLAPPED, LPDWORD, DWORD, BOOL);

    static impl_t real_impl = NULL;
    static bool is_real_impl_valid = false;

    if (!is_real_impl_valid)
    {
        real_impl = (impl_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetOverlappedResultEx");
        is_real_impl_valid = true;
    }

    if (real_impl != NULL)
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
    tr_watchdir_t const handle = context;
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

        if (!ReadDirectoryChangesW(backend->fd, backend->buffer, sizeof(backend->buffer), FALSE, WIN32_WATCH_MASK, NULL,
            &backend->overlapped, NULL))
        {
            log_error("Failed to read directory changes");
            return 0;
        }
    }

    if (GetLastError() != ERROR_OPERATION_ABORTED)
    {
        log_error("Failed to wait for directory changes");
    }

    return 0;
}

static void tr_watchdir_win32_on_first_scan(evutil_socket_t fd UNUSED, short type UNUSED, void* context)
{
    tr_watchdir_t const handle = context;

    tr_watchdir_scan(handle, NULL);
}

static void tr_watchdir_win32_on_event(struct bufferevent* event, void* context)
{
    tr_watchdir_t const handle = context;
    size_t nread;
    size_t name_size = MAX_PATH * sizeof(WCHAR);
    char* buffer = tr_malloc(sizeof(FILE_NOTIFY_INFORMATION) + name_size);
    PFILE_NOTIFY_INFORMATION ev = (PFILE_NOTIFY_INFORMATION)buffer;
    size_t const header_size = offsetof(FILE_NOTIFY_INFORMATION, FileName);

    /* Read the size of the struct excluding name into buf. Guaranteed to have at
       least sizeof(*ev) available */
    while ((nread = bufferevent_read(event, ev, header_size)) != 0)
    {
        if (nread == (size_t)-1)
        {
            log_error("Failed to read event: %s", tr_strerror(errno));
            break;
        }

        if (nread != header_size)
        {
            log_error("Failed to read event: expected %zu, got %zu bytes.", header_size, nread);
            break;
        }

        size_t const nleft = ev->NextEntryOffset - nread;

        TR_ASSERT(ev->FileNameLength % sizeof(WCHAR) == 0);
        TR_ASSERT(ev->FileNameLength > 0);
        TR_ASSERT(ev->FileNameLength <= nleft);

        if (nleft > name_size)
        {
            name_size = nleft;
            buffer = tr_realloc(buffer, sizeof(FILE_NOTIFY_INFORMATION) + name_size);
            ev = (PFILE_NOTIFY_INFORMATION)buffer;
        }

        /* Consume entire name into buffer */
        if ((nread = bufferevent_read(event, buffer + header_size, nleft)) == (size_t)-1)
        {
            log_error("Failed to read name: %s", tr_strerror(errno));
            break;
        }

        if (nread != nleft)
        {
            log_error("Failed to read name: expected %zu, got %zu bytes.", nleft, nread);
            break;
        }

        if (ev->Action == FILE_ACTION_ADDED || ev->Action == FILE_ACTION_MODIFIED || ev->Action == FILE_ACTION_RENAMED_NEW_NAME)
        {
            char* name = tr_win32_native_to_utf8(ev->FileName, ev->FileNameLength / sizeof(WCHAR));

            if (name != NULL)
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

    if (backend == NULL)
    {
        return;
    }

    TR_ASSERT(backend->base.free_func == &tr_watchdir_win32_free);

    if (backend->fd != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(backend->fd, &backend->overlapped);
    }

    if (backend->thread != NULL)
    {
        WaitForSingleObject(backend->thread, INFINITE);
        CloseHandle(backend->thread);
    }

    if (backend->event != NULL)
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
    wchar_t* wide_path;
    tr_watchdir_win32* backend;

    backend = tr_new0(tr_watchdir_win32, 1);
    backend->base.free_func = &tr_watchdir_win32_free;
    backend->fd = INVALID_HANDLE_VALUE;
    backend->notify_pipe[0] = backend->notify_pipe[1] = TR_BAD_SOCKET;

    if ((wide_path = tr_win32_utf8_to_native(path, -1)) == NULL)
    {
        log_error("Failed to convert \"%s\" to native path", path);
        goto fail;
    }

    if ((backend->fd = CreateFileW(wide_path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL)) == INVALID_HANDLE_VALUE)
    {
        log_error("Failed to open directory \"%s\"", path);
        goto fail;
    }

    tr_free(wide_path);
    wide_path = NULL;

    backend->overlapped.Pointer = handle;

    if (!ReadDirectoryChangesW(backend->fd, backend->buffer, sizeof(backend->buffer), FALSE, WIN32_WATCH_MASK, NULL,
        &backend->overlapped, NULL))
    {
        log_error("Failed to read directory changes");
        goto fail;
    }

    if (evutil_socketpair(AF_INET, SOCK_STREAM, 0, backend->notify_pipe) == -1)
    {
        log_error("Failed to create notify pipe: %s", tr_strerror(errno));
        goto fail;
    }

    if ((backend->event = bufferevent_socket_new(tr_watchdir_get_event_base(handle), backend->notify_pipe[0], 0)) == NULL)
    {
        log_error("Failed to create event buffer: %s", tr_strerror(errno));
        goto fail;
    }

    bufferevent_setwatermark(backend->event, EV_READ, sizeof(FILE_NOTIFY_INFORMATION), 0);
    bufferevent_setcb(backend->event, &tr_watchdir_win32_on_event, NULL, NULL, handle);
    bufferevent_enable(backend->event, EV_READ);

    if ((backend->thread = (HANDLE)_beginthreadex(NULL, 0, &tr_watchdir_win32_thread, handle, 0, NULL)) == NULL)
    {
        log_error("Failed to create thread");
        goto fail;
    }

    /* Perform an initial scan on the directory */
    if (event_base_once(tr_watchdir_get_event_base(handle), -1, EV_TIMEOUT, &tr_watchdir_win32_on_first_scan, handle,
        NULL) == -1)
    {
        log_error("Failed to perform initial scan: %s", tr_strerror(errno));
    }

    return BACKEND_DOWNCAST(backend);

fail:
    tr_watchdir_win32_free(BACKEND_DOWNCAST(backend));
    tr_free(wide_path);
    return NULL;
}
