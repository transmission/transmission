// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <QAxFactory>
#include <QAxObject>
#include <QString>
#include <QUuid>
#include <QVariant>

#include "ComInteropHelper.h"
#include "InteropObject.h"

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
QAXFACTORY_BEGIN("{1e405fc2-1a3a-468b-8bd6-bfbb58770390}", "{792d1aac-53cc-4dc9-bc29-e5295fdb93a9}")
QAXCLASS(InteropObject)
QAXFACTORY_END() // NOLINT

// These are ActiveQt internals; declaring here as I don't like their WinMain much...
extern bool qAxOutProcServer; // NOLINT
extern wchar_t qAxModuleFilename[MAX_PATH]; // NOLINT
extern QString qAxInit(); // NOLINT

namespace
{

// Returns the interop object of a Qt client that is already running, or nullptr
// if none is. GetActiveObject reads the running object table and nothing else,
// so a miss stays a miss; handing the class id to QAxObject instead would let
// COM start a client from its registered LocalServer32 and report success.
std::unique_ptr<QAxObject> connectToRunningClient()
{
    auto* unknown = static_cast<IUnknown*>(nullptr);

    if (::GetActiveObject(QUuid{ QStringLiteral(TR_COM_CLIENT_CLSID) }, nullptr, &unknown) != S_OK || unknown == nullptr)
    {
        return {};
    }

    // QAxObject adopts the reference that GetActiveObject returned.
    return std::make_unique<QAxObject>(unknown);
}

} // namespace

ComInteropHelper::ComInteropHelper()
    : client_{ connectToRunningClient() }
{
}

bool ComInteropHelper::isConnected() const
{
    return client_ != nullptr && !client_->isNull();
}

QVariant ComInteropHelper::addMetainfo(QString const& metainfo) const
{
    if (client_ == nullptr)
    {
        return {};
    }

    return client_->dynamicCall("AddMetainfo(QString)", metainfo);
}

void ComInteropHelper::initialize()
{
    qAxOutProcServer = true;
    ::GetModuleFileNameW(nullptr, qAxModuleFilename, MAX_PATH);

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    qAxInit();
}

void ComInteropHelper::registerObject(QObject* parent)
{
    QAxFactory::startServer();
    QAxFactory::registerActiveObject(new InteropObject{ parent });
}
