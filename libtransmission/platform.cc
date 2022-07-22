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

static std::string win32_get_known_folder_ex(REFKNOWNFOLDERID folder_id, DWORD flags)
{
    PWSTR path;

    if (SHGetKnownFolderPath(folder_id, flags | KF_FLAG_DONT_UNEXPAND, nullptr, &path) == S_OK)
    {
        auto* utf8_cstr = tr_win32_native_to_utf8(path, -1);
        auto ret = std::string{ utf8_cstr };
        tr_free(utf8_cstr);
        CoTaskMemFree(path);
        return ret;
    }

    return {};
}

static auto win32_get_known_folder(REFKNOWNFOLDERID folder_id)
{
    return win32_get_known_folder_ex(folder_id, KF_FLAG_DONT_VERIFY);
}

#endif

static std::string getHomeDir()
{
    if (auto* const dir = tr_env_get_string("HOME", nullptr); dir != nullptr)
    {
        auto ret = std::string{ dir };
        tr_free(dir);
        return ret;
    }

#ifdef _WIN32

    if (auto dir = win32_get_known_folder(FOLDERID_Profile); !std::empty(dir))
    {
        return dir;
    }

#else

    struct passwd pwent;
    struct passwd* pw = nullptr;
    char buf[4096];
    getpwuid_r(getuid(), &pwent, buf, sizeof buf, &pw);
    if (pw != nullptr)
    {
        return pw->pw_dir;
    }

#endif

    return {};
}

static std::string xdgConfigHome()
{
    if (auto* const dir = tr_env_get_string("XDG_CONFIG_HOME", nullptr); dir != nullptr)
    {
        auto ret = std::string{ dir };
        tr_free(dir);
        return ret;
    }

    return fmt::format("{:s}/.config"sv, getHomeDir());
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
    session->resume_dir = fmt::format("{:s}/{:s}"sv, config_dir, ResumeSubdir);
    session->torrent_dir = fmt::format("{:s}/{:s}"sv, config_dir, TorrentSubdir);
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

char* tr_getDefaultConfigDir(char const* appname)
{
    if (auto* dir = tr_env_get_string("TRANSMISSION_HOME", nullptr); dir != nullptr)
    {
        return dir;
    }

    if (tr_str_is_empty(appname))
    {
        appname = "Transmission";
    }

#ifdef __APPLE__

    return tr_strvDup(fmt::format("{:s}/Library/Application Support/{:s}"sv, getHomeDir(), appname));

#elif defined(_WIN32)

    auto const appdata = win32_get_known_folder(FOLDERID_LocalAppData);
    return tr_strvDup(fmt::format("{:s}/{:s}"sv, appdata, appname));

#elif defined(__HAIKU__)

    char buf[PATH_MAX];
    find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, buf, sizeof(buf));
    return tr_strvDup(fmt::format("{:s}/{:s}"sv, buf, appname);

#else

    return tr_strvDup(fmt::format("{:s}/{:s}"sv, xdgConfigHome(), appname));

#endif
}

static std::string getXdgEntryFromUserDirs(std::string_view key)
{
    auto content = std::vector<char>{};
    auto const filename = fmt::format("{:s}/{:s}"sv, xdgConfigHome(), "user-dirs.dirs"sv);
    if (!tr_sys_path_exists(filename) || !tr_loadFile(filename, content) || std::empty(content))
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
        val.replace(it, it + std::size(Home), getHomeDir());
    }

    return val;
}

char* tr_getDefaultDownloadDir()
{
    if (auto const dir = getXdgEntryFromUserDirs("XDG_DOWNLOAD_DIR"sv); !std::empty(dir))
    {
        return tr_strvDup(dir);
    }

#ifdef _WIN32
    if (auto dir = win32_get_known_folder(FOLDERID_Downloads); !std::empty(dir))
    {
        return tr_strvDup(dir);
    }
#endif

#ifdef __HAIKU__
    return tr_strvDup(fmt::format("{:s}/Desktop"sv, getHomeDir()));
#endif

    return tr_strvDup(fmt::format("{:s}/Downloads"sv, getHomeDir()));
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
            auto const dir = win32_get_known_folder(*known_folder_ids[i]);
            if (auto const path = tr_pathbuf{ dir, "/Transmission/Web"sv }; isWebClientDir(path))
            {
                s = tr_strvDup(path);
            }
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
            candidates.emplace_back(fmt::format("{:s}/.local/share"sv, getHomeDir()));
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

    auto const program_data_dir = win32_get_known_folder_ex(FOLDERID_ProgramData, KF_FLAG_CREATE);
    auto result = fmt::format("{:s}/Transmission"sv, program_data_dir);
    tr_sys_dir_create(result, 0, 0);
    return result;

#endif
}
