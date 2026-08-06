// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tr::interop
{

enum class Intent : uint8_t
{
    // This launch wants something no already-running instance can give it:
    // a session on another host, or a window that opens minimized.
    // If we handed it over, we would drop what it asked for and report success.
    Standalone,
    Present,
    AddTorrents
};

// What a launch would have another instance do for it, if one is there.
// `standalone` names anything an already-running instance cannot satisfy.
[[nodiscard]] constexpr Intent intent_of(bool const standalone, bool const have_torrents) noexcept
{
    if (standalone)
    {
        return Intent::Standalone;
    }

    return have_torrents ? Intent::AddTorrents : Intent::Present;
}

// What a call to an instance came back with.
// Yes and No are the instance's own answer. The other two are calls that got none.
enum class Reply : uint8_t
{
    Yes,
    No,

    // The instance's process has exited. The kernel released that process's config dir
    // lock when it died, so a launch it stops answering may start.
    Gone,

    // No answer, and no proof the instance is gone. A busy instance looks like this,
    // and so does a stale record that reaches an unrelated process.
    Unanswered,
};

// An instance we can talk to: this process, or one reached over a transport.
// The caller does not need to know which, or even whether it is this program.
// A separately-built client sharing the config dir answers here too,
// so the methods below have to work for any implementation.
class Instance
{
public:
    virtual ~Instance() = default;

    // Gone requires proof from the transport that the instance's process has exited.
    // Silence without that proof is Unanswered.
    //
    // `activation_token` lets focus follow the caller's launch to this instance's
    // window; see activation_token() below. Empty when the caller has none, and the
    // instance then presents as well as its platform allows without one.
    [[nodiscard]] virtual Reply present_window(std::string_view activation_token) = 0;
    [[nodiscard]] virtual Reply add_metainfo(std::string_view metainfo) = 0;

    // The canonical config dir this instance answers for, canonical as interop-names.h
    // defines it. Empty when the instance did not say. A release older than the
    // ConfigDir() method cannot.
    // A client attached to a session on another host answers with the config dir it was
    // launched on. Torrents handed to it land in the session its window shows.
    [[nodiscard]] virtual std::string config_dir() = 0;

    // For log messages. Never parsed.
    [[nodiscard]] virtual std::string description() const = 0;
};

class Transport
{
public:
    virtual ~Transport() = default;

    // `self` must answer until this Transport is destroyed.
    // Whoever owns the Instance must destroy the Transport before releasing it.
    virtual void publish(Instance& self) = 0;

    // Does NOT filter by config dir. StartupCoordinator decides which instance counts as
    // a match (startup-coordinator.h), so every transport answers this the same way.
    // Must never return this process.
    [[nodiscard]] virtual std::unique_ptr<Instance> find_other_instance() = 0;
};

// The present-window wire rule, shared by every D-Bus transport. Offer the caller's
// token through PresentWindowWithToken first; only silence sends the call on to the
// tokenless PresentWindow, so a client from before the token method still presents, at
// the price of one unanswered call, and a wedged client costs one extra timeout.
// The transport supplies the two calls and their argument wiring.
template<typename CallToken, typename CallPlain>
[[nodiscard]] Reply present_window_via(
    std::string_view const activation_token,
    CallToken&& call_with_token,
    CallPlain&& call_plain)
{
    if (!std::empty(activation_token))
    {
        if (auto const reply = call_with_token(activation_token); reply != Reply::Unanswered)
        {
            return reply;
        }
    }

    return call_plain();
}

// The one implementation of the canonicalization rule in interop-names.h.
// Every ConfigDir() answer, every comparison, and every COM moniker item goes through here.
[[nodiscard]] std::string canonical_config_dir(std::string_view config_dir);

// The canonical form of `config_dir`, creating the directory first if it is not there.
// We cannot resolve a dir that does not exist, so a first launch would otherwise settle on a
// spelling that later launches disagree with, once the dir exists for them to resolve.
// D-Bus survives that, because both ends re-canonicalize before comparing.
// The COM moniker does not. A lookup matches it as an exact string.
[[nodiscard]] std::string canonical_config_dir_created(std::string_view config_dir);

// The running-object-table item a Windows client registers for `config_dir`, and the one a
// launch looks up. The lookup matches this string exactly and nothing re-canonicalizes it,
// so a client that builds it differently is never found.
// This canonicalizes `config_dir` itself rather than trust the caller to have done it.
// Declared here rather than in the Windows code so that every platform can test it.
[[nodiscard]] std::string com_config_moniker_item(std::string_view config_dir);

// The single-use pass the desktop put in this launch's environment so that focus may
// follow the launch to whichever window ends up presenting. Wayland spells it
// XDG_ACTIVATION_TOKEN; X11's DESKTOP_STARTUP_ID is the same idea. Empty when the
// desktop gave none, a launch from a terminal most often.
// A toolkit can move the variables before main() runs. GTK4 does, so the GTK client
// calls gtr_activation_token() (gtk/Utils.h) rather than this directly.
[[nodiscard]] std::string activation_token();

// Returns true if `arg` is a URL or a magnet link.
// Returns false if `arg` is a base64-encoded .torrent.
[[nodiscard]] bool is_metainfo_link(std::string_view arg);

// encode_metainfo_arg() over a whole argument list, reporting each unusable argument
// on stderr as it is found. The report belongs here because both clients must make it
// the same way: a handed-over launch never reaches the client code that would
// otherwise report, and StartupCoordinator's nothing-to-add exit relies on the report
// having been made.
[[nodiscard]] std::vector<std::string> encode_metainfo_args(std::vector<std::string> const& args);

// Converts `arg` to a wire form.
// Filenames and file:// URIs load the .torrent file and convert to base64.
// torrent URLs and magnet links are passed through as-is.
// Returns empty when the argument is none of the above.
[[nodiscard]] std::optional<std::string> encode_metainfo_arg(std::string_view arg);

// Reads back the wire form encode_metainfo_arg() writes, so we decide what counts as a
// torrent here rather than once per client.
// Returns the .torrent contents `metainfo` carries, or nothing when it carries none:
// it is a link, it is not base64, or what it decodes to is not a torrent.
// Ask is_metainfo_link() first. A link is the other form, not a malformed torrent.
[[nodiscard]] std::optional<std::string> decode_metainfo_torrent(std::string_view metainfo);

} // namespace tr::interop
