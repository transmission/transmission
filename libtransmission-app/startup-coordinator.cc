// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/file.h>
#include <libtransmission/lock-file.h>
#include <libtransmission/log.h>
#include <libtransmission/tr-assert.h>
#include <libtransmission/tr-strbuf.h>

#include "libtransmission-app/interop-names.h"
#include "libtransmission-app/interop.h"
#include "libtransmission-app/startup-coordinator.h"

namespace tr::interop
{
namespace
{

enum class StartupDecision : uint8_t
{
    Start,
    Delegated,
    Refused,
    Interrupted, // the instance took some torrents, then stopped answering
    NothingToAdd, // an instance was there, but no argument was usable
    Busy
};

// `ours_canonical` is the canonical form of `config_dir`, which the caller computes
// once rather than re-resolving per candidate.
[[nodiscard]] bool matches(Instance& other, std::string_view const config_dir, std::string_view const ours_canonical)
{
    auto const theirs = other.config_dir();

    // An empty answer means a release too old to have ConfigDir().
    // Take it rather than start a second instance.
    if (std::empty(theirs))
    {
        return true;
    }

    // Check filesystem identity first. When both dirs exist, this sees through bind mounts
    // and hard links, which comparing resolved paths does not.
    if (tr_sys_path_is_same(theirs.c_str(), tr_pathbuf{ config_dir }.c_str()))
    {
        return true;
    }

    return canonical_config_dir(theirs) == ours_canonical;
}

// One attempt to hand this launch over. Start means nobody took it.
[[nodiscard]] StartupDecision try_delegate(
    Transport& transport,
    std::string_view const config_dir,
    std::string_view const ours_canonical,
    Intent const intent,
    MetainfoProvider const& metainfos,
    std::string_view const activation_token)
{
    if (intent == Intent::Standalone)
    {
        return StartupDecision::Start;
    }

    auto const other = transport.find_other_instance();
    if (!other || !matches(*other, config_dir, ours_canonical))
    {
        return StartupDecision::Start;
    }

    tr_logAddDebug(fmt::format("handing this launch to {:s}", other->description()));

    // An instance that answers has handled the request, yes or no.
    // Only silence, or an instance that has gone, leaves the launch with us.
    switch (intent)
    {
    case Intent::AddTorrents:
        {
            auto decision = StartupDecision::Start;

            // Read the torrents here, not earlier.
            // Only now do we know an instance is there and serving this config dir.
            auto const encoded = metainfos();

            // Every argument failed to encode, and the provider reported each one.
            // Starting anyway would end on the instance's config dir lock, blaming
            // the dir for what is an argument problem.
            if (std::empty(encoded))
            {
                return StartupDecision::NothingToAdd;
            }

            for (auto const& metainfo : encoded)
            {
                switch (other->add_metainfo(metainfo))
                {
                case Reply::Yes:
                    if (decision == StartupDecision::Start)
                    {
                        decision = StartupDecision::Delegated;
                    }

                    break;

                case Reply::No:
                    // One refusal decides the launch. A file the user named went nowhere,
                    // and that is worth reporting even when the others landed.
                    decision = StartupDecision::Refused;
                    break;

                case Reply::Gone:
                    // The instance died mid-handoff, and its config dir lock died with
                    // it, so this launch starts and keeps the files. Whatever the
                    // instance already took, the new session may re-take as a duplicate.
                    return StartupDecision::Start;

                case Reply::Unanswered:
                    // Silence before any answer reads as no instance at all,
                    // most likely a stale record, so the launch starts.
                    if (decision == StartupDecision::Start)
                    {
                        return StartupDecision::Start;
                    }

                    // Silence after an answer is an instance that is still there and has
                    // stopped answering. Starting would fail on its config dir lock,
                    // so report what was left undelivered instead.
                    return StartupDecision::Interrupted;
                }
            }

            return decision;
        }

    case Intent::Present:
        switch (other->present_window(activation_token))
        {
        case Reply::Yes:
            return StartupDecision::Delegated;

        case Reply::No:
            return StartupDecision::Refused;

        case Reply::Gone:
        case Reply::Unanswered:
            return StartupDecision::Start;
        }

        break;

    case Intent::Standalone:
        break;
    }

    return StartupDecision::Start;
}

[[nodiscard]] std::optional<int> report_startup_decision(
    StartupDecision const decision,
    Intent const intent,
    std::string_view const config_dir)
{
    switch (decision)
    {
    case StartupDecision::Delegated:
        if (intent == Intent::Present)
        {
            fmt::print(stderr, "Already running on '{:s}'; presenting it.\n", config_dir);
        }

        return 0;

    case StartupDecision::Refused:
        fmt::print(stderr, "The client already running on '{:s}' could not do that.\n", config_dir);
        return 1;

    case StartupDecision::Interrupted:
        fmt::print(stderr, "The client running on '{:s}' stopped answering; not every torrent was handed over.\n", config_dir);
        return 1;

    case StartupDecision::NothingToAdd:
        // The metainfo provider already reported each unusable argument.
        return 1;

    case StartupDecision::Busy:
        return report_config_dir_busy(config_dir);

    case StartupDecision::Start:
        break;
    }

    return {};
}

} // namespace

class StartupCoordinator::Impl
{
public:
    // `config_dir` must already exist. Both clients create it with
    // canonical_config_dir_created() before constructing the coordinator, and a dir
    // must exist before a file in it can be locked.
    Impl(std::string config_dir, std::unique_ptr<Transport> transport)
        : config_dir_{ std::move(config_dir) }
        , ours_canonical_{ canonical_config_dir(config_dir_) }
        , transport_{ std::move(transport) }
        , lock_{ tr_pathbuf{ config_dir_, '/', StartupLockFilename } }
    {
        TR_ASSERT(transport_ != nullptr);

        (void)lock_.try_acquire();
    }

    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl() = default;

    [[nodiscard]] StartupDecision arbitrate(
        Intent const intent,
        MetainfoProvider const& metainfos,
        std::string_view const activation_token,
        std::chrono::milliseconds const patience)
    {
        using namespace std::chrono_literals;

        // Read the torrents at most once. The retry after the lock wait below would
        // otherwise re-read every file and re-print every per-argument report.
        auto encoded = std::optional<std::vector<std::string>>{};
        auto const metainfos_once = MetainfoProvider{ [&]
                                                      {
                                                          if (!encoded)
                                                          {
                                                              encoded = metainfos ? metainfos() : std::vector<std::string>{};
                                                          }

                                                          return *encoded;
                                                      } };

        auto const delegate = [&]
        {
            return try_delegate(*transport_, config_dir_, ours_canonical_, intent, metainfos_once, activation_token);
        };

        if (auto const decision = delegate(); decision != StartupDecision::Start)
        {
            return decision;
        }

        if (lock_.status() != tr_lock_file::Status::Contended)
        {
            return StartupDecision::Start;
        }

        // Another launch is between delegation and publication. It releases this lock just
        // after it publishes, so wait for that, or for it to give up and exit.
        auto const deadline = std::chrono::steady_clock::now() + patience;
        while (!lock_.try_acquire())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return StartupDecision::Busy;
            }

            std::this_thread::sleep_for(50ms);
        }

        // Whoever released the lock published first,
        // so give that peer one last chance before starting.
        return delegate();
    }

    [[nodiscard]] std::optional<int> delegate(
        Intent const intent,
        MetainfoProvider const& metainfos,
        std::string_view const activation_token,
        std::chrono::milliseconds const patience)
    {
        return report_startup_decision(arbitrate(intent, metainfos, activation_token, patience), intent, config_dir_);
    }

    void publish(Instance& self)
    {
        transport_->publish(self);
        lock_.release();
    }

private:
    std::string const config_dir_;
    std::string const ours_canonical_;
    std::unique_ptr<Transport> const transport_;
    tr_lock_file lock_;
};

StartupCoordinator::StartupCoordinator(std::string config_dir, std::unique_ptr<Transport> transport)
    : impl_{ std::make_unique<Impl>(std::move(config_dir), std::move(transport)) }
{
}

StartupCoordinator::~StartupCoordinator() = default;

std::optional<int> StartupCoordinator::delegate(
    Intent const intent,
    MetainfoProvider const& metainfos,
    std::string_view const activation_token,
    std::chrono::milliseconds const patience)
{
    return impl_->delegate(intent, metainfos, activation_token, patience);
}

int report_config_dir_busy(std::string_view const config_dir)
{
    fmt::print(stderr, "Another process is already using '{:s}'.\n", config_dir);
    return 1;
}

void StartupCoordinator::publish(Instance& self)
{
    impl_->publish(self);
}

} // namespace tr::interop
