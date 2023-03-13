// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QObject>

#include <libtransmission/tr-macros.h>

class InteropObject : public QObject
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(InteropObject)

#ifdef ENABLE_DBUS_INTEROP
    Q_CLASSINFO("D-Bus Interface", "com.transmissionbt.Transmission")
#endif

#ifdef ENABLE_COM_INTEROP
    Q_CLASSINFO("ClassID", "{0e2c952c-0597-491f-ba26-249d7e6fab49}")
    Q_CLASSINFO("InterfaceID", "{9402f54f-4906-4f20-ad73-afcfeb5b228d}")
    Q_CLASSINFO("RegisterObject", "yes")
    Q_CLASSINFO("CoClassAlias", "QtClient")
    Q_CLASSINFO("Description", "Transmission Qt Client Class")
#endif

public:
    explicit InteropObject(QObject* parent = nullptr);

public slots:
    bool PresentWindow() const;
    bool AddMetainfo(QString const& metainfo) const;
};
