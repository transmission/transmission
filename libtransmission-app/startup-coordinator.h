// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission-app/interop.h" // Instance, Intent, Transport

namespace tr::interop
{

// Produces the launch's torrent arguments in wire form (encode_metainfo_arg).
// We call this at most once, and only when an instance will take them,
// so the common launch that finds nobody never reads a torrent file.
using MetainfoProvider = std::function<std::vector<std::string>()>;

// Owns startup delegation and publication: the transport, and the startup lock we hold
// between them. Do not confuse that lock with the config dir lock a session takes.
// The config dir lock is the one that matters once startup is over.
class StartupCoordinator
{
public:
    // `config_dir` must already exist; create it with canonical_config_dir_created().
    StartupCoordinator(std::string config_dir, std::unique_ptr<Transport> transport);
    StartupCoordinator(StartupCoordinator&&) = delete;
    StartupCoordinator(StartupCoordinator const&) = delete;
    StartupCoordinator& operator=(StartupCoordinator&&) = delete;
    StartupCoordinator& operator=(StartupCoordinator const&) = delete;
    ~StartupCoordinator();

    // Returns an exit code when another instance took the launch or startup
    // timed out. Empty means this process should continue starting.
    // `activation_token` rides along on a Present handoff; see interop.h.
    [[nodiscard]] std::optional<int> delegate(
        Intent intent,
        MetainfoProvider const& metainfos,
        std::string_view activation_token = {},
        std::chrono::milliseconds patience = std::chrono::minutes{ 2 });

    // Makes `self` reachable before we release the startup lock.
    // `self` must outlive this coordinator.
    // Destroying the coordinator stops the transport answering.
    void publish(Instance& self);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};

// Reports that the config dir is busy and returns the exit code to leave with.
// We call this when a launch has already started and then finds the dir held by a
// process it cannot delegate to.
[[nodiscard]] int report_config_dir_busy(std::string_view config_dir);

} // namespace tr::interop
