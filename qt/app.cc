/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <cassert>
#include <ctime>
#include <iostream>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QLibraryInfo>
#include <QProcess>
#include <QRect>

#include <libtransmission/transmission.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "add-data.h"
#include "app.h"
#include "dbus-adaptor.h"
#include "formatter.h"
#include "mainwin.h"
#include "options.h"
#include "prefs.h"
#include "session.h"
#include "session-dialog.h"
#include "torrent-model.h"
#include "utils.h"
#include "watchdir.h"

namespace
{
  const QString DBUS_SERVICE     = QString::fromUtf8 ("com.transmissionbt.Transmission" );
  const QString DBUS_OBJECT_PATH = QString::fromUtf8 ("/com/transmissionbt/Transmission");
  const QString DBUS_INTERFACE   = QString::fromUtf8 ("com.transmissionbt.Transmission" );

  const char * MY_READABLE_NAME ("transmission-qt");

  const tr_option opts[] =
  {
    { 'g', "config-dir", "Where to look for configuration files", "g", 1, "<path>" },
    { 'm', "minimized",  "Start minimized in system tray", "m", 0, NULL },
    { 'p', "port",  "Port to use when connecting to an existing session", "p", 1, "<port>" },
    { 'r', "remote",  "Connect to an existing session at the specified hostname", "r", 1, "<host>" },
    { 'u', "username", "Username to use when connecting to an existing session", "u", 1, "<username>" },
    { 'v', "version", "Show version number and exit", "v", 0, NULL },
    { 'w', "password", "Password to use when connecting to an existing session", "w", 1, "<password>" },
    { 0, NULL, NULL, NULL, 0, NULL }
  };

  const char*
  getUsage (void)
  {
    return "Usage:\n"
           "  transmission [OPTIONS...] [torrent files]";
  }

  void
  showUsage (void)
  {
    tr_getopt_usage (MY_READABLE_NAME, getUsage (), opts);
    exit (0);
  }

  enum
  {
    STATS_REFRESH_INTERVAL_MSEC   = 3000,
    SESSION_REFRESH_INTERVAL_MSEC = 3000,
    MODEL_REFRESH_INTERVAL_MSEC   = 3000
  };
}

MyApp :: MyApp (int& argc, char ** argv):
  QApplication (argc, argv),
  myLastFullUpdateTime (0),
  myPrefs(nullptr),
  mySession(nullptr),
  myModel(nullptr),
  myWindow(nullptr),
  myWatchDir(nullptr)
{
  const QString MY_CONFIG_NAME = QString::fromUtf8 ("transmission");

  setApplicationName (MY_CONFIG_NAME);

  // install the qt translator
  qtTranslator.load ("qt_" + QLocale::system ().name (), QLibraryInfo::location (QLibraryInfo::TranslationsPath));
  installTranslator (&qtTranslator);

  // install the transmission translator
  appTranslator.load (QString (MY_CONFIG_NAME) + "_" + QLocale::system ().name (), QCoreApplication::applicationDirPath () + "/translations");
  installTranslator (&appTranslator);

  Formatter::initUnits ();

  // set the default icon
  QIcon icon;
  QList<int> sizes;
  sizes << 16 << 22 << 24 << 32 << 48 << 64 << 72 << 96 << 128 << 192 << 256;
  foreach (int size, sizes)
    icon.addPixmap (QPixmap (QString::fromUtf8 (":/icons/transmission-%1.png").arg (size)));
  setWindowIcon (icon);

  // parse the command-line arguments
  int c;
  bool minimized = false;
  const char * optarg;
  const char * host = 0;
  const char * port = 0;
  const char * username = 0;
  const char * password = 0;
  const char * configDir = 0;
  QStringList filenames;
  while ((c = tr_getopt (getUsage(), argc, (const char**)argv, opts, &optarg)))
    {
      switch (c)
        {
          case 'g': configDir = optarg; break;
          case 'p': port = optarg; break;
          case 'r': host = optarg; break;
          case 'u': username = optarg; break;
          case 'w': password = optarg; break;
          case 'm': minimized = true; break;
          case 'v': std::cerr << MY_READABLE_NAME << ' ' << LONG_VERSION_STRING << std::endl; ::exit (0); break;
          case TR_OPT_ERR: Utils::toStderr (QObject::tr ("Invalid option")); showUsage (); break;
          default:         filenames.append (optarg); break;
        }
    }

  // try to delegate the work to an existing copy of Transmission
  // before starting ourselves...
  QDBusConnection bus = QDBusConnection::sessionBus ();
  if (bus.isConnected ())
  {
    bool delegated = false;
    for (int i=0, n=filenames.size (); i<n; ++i)
      {
        QDBusMessage request = QDBusMessage::createMethodCall (DBUS_SERVICE,
                                                               DBUS_OBJECT_PATH,
                                                               DBUS_INTERFACE,
                                                               QString::fromUtf8 ("AddMetainfo"));
        QList<QVariant> arguments;
        AddData a (filenames[i]);
        switch (a.type)
          {
            case AddData::URL:      arguments.push_back (a.url.toString ()); break;
            case AddData::MAGNET:   arguments.push_back (a.magnet); break;
            case AddData::FILENAME: arguments.push_back (a.toBase64 ().constData ()); break;
            case AddData::METAINFO: arguments.push_back (a.toBase64 ().constData ()); break;
            default:                break;
          }
        request.setArguments (arguments);

        QDBusMessage response = bus.call (request);
        //std::cerr << qPrintable (response.errorName ()) << std::endl;
        //std::cerr << qPrintable (response.errorMessage ()) << std::endl;
        arguments = response.arguments ();
        delegated |= (arguments.size ()==1) && arguments[0].toBool ();
      }

    if (delegated)
      {
        QTimer::singleShot (0, this, SLOT (quit ()));
        return;
      }
  }

  // set the fallback config dir
  if (configDir == 0)
    configDir = tr_getDefaultConfigDir ("transmission");

  // ensure our config directory exists
  QDir dir (configDir);
  if (!dir.exists ())
    dir.mkpath (configDir);

  // is this the first time we've run transmission?
  const bool firstTime = !QFile (QDir (configDir).absoluteFilePath ("settings.json")).exists ();

  // initialize the prefs
  myPrefs = new Prefs (configDir);
  if (host != 0)
    myPrefs->set (Prefs::SESSION_REMOTE_HOST, host);
  if (port != 0)
    myPrefs->set (Prefs::SESSION_REMOTE_PORT, port);
  if (username != 0)
    myPrefs->set (Prefs::SESSION_REMOTE_USERNAME, username);
  if (password != 0)
    myPrefs->set (Prefs::SESSION_REMOTE_PASSWORD, password);
  if ((host != 0) || (port != 0) || (username != 0) || (password != 0))
    myPrefs->set (Prefs::SESSION_IS_REMOTE, true);
  if (myPrefs->getBool (Prefs::START_MINIMIZED))
    minimized = true;

  // start as minimized only if the system tray present
  if (!myPrefs->getBool (Prefs::SHOW_TRAY_ICON))
    minimized = false;

  mySession = new Session (configDir, *myPrefs);
  myModel = new TorrentModel (*myPrefs);
  myWindow = new TrMainWindow (*mySession, *myPrefs, *myModel, minimized);
  myWatchDir = new WatchDir (*myModel);

  // when the session gets torrent info, update the model
  connect (mySession, SIGNAL (torrentsUpdated (tr_variant*,bool)), myModel, SLOT (updateTorrents (tr_variant*,bool)));
  connect (mySession, SIGNAL (torrentsUpdated (tr_variant*,bool)), myWindow, SLOT (refreshActionSensitivity ()));
  connect (mySession, SIGNAL (torrentsRemoved (tr_variant*)), myModel, SLOT (removeTorrents (tr_variant*)));
  // when the session source gets changed, request a full refresh
  connect (mySession, SIGNAL (sourceChanged ()), this, SLOT (onSessionSourceChanged ()));
  // when the model sees a torrent for the first time, ask the session for full info on it
  connect (myModel, SIGNAL (torrentsAdded (QSet<int>)), mySession, SLOT (initTorrents (QSet<int>)));
  connect (myModel, SIGNAL (torrentsAdded (QSet<int>)), this, SLOT (onTorrentsAdded (QSet<int>)));

  mySession->initTorrents ();
  mySession->refreshSessionStats ();

  // when torrents are added to the watch directory, tell the session
  connect (myWatchDir, SIGNAL (torrentFileAdded (QString)), this, SLOT (addTorrent (QString)));

  // init from preferences
  QList<int> initKeys;
  initKeys << Prefs::DIR_WATCH;
  foreach (int key, initKeys)
    refreshPref (key);
  connect (myPrefs, SIGNAL (changed (int)), this, SLOT (refreshPref (const int)));

  QTimer * timer = &myModelTimer;
  connect (timer, SIGNAL (timeout ()), this, SLOT (refreshTorrents ()));
  timer->setSingleShot (false);
  timer->setInterval (MODEL_REFRESH_INTERVAL_MSEC);
  timer->start ();

  timer = &myStatsTimer;
  connect (timer, SIGNAL (timeout ()), mySession, SLOT (refreshSessionStats ()));
  timer->setSingleShot (false);
  timer->setInterval (STATS_REFRESH_INTERVAL_MSEC);
  timer->start ();

  timer = &mySessionTimer;
  connect (timer, SIGNAL (timeout ()), mySession, SLOT (refreshSessionInfo ()));
  timer->setSingleShot (false);
  timer->setInterval (SESSION_REFRESH_INTERVAL_MSEC);
  timer->start ();

  maybeUpdateBlocklist ();

  if (!firstTime)
    {
      mySession->restart ();
    }
  else
    {
      QDialog * d = new SessionDialog (*mySession, *myPrefs, myWindow);
      d->show ();
    }

  if (!myPrefs->getBool (Prefs::USER_HAS_GIVEN_INFORMED_CONSENT))
    {
      QDialog * dialog = new QDialog (myWindow);
      dialog->setModal (true);
      QVBoxLayout * v = new QVBoxLayout (dialog);
      QLabel * l = new QLabel (tr ("Transmission is a file sharing program. When you run a torrent, its data will be made available to others by means of upload. Any content you share is your sole responsibility."));
      l->setWordWrap (true);
      v->addWidget (l);
      QDialogButtonBox * box = new QDialogButtonBox;
      box->addButton (new QPushButton (tr ("&Cancel")), QDialogButtonBox::RejectRole);
      QPushButton * agree = new QPushButton (tr ("I &Agree"));
      agree->setDefault (true);
      box->addButton (agree, QDialogButtonBox::AcceptRole);
      box->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
      box->setOrientation (Qt::Horizontal);
      v->addWidget (box);
      connect (box, SIGNAL (rejected ()), this, SLOT (quit ()));
      connect (box, SIGNAL (accepted ()), dialog, SLOT (deleteLater ()));
      connect (box, SIGNAL (accepted ()), this, SLOT (consentGiven ()));
      dialog->show ();
    }

  for (QStringList::const_iterator it=filenames.begin (), end=filenames.end (); it!=end; ++it)
    addTorrent (*it);

  // register as the dbus handler for Transmission
  if (bus.isConnected ())
    {
      new TrDBusAdaptor (this);
      if (!bus.registerService (DBUS_SERVICE))
        std::cerr << "couldn't register " << qPrintable (DBUS_SERVICE) << std::endl;
      if (!bus.registerObject (DBUS_OBJECT_PATH, this))
        std::cerr << "couldn't register " << qPrintable (DBUS_OBJECT_PATH) << std::endl;
    }
}

/* these functions are for popping up desktop notifications */

void
MyApp :: onTorrentsAdded (QSet<int> torrents)
{
  if (!myPrefs->getBool (Prefs::SHOW_NOTIFICATION_ON_ADD))
    return;

  foreach (int id, torrents)
    {
      Torrent * tor = myModel->getTorrentFromId (id);

      if (tor->name ().isEmpty ()) // wait until the torrent's INFO fields are loaded
        {
          connect (tor, SIGNAL (torrentChanged (int)), this, SLOT (onNewTorrentChanged (int)));
        }
      else
        {
          onNewTorrentChanged (id);

          if (!tor->isSeed ())
	    connect (tor, SIGNAL (torrentCompleted (int)), this, SLOT (onTorrentCompleted (int)));
        }
    }
}

void
MyApp :: onTorrentCompleted (int id)
{
  Torrent * tor = myModel->getTorrentFromId (id);

  if (tor)
    {
      if (myPrefs->getBool (Prefs::SHOW_NOTIFICATION_ON_COMPLETE))
        notifyApp (tr ("Torrent Completed"), tor->name ());

      if (myPrefs->getBool (Prefs::COMPLETE_SOUND_ENABLED))
        {
#if defined (Q_OS_WIN) || defined (Q_OS_MAC)
          QApplication::beep ();
#else
          QProcess::execute (myPrefs->getString (Prefs::COMPLETE_SOUND_COMMAND));
#endif
        }

      disconnect (tor, SIGNAL (torrentCompleted (int)), this, SLOT (onTorrentCompleted (int)));
    }
}

void
MyApp :: onNewTorrentChanged (int id)
{
  Torrent * tor = myModel->getTorrentFromId (id);

  if (tor && !tor->name ().isEmpty ())
    {
      const int age_secs = tor->dateAdded ().secsTo (QDateTime::currentDateTime ());
      if (age_secs < 30)
        notifyApp (tr ("Torrent Added"), tor->name ());

      disconnect (tor, SIGNAL (torrentChanged (int)), this, SLOT (onNewTorrentChanged (int)));

      if (!tor->isSeed ())
        connect (tor, SIGNAL (torrentCompleted (int)), this, SLOT (onTorrentCompleted (int)));
    }
}

/***
****
***/

void
MyApp :: consentGiven ()
{
  myPrefs->set<bool> (Prefs::USER_HAS_GIVEN_INFORMED_CONSENT, true);
}

MyApp :: ~MyApp ()
{
  if (myPrefs != nullptr && myWindow != nullptr)
    {
      const QRect mainwinRect (myWindow->geometry ());
      myPrefs->set (Prefs :: MAIN_WINDOW_HEIGHT, std::max (100, mainwinRect.height ()));
      myPrefs->set (Prefs :: MAIN_WINDOW_WIDTH, std::max (100, mainwinRect.width ()));
      myPrefs->set (Prefs :: MAIN_WINDOW_X, mainwinRect.x ());
      myPrefs->set (Prefs :: MAIN_WINDOW_Y, mainwinRect.y ());
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

void
MyApp :: refreshPref (int key)
{
  switch (key)
    {
      case Prefs :: BLOCKLIST_UPDATES_ENABLED:
        maybeUpdateBlocklist ();
        break;

      case Prefs :: DIR_WATCH:
      case Prefs :: DIR_WATCH_ENABLED:
        {
          const QString path (myPrefs->getString (Prefs::DIR_WATCH));
          const bool isEnabled (myPrefs->getBool (Prefs::DIR_WATCH_ENABLED));
          myWatchDir->setPath (path, isEnabled);
          break;
        }

      default:
        break;
    }
}

void
MyApp :: maybeUpdateBlocklist ()
{
  if (!myPrefs->getBool (Prefs :: BLOCKLIST_UPDATES_ENABLED))
    return;

  const QDateTime lastUpdatedAt = myPrefs->getDateTime (Prefs :: BLOCKLIST_DATE);
  const QDateTime nextUpdateAt = lastUpdatedAt.addDays (7);
  const QDateTime now = QDateTime::currentDateTime ();

  if (now < nextUpdateAt)
    {
      mySession->updateBlocklist ();
      myPrefs->set (Prefs :: BLOCKLIST_DATE, now);
    }
}

void
MyApp :: onSessionSourceChanged ()
{
  mySession->initTorrents ();
  mySession->refreshSessionStats ();
  mySession->refreshSessionInfo ();
}

void
MyApp :: refreshTorrents ()
{
  // usually we just poll the torrents that have shown recent activity,
  // but we also periodically ask for updates on the others to ensure
  // nothing's falling through the cracks.
  const time_t now = time (NULL);
  if (myLastFullUpdateTime + 60 >= now)
    {
      mySession->refreshActiveTorrents ();
    }
  else
    {
      myLastFullUpdateTime = now;
      mySession->refreshAllTorrents ();
    }
}

/***
****
***/

void
MyApp :: addTorrent (const QString& key)
{
  const AddData addme (key);

  if (addme.type != addme.NONE)
    addTorrent (addme);
}

void
MyApp :: addTorrent (const AddData& addme)
{
  if (!myPrefs->getBool (Prefs :: OPTIONS_PROMPT))
    {
      mySession->addTorrent (addme);
    }
  else
    {
      Options * o = new Options (*mySession, *myPrefs, addme, myWindow);
      o->show ();
    }

  raise ();
}

/***
****
***/

void
MyApp :: raise ()
{
  QApplication :: alert (myWindow);
}

bool
MyApp :: notifyApp (const QString& title, const QString& body) const
{
  const QString dbusServiceName   = QString::fromUtf8 ("org.freedesktop.Notifications");
  const QString dbusInterfaceName = QString::fromUtf8 ("org.freedesktop.Notifications");
  const QString dbusPath          = QString::fromUtf8 ("/org/freedesktop/Notifications");

  QDBusMessage m = QDBusMessage::createMethodCall (dbusServiceName, dbusPath, dbusInterfaceName, QString::fromUtf8 ("Notify"));
  QList<QVariant> args;
  args.append (QString::fromUtf8 ("Transmission")); // app_name
  args.append (0U);                                   // replaces_id
  args.append (QString::fromUtf8 ("transmission")); // icon
  args.append (title);                                // summary
  args.append (body);                                 // body
  args.append (QStringList ());                       // actions - unused for plain passive popups
  args.append (QVariantMap ());                       // hints - unused atm
  args.append (int32_t (-1));                          // use the default timeout period
  m.setArguments (args);
  QDBusMessage replyMsg = QDBusConnection::sessionBus ().call (m);
  //std::cerr << qPrintable (replyMsg.errorName ()) << std::endl;
  //std::cerr << qPrintable (replyMsg.errorMessage ()) << std::endl;
  return (replyMsg.type () == QDBusMessage::ReplyMessage) && !replyMsg.arguments ().isEmpty ();
}

/***
****
***/

int
main (int argc, char * argv[])
{
#ifdef _WIN32
  tr_win32_make_args_utf8 (&argc, &argv);
#endif

  MyApp app (argc, argv);
  return app.exec ();
}
