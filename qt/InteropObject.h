// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QObject>
#include <QString>

#include <libtransmission-app/interop-names.h>
#include <libtransmission-app/interop.h>

// Exposes this process's Instance through QtDBus and ActiveQt.
// Each slot converts its arguments and forwards one call.
// Matching and launch policy belong to tr::interop.
//
// Incoming calls run on the GUI thread.
// QtDBus uses the thread that registered the object,
// while apartment-threaded COM marshals calls to the main thread.
// The forwarded Instance may therefore access widgets directly.
class InteropObject : public QObject
{
    Q_OBJECT

#ifdef ENABLE_DBUS_INTEROP
    // The interface this build exports.
    // A peer record may point callers at a different interface exported by another client.
    Q_CLASSINFO("D-Bus Interface", TR_INTEROP_DBUS_INTERFACE_NAME)
#endif

#ifdef ENABLE_COM_INTEROP
    Q_CLASSINFO("ClassID", TR_INTEROP_COM_CLASS_ID)
    // The interface id is ActiveQt registration internals, not wire contract.
    Q_CLASSINFO("InterfaceID", "{9402f54f-4906-4f20-ad73-afcfeb5b228d}")
    Q_CLASSINFO("RegisterObject", "yes")
    Q_CLASSINFO("CoClassAlias", "QtClient")
    Q_CLASSINFO("Description", "Transmission Qt Client Class")
#endif

public:
    // QAxFactory passes only a parent when it constructs COM objects,
    // so those objects answer for whatever publish_instance() named.
    explicit InteropObject(QObject* parent = nullptr);
    explicit InteropObject(tr::interop::Instance& instance, QObject* parent = nullptr);
    ~InteropObject() override = default;
    InteropObject& operator=(InteropObject&&) = delete;
    InteropObject& operator=(InteropObject const&) = delete;
    InteropObject(InteropObject&&) = delete;
    InteropObject(InteropObject const&) = delete;

    // Names the Instance every InteropObject answers for.
    // The Instance must outlive every InteropObject that forwards calls to it.
    // NOLINTNEXTLINE(readability-identifier-naming): spelled like the tr::interop API it serves
    static void publish_instance(tr::interop::Instance* instance) noexcept;

public slots:
    // NOLINTBEGIN(readability-identifier-naming)
    [[nodiscard]] bool PresentWindow() const;
    [[nodiscard]] bool AddMetainfo(QString const& metainfo) const;

    // Return the canonical absolute path of this client's config directory.
    // Callers use it to avoid handing a launch to a client serving a different session.
    // See interop-names.h for the normalization contract.
    [[nodiscard]] QString ConfigDir() const;
    // NOLINTEND(readability-identifier-naming)

private:
    tr::interop::Instance* const instance_;
};
