/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "Application.h"

#include <algorithm>
#include <array>
#include <ctime>

#include <QIcon>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QProcess>
#include <QRect>
#include <QSystemTrayIcon>
#include <QtDebug>

#ifdef QT_DBUS_LIB
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#endif

#include <libtransmission/tr-getopt.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "AddData.h"
#include "Formatter.h"
#include "InteropHelper.h"
#include "MainWindow.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "Session.h"
#include "TorrentModel.h"
#include "WatchDir.h"

namespace
{

std::array<tr_option, 8> const Opts =
{
    tr_option{ 'g', "config-dir", "Where to look for configuration files", "g", true, "<path>" },
    { 'm', "minimized", "Start minimized in system tray", "m", false, nullptr },
    { 'p', "port", "Port to use when connecting to an existing session", "p", true, "<port>" },
    { 'r', "remote", "Connect to an existing session at the specified hostname", "r", true, "<host>" },
    { 'u', "username", "Username to use when connecting to an existing session", "u", true, "<username>" },
    { 'v', "version", "Show version number and exit", "v", false, nullptr },
    { 'w', "password", "Password to use when connecting to an existing session", "w", true, "<password>" },
    { 0, nullptr, nullptr, nullptr, false, nullptr }
};

char const* getUsage()
{
    return "Usage:\n"
        "  transmission [OPTIONS...] [torrent files]";
}

enum
{
    STATS_REFRESH_INTERVAL_MSEC = 3000,
    SESSION_REFRESH_INTERVAL_MSEC = 3000,
    MODEL_REFRESH_INTERVAL_MSEC = 3000
};

bool loadTranslation(QTranslator& translator, QString const& name, QLocale const& locale, QStringList const& search_directories)
{
    for (QString const& directory : search_directories)
    {
        if (translator.load(locale, name, QStringLiteral("_"), directory))
        {
            return true;
        }
    }

    return false;
}

} // namespace

Application::Application(int& argc, char** argv) :
    QApplication(argc, argv),
    config_name_{QStringLiteral("transmission")},
    display_name_{QStringLiteral("transmission-qt")}
{
    setApplicationName(config_name_);
    loadTranslations();

#if defined(_WIN32) || defined(__APPLE__)

    if (QIcon::themeName().isEmpty())
    {
        QIcon::setThemeName(QStringLiteral("Faenza"));
    }

#endif

    // set the default icon
    QIcon icon = QIcon::fromTheme(QStringLiteral("transmission"));

    if (icon.isNull())
    {
        static std::array<int, 11> constexpr Sizes = { 16, 22, 24, 32, 48, 64, 72, 96, 128, 192, 256 };
        for (auto const size : Sizes)
        {
            icon.addPixmap(QPixmap(QStringLiteral(":/icons/transmission-%1.png").arg(size)));
        }
    }

    setWindowIcon(icon);

#ifdef __APPLE__
    setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    // parse the command-line arguments
    int c;
    bool minimized = false;
    char const* optarg;
    QString host;
    QString port;
    QString username;
    QString password;
    QString config_dir;
    QStringList filenames;

    while ((c = tr_getopt(getUsage(), argc, const_cast<char const**>(argv), Opts.data(), &optarg)) != TR_OPT_DONE)
    {
        switch (c)
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
            qInfo() << qPrintable(display_name_) << LONG_VERSION_STRING;
            quitLater();
            return;

        case TR_OPT_ERR:
            qWarning() << qPrintable(QObject::tr("Invalid option"));
            tr_getopt_usage(qPrintable(display_name_), getUsage(), Opts.data());
            quitLater();
            return;

        default:
            filenames.append(QString::fromUtf8(optarg));
            break;
        }
    }

    // try to delegate the work to an existing copy of Transmission
    // before starting ourselves...
    InteropHelper interop_client;

    if (interop_client.isConnected())
    {
        bool delegated = false;

        for (QString const& filename : filenames)
        {
            auto const a = AddData(filename);
            QString metainfo;

            switch (a.type)
            {
            case AddData::URL:
                metainfo = a.url.toString();
                break;

            case AddData::MAGNET:
                metainfo = a.magnet;
                break;

            case AddData::FILENAME:
            case AddData::METAINFO:
                metainfo = QString::fromUtf8(a.toBase64());
                break;

            default:
                break;
            }

            if (!metainfo.isEmpty() && interop_client.addMetainfo(metainfo))
            {
                delegated = true;
            }
        }

        if (delegated)
        {
            quitLater();
            return;
        }
    }

    // set the fallback config dir
    if (config_dir.isNull())
    {
        config_dir = QString::fromUtf8(tr_getDefaultConfigDir("transmission"));
    }

    // ensure our config directory exists
    QDir dir(config_dir);

    if (!dir.exists())
    {
        dir.mkpath(config_dir);
    }

    // is this the first time we've run transmission?
    bool const first_time = !dir.exists(QStringLiteral("settings.json"));

    // initialize the prefs
    prefs_ = std::make_unique<Prefs>(config_dir);

    if (!host.isNull())
    {
        prefs_->set(Prefs::SESSION_REMOTE_HOST, host);
    }

    if (!port.isNull())
    {
        prefs_->set(Prefs::SESSION_REMOTE_PORT, port.toUInt());
    }

    if (!username.isNull())
    {
        prefs_->set(Prefs::SESSION_REMOTE_USERNAME, username);
    }

    if (!password.isNull())
    {
        prefs_->set(Prefs::SESSION_REMOTE_PASSWORD, password);
    }

    if (!host.isNull() || !port.isNull() || !username.isNull() || !password.isNull())
    {
        prefs_->set(Prefs::SESSION_IS_REMOTE, true);
    }

    if (prefs_->getBool(Prefs::START_MINIMIZED))
    {
        minimized = true;
    }

    // start as minimized only if the system tray present
    if (!prefs_->getBool(Prefs::SHOW_TRAY_ICON))
    {
        minimized = false;
    }

    session_ = std::make_unique<Session>(config_dir, *prefs_);
    model_ = std::make_unique<TorrentModel>(*prefs_);
    window_ = std::make_unique<MainWindow>(*session_, *prefs_, *model_, minimized);
    watch_dir_ = std::make_unique<WatchDir>(*model_);

    connect(this, &QCoreApplication::aboutToQuit, this, &Application::saveGeometry);
    connect(model_.get(), &TorrentModel::torrentsAdded, this, &Application::onTorrentsAdded);
    connect(model_.get(), &TorrentModel::torrentsCompleted, this, &Application::onTorrentsCompleted);
    connect(model_.get(), &TorrentModel::torrentsEdited, this, &Application::onTorrentsEdited);
    connect(model_.get(), &TorrentModel::torrentsNeedInfo, this, &Application::onTorrentsNeedInfo);
    connect(prefs_.get(), &Prefs::changed, this, &Application::refreshPref);
    connect(session_.get(), &Session::sourceChanged, this, &Application::onSessionSourceChanged);
    connect(session_.get(), &Session::torrentsRemoved, model_.get(), &TorrentModel::removeTorrents);
    connect(session_.get(), &Session::torrentsUpdated, model_.get(), &TorrentModel::updateTorrents);
    connect(watch_dir_.get(), &WatchDir::torrentFileAdded, this, qOverload<QString const&>(&Application::addTorrent));

    // init from preferences
    for (auto const key : { Prefs::DIR_WATCH })
    {
        refreshPref(key);
    }

    QTimer* timer = &model_timer_;
    connect(timer, &QTimer::timeout, this, &Application::refreshTorrents);
    timer->setSingleShot(false);
    timer->setInterval(MODEL_REFRESH_INTERVAL_MSEC);
    timer->start();

    timer = &stats_timer_;
    connect(timer, &QTimer::timeout, session_.get(), &Session::refreshSessionStats);
    timer->setSingleShot(false);
    timer->setInterval(STATS_REFRESH_INTERVAL_MSEC);
    timer->start();

    timer = &session_timer_;
    connect(timer, &QTimer::timeout, session_.get(), &Session::refreshSessionInfo);
    timer->setSingleShot(false);
    timer->setInterval(SESSION_REFRESH_INTERVAL_MSEC);
    timer->start();

    maybeUpdateBlocklist();

    if (!first_time)
    {
        session_->restart();
    }
    else
    {
        window_->openSession();
    }

    if (!prefs_->getBool(Prefs::USER_HAS_GIVEN_INFORMED_CONSENT))
    {
        auto* dialog = new QMessageBox(QMessageBox::Information, QString(),
            tr("<b>Transmission is a file sharing program.</b>"), QMessageBox::Ok | QMessageBox::Cancel, window_.get());
        dialog->setInformativeText(tr("When you run a torrent, its data will be made available to others by means of upload. "
            "Any content you share is your sole responsibility."));
        dialog->button(QMessageBox::Ok)->setText(tr("I &Agree"));
        dialog->setDefaultButton(QMessageBox::Ok);
        dialog->setModal(true);

        connect(dialog, &QDialog::finished, this, &Application::consentGiven);

        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }

    for (QString const& filename : filenames)
    {
        addTorrent(filename);
    }

    InteropHelper::registerObject(this);
}

void Application::loadTranslations()
{
    QStringList const qt_qm_dirs = QStringList() << QLibraryInfo::location(QLibraryInfo::TranslationsPath) <<
#ifdef TRANSLATIONS_DIR
        QStringLiteral(TRANSLATIONS_DIR) <<
#endif
        (applicationDirPath() + QStringLiteral("/translations"));

    QStringList const app_qm_dirs = QStringList() <<
#ifdef TRANSLATIONS_DIR
        QStringLiteral(TRANSLATIONS_DIR) <<
#endif
        (applicationDirPath() + QStringLiteral("/translations"));

    auto const qt_file_name = QStringLiteral("qtbase");

    QLocale const locale;
    QLocale const english_locale(QLocale::English, QLocale::UnitedStates);

    if (loadTranslation(qt_translator_, qt_file_name, locale, qt_qm_dirs) ||
        loadTranslation(qt_translator_, qt_file_name, english_locale, qt_qm_dirs))
    {
        installTranslator(&qt_translator_);
    }

    if (loadTranslation(app_translator_, config_name_, locale, app_qm_dirs) ||
        loadTranslation(app_translator_, config_name_, english_locale, app_qm_dirs))
    {
        installTranslator(&app_translator_);
    }
}

void Application::quitLater() const
{
    QTimer::singleShot(0, this, SLOT(quit()));
}

void Application::onTorrentsEdited(torrent_ids_t const& ids) const
{
    // the backend's tr_info has changed, so reload those fields
    session_->initTorrents(ids);
}

QStringList Application::getNames(torrent_ids_t const& ids) const
{
    QStringList names;
    for (auto const& id : ids)
    {
        names.push_back(model_->getTorrentFromId(id)->name());
    }

    names.sort();
    return names;
}

void Application::onTorrentsAdded(torrent_ids_t const& ids) const
{
    if (prefs_->getBool(Prefs::SHOW_NOTIFICATION_ON_ADD))
    {
        auto const title = tr("Torrent(s) Added", nullptr, static_cast<int>(ids.size()));
        auto const body = getNames(ids).join(QStringLiteral("\n"));
        notifyApp(title, body);
    }
}

void Application::onTorrentsCompleted(torrent_ids_t const& ids) const
{
    if (prefs_->getBool(Prefs::SHOW_NOTIFICATION_ON_COMPLETE))
    {
        auto const title = tr("Torrent Completed", nullptr, static_cast<int>(ids.size()));
        auto const body = getNames(ids).join(QStringLiteral("\n"));
        notifyApp(title, body);
    }

    if (prefs_->getBool(Prefs::COMPLETE_SOUND_ENABLED))
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        beep();
#else
        QProcess::execute(prefs_->getString(Prefs::COMPLETE_SOUND_COMMAND));
#endif
    }
}

void Application::onTorrentsNeedInfo(torrent_ids_t const& ids) const
{
    if (!ids.empty())
    {
        session_->initTorrents(ids);
    }
}

/***
****
***/

void Application::consentGiven(int result) const
{
    if (result == QMessageBox::Ok)
    {
        prefs_->set<bool>(Prefs::USER_HAS_GIVEN_INFORMED_CONSENT, true);
    }
    else
    {
        quit();
    }
}

void Application::saveGeometry() const
{
    if (prefs_ != nullptr && window_ != nullptr)
    {
        auto const geometry = window_->geometry();
        prefs_->set(Prefs::MAIN_WINDOW_HEIGHT, std::max(100, geometry.height()));
        prefs_->set(Prefs::MAIN_WINDOW_WIDTH, std::max(100, geometry.width()));
        prefs_->set(Prefs::MAIN_WINDOW_X, geometry.x());
        prefs_->set(Prefs::MAIN_WINDOW_Y, geometry.y());
    }
}

/***
****
***/

void Application::refreshPref(int key) const
{
    switch (key)
    {
    case Prefs::BLOCKLIST_UPDATES_ENABLED:
        maybeUpdateBlocklist();
        break;

    case Prefs::DIR_WATCH:
    case Prefs::DIR_WATCH_ENABLED:
        watch_dir_->setPath(prefs_->getString(Prefs::DIR_WATCH),
            prefs_->getBool(Prefs::DIR_WATCH_ENABLED));
        break;

    default:
        break;
    }
}

void Application::maybeUpdateBlocklist() const
{
    if (!prefs_->getBool(Prefs::BLOCKLIST_UPDATES_ENABLED))
    {
        return;
    }

    QDateTime const last_updated_at = prefs_->getDateTime(Prefs::BLOCKLIST_DATE);
    QDateTime const next_update_at = last_updated_at.addDays(7);
    QDateTime const now = QDateTime::currentDateTime();

    if (now < next_update_at)
    {
        session_->updateBlocklist();
        prefs_->set(Prefs::BLOCKLIST_DATE, now);
    }
}

void Application::onSessionSourceChanged() const
{
    session_->initTorrents();
    session_->refreshSessionStats();
    session_->refreshSessionInfo();
}

void Application::refreshTorrents()
{
    // usually we just poll the torrents that have shown recent activity,
    // but we also periodically ask for updates on the others to ensure
    // nothing's falling through the cracks.
    time_t const now = time(nullptr);

    if (last_full_update_time_ + 60 >= now)
    {
        session_->refreshActiveTorrents();
    }
    else
    {
        last_full_update_time_ = now;
        session_->refreshAllTorrents();
    }
}

/***
****
***/

void Application::addTorrent(QString const& addme) const
{
    addTorrent(AddData(addme));
}

void Application::addTorrent(AddData const& addme) const
{
    if (addme.type == addme.NONE)
    {
        return;
    }

    if (!prefs_->getBool(Prefs::OPTIONS_PROMPT))
    {
        session_->addTorrent(addme);
    }
    else
    {
        auto* o = new OptionsDialog(*session_, *prefs_, addme, window_.get());
        o->show();
    }

    raise();
}

/***
****
***/

void Application::raise() const
{
    alert(window_.get());
}

bool Application::notifyApp(QString const& title, QString const& body) const
{
#ifdef QT_DBUS_LIB

    auto const dbus_service_name = QStringLiteral("org.freedesktop.Notifications");
    auto const dbus_interface_name = QStringLiteral("org.freedesktop.Notifications");
    auto const dbus_path = QStringLiteral("/org/freedesktop/Notifications");

    QDBusConnection bus = QDBusConnection::sessionBus();

    if (bus.isConnected())
    {
        QDBusMessage m =
            QDBusMessage::createMethodCall(dbus_service_name, dbus_path, dbus_interface_name, QStringLiteral("Notify"));
        QVariantList args;
        args.append(QStringLiteral("Transmission")); // app_name
        args.append(0U); // replaces_id
        args.append(QStringLiteral("transmission")); // icon
        args.append(title); // summary
        args.append(body); // body
        args.append(QStringList()); // actions - unused for plain passive popups
        args.append(QVariantMap({
            std::make_pair(QStringLiteral("category"), QVariant(QStringLiteral("transfer.complete")))
        })); // hints
        args.append(static_cast<int32_t>(-1)); // use the default timeout period
        m.setArguments(args);
        QDBusReply<quint32> const reply_msg = bus.call(m);

        if (reply_msg.isValid() && reply_msg.value() > 0)
        {
            return true;
        }
    }

#endif

    window_->trayIcon().showMessage(title, body);
    return true;
}

FaviconCache& Application::faviconCache()
{
    return favicons_;
}

/***
****
***/

int tr_main(int argc, char** argv)
{
    InteropHelper::initialize();

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    Application::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    Application::setAttribute(Qt::AA_UseHighDpiPixmaps);

    Application app(argc, argv);
    return QApplication::exec();
}
