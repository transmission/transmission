// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

class QObject;
class QString;
class QVariant;

class DBusInteropHelper
{
public:
    DBusInteropHelper() = default;

    bool isConnected() const;

    QVariant addMetainfo(QString const& metainfo) const;

    static void registerObject(QObject* parent);
};
