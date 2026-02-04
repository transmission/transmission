// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Application.h"

#include <algorithm>
#include <utility>

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

#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif

#include <libtransmission/transmission.h>

#include <libtransmission/values.h>

#include "AccessibleSqueezeLabel.h"
#include "AddData.h"
#include "InteropHelper.h"
#include "MainWindow.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "Session.h"
#include "TorrentModel.h"
#include "WatchDir.h"

namespace
{

// NOLINTBEGIN(cert-err58-cpp)
auto const ConfigName = QStringLiteral("transmission");
#ifdef QT_DBUS_LIB
auto const FDONotificationsServiceName = QStringLiteral("org.freedesktop.Notifications");
auto const FDONotificationsPath = QStringLiteral("/org/freedesktop/Notifications");
auto const FDONotificationsInterfaceName = QStringLiteral("org.freedesktop.Notifications");
#endif
// NOLINTEND(cert-err58-cpp)

auto constexpr StatsRefreshIntervalMsec = 3000;
auto constexpr SessionRefreshIntervalMsec = 3000;
auto constexpr ModelRefreshIntervalMsec = 3000;

bool load_translation(
    QTranslator& translator,
    QString const& name,
    QLocale const& locale,
    QStringList const& search_directories)
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

void init_units()
{
    using Config = tr::Values::Config;

    Config::speed = { Config::Base::Kilo,
                      QObject::tr("B/s").toStdString(),
                      QObject::tr("kB/s").toStdString(),
                      QObject::tr("MB/s").toStdString(),
                      QObject::tr("GB/s").toStdString(),
                      QObject::tr("TB/s").toStdString() };

    Config::memory = { Config::Base::Kibi,
                       QObject::tr("B").toStdString(),
                       QObject::tr("KiB").toStdString(),
                       QObject::tr("MiB").toStdString(),
                       QObject::tr("GiB").toStdString(),
                       QObject::tr("TiB").toStdString() };

    Config::storage = { Config::Base::Kilo,
                        QObject::tr("B").toStdString(),
                        QObject::tr("kB").toStdString(),
                        QObject::tr("MB").toStdString(),
                        QObject::tr("GB").toStdString(),
                        QObject::tr("TB").toStdString() };
}

[[nodiscard]] auto make_window_icon()
{
    // first, try to load it from the system theme
    if (auto icon = QIcon::fromTheme(QStringLiteral("transmission")); !icon.isNull())
    {
        return icon;
    }

    // if that fails, use our own as the fallback
    return QIcon{ QStringLiteral(":/icons/transmission.svg") };
}

#if QT_CONFIG(accessibility)

QAccessibleInterface* accessible_factory(QString const& className, QObject* object)
{
    auto* widget = qobject_cast<QWidget*>(object);

    if (widget != nullptr)
    {
        if (className == QStringLiteral("SqueezeLabel"))
        {
            return new AccessibleSqueezeLabel(widget);
        }
    }

    return nullptr;
}

#endif // QT_CONFIG(accessibility)

} // namespace

Application::Application(
    Prefs& prefs,
    RpcClient& rpc,
    bool minimized,
    QString const& config_dir,
    QStringList const& filenames,
    int& argc,
    char** argv)
    : QApplication{ argc, argv }
    , prefs_{ prefs }
{
    setApplicationName(ConfigName);
    load_translations();
    init_units();

    setWindowIcon(make_window_icon());

#ifdef __APPLE__
    setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    // ensure our config directory exists
    QDir const dir(config_dir);

    if (!dir.exists())
    {
        dir.mkpath(config_dir);
    }

    // is this the first time we've run transmission?
    bool const first_time = !dir.exists(QStringLiteral("settings.json"));

#if QT_CONFIG(accessibility)
    QAccessible::installFactory(&accessible_factory);
#endif

    session_ = std::make_unique<Session>(config_dir, prefs_, rpc);
    model_ = std::make_unique<TorrentModel>(prefs_);
    window_ = std::make_unique<MainWindow>(*session_, prefs_, *model_, minimized);
    watch_dir_ = std::make_unique<WatchDir>(*model_);

    connect(this, &QCoreApplication::aboutToQuit, this, &Application::save_geometry);
    connect(model_.get(), &TorrentModel::torrents_added, this, &Application::on_torrents_added);
    connect(model_.get(), &TorrentModel::torrents_completed, this, &Application::on_torrents_completed);
    connect(model_.get(), &TorrentModel::torrents_edited, this, &Application::on_torrents_edited);
    connect(model_.get(), &TorrentModel::torrents_need_info, this, &Application::on_torrents_need_info);
    connect(&prefs_, &Prefs::changed, this, &Application::refresh_pref);
    connect(session_.get(), &Session::source_changed, this, &Application::on_session_source_changed);
    connect(session_.get(), &Session::torrents_removed, model_.get(), &TorrentModel::remove_torrents);
    connect(session_.get(), &Session::torrents_updated, model_.get(), &TorrentModel::update_torrents);
    connect(
        watch_dir_.get(),
        &WatchDir::torrent_file_added,
        this,
        qOverload<QString const&>(&Application::add_watchdir_torrent));

    // init from preferences
    for (auto const key : { Prefs::DIR_WATCH })
    {
        refresh_pref(key);
    }

    QTimer* timer = &model_timer_;
    connect(timer, &QTimer::timeout, this, &Application::refresh_torrents);
    timer->setSingleShot(false);
    timer->setInterval(ModelRefreshIntervalMsec);
    timer->start();

    timer = &stats_timer_;
    connect(timer, &QTimer::timeout, session_.get(), &Session::refresh_session_stats);
    timer->setSingleShot(false);
    timer->setInterval(StatsRefreshIntervalMsec);
    timer->start();

    timer = &session_timer_;
    connect(timer, &QTimer::timeout, session_.get(), &Session::refresh_session_info);
    timer->setSingleShot(false);
    timer->setInterval(SessionRefreshIntervalMsec);
    timer->start();

    maybe_update_blocklist();

    if (!first_time)
    {
        session_->restart();
    }
    else
    {
        window_->open_session();
    }

    // torrent files passed in on the command line
    for (QString const& filename : filenames)
    {
        add_torrent(AddData{ filename });
    }

    InteropHelper::register_object(this);

#ifdef QT_DBUS_LIB
    if (auto bus = QDBusConnection::sessionBus(); bus.isConnected())
    {
        bus.connect(
            FDONotificationsServiceName,
            FDONotificationsPath,
            FDONotificationsInterfaceName,
            QStringLiteral("ActionInvoked"),
            this,
            SLOT(on_notification_action_invoked(quint32, QString)));
    }

#endif
}

Application::~Application() = default;

void Application::load_translations()
{
    auto const qt_qm_dirs = QStringList{} <<
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QLibraryInfo::path(QLibraryInfo::TranslationsPath) <<
#else
        QLibraryInfo::location(QLibraryInfo::TranslationsPath) <<
#endif
#ifdef TRANSLATIONS_DIR
        QStringLiteral(TRANSLATIONS_DIR) <<
#endif
        (applicationDirPath() + QStringLiteral("/translations"));

    QStringList const app_qm_dirs = QStringList{} <<
#ifdef TRANSLATIONS_DIR
        QStringLiteral(TRANSLATIONS_DIR) <<
#endif
        (applicationDirPath() + QStringLiteral("/translations"));

    auto const qt_file_name = QStringLiteral("qtbase");

    QLocale const locale;
    QLocale const english_locale(QLocale::English, QLocale::UnitedStates);

    if (load_translation(qt_translator_, qt_file_name, locale, qt_qm_dirs) ||
        load_translation(qt_translator_, qt_file_name, english_locale, qt_qm_dirs))
    {
        installTranslator(&qt_translator_);
    }

    if (load_translation(app_translator_, ConfigName, locale, app_qm_dirs) ||
        load_translation(app_translator_, ConfigName, english_locale, app_qm_dirs))
    {
        installTranslator(&app_translator_);
    }
}

void Application::on_torrents_edited(torrent_ids_t const& torrent_ids) const
{
    // the backend's tr_info has changed, so reload those fields
    session_->init_torrents(torrent_ids);
}

QStringList Application::get_names(torrent_ids_t const& torrent_ids) const
{
    QStringList names;
    for (auto const& id : torrent_ids)
    {
        names.push_back(model_->get_torrent_from_id(id)->name());
    }

    names.sort();
    return names;
}

void Application::on_torrents_added(torrent_ids_t const& torrent_ids) const
{
    if (!prefs_.get<bool>(Prefs::SHOW_NOTIFICATION_ON_ADD))
    {
        return;
    }

    for (auto id : torrent_ids)
    {
        notify_torrent_added(model_->get_torrent_from_id(id));
    }
}

void Application::on_torrents_completed(torrent_ids_t const& torrent_ids) const
{
    if (prefs_.get<bool>(Prefs::SHOW_NOTIFICATION_ON_COMPLETE))
    {
        auto const title = tr("Torrent(s) Completed", nullptr, static_cast<int>(std::size(torrent_ids)));
        auto const body = get_names(torrent_ids).join(QStringLiteral("\n"));
        notify_app(title, body);
    }

    if (prefs_.get<bool>(Prefs::COMPLETE_SOUND_ENABLED))
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        beep();
#else
        auto args = prefs_.get<QStringList>(Prefs::COMPLETE_SOUND_COMMAND);
        auto const command = args.takeFirst();
        QProcess::execute(command, args);
#endif
    }
}

void Application::on_torrents_need_info(torrent_ids_t const& torrent_ids) const
{
    if (!torrent_ids.empty())
    {
        session_->init_torrents(torrent_ids);
    }
}

void Application::notify_torrent_added(Torrent const* tor) const
{
    QStringList actions;
    actions << QString{ QStringLiteral("start-now(%1)") }.arg(tor->id()) << QObject::tr("Start Now");
    notify_app(tr("Torrent Added"), tor->name(), actions);
}

// ---

void Application::save_geometry() const
{
    if (window_ != nullptr)
    {
        auto const geometry = window_->geometry();
        prefs_.set(Prefs::MAIN_WINDOW_HEIGHT, std::max(100, geometry.height()));
        prefs_.set(Prefs::MAIN_WINDOW_WIDTH, std::max(100, geometry.width()));
        prefs_.set(Prefs::MAIN_WINDOW_X, geometry.x());
        prefs_.set(Prefs::MAIN_WINDOW_Y, geometry.y());
    }
}

// ---

void Application::refresh_pref(int key) const
{
    switch (key)
    {
    case Prefs::BLOCKLIST_UPDATES_ENABLED:
        maybe_update_blocklist();
        break;

    case Prefs::DIR_WATCH:
    case Prefs::DIR_WATCH_ENABLED:
        watch_dir_->set_path(prefs_.get<QString>(Prefs::DIR_WATCH), prefs_.get<bool>(Prefs::DIR_WATCH_ENABLED));
        break;

    default:
        break;
    }
}

void Application::maybe_update_blocklist() const
{
    if (!prefs_.get<bool>(Prefs::BLOCKLIST_UPDATES_ENABLED))
    {
        return;
    }

    auto const last_updated_at = prefs_.get<QDateTime>(Prefs::BLOCKLIST_DATE);
    auto const next_update_at = last_updated_at.addDays(7);
    auto const now = QDateTime::currentDateTime();

    if (now < next_update_at)
    {
        session_->update_blocklist();
        prefs_.set(Prefs::BLOCKLIST_DATE, now);
    }
}

void Application::on_session_source_changed() const
{
    session_->init_torrents();
    session_->refresh_session_stats();
    session_->refresh_session_info();
}

void Application::refresh_torrents()
{
    // usually we just poll the torrents that have shown recent activity,
    // but we also periodically ask for updates on the others to ensure
    // nothing's falling through the cracks.
    time_t const now = time(nullptr);

    if (last_full_update_time_ + 60 >= now)
    {
        session_->refresh_active_torrents();
    }
    else
    {
        last_full_update_time_ = now;
        session_->refresh_all_torrents();
    }
}

/***
****
***/

void Application::add_watchdir_torrent(QString const& filename) const
{
    auto add_data = AddData{ filename };
    auto const disposal = prefs_.get<bool>(Prefs::TRASH_ORIGINAL) ? AddData::FilenameDisposal::Delete :
                                                                    AddData::FilenameDisposal::Rename;
    add_data.set_file_disposal(disposal);
    add_torrent(std::move(add_data));
}

void Application::add_torrent(AddData addme) const
{
    if (addme.type == AddData::NONE)
    {
        return;
    }

    // if there's not already a disposal action set,
    // then honor the `trash original` preference setting
    if (!addme.file_disposal() && prefs_.get<bool>(Prefs::TRASH_ORIGINAL))
    {
        addme.set_file_disposal(AddData::FilenameDisposal::Delete);
    }

    if (!prefs_.get<bool>(Prefs::OPTIONS_PROMPT))
    {
        session_->add_torrent(addme);
    }
    else
    {
        auto* o = new OptionsDialog{ *session_, prefs_, addme, window_.get() };
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

bool Application::notify_app(QString const& title, QString const& body, QStringList const& actions) const
{
#ifdef QT_DBUS_LIB
    if (auto bus = QDBusConnection::sessionBus(); bus.isConnected())
    {
        QDBusMessage m = QDBusMessage::createMethodCall(
            FDONotificationsServiceName,
            FDONotificationsPath,
            FDONotificationsInterfaceName,
            QStringLiteral("Notify"));
        QVariantList args;
        args.append(QStringLiteral("Transmission")); // app_name
        args.append(0U); // replaces_id
        args.append(QStringLiteral("transmission")); // icon
        args.append(title); // summary
        args.append(body); // body
        args.append(actions);
        args.append(
            QVariantMap{ {
                std::make_pair(QStringLiteral("category"), QVariant{ QStringLiteral("transfer.complete") }),
            } }); // hints
        args.append(static_cast<int32_t>(-1)); // use the default timeout period
        m.setArguments(args);
        QDBusReply<quint32> const reply_msg = bus.call(m);

        if (reply_msg.isValid() && reply_msg.value() > 0)
        {
            return true;
        }
    }

#endif

    window_->tray_icon().showMessage(title, body);
    return true;
}

#ifdef QT_DBUS_LIB
// NOLINTNEXTLINE(performance-unnecessary-value-param)
void Application::on_notification_action_invoked(quint32 /* notification_id */, QString action_key)
{
    static QRegularExpression const StartNowRegex{ QStringLiteral(R"rgx(start-now\((\d+)\))rgx") };

    if (auto const match = StartNowRegex.match(action_key); match.hasMatch())
    {
        int const torrent_id = match.captured(1).toInt();
        session_->start_torrents_now({ torrent_id });
    }
}
#endif
