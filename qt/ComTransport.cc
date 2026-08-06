// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <QAxFactory>
#include <QAxObject>
#include <QDebug>
#include <QString>
#include <QUuid>
#include <QVariant>

#include <libtransmission-app/interop-names.h>

#include "InteropObject.h"
#include "Transports.h"
#include "Utils.h"

// The typelib and app ids are ActiveQt registration internals, not wire contract.
// No launcher looks them up.
// The class id in interop-names.h is the one other builds must agree on.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
QAXFACTORY_BEGIN("{1e405fc2-1a3a-468b-8bd6-bfbb58770390}", "{792d1aac-53cc-4dc9-bc29-e5295fdb93a9}")
QAXCLASS(InteropObject)
QAXFACTORY_END() // NOLINT

// These are ActiveQt internals; declaring here as I don't like their WinMain much...
extern bool qAxOutProcServer; // NOLINT
extern wchar_t qAxModuleFilename[MAX_PATH]; // NOLINT
extern QString qAxInit(); // NOLINT
extern QAxFactory* qAxFactory(); // NOLINT

namespace
{

class ComTransport final : public tr::interop::Transport
{
public:
    // COM and the ActiveQt out-of-process machinery are initialized here,
    // so build the transport before QApplication.
    explicit ComTransport(QString const& config_dir);
    ~ComTransport() override;

    void publish(tr::interop::Instance& self) override;

    [[nodiscard]] std::unique_ptr<tr::interop::Instance> find_other_instance() override;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};

[[nodiscard]] HRESULT initializeActiveQt()
{
    static auto const Result = []
    {
        qAxOutProcServer = true;
        ::GetModuleFileNameW(nullptr, qAxModuleFilename, MAX_PATH);

        auto const result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(result))
        {
            qAxInit();
        }

        return result;
    }();

    return Result;
}

// Takes ownership of the caller's reference: QAxObject adds its own.
[[nodiscard]] std::unique_ptr<QAxObject> wrap(IUnknown* const unknown)
{
    auto client = std::make_unique<QAxObject>(unknown);
    unknown->Release();
    return client;
}

// How one machine routes to the right client. The running object table is keyed by moniker,
// so an item moniker naming the config dir gives every dir its own entry.
// GetActiveObject(), the fallback below, takes only a class id,
// so it can only ever find "some" client.
//
// The prefix is part of the wire contract. A separately-built client that shares it
// finds this one, and this one finds it.
// `item` is com_config_moniker_item()'s answer, which the transport computes once,
// since canonicalizing the dir costs a filesystem round trip.
[[nodiscard]] IMoniker* createConfigMoniker(QString const& item)
{
    auto* moniker = static_cast<IMoniker*>(nullptr);
    return SUCCEEDED(::CreateItemMoniker(L"!", reinterpret_cast<LPCOLESTR>(item.utf16()), &moniker)) ? moniker : nullptr;
}

// Returns the object registered for this exact config dir.
// Unlike GetActiveObject(CLSID),
// the item moniker remains unambiguous when several clients are running.
[[nodiscard]] std::unique_ptr<QAxObject> connectToConfigClient(QString const& moniker_item)
{
    auto* table = static_cast<IRunningObjectTable*>(nullptr);
    if (FAILED(::GetRunningObjectTable(0, &table)) || table == nullptr)
    {
        return {};
    }

    auto* moniker = createConfigMoniker(moniker_item);
    if (moniker == nullptr)
    {
        table->Release();
        return {};
    }

    auto* unknown = static_cast<IUnknown*>(nullptr);
    auto const result = table->GetObject(moniker, &unknown);
    moniker->Release();
    table->Release();

    if (FAILED(result) || unknown == nullptr)
    {
        return {};
    }

    return wrap(unknown);
}

// Compatibility fallback for a client that predates config-specific monikers.
// GetActiveObject reads the ROT and never starts a registered LocalServer32.
[[nodiscard]] std::unique_ptr<QAxObject> connectToLegacyClient()
{
    auto* unknown = static_cast<IUnknown*>(nullptr);

    if (::GetActiveObject(QUuid{ QStringLiteral(TR_INTEROP_COM_CLASS_ID) }, nullptr, &unknown) != S_OK || unknown == nullptr)
    {
        return {};
    }

    return wrap(unknown);
}

// The client's IDispatch, or nullptr. The caller owns the reference and must Release() it.
[[nodiscard]] IDispatch* dispatchOf(QAxObject& client)
{
    auto* dispatch = static_cast<IDispatch*>(nullptr);
    client.queryInterface(
        QUuid{ QStringLiteral("{00020400-0000-0000-C000-000000000046}") }, // IID_IDispatch
        reinterpret_cast<void**>(&dispatch));
    return dispatch;
}

class RemoteInstance final : public tr::interop::Instance
{
public:
    explicit RemoteInstance(std::unique_ptr<QAxObject> client)
        : client_{ std::move(client) }
    {
    }

    [[nodiscard]] tr::interop::Reply present_window(std::string_view const /*activation_token*/) override
    {
        // Windows has no activation token. Its pass is a foreground right the caller
        // transfers to the server, so the server's SetForegroundWindow succeeds instead
        // of degrading to a taskbar flash.
        grant_foreground();
        return call_reply("PresentWindow()");
    }

    [[nodiscard]] tr::interop::Reply add_metainfo(std::string_view const metainfo) override
    {
        return call_reply("AddMetainfo(QString)", Utils::toQString(metainfo));
    }

    [[nodiscard]] std::string config_dir() override
    {
        auto const reply = call("ConfigDir()");
        return reply.isValid() ? reply.toString().toStdString() : std::string{};
    }

    [[nodiscard]] std::string description() const override
    {
        return "COM running object " TR_INTEROP_COM_CLASS_ID;
    }

private:
    // dynamicCall() reports every failure the same way, an invalid QVariant, so a wedged
    // client and a dead one look alike from the reply. server_reachable() tells them apart.
    // `signature` is dynamicCall()'s method-plus-parameter-types form, e.g. "ConfigDir()".
    // The method names inside are the wire contract from interop-names.h.
    [[nodiscard]] QVariant call(char const* const signature, QVariant const& arg = {})
    {
        if (!arg.isValid())
        {
            return client_->dynamicCall(signature);
        }

        return client_->dynamicCall(signature, arg);
    }

    // A call whose wire answer is a bool, classified for tr::interop.
    [[nodiscard]] tr::interop::Reply call_reply(char const* const signature, QVariant const& arg = {})
    {
        if (auto const reply = call(signature, arg); reply.isValid())
        {
            return reply.toBool() ? tr::interop::Reply::Yes : tr::interop::Reply::No;
        }

        return server_reachable() ? tr::interop::Reply::Unanswered : tr::interop::Reply::Gone;
    }

    // Best effort: the grant transfers a right this process must itself hold, and it
    // fails on a same-apartment pointer, where there is no proxy. The present call is
    // still worth making without it. The window then flashes rather than comes forward.
    void grant_foreground()
    {
        if (auto* const dispatch = dispatchOf(*client_); dispatch != nullptr)
        {
            ::CoAllowSetForegroundWindow(dispatch, nullptr);
            dispatch->Release();
        }
    }

    // One cheap round trip to the server. Every IDispatch implements GetTypeInfoCount(),
    // so any completed call proves the process is there, and only a dead server cannot
    // complete one.
    [[nodiscard]] bool server_reachable()
    {
        auto* const dispatch = dispatchOf(*client_);
        if (dispatch == nullptr)
        {
            return false;
        }

        auto count = UINT{};
        auto const result = dispatch->GetTypeInfoCount(&count);
        dispatch->Release();
        return SUCCEEDED(result);
    }

    std::unique_ptr<QAxObject> client_;
};

} // namespace

class ComTransport::Impl
{
public:
    explicit Impl(QString const& config_dir)
        : moniker_item_{ Utils::toQString(tr::interop::com_config_moniker_item(config_dir.toStdString())) }
        , co_initialize_result_{ initializeActiveQt() }
    {
    }

    ~Impl()
    {
        // Cleared first: COM can still hand out a factory-built InteropObject
        // until every revocation below has landed.
        InteropObject::publish_instance(nullptr);

        if (config_cookie_ != 0U && table_ != nullptr)
        {
            table_->Revoke(config_cookie_);
        }

        if (legacy_cookie_ != 0U)
        {
            ::RevokeActiveObject(legacy_cookie_, nullptr);
        }

        if (table_ != nullptr)
        {
            table_->Release();
        }

        if (wrapper_ != nullptr)
        {
            wrapper_->Release();
        }
    }

    void publish(tr::interop::Instance& self)
    {
        if (FAILED(co_initialize_result_))
        {
            return;
        }

        InteropObject::publish_instance(&self);

        QAxFactory::startServer();
        // The default constructor answers for whatever publish_instance() named, the
        // same way a factory-built object does, so both paths have one decision point.
        object_ = std::make_unique<InteropObject>();
        auto* const factory = qAxFactory();
        if (factory == nullptr || !factory->createObjectWrapper(object_.get(), &wrapper_) || wrapper_ == nullptr)
        {
            qWarning() << "couldn't create the COM interop wrapper";
            return;
        }

        // When two clients publish for one config dir, both registrations stand.
        // COM does not say which one a later lookup reaches: not the first, not the last,
        // undefined. Do not build on whichever a given Windows happens to return.
        // D-Bus differs here, because its record is a file and the last writer holds it.
        //
        // The config dir lock keeps a second session off the dir, but a client attached
        // to a session on another host holds no session lock, so one of those plus a
        // local client can still publish for one dir.
        if (auto* moniker = createConfigMoniker(moniker_item_); moniker != nullptr)
        {
            if (SUCCEEDED(::GetRunningObjectTable(0, &table_)) && table_ != nullptr &&
                FAILED(table_->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, wrapper_, moniker, &config_cookie_)))
            {
                config_cookie_ = 0U;
                qWarning() << "couldn't register the config-specific COM interop object";
            }

            moniker->Release();
        }

        // Preserve the fixed-CLSID entry for older launchers,
        // but do not add another ambiguous entry when any client already owns it.
        if (connectToLegacyClient() == nullptr &&
            FAILED(
                ::RegisterActiveObject(
                    wrapper_,
                    QUuid{ QStringLiteral(TR_INTEROP_COM_CLASS_ID) },
                    ACTIVEOBJECT_STRONG,
                    &legacy_cookie_)))
        {
            legacy_cookie_ = 0U;
            qWarning() << "couldn't register the legacy COM interop object";
        }
    }

    [[nodiscard]] std::unique_ptr<tr::interop::Instance> find_other_instance() const
    {
        if (FAILED(co_initialize_result_))
        {
            return {};
        }

        // The moniker picks out this config dir. The class id knows nothing about dirs, so
        // tr::interop::StartupCoordinator checks whatever it finds like any other candidate.
        auto client = connectToConfigClient(moniker_item_);
        if (client == nullptr)
        {
            client = connectToLegacyClient();
        }

        return client != nullptr ? std::make_unique<RemoteInstance>(std::move(client)) : nullptr;
    }

private:
    QString const moniker_item_;
    std::unique_ptr<InteropObject> object_;
    IDispatch* wrapper_ = nullptr;
    IRunningObjectTable* table_ = nullptr;
    DWORD config_cookie_ = 0U;
    DWORD legacy_cookie_ = 0U;
    HRESULT const co_initialize_result_;
};

ComTransport::ComTransport(QString const& config_dir)
    : impl_{ std::make_unique<Impl>(config_dir) }
{
}

ComTransport::~ComTransport() = default;

void ComTransport::publish(tr::interop::Instance& self)
{
    impl_->publish(self);
}

std::unique_ptr<tr::interop::Instance> ComTransport::find_other_instance()
{
    // Never this process. We register in publish(),
    // after this launch has given up on delegating.
    return impl_->find_other_instance();
}

namespace tr::interop
{

std::unique_ptr<Transport> make_transport(QString const& config_dir)
{
    return std::make_unique<ComTransport>(config_dir);
}

} // namespace tr::interop
