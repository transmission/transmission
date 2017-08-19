/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

class QObject;
class QString;
class QVariant;

class DBusInteropHelper
{
public:
    bool isConnected() const;

    QVariant addMetainfo(QString const& metainfo);

    static void registerObject(QObject* parent);
};
