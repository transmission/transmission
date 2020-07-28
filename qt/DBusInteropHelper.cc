/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
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

auto const DBusService = QStringLiteral("com.transmissionbt.Transmission");
auto const DBusObjectPath = QStringLiteral("/com/transmissionbt/Transmission");
auto const DBusInterface = QStringLiteral("com.transmissionbt.Transmission");

} // namespace

bool DBusInteropHelper::isConnected() const
{
    return QDBusConnection::sessionBus().isConnected();
}

QVariant DBusInteropHelper::addMetainfo(QString const& metainfo)
{
    QDBusMessage request = QDBusMessage::createMethodCall(DBusService, DBusObjectPath, DBusInterface,
        QStringLiteral("AddMetainfo"));
    request.setArguments(QVariantList() << metainfo);

    QDBusReply<bool> const response = QDBusConnection::sessionBus().call(request);
    return response.isValid() ? QVariant(response.value()) : QVariant();
}

void DBusInteropHelper::registerObject(QObject* parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.isConnected())
    {
        return;
    }

    if (!bus.registerService(DBusService))
    {
        std::cerr << "couldn't register " << qPrintable(DBusService) << std::endl;
    }

    if (!bus.registerObject(DBusObjectPath, new InteropObject(parent), QDBusConnection::ExportAllSlots))
    {
        std::cerr << "couldn't register " << qPrintable(DBusObjectPath) << std::endl;
    }
}
