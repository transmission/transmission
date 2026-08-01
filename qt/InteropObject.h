// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QObject>

#ifdef ENABLE_COM_INTEROP
// The class id a running Qt client is registered under. transmission-qt.idl and
// dist/msi/components/QtClient.wxs spell it out again, each in the form its own
// format wants -- bare and uppercase for MIDL, braced for WiX -- so no one
// definition can serve all three. Changing it here means changing it there.
#define TR_COM_CLIENT_CLSID "{0e2c952c-0597-491f-ba26-249d7e6fab49}"
#endif

class InteropObject : public QObject
{
    Q_OBJECT

#ifdef ENABLE_DBUS_INTEROP
    Q_CLASSINFO("D-Bus Interface", "com.transmissionbt.Transmission")
#endif

#ifdef ENABLE_COM_INTEROP
    Q_CLASSINFO("ClassID", TR_COM_CLIENT_CLSID)
    Q_CLASSINFO("InterfaceID", "{9402f54f-4906-4f20-ad73-afcfeb5b228d}")
    Q_CLASSINFO("RegisterObject", "yes")
    Q_CLASSINFO("CoClassAlias", "QtClient")
    Q_CLASSINFO("Description", "Transmission Qt Client Class")
#endif

public:
    explicit InteropObject(QObject* parent = nullptr);
    ~InteropObject() override = default;
    InteropObject& operator=(InteropObject&&) = delete;
    InteropObject& operator=(InteropObject const&) = delete;
    InteropObject(InteropObject&&) = delete;
    InteropObject(InteropObject const&) = delete;

public slots:
    // NOLINTBEGIN(readability-identifier-naming)
    [[nodiscard]] bool PresentWindow() const;
    [[nodiscard]] bool AddMetainfo(QString const& metainfo) const;
    // NOLINTEND(readability-identifier-naming)
};
