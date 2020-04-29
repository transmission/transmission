/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <ctime>
#include <iostream>

#include <QIcon>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QProcess>
#include <QRect>
#include <QSystemTrayIcon>

#ifdef QT_DBUS_LIB
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#endif

#include <libtransmission/transmission.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "AddData.h"
#include "Application.h"
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

QLatin1String const MY_CONFIG_NAME("transmission");
QLatin1String const MY_READABLE_NAME("transmission-qt");

tr_option const opts[] =
{
    { 'g', "config-dir", "Where to look for configuration files", "g", true, "<path>" },
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

bool loadTranslation(QTranslator& translator, QString const& name, QLocale const& locale, QStringList const& searchDirectories)
{
    for (QString const& directory : searchDirectories)
    {
        if (translator.load(locale, name, QLatin1String("_"), directory))
        {
            return true;
        }
    }

    return false;
}

} // namespace

Application::Application(int& argc, char** argv) :
    QApplication(argc, argv),
    myPrefs(nullptr),
    mySession(nullptr),
    myModel(nullptr),
    myWindow(nullptr),
    myWatchDir(nullptr),
    myLastFullUpdateTime(0)
{
    setApplicationName(MY_CONFIG_NAME);
    loadTranslations();

    Formatter::initUnits();

#if defined(_WIN32) || defined(__APPLE__)

    if (QIcon::themeName().isEmpty())
    {
        QIcon::setThemeName(QLatin1String("Faenza"));
    }

#endif

    // set the default icon
    QIcon icon = QIcon::fromTheme(QLatin1String("transmission"));

    if (icon.isNull())
    {
        QList<int> sizes;
        sizes << 16 << 22 << 24 << 32 << 48 << 64 << 72 << 96 << 128 << 192 << 256;

        for (int const size : sizes)
        {
            icon.addPixmap(QPixmap(QString::fromLatin1(":/icons/transmission-%1.png").arg(size)));
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
    QString configDir;
    QStringList filenames;

    while ((c = tr_getopt(getUsage(), argc, const_cast<char const**>(argv), opts, &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'g':
            configDir = QString::fromUtf8(optarg);
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
            std::cerr << MY_READABLE_NAME.latin1() << ' ' << LONG_VERSION_STRING << std::endl;
            quitLater();
            return;

        case TR_OPT_ERR:
            std::cerr << qPrintable(QObject::tr("Invalid option")) << std::endl;
            tr_getopt_usage(MY_READABLE_NAME.latin1(), getUsage(), opts);
            quitLater();
            return;

        default:
            filenames.append(QString::fromUtf8(optarg));
            break;
        }
    }

    // try to delegate the work to an existing copy of Transmission
    // before starting ourselves...
    InteropHelper interopClient;

    if (interopClient.isConnected())
    {
        bool delegated = false;

        for (QString const& filename : filenames)
        {
            QString metainfo;

            AddData a(filename);

            switch (a.type)
            {
            case AddData::URL:
                metainfo = a.url.toString();
                break;

            case AddData::MAGNET:
                metainfo = a.magnet;
                break;

            case AddData::FILENAME:
                metainfo = QString::fromLatin1(a.toBase64());
                break;

            case AddData::METAINFO:
                metainfo = QString::fromLatin1(a.toBase64());
                break;

            default:
                break;
            }

            if (!metainfo.isEmpty() && interopClient.addMetainfo(metainfo))
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
    if (configDir.isNull())
    {
        configDir = QString::fromUtf8(tr_getDefaultConfigDir("transmission"));
    }

    // ensure our config directory exists
    QDir dir(configDir);

    if (!dir.exists())
    {
        dir.mkpath(configDir);
    }

    // is this the first time we've run transmission?
    bool const firstTime = !dir.exists(QLatin1String("settings.json"));

    // initialize the prefs
    myPrefs = new Prefs(configDir);

    if (!host.isNull())
    {
        myPrefs->set(Prefs::SESSION_REMOTE_HOST, host);
    }

    if (!port.isNull())
    {
        myPrefs->set(Prefs::SESSION_REMOTE_PORT, port.toUInt());
    }

    if (!username.isNull())
    {
        myPrefs->set(Prefs::SESSION_REMOTE_USERNAME, username);
    }

    if (!password.isNull())
    {
        myPrefs->set(Prefs::SESSION_REMOTE_PASSWORD, password);
    }

    if (!host.isNull() || !port.isNull() || !username.isNull() || !password.isNull())
    {
        myPrefs->set(Prefs::SESSION_IS_REMOTE, true);
    }

    if (myPrefs->getBool(Prefs::START_MINIMIZED))
    {
        minimized = true;
    }

    // start as minimized only if the system tray present
    if (!myPrefs->getBool(Prefs::SHOW_TRAY_ICON))
    {
        minimized = false;
    }

    mySession = new Session(configDir, *myPrefs);
    myModel = new TorrentModel(*myPrefs);
    myWindow = new MainWindow(*mySession, *myPrefs, *myModel, minimized);
    myWatchDir = new WatchDir(*myModel);

    connect(myModel, &TorrentModel::torrentsAdded, this, &Application::onTorrentsAdded);
    connect(myModel, &TorrentModel::torrentsCompleted, this, &Application::onTorrentsCompleted);
    connect(myModel, &TorrentModel::torrentsNeedInfo, this, &Application::onTorrentsNeedInfo);
    connect(myPrefs, &Prefs::changed, this, &Application::refreshPref);
    connect(mySession, &Session::sourceChanged, this, &Application::onSessionSourceChanged);
    connect(mySession, &Session::torrentsRemoved, myModel, &TorrentModel::removeTorrents);
    connect(mySession, &Session::torrentsUpdated, myModel, &TorrentModel::updateTorrents);
    connect(myWatchDir, &WatchDir::torrentFileAdded, this, &Application::addTorrent);

    // init from preferences
    for (auto const key : { Prefs::DIR_WATCH })
    {
        refreshPref(key);
    }

    QTimer* timer = &myModelTimer;
    connect(timer, &QTimer::timeout, this, &Application::refreshTorrents);
    timer->setSingleShot(false);
    timer->setInterval(MODEL_REFRESH_INTERVAL_MSEC);
    timer->start();

    timer = &myStatsTimer;
    connect(timer, &QTimer::timeout, mySession, &Session::refreshSessionStats);
    timer->setSingleShot(false);
    timer->setInterval(STATS_REFRESH_INTERVAL_MSEC);
    timer->start();

    timer = &mySessionTimer;
    connect(timer, &QTimer::timeout, mySession, &Session::refreshSessionInfo);
    timer->setSingleShot(false);
    timer->setInterval(SESSION_REFRESH_INTERVAL_MSEC);
    timer->start();

    maybeUpdateBlocklist();

    if (!firstTime)
    {
        mySession->restart();
    }
    else
    {
        myWindow->openSession();
    }

    if (!myPrefs->getBool(Prefs::USER_HAS_GIVEN_INFORMED_CONSENT))
    {
        QMessageBox* dialog = new QMessageBox(QMessageBox::Information, QString(),
            tr("<b>Transmission is a file sharing program.</b>"), QMessageBox::Ok | QMessageBox::Cancel, myWindow);
        dialog->setInformativeText(tr("When you run a torrent, its data will be made available to others by means of upload. "
            "Any content you share is your sole responsibility."));
        dialog->button(QMessageBox::Ok)->setText(tr("I &Agree"));
        dialog->setDefaultButton(QMessageBox::Ok);
        dialog->setModal(true);

        connect(dialog, SIGNAL(finished(int)), this, SLOT(consentGiven(int)));

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
    QStringList const qtQmDirs = QStringList() << QLibraryInfo::location(QLibraryInfo::TranslationsPath) <<
#ifdef TRANSLATIONS_DIR
        QString::fromUtf8(TRANSLATIONS_DIR) <<
#endif
        (applicationDirPath() + QLatin1String("/translations"));

    QStringList const appQmDirs = QStringList() <<
#ifdef TRANSLATIONS_DIR
        QString::fromUtf8(TRANSLATIONS_DIR) <<
#endif
        (applicationDirPath() + QLatin1String("/translations"));

    QString const qtFileName = QLatin1String("qtbase");

    QLocale const locale;
    QLocale const englishLocale(QLocale::English, QLocale::UnitedStates);

    if (loadTranslation(myQtTranslator, qtFileName, locale, qtQmDirs) ||
        loadTranslation(myQtTranslator, qtFileName, englishLocale, qtQmDirs))
    {
        installTranslator(&myQtTranslator);
    }

    if (loadTranslation(myAppTranslator, MY_CONFIG_NAME, locale, appQmDirs) ||
        loadTranslation(myAppTranslator, MY_CONFIG_NAME, englishLocale, appQmDirs))
    {
        installTranslator(&myAppTranslator);
    }
}

void Application::quitLater()
{
    QTimer::singleShot(0, this, SLOT(quit()));
}

void Application::onTorrentsEdited(torrent_ids_t const& ids)
{
    // the backend's tr_info has changed, so reload those fields
    mySession->initTorrents(ids);
}

QStringList Application::getNames(torrent_ids_t const& ids) const
{
    QStringList names;
    for (auto const& id : ids)
    {
        names.push_back(myModel->getTorrentFromId(id)->name());
    }

    names.sort();
    return names;
}

void Application::onTorrentsAdded(torrent_ids_t const& ids)
{
    if (myPrefs->getBool(Prefs::SHOW_NOTIFICATION_ON_ADD))
    {
        auto const title = tr("Torrent(s) Added", nullptr, ids.size());
        auto const body = getNames(ids).join(QStringLiteral("\n"));
        notifyApp(title, body);
    }
}

void Application::onTorrentsCompleted(torrent_ids_t const& ids)
{
    if (myPrefs->getBool(Prefs::SHOW_NOTIFICATION_ON_COMPLETE))
    {
        auto const title = tr("Torrent Completed", nullptr, ids.size());
        auto const body = getNames(ids).join(QStringLiteral("\n"));
        notifyApp(title, body);
    }

    if (myPrefs->getBool(Prefs::COMPLETE_SOUND_ENABLED))
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        beep();
#else
        QProcess::execute(myPrefs->getString(Prefs::COMPLETE_SOUND_COMMAND));
#endif
    }
}

void Application::onTorrentsNeedInfo(torrent_ids_t const& ids)
{
    if (!ids.empty())
    {
        mySession->initTorrents(ids);
    }
}

/***
****
***/

void Application::consentGiven(int result)
{
    if (result == QMessageBox::Ok)
    {
        myPrefs->set<bool>(Prefs::USER_HAS_GIVEN_INFORMED_CONSENT, true);
    }
    else
    {
        quit();
    }
}

Application::~Application()
{
    if (myPrefs != nullptr && myWindow != nullptr)
    {
        QRect const mainwinRect(myWindow->geometry());
        myPrefs->set(Prefs::MAIN_WINDOW_HEIGHT, std::max(100, mainwinRect.height()));
        myPrefs->set(Prefs::MAIN_WINDOW_WIDTH, std::max(100, mainwinRect.width()));
        myPrefs->set(Prefs::MAIN_WINDOW_X, mainwinRect.x());
        myPrefs->set(Prefs::MAIN_WINDOW_Y, mainwinRect.y());
    }

    delete myWatchDir;
    delete myWindow;
    delete myModel;
    delete mySession;
    delete myPrefs;
}

/***
****
***/

void Application::refreshPref(int key)
{
    switch (key)
    {
    case Prefs::BLOCKLIST_UPDATES_ENABLED:
        maybeUpdateBlocklist();
        break;

    case Prefs::DIR_WATCH:
    case Prefs::DIR_WATCH_ENABLED:
        {
            QString const path(myPrefs->getString(Prefs::DIR_WATCH));
            bool const isEnabled(myPrefs->getBool(Prefs::DIR_WATCH_ENABLED));
            myWatchDir->setPath(path, isEnabled);
            break;
        }

    default:
        break;
    }
}

void Application::maybeUpdateBlocklist()
{
    if (!myPrefs->getBool(Prefs::BLOCKLIST_UPDATES_ENABLED))
    {
        return;
    }

    QDateTime const lastUpdatedAt = myPrefs->getDateTime(Prefs::BLOCKLIST_DATE);
    QDateTime const nextUpdateAt = lastUpdatedAt.addDays(7);
    QDateTime const now = QDateTime::currentDateTime();

    if (now < nextUpdateAt)
    {
        mySession->updateBlocklist();
        myPrefs->set(Prefs::BLOCKLIST_DATE, now);
    }
}

void Application::onSessionSourceChanged()
{
    mySession->initTorrents();
    mySession->refreshSessionStats();
    mySession->refreshSessionInfo();
}

void Application::refreshTorrents()
{
    // usually we just poll the torrents that have shown recent activity,
    // but we also periodically ask for updates on the others to ensure
    // nothing's falling through the cracks.
    time_t const now = time(nullptr);

    if (myLastFullUpdateTime + 60 >= now)
    {
        mySession->refreshActiveTorrents();
    }
    else
    {
        myLastFullUpdateTime = now;
        mySession->refreshAllTorrents();
    }
}

/***
****
***/

void Application::addTorrent(AddData const& addme)
{
    if (addme.type == addme.NONE)
    {
        return;
    }

    if (!myPrefs->getBool(Prefs::OPTIONS_PROMPT))
    {
        mySession->addTorrent(addme);
    }
    else
    {
        auto o = new OptionsDialog(*mySession, *myPrefs, addme, myWindow);
        o->show();
    }

    raise();
}

/***
****
***/

void Application::raise()
{
    alert(myWindow);
}

bool Application::notifyApp(QString const& title, QString const& body) const
{
#ifdef QT_DBUS_LIB

    QLatin1String const dbusServiceName("org.freedesktop.Notifications");
    QLatin1String const dbusInterfaceName("org.freedesktop.Notifications");
    QLatin1String const dbusPath("/org/freedesktop/Notifications");

    QDBusConnection bus = QDBusConnection::sessionBus();

    if (bus.isConnected())
    {
        QDBusMessage m = QDBusMessage::createMethodCall(dbusServiceName, dbusPath, dbusInterfaceName, QLatin1String("Notify"));
        QVariantList args;
        args.append(QLatin1String("Transmission")); // app_name
        args.append(0U); // replaces_id
        args.append(QLatin1String("transmission")); // icon
        args.append(title); // summary
        args.append(body); // body
        args.append(QStringList()); // actions - unused for plain passive popups
        args.append(QVariantMap()); // hints - unused atm
        args.append(static_cast<int32_t>(-1)); // use the default timeout period
        m.setArguments(args);
        QDBusReply<quint32> const replyMsg = bus.call(m);

        if (replyMsg.isValid() && replyMsg.value() > 0)
        {
            return true;
        }
    }

#endif

    myWindow->trayIcon().showMessage(title, body);
    return true;
}

FaviconCache& Application::faviconCache()
{
    return myFavicons;
}

/***
****
***/

int tr_main(int argc, char* argv[])
{
    InteropHelper::initialize();

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    Application::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    Application::setAttribute(Qt::AA_UseHighDpiPixmaps);

    Application app(argc, argv);
    return app.exec();
}
