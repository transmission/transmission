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

bool DBusInteropHelper::isConnected() const
{
    return QDBusConnection::sessionBus().isConnected();
}

QVariant DBusInteropHelper::addMetainfo(QString const& metainfo)
{
    auto request = QDBusMessage::createMethodCall(
        QStringLiteral("com.transmissionbt.Transmission"),
        QStringLiteral("/com/transmissionbt/Transmission"),
        QStringLiteral("com.transmissionbt.Transmission"),
        QStringLiteral("AddMetainfo")
        );
    request.setArguments(QVariantList() << metainfo);

    QDBusReply<bool> const response = QDBusConnection::sessionBus().call(request);
    return response.isValid() ? QVariant(response.value()) : QVariant();
}

void DBusInteropHelper::registerObject(QObject* parent)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
    {
        return;
    }

    auto const service_name = QStringLiteral("com.transmissionbt.Transmission");
    if (!bus.registerService(service_name))
    {
        std::cerr << "couldn't register " << qPrintable(service_name) << std::endl;
    }

    auto const object_path = QStringLiteral("/com/transmissionbt/Transmission");
    if (!bus.registerObject(object_path, new InteropObject(parent), QDBusConnection::ExportAllSlots))
    {
        std::cerr << "couldn't register " << qPrintable(object_path) << std::endl;
    }
}
