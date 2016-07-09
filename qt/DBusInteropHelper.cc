/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <iostream>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QString>
#include <QVariant>

#include "DBusInteropHelper.h"
#include "InteropObject.h"

namespace
{
  const QLatin1String DBUS_SERVICE ("com.transmissionbt.Transmission");
  const QLatin1String DBUS_OBJECT_PATH ("/com/transmissionbt/Transmission");
  const QLatin1String DBUS_INTERFACE ("com.transmissionbt.Transmission");
}

bool
DBusInteropHelper::isConnected () const
{
  return QDBusConnection::sessionBus ().isConnected ();
}

QVariant
DBusInteropHelper::addMetainfo (const QString& metainfo)
{
  QDBusMessage request = QDBusMessage::createMethodCall (DBUS_SERVICE, DBUS_OBJECT_PATH,
                                                         DBUS_INTERFACE, QLatin1String ("AddMetainfo"));
  request.setArguments (QVariantList () << metainfo);

  const QDBusReply<bool> response = QDBusConnection::sessionBus ().call (request);
  return response.isValid () ? QVariant (response.value ()) : QVariant ();
}

void
DBusInteropHelper::registerObject (QObject * parent)
{
  QDBusConnection bus = QDBusConnection::sessionBus ();
  if (!bus.isConnected ())
    return;

  if (!bus.registerService (DBUS_SERVICE))
    std::cerr << "couldn't register " << qPrintable (DBUS_SERVICE) << std::endl;
  if (!bus.registerObject (DBUS_OBJECT_PATH, new InteropObject (parent), QDBusConnection::ExportAllSlots))
    std::cerr << "couldn't register " << qPrintable (DBUS_OBJECT_PATH) << std::endl;
}
