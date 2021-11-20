/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstdlib>
#include <cstring>
#include <list>
#include <string>

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600 /* needed for recursive locks. */
#endif
#ifndef __USE_UNIX98
#define __USE_UNIX98 /* some older Linuxes need it spelt out for them */
#endif

#ifdef __HAIKU__
#include <limits.h> /* PATH_MAX */
#endif

#ifdef _WIN32
#include <process.h> /* _beginthreadex(), _endthreadex() */
#include <windows.h>
#include <shlobj.h> /* SHGetKnownFolderPath(), FOLDERID_... */
#else
#include <unistd.h> /* getuid() */
#ifdef BUILD_MAC_CLIENT
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef __HAIKU__
#include <FindDirectory.h>
#endif
#include <pthread.h>
#endif

#include "transmission.h"
#include "file.h"
#include "log.h"
#include "platform.h"
#include "session.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

/***
****  THREADS
***/

#ifdef _WIN32
using tr_thread_id = DWORD;
#else
using tr_thread_id = pthread_t;
#endif

static tr_thread_id tr_getCurrentThread(void)
{
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

static bool tr_areThreadsEqual(tr_thread_id a, tr_thread_id b)
{
#ifdef _WIN32
    return a == b;
#else
    return pthread_equal(a, b) != 0;
#endif
}

/** @brief portability wrapper around OS-dependent threads */
struct tr_thread
{
    void (*func)(void*);
    void* arg;
    tr_thread_id thread;

#ifdef _WIN32
    HANDLE thread_handle;
#endif
};

bool tr_amInThread(tr_thread const* t)
{
    return tr_areThreadsEqual(tr_getCurrentThread(), t->thread);
}

#ifdef _WIN32
#define ThreadFuncReturnType unsigned WINAPI
#else
#define ThreadFuncReturnType void*
#endif

static ThreadFuncReturnType ThreadFunc(void* _t)
{
#ifndef _WIN32
    pthread_detach(pthread_self());
#endif

    auto* t = static_cast<tr_thread*>(_t);

    t->func(t->arg);

    tr_free(t);

#ifdef _WIN32
    _endthreadex(0);
    return 0;
#else
    return nullptr;
#endif
}

tr_thread* tr_threadNew(void (*func)(void*), void* arg)
{
    auto* t = static_cast<tr_thread*>(tr_new0(tr_thread, 1));

    t->func = func;
    t->arg = arg;

#ifdef _WIN32

    {
        unsigned int id;
        t->thread_handle = (HANDLE)_beginthreadex(nullptr, 0, &ThreadFunc, t, 0, &id);
        t->thread = (DWORD)id;
    }

#else

    pthread_create(&t->thread, nullptr, (void* (*)(void*))ThreadFunc, t);

#endif

    return t;
}

/***
****  PATHS
***/

#ifndef _WIN32
#include <pwd.h>
#endif

#ifdef _WIN32

static char* win32_get_known_folder_ex(REFKNOWNFOLDERID folder_id, DWORD flags)
{
    char* ret = nullptr;
    PWSTR path;

    if (SHGetKnownFolderPath(folder_id, flags | KF_FLAG_DONT_UNEXPAND, nullptr, &path) == S_OK)
    {
        ret = tr_win32_native_to_utf8(path, -1);
        CoTaskMemFree(path);
    }

    return ret;
}

static char* win32_get_known_folder(REFKNOWNFOLDERID folder_id)
{
    return win32_get_known_folder_ex(folder_id, KF_FLAG_DONT_VERIFY);
}

#endif

static char const* getHomeDir(void)
{
    static char const* home = nullptr;

    if (home == nullptr)
    {
        home = tr_env_get_string("HOME", nullptr);

        if (home == nullptr)
        {
#ifdef _WIN32

            home = win32_get_known_folder(FOLDERID_Profile);

#else

            struct passwd pwent;
            struct passwd* pw = nullptr;
            char buf[4096];
            getpwuid_r(getuid(), &pwent, buf, sizeof buf, &pw);
            if (pw != nullptr)
            {
                home = tr_strdup(pw->pw_dir);
            }

#endif
        }

        if (home == nullptr)
        {
            home = tr_strdup("");
        }
    }

    return home;
}

#if defined(__APPLE__) || defined(_WIN32)
#define RESUME_SUBDIR "Resume"
#define TORRENT_SUBDIR "Torrents"
#else
#define RESUME_SUBDIR "resume"
#define TORRENT_SUBDIR "torrents"
#endif

void tr_setConfigDir(tr_session* session, char const* configDir)
{
    session->configDir = tr_strdup(configDir);

    char* path = tr_buildPath(configDir, RESUME_SUBDIR, nullptr);
    tr_sys_dir_create(path, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);
    session->resumeDir = path;

    path = tr_buildPath(configDir, TORRENT_SUBDIR, nullptr);
    tr_sys_dir_create(path, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);
    session->torrentDir = path;
}

char const* tr_sessionGetConfigDir(tr_session const* session)
{
    return session->configDir;
}

char const* tr_getTorrentDir(tr_session const* session)
{
    return session->torrentDir;
}

char const* tr_getResumeDir(tr_session const* session)
{
    return session->resumeDir;
}

char const* tr_getDefaultConfigDir(char const* appname)
{
    static char const* s = nullptr;

    if (tr_str_is_empty(appname))
    {
        appname = "Transmission";
    }

    if (s == nullptr)
    {
        s = tr_env_get_string("TRANSMISSION_HOME", nullptr);

        if (s == nullptr)
        {
#ifdef __APPLE__

            s = tr_buildPath(getHomeDir(), "Library", "Application Support", appname, nullptr);

#elif defined(_WIN32)

            char* appdata = win32_get_known_folder(FOLDERID_LocalAppData);
            s = tr_buildPath(appdata, appname, nullptr);
            tr_free(appdata);

#elif defined(__HAIKU__)

            char buf[PATH_MAX];
            find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, buf, sizeof(buf));
            s = tr_buildPath(buf, appname, nullptr);

#else

            char* const xdg_config_home = tr_env_get_string("XDG_CONFIG_HOME", nullptr);

            if (xdg_config_home != nullptr)
            {
                s = tr_buildPath(xdg_config_home, appname, nullptr);
                tr_free(xdg_config_home);
            }
            else
            {
                s = tr_buildPath(getHomeDir(), ".config", appname, nullptr);
            }

#endif
        }
    }

    return s;
}

char const* tr_getDefaultDownloadDir(void)
{
    static char const* user_dir = nullptr;

    if (user_dir == nullptr)
    {
        /* figure out where to look for user-dirs.dirs */
        char* const config_home = tr_env_get_string("XDG_CONFIG_HOME", nullptr);

        auto const config_file = !tr_str_is_empty(config_home) ? tr_strvPath(config_home, "user-dirs.dirs") :
                                                                 tr_strvPath(getHomeDir(), ".config", "user-dirs.dirs");

        tr_free(config_home);

        /* read in user-dirs.dirs and look for the download dir entry */
        size_t content_len = 0;
        auto* const content = (char*)tr_loadFile(config_file.c_str(), &content_len, nullptr);

        if (content != nullptr && content_len > 0)
        {
            char const* key = "XDG_DOWNLOAD_DIR=\"";
            char* line = strstr(content, key);

            if (line != nullptr)
            {
                char* value = line + strlen(key);
                char* end = strchr(value, '"');

                if (end != nullptr)
                {
                    *end = '\0';

                    if (strncmp(value, "$HOME/", 6) == 0)
                    {
                        user_dir = tr_buildPath(getHomeDir(), value + 6, nullptr);
                    }
                    else if (strcmp(value, "$HOME") == 0)
                    {
                        user_dir = tr_strdup(getHomeDir());
                    }
                    else
                    {
                        user_dir = tr_strdup(value);
                    }
                }
            }
        }

#ifdef _WIN32

        if (user_dir == nullptr)
        {
            user_dir = win32_get_known_folder(FOLDERID_Downloads);
        }

#endif

        if (user_dir == nullptr)
        {
#ifdef __HAIKU__
            user_dir = tr_buildPath(getHomeDir(), "Desktop", nullptr);
#else
            user_dir = tr_buildPath(getHomeDir(), "Downloads", nullptr);
#endif
        }

        tr_free(content);
    }

    return user_dir;
}

/***
****
***/

static bool isWebClientDir(char const* path)
{
    auto tmp = tr_strvPath(path, "index.html");
    bool const ret = tr_sys_path_exists(tmp.c_str(), nullptr);
    tr_logAddInfo(_("Searching for web interface file \"%s\""), tmp.c_str());

    return ret;
}

char const* tr_getWebClientDir([[maybe_unused]] tr_session const* session)
{
    static char const* s = nullptr;

    if (s == nullptr)
    {
        s = tr_env_get_string("CLUTCH_HOME", nullptr);

        if (s == nullptr)
        {
            s = tr_env_get_string("TRANSMISSION_WEB_HOME", nullptr);
        }

        if (s == nullptr)
        {
#ifdef BUILD_MAC_CLIENT /* on Mac, look in the Application Support folder first, then in the app bundle. */

            /* Look in the Application Support folder */
            s = tr_buildPath(tr_sessionGetConfigDir(session), "public_html", nullptr);

            if (!isWebClientDir(s))
            {
                tr_free(const_cast<char*>(s));

                CFURLRef appURL = CFBundleCopyBundleURL(CFBundleGetMainBundle());
                CFStringRef appRef = CFURLCopyFileSystemPath(appURL, kCFURLPOSIXPathStyle);
                CFIndex const appStringLength = CFStringGetMaximumSizeOfFileSystemRepresentation(appRef);

                char* appString = static_cast<char*>(tr_malloc(appStringLength));
                bool const success = CFStringGetFileSystemRepresentation(appRef, appString, appStringLength);
                TR_ASSERT(success);

                CFRelease(appURL);
                CFRelease(appRef);

                /* Fallback to the app bundle */
                s = tr_buildPath(appString, "Contents", "Resources", "public_html", nullptr);

                if (!isWebClientDir(s))
                {
                    tr_free(const_cast<char*>(s));
                    s = nullptr;
                }

                tr_free(appString);
            }

#elif defined(_WIN32)

            /* Generally, Web interface should be stored in a Web subdir of
             * calling executable dir. */

            static KNOWNFOLDERID const* const known_folder_ids[] = {
                &FOLDERID_LocalAppData,
                &FOLDERID_RoamingAppData,
                &FOLDERID_ProgramData,
            };

            for (size_t i = 0; s == nullptr && i < TR_N_ELEMENTS(known_folder_ids); ++i)
            {
                char* dir = win32_get_known_folder(*known_folder_ids[i]);
                char* path = tr_buildPath(dir, "Transmission", "Web", nullptr);
                tr_free(dir);

                if (isWebClientDir(path))
                {
                    s = path;
                }
                else
                {
                    tr_free(path);
                }
            }

            if (s == nullptr) /* check calling module place */
            {
                wchar_t wide_module_path[MAX_PATH];
                GetModuleFileNameW(nullptr, wide_module_path, TR_N_ELEMENTS(wide_module_path));
                char* module_path = tr_win32_native_to_utf8(wide_module_path, -1);
                char* dir = tr_sys_path_dirname(module_path, nullptr);
                tr_free(module_path);

                if (dir != nullptr)
                {
                    char* path = tr_buildPath(dir, "Web", nullptr);
                    tr_free(dir);

                    if (isWebClientDir(path))
                    {
                        s = path;
                    }
                    else
                    {
                        tr_free(path);
                    }
                }
            }

#else /* everyone else, follow the XDG spec */

            auto candidates = std::list<std::string>{};

            /* XDG_DATA_HOME should be the first in the list of candidates */
            char* tmp = tr_env_get_string("XDG_DATA_HOME", nullptr);
            if (!tr_str_is_empty(tmp))
            {
                candidates.emplace_back(tmp);
            }
            else
            {
                candidates.emplace_back(tr_strvPath(getHomeDir(), ".local"sv, "share"sv));
            }
            tr_free(tmp);

            /* XDG_DATA_DIRS are the backup directories */
            {
                char const* const pkg = PACKAGE_DATA_DIR;
                char* xdg = tr_env_get_string("XDG_DATA_DIRS", nullptr);
                char const* fallback = "/usr/local/share:/usr/share";
                char* buf = tr_strdup_printf("%s:%s:%s", pkg, xdg != nullptr ? xdg : "", fallback);
                tr_free(xdg);
                tmp = buf;

                while (!tr_str_is_empty(tmp))
                {
                    char const* end = strchr(tmp, ':');

                    if (end != nullptr)
                    {
                        if (end - tmp > 1)
                        {
                            candidates.emplace_back(tmp, (size_t)(end - tmp));
                        }

                        tmp = (char*)end + 1;
                    }
                    else if (!tr_str_is_empty(tmp))
                    {
                        candidates.emplace_back(tmp);
                        break;
                    }
                }

                tr_free(buf);
            }

            /* walk through the candidates & look for a match */
            for (auto const& dir : candidates)
            {
                char* path = tr_buildPath(dir.c_str(), "transmission", "public_html", nullptr);
                bool const found = isWebClientDir(path);

                if (found)
                {
                    s = path;
                    break;
                }

                tr_free(path);
            }

#endif
        }
    }

    return s;
}

std::string tr_getSessionIdDir()
{
#ifndef _WIN32

    return std::string{ "/tmp"sv };

#else

    char* program_data_dir = win32_get_known_folder_ex(FOLDERID_ProgramData, KF_FLAG_CREATE);
    auto const result = tr_strvPath(program_data_dir, "Transmission");
    tr_free(program_data_dir);
    tr_sys_dir_create(result.c_str(), 0, 0, nullptr);
    return result;

#endif
}
