// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstring>
#include <list>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef __HAIKU__
#include <limits.h> /* PATH_MAX */
#endif

#ifdef _WIN32
#include <process.h> /* _beginthreadex(), _endthreadex() */
#include <windows.h>
#include <shlobj.h> /* SHGetKnownFolderPath(), FOLDERID_... */
#else
#include <pwd.h>
#include <unistd.h> /* getuid() */
#endif

#ifdef BUILD_MAC_CLIENT
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef __HAIKU__
#include <FindDirectory.h>
#endif

#include <fmt/format.h>

#include "transmission.h"

#include "file.h"
#include "log.h"
#include "platform.h"
#include "session.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

/***
****  PATHS
***/

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

static char const* getHomeDir()
{
    static char const* home = nullptr;

    if (home == nullptr)
    {
        home = tr_env_get_string("HOME", nullptr);
    }

#ifdef _WIN32

    if (home == nullptr)
    {
        home = win32_get_known_folder(FOLDERID_Profile);
    }

#else

    if (home == nullptr)
    {
        struct passwd pwent;
        struct passwd* pw = nullptr;
        char buf[4096];
        getpwuid_r(getuid(), &pwent, buf, sizeof buf, &pw);
        if (pw != nullptr)
        {
            home = tr_strdup(pw->pw_dir);
        }
    }

#endif

    if (home == nullptr)
    {
        home = tr_strdup("");
    }

    return home;
}

void tr_setConfigDir(tr_session* session, std::string_view config_dir)
{
#if defined(__APPLE__) || defined(_WIN32)
    auto constexpr ResumeSubdir = "Resume"sv;
    auto constexpr TorrentSubdir = "Torrents"sv;
#else
    auto constexpr ResumeSubdir = "resume"sv;
    auto constexpr TorrentSubdir = "torrents"sv;
#endif

    session->config_dir = config_dir;
    session->resume_dir = tr_strvPath(config_dir, ResumeSubdir);
    session->torrent_dir = tr_strvPath(config_dir, TorrentSubdir);
    tr_sys_dir_create(session->resume_dir, TR_SYS_DIR_CREATE_PARENTS, 0777);
    tr_sys_dir_create(session->torrent_dir, TR_SYS_DIR_CREATE_PARENTS, 0777);
}

char const* tr_sessionGetConfigDir(tr_session const* session)
{
    return session->config_dir.c_str();
}

char const* tr_getTorrentDir(tr_session const* session)
{
    return session->torrent_dir.c_str();
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

            s = tr_strvDup(tr_strvPath(getHomeDir(), "Library", "Application Support", appname));

#elif defined(_WIN32)

            char* appdata = win32_get_known_folder(FOLDERID_LocalAppData);
            s = tr_strvDup(tr_strvPath(appdata, appname));
            tr_free(appdata);

#elif defined(__HAIKU__)

            char buf[PATH_MAX];
            find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, buf, sizeof(buf));
            s = tr_strvDup(tr_strvPath(buf, appname));

#else

            char* const xdg_config_home = tr_env_get_string("XDG_CONFIG_HOME", nullptr);

            if (xdg_config_home != nullptr)
            {
                s = tr_strvDup(tr_strvPath(xdg_config_home, appname));
                tr_free(xdg_config_home);
            }
            else
            {
                s = tr_strvDup(tr_strvPath(getHomeDir(), ".config", appname));
            }

#endif
        }
    }

    return s;
}

static std::string getUserDirsFilename()
{
    char* const config_home = tr_env_get_string("XDG_CONFIG_HOME", nullptr);
    auto config_file = !tr_str_is_empty(config_home) ? tr_strvPath(config_home, "user-dirs.dirs") :
                                                       tr_strvPath(getHomeDir(), ".config", "user-dirs.dirs");

    tr_free(config_home);
    return config_file;
}

static std::string getXdgEntryFromUserDirs(std::string_view key)
{
    auto content = std::vector<char>{};
    if (!tr_loadFile(getUserDirsFilename(), content) && std::empty(content))
    {
        return {};
    }

    // search for key="val" and extract val
    auto const search = fmt::format(FMT_STRING("{:s}=\""), key);
    auto begin = std::search(std::begin(content), std::end(content), std::begin(search), std::end(search));
    if (begin == std::end(content))
    {
        return {};
    }
    std::advance(begin, std::size(search));
    auto const end = std::find(begin, std::end(content), '"');
    if (end == std::end(content))
    {
        return {};
    }
    auto val = std::string{ begin, end };

    // if val contains "$HOME", replace that with getHomeDir()
    auto constexpr Home = "$HOME"sv;
    if (auto const it = std::search(std::begin(val), std::end(val), std::begin(Home), std::end(Home)); it != std::end(val))
    {
        val.replace(it, it + std::size(Home), std::string_view{ getHomeDir() });
    }

    return val;
}

char const* tr_getDefaultDownloadDir()
{
    static char const* user_dir = nullptr;

    if (user_dir == nullptr)
    {
        if (auto const xdg_user_dir = getXdgEntryFromUserDirs("XDG_DOWNLOAD_DIR"sv); !std::empty(xdg_user_dir))
        {
            user_dir = tr_strvDup(xdg_user_dir);
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
            user_dir = tr_strvDup(tr_strvPath(getHomeDir(), "Desktop"));
#else
            user_dir = tr_strvDup(tr_strvPath(getHomeDir(), "Downloads"));
#endif
        }
    }

    return user_dir;
}

/***
****
***/

static bool isWebClientDir(std::string_view path)
{
    auto const filename = tr_pathbuf{ path, '/', "index.html"sv };
    bool const found = tr_sys_path_exists(filename);
    tr_logAddTrace(fmt::format(FMT_STRING("Searching for web interface file '{:s}'"), filename));
    return found;
}

char const* tr_getWebClientDir([[maybe_unused]] tr_session const* session)
{
    static char const* s = nullptr;

    if (s == nullptr)
    {
        s = tr_env_get_string("CLUTCH_HOME", nullptr);
    }

    if (s == nullptr)
    {
        s = tr_env_get_string("TRANSMISSION_WEB_HOME", nullptr);
    }

#ifdef BUILD_MAC_CLIENT

    // look in the Application Support folder
    if (s == nullptr)
    {
        if (auto path = tr_pathbuf{ session->config_dir, "/public_html"sv }; isWebClientDir(path))
        {
            s = tr_strvDup(path);
        }
    }

    // look in the resource bundle
    if (s == nullptr)
    {
        auto app_url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
        auto app_ref = CFURLCopyFileSystemPath(app_url, kCFURLPOSIXPathStyle);
        auto const buflen = CFStringGetMaximumSizeOfFileSystemRepresentation(app_ref);
        auto buf = std::vector<char>(buflen, '\0');
        bool const success = CFStringGetFileSystemRepresentation(app_ref, std::data(buf), std::size(buf));
        TR_ASSERT(success);
        CFRelease(app_url);
        CFRelease(app_ref);

        if (auto const path = tr_pathbuf{ std::string_view{ std::data(buf) }, "/Contents/Resources/public_html"sv };
            isWebClientDir(path))
        {
            s = tr_strvDup(path);
        }
    }

#elif defined(_WIN32)

    if (s == nullptr)
    {
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

            if (auto const path = tr_pathbuf{ std::string_view{ dir }, "/Transmission/Web"sv }; isWebClientDir(path))
            {
                s = tr_strvDup(path);
            }

            tr_free(dir);
        }
    }

    if (s == nullptr) /* check calling module place */
    {
        wchar_t wide_module_path[MAX_PATH];
        GetModuleFileNameW(nullptr, wide_module_path, TR_N_ELEMENTS(wide_module_path));
        char* module_path = tr_win32_native_to_utf8(wide_module_path, -1);

        if (auto const dir = tr_sys_path_dirname(module_path); !std::empty(dir))
        {
            if (auto const path = tr_pathbuf{ dir, "/Web"sv }; isWebClientDir(path))
            {
                s = tr_strvDup(path);
            }
        }

        tr_free(module_path);
    }

#else // everyone else, follow the XDG spec

    if (s == nullptr)
    {
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
            auto* xdg = tr_env_get_string("XDG_DATA_DIRS", "");
            auto const buf = fmt::format(FMT_STRING("{:s}:{:s}:/usr/local/share:/usr/share"), pkg, xdg);
            tr_free(xdg);

            auto sv = std::string_view{ buf };
            auto token = std::string_view{};
            while (tr_strvSep(&sv, &token, ':'))
            {
                token = tr_strvStrip(token);
                if (!std::empty(token))
                {
                    candidates.emplace_back(token);
                }
            }
        }

        /* walk through the candidates & look for a match */
        for (auto const& dir : candidates)
        {
            if (auto const path = tr_pathbuf{ dir, "/transmission/public_html"sv }; isWebClientDir(path))
            {
                s = tr_strvDup(path);
                break;
            }
        }
    }

#endif

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
    tr_sys_dir_create(result, 0, 0);
    return result;

#endif
}
