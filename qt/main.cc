// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/transmission.h>

#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include <libtransmission-app/app.h>
#include <libtransmission-app/interop.h>
#include <libtransmission-app/startup-coordinator.h>

#include "Application.h"
#include "Prefs.h"
#include "Transports.h"
#include "Utils.h"
#include "VariantHelpers.h"

using namespace std::string_view_literals;

namespace
{

char const* const DisplayName = "transmission-qt";

auto constexpr FileArgsSeparator = "--"sv;
auto constexpr QtArgsSeparator = "---"sv;

using Arg = tr_option::Arg;
auto constexpr Opts = std::array<tr_option, 8>{ {
    { 'g', "config-dir", "Where to look for configuration files", "g", Arg::Required, "<path>" },
    { 'm', "minimized", "Start minimized in system tray", "m", Arg::None, nullptr },
    { 'p', "port", "Port to use when connecting to an existing session", "p", Arg::Required, "<port>" },
    { 'r', "remote", "Connect to an existing session at the specified hostname", "r", Arg::Required, "<host>" },
    { 'u', "username", "Username to use when connecting to an existing session", "u", Arg::Required, "<username>" },
    { 'v', "version", "Show version number and exit", "v", Arg::None, nullptr },
    { 'w', "password", "Password to use when connecting to an existing session", "w", Arg::Required, "<password>" },
    { 0, nullptr, nullptr, nullptr, Arg::None, nullptr },
} };
static_assert(Opts[std::size(Opts) - 2].val != 0);
} // namespace

namespace
{
char const* getUsage()
{
    return "Usage:\n"
           "  transmission-qt [options...] [[--] torrent files...] [--- Qt options...]";
}

// What this launch would have another instance do for it, if one is there.
[[nodiscard]] tr::interop::Intent intentOf(bool const standalone, bool const have_torrents)
{
    if (standalone)
    {
        return tr::interop::Intent::Standalone;
    }

    return have_torrents ? tr::interop::Intent::AddTorrents : tr::interop::Intent::Present;
}

// Arguments in AddMetainfo(s) wire form; see tr::interop::encode_metainfo_arg().
[[nodiscard]] std::vector<std::string> delegatableMetainfos(QStringList const& filenames)
{
    auto metainfos = std::vector<std::string>{};
    metainfos.reserve(std::size(filenames));

    for (auto const& filename : filenames)
    {
        if (auto encoded = tr::interop::encode_metainfo_arg(filename.toStdString()))
        {
            metainfos.push_back(std::move(*encoded));
        }
        else
        {
            // Report it here. This launch is being handed over, so it never reaches the code
            // that would otherwise report it, and saying nothing would look like every
            // argument landed.
            fmt::print(stderr, "Skipping '{:s}': not a torrent file, URL or magnet link.\n", filename.toStdString());
        }
    }

    return metainfos;
}

} // namespace

int tr_main(int argc, char** argv)
{
    transmission::app::init();
    trqt::variant_helpers::register_qt_converters();

    // parse the command-line arguments
    bool minimized = false;
    QString host;
    QString port;
    QString username;
    QString password;
    QString config_dir;
    QStringList filenames;

    int opt = 0;
    char const* optarg = nullptr;
    int file_args_start_idx = -1;
    int qt_args_start_idx = -1;
    while (file_args_start_idx < 0 && qt_args_start_idx < 0 &&
           (opt = tr_getopt(getUsage(), argc, static_cast<char const* const*>(argv), std::data(Opts), &optarg)) != TR_OPT_DONE)
    {
        switch (opt)
        {
        case 'g':
            config_dir = QString::fromUtf8(optarg);
            break;

        case 'p':
            port = QString::fromUtf8(optarg);
            break;

        case 'r':
            host = QString::fromUtf8(optarg);
            break;

        case 'u':
            username = QString::fromUtf8(optarg);
            break;

        case 'w':
            password = QString::fromUtf8(optarg);
            break;

        case 'm':
            minimized = true;
            break;

        case 'v':
            fmt::print("{:s} {:s}\n", DisplayName, LONG_VERSION_STRING);
            return 0;

        case TR_OPT_ERR:
            fmt::print(stderr, "Invalid option\n");
            tr_getopt_usage(DisplayName, getUsage(), std::data(Opts));
            return 1;

        default:
            if (optarg == FileArgsSeparator)
            {
                file_args_start_idx = tr_optind;
            }
            else if (optarg == QtArgsSeparator)
            {
                qt_args_start_idx = tr_optind;
            }
            else
            {
                filenames.append(QString::fromUtf8(optarg));
            }

            break;
        }
    }

    if (file_args_start_idx >= 0)
    {
        for (int i = file_args_start_idx; i < argc; ++i)
        {
            if (argv[i] == QtArgsSeparator)
            {
                qt_args_start_idx = i + 1;
                break;
            }

            filenames.push_back(QString::fromUtf8(argv[i]));
        }
    }

    // Resolve the config dir before asking whether a client is already running. The answer
    // is per config dir, and two clients on different dirs are separate instances,
    // not duplicates.
    if (config_dir.isNull())
    {
        config_dir = QString::fromStdString(tr_getDefaultConfigDir(TR_CONFIG_DIR_NAME));
    }

    // Spell the dir the way every client does,
    // so that `-g dir`, `-g dir/` and a symlink to it all name one instance.
    auto const config_dir_str = tr::interop::canonical_config_dir_created(config_dir.toStdString());
    config_dir = Utils::toQString(config_dir_str);
    auto startup_coordinator = std::make_unique<tr::interop::StartupCoordinator>(
        config_dir_str,
        tr::interop::make_transport(config_dir));
    // -r/-p/-u/-w name a session on another host, which is not this config dir's instance at all.
    // -m asks for a window that opens minimized, and presenting one would do the opposite.
    // These are read into prefs further down, past the point a handed-over launch returns from.
    auto const standalone = minimized || !host.isNull() || !port.isNull() || !username.isNull() || !password.isNull();

    auto const intent = intentOf(standalone, !filenames.isEmpty());

    if (auto const exit_code = startup_coordinator->delegate(intent, [&filenames] { return delegatableMetainfos(filenames); });
        exit_code)
    {
        return *exit_code;
    }

    // initialize the prefs
    auto prefs = std::make_unique<Prefs>(config_dir);

    if (!host.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_HOST, host);
    }

    if (!port.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_PORT, port.toUInt());
    }

    if (!username.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_USERNAME, username);
    }

    if (!password.isNull())
    {
        prefs->set(Prefs::SESSION_REMOTE_PASSWORD, password);
    }

    if (!host.isNull() || !port.isNull() || !username.isNull() || !password.isNull())
    {
        prefs->set(Prefs::SESSION_IS_REMOTE, true);
    }

    if (prefs->getBool(Prefs::START_MINIMIZED))
    {
        minimized = true;
    }

    // start as minimized only if the system tray present
    if (!prefs->getBool(Prefs::SHOW_TRAY_ICON))
    {
        minimized = false;
    }

    auto qt_argv = std::vector<char*>{ argv[0] };
    if (qt_args_start_idx >= 0)
    {
        qt_argv.insert(qt_argv.end(), &argv[qt_args_start_idx], &argv[argc]);
    }

    auto qt_argc = static_cast<int>(std::size(qt_argv));

    auto app = Application{ std::move(prefs),  std::move(startup_coordinator), minimized, config_dir, filenames, qt_argc,
                            std::data(qt_argv) };

    if (app.configDirIsContended())
    {
        return tr::interop::report_config_dir_busy(config_dir_str);
    }

    auto const result = QApplication::exec();

    // A launch that starts its session from the connection dialog only finds out here that the dir is held.
    if (app.configDirIsContended())
    {
        return tr::interop::report_config_dir_busy(config_dir_str);
    }

    return result;
}
