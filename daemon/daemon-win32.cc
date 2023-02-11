// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <process.h> /* _beginthreadex() */
#include <windows.h>

#include <algorithm> /* std::max() */

#include <fmt/format.h>

#include "daemon.h"

#ifndef SERVICE_ACCEPT_PRESHUTDOWN
#define SERVICE_ACCEPT_PRESHUTDOWN 0x00000100
#endif
#ifndef SERVICE_CONTROL_PRESHUTDOWN
#define SERVICE_CONTROL_PRESHUTDOWN 0x0000000F
#endif

static LPCWSTR const service_name = L"TransmissionDaemon";

// If we can get rid of this global variable...
static tr_daemon* daemon;

// ...these becomes a good candidates for being converted to 'class tr_daemon' members.
static SERVICE_STATUS_HANDLE status_handle = nullptr;
static DWORD current_state = SERVICE_STOPPED;
static HANDLE service_thread = nullptr;
static HANDLE service_stop_thread = nullptr;

static void set_system_error(tr_error** error, DWORD code, char const* message)
{
    auto const system_message = tr_win32_format_message(code);
    tr_error_set(error, code, fmt::format(FMT_STRING("{:s} ({:#08x}): {:s})"), message, code, system_message));
}

static void do_log_system_error(char const* file, int line, tr_log_level level, DWORD code, char const* message)
{
    auto const system_message = tr_win32_format_message(code);
    tr_logAddMessage(file, line, level, "tr_daemon", fmt::format("[tr_daemon] {} ({:#x}): {}", message, code, system_message));
}

#define log_system_error(level, code, message) \
    do \
    { \
        DWORD const local_code = (code); \
\
        if (tr_logLevelIsActive((level))) \
        { \
            do_log_system_error(__FILE__, __LINE__, (level), local_code, (message)); \
        } \
    } while (0)

static BOOL WINAPI handle_console_ctrl(DWORD /*control_type*/)
{
    daemon->stop();
    return TRUE;
}

static void update_service_status(
    DWORD new_state,
    DWORD win32_exit_code,
    DWORD service_specific_exit_code,
    DWORD check_point,
    DWORD wait_hint)
{
    SERVICE_STATUS status;
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = new_state;
    status.dwControlsAccepted = new_state != SERVICE_RUNNING ?
        0 :
        SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PARAMCHANGE;
    status.dwWin32ExitCode = service_specific_exit_code == 0 ? win32_exit_code : ERROR_SERVICE_SPECIFIC_ERROR;
    status.dwServiceSpecificExitCode = service_specific_exit_code;
    status.dwCheckPoint = check_point;
    status.dwWaitHint = wait_hint;

    if (SetServiceStatus(status_handle, &status))
    {
        current_state = new_state;
    }
    else
    {
        log_system_error(TR_LOG_DEBUG, GetLastError(), "SetServiceStatus() failed");
    }
}

static unsigned int __stdcall service_stop_thread_main(void* param)
{
    daemon->stop();

    DWORD const sleep_time = 500;
    DWORD wait_time = (DWORD)(UINT_PTR)param;

    for (DWORD checkpoint = 2; WaitForSingleObject(service_thread, sleep_time) == WAIT_TIMEOUT; ++checkpoint)
    {
        wait_time = wait_time >= sleep_time ? wait_time - sleep_time : 0;
        update_service_status(SERVICE_STOP_PENDING, NO_ERROR, 0, checkpoint, std::max(wait_time, sleep_time * 2));
    }

    return 0;
}

static void stop_service(void)
{
    if (service_stop_thread != nullptr)
    {
        return;
    }

    DWORD const wait_time = 30 * 1000;

    update_service_status(SERVICE_STOP_PENDING, NO_ERROR, 0, 1, wait_time);

    service_stop_thread = (HANDLE)
        _beginthreadex(nullptr, 0, &service_stop_thread_main, (LPVOID)(UINT_PTR)wait_time, 0, nullptr);

    if (service_stop_thread == nullptr)
    {
        log_system_error(TR_LOG_DEBUG, GetLastError(), "_beginthreadex() failed, trying to stop synchronously");
        service_stop_thread_main((LPVOID)(UINT_PTR)wait_time);
    }
}

static DWORD WINAPI handle_service_ctrl(DWORD control_code, DWORD /*event_type*/, LPVOID /*event_data*/, LPVOID /*context*/)
{
    switch (control_code)
    {
    case SERVICE_CONTROL_PRESHUTDOWN:
    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
        stop_service();
        return NO_ERROR;

    case SERVICE_CONTROL_PARAMCHANGE:
        daemon->reconfigure();
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        update_service_status(current_state, NO_ERROR, 0, 0, 0);
        return NO_ERROR;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static unsigned int __stdcall service_thread_main(void* /*context*/)
{
    return daemon->start(false);
}

static VOID WINAPI service_main(DWORD /*argc*/, LPWSTR* /*argv*/)
{
    status_handle = RegisterServiceCtrlHandlerExW(service_name, &handle_service_ctrl, nullptr);

    if (status_handle == nullptr)
    {
        log_system_error(TR_LOG_ERROR, GetLastError(), "RegisterServiceCtrlHandlerEx() failed");
        return;
    }

    update_service_status(SERVICE_START_PENDING, NO_ERROR, 0, 1, 1000);

    service_thread = (HANDLE)_beginthreadex(nullptr, 0, &service_thread_main, nullptr, 0, nullptr);

    if (service_thread == nullptr)
    {
        log_system_error(TR_LOG_ERROR, GetLastError(), "_beginthreadex() failed");
        return;
    }

    update_service_status(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);

    if (WaitForSingleObject(service_thread, INFINITE) != WAIT_OBJECT_0)
    {
        log_system_error(TR_LOG_ERROR, GetLastError(), "WaitForSingleObject() failed");
    }

    if (service_stop_thread != nullptr)
    {
        WaitForSingleObject(service_stop_thread, INFINITE);
        CloseHandle(service_stop_thread);
    }

    DWORD exit_code;

    if (!GetExitCodeThread(service_thread, &exit_code))
    {
        exit_code = 1;
    }

    CloseHandle(service_thread);

    update_service_status(SERVICE_STOPPED, NO_ERROR, exit_code, 0, 0);
}

bool tr_daemon::setup_signals()
{
    return true;
}

bool tr_daemon::spawn(bool foreground, int* exit_code, tr_error** error)
{
    daemon = this;

    *exit_code = 1;

    if (foreground)
    {
        if (!SetConsoleCtrlHandler(&handle_console_ctrl, TRUE))
        {
            set_system_error(error, GetLastError(), "SetConsoleCtrlHandler() failed");
            return false;
        }

        *exit_code = start(true);
    }
    else
    {
        SERVICE_TABLE_ENTRY const service_table[] = {
            { (LPWSTR)service_name, &service_main },
            { nullptr, nullptr },
        };

        if (!StartServiceCtrlDispatcherW(service_table))
        {
            set_system_error(error, GetLastError(), "StartServiceCtrlDispatcher() failed");
            return false;
        }

        *exit_code = 0;
    }

    return true;
}
