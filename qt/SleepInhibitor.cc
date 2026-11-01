// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <functional>
#include <utility>

#include <QtDebug>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif !defined(Q_OS_MAC)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QString>
#include <QVariantMap>
#endif

#include "SleepInhibitor.h"

SleepInhibitor::SleepInhibitor(Inhibit inhibit)
    : inhibit_{ std::move(inhibit) }
{
}

SleepInhibitor::~SleepInhibitor()
{
    setInhibited(false);
}

void SleepInhibitor::setInhibited(bool inhibited)
{
    if (!inhibited)
    {
        failed_ = false;

        if (release_)
        {
            std::exchange(release_, {})();
        }
    }
    else if (!release_ && !failed_)
    {
        release_ = inhibit_();
        failed_ = !release_;
    }
}

#if defined(Q_OS_WIN)

std::function<void()> SleepInhibitor::platformInhibit()
{
    // The execution state belongs to the calling thread, so this and the
    // release must both run on the GUI thread.
    if (::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) == 0)
    {
        qWarning() << "couldn't inhibit sleep: error" << ::GetLastError();
        return {};
    }

    return []()
    {
        ::SetThreadExecutionState(ES_CONTINUOUS);
    };
}

#elif !defined(Q_OS_MAC) // macOS: SleepInhibitorMac.mm

// https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Inhibit.html
std::function<void()> SleepInhibitor::platformInhibit()
{
    static auto constexpr TimeoutMsec = 1000;
    static auto constexpr InhibitSuspend = 4U;
    static auto request_count = 0U;
    auto const service = QStringLiteral("org.freedesktop.portal.Desktop");
    auto bus = QDBusConnection::sessionBus();

    // The portal derives the request handle from our unique name and this
    // token, so it is known even if the reply times out while the portal
    // starts up, and the inhibition can still be released.
    auto const token = QStringLiteral("transmission%1").arg(++request_count);
    auto const sender = bus.baseService().mid(1).replace(QLatin1Char{ '.' }, QLatin1Char{ '_' });
    auto const handle = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    auto request = QDBusMessage::createMethodCall(
        service,
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Inhibit"),
        QStringLiteral("Inhibit"));
    request << QString{} << InhibitSuspend
            << QVariantMap{
                   { QStringLiteral("handle_token"), token },
                   { QStringLiteral("reason"), QStringLiteral("BitTorrent Activity") },
               };

    if (auto const reply = bus.call(request, QDBus::Block, TimeoutMsec);
        reply.type() == QDBusMessage::ErrorMessage && reply.errorName() != QStringLiteral("org.freedesktop.DBus.Error.NoReply"))
    {
        qWarning() << "couldn't inhibit sleep:" << qPrintable(reply.errorMessage());
        return {};
    }

    // The inhibition lasts until its request is closed.
    return [bus, service, handle]()
    {
        auto const close = QDBusMessage::createMethodCall(
            service,
            handle,
            QStringLiteral("org.freedesktop.portal.Request"),
            QStringLiteral("Close"));
        bus.send(close);
    };
}

#endif
