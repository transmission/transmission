// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <windows.h>
#include <objbase.h>

#include <QAxFactory>
#include <QString>
#include <QVariant>

#include "ComInteropHelper.h"
#include "InteropObject.h"

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
QAXFACTORY_BEGIN("{1e405fc2-1a3a-468b-8bd6-bfbb58770390}", "{792d1aac-53cc-4dc9-bc29-e5295fdb93a9}")
QAXCLASS(InteropObject)
QAXFACTORY_END() // NOLINT

// These are ActiveQt internals; declaring here as I don't like their WinMain much...
extern HANDLE qAxInstance; // NOLINT
extern bool qAxOutProcServer; // NOLINT
extern wchar_t qAxModuleFilename[MAX_PATH]; // NOLINT
extern QString qAxInit(); // NOLINT

ComInteropHelper::ComInteropHelper()
    : client_(new QAxObject(QStringLiteral("Transmission.QtClient")))
{
}

bool ComInteropHelper::isConnected() const
{
    return !client_->isNull();
}

QVariant ComInteropHelper::addMetainfo(QString const& metainfo) const
{
    return client_->dynamicCall("AddMetainfo(QString)", metainfo);
}

void ComInteropHelper::initialize()
{
    qAxOutProcServer = true;
    ::GetModuleFileNameW(nullptr, qAxModuleFilename, MAX_PATH);
    qAxInstance = ::GetModuleHandleW(nullptr);

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    qAxInit();
}

void ComInteropHelper::registerObject(QObject* parent)
{
    QAxFactory::startServer();
    QAxFactory::registerActiveObject(new InteropObject(parent));
}
