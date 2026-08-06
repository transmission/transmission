// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h> // symlink()
#endif

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h> // tr_base64_decode()
#include <libtransmission/file.h>
#include <libtransmission/utils.h> // tr_file_save()

#include <libtransmission-app/interop-names.h>
#include <libtransmission-app/interop.h>
#include <libtransmission-app/startup-coordinator.h>

#include "test-fixtures.h"

namespace tr::test
{
namespace
{

using ::tr::interop::Intent;

// What the instance a fake transport hands out should answer, and what it was asked.
// The arbitration owns that Instance,
// so both live out here where the test can still read them.
struct Script
{
    explicit Script(std::string dir = {})
        : config_dir{ std::move(dir) }
    {
    }

    std::string config_dir;
    interop::Reply present_answer = interop::Reply::Yes;
    interop::Reply add_answer = interop::Reply::Yes;

    // when non-empty, answers add_metainfo() one call at a time instead of
    // `add_answer`
    std::deque<interop::Reply> add_answers;

    int config_dir_asks = 0;
    int present_asks = 0;
    std::vector<std::string> adds;
};

class ScriptedInstance final : public interop::Instance
{
public:
    explicit ScriptedInstance(Script& script)
        : script_{ script }
    {
    }

    [[nodiscard]] interop::Reply present_window() override
    {
        ++script_.present_asks;
        return script_.present_answer;
    }

    [[nodiscard]] interop::Reply add_metainfo(std::string_view const metainfo) override
    {
        script_.adds.emplace_back(metainfo);

        if (!std::empty(script_.add_answers))
        {
            auto const answer = script_.add_answers.front();
            script_.add_answers.pop_front();
            return answer;
        }

        return script_.add_answer;
    }

    [[nodiscard]] std::string config_dir() override
    {
        ++script_.config_dir_asks;
        return script_.config_dir;
    }

    [[nodiscard]] std::string description() const override
    {
        return "scripted instance";
    }

private:
    Script& script_;
};

class FakeTransport final : public interop::Transport
{
public:
    explicit FakeTransport(Script* script = nullptr)
        : other{ script }
    {
    }

    // Atomic so a test can stage "an instance appears while another thread is arbitrating".
    std::atomic<Script*> other;

    void publish(interop::Instance& /*self*/) override
    {
    }

    [[nodiscard]] std::unique_ptr<interop::Instance> find_other_instance() override
    {
        auto* const script = other.load();
        return script != nullptr ? std::make_unique<ScriptedInstance>(*script) : nullptr;
    }
};

// The arbitration takes the arguments as something to call, not as a list, so that a launch
// finding nobody never reads a torrent file. Tests state the list and let this wrap it.
[[nodiscard]] interop::MetainfoProvider offering(std::vector<std::string> metainfos)
{
    return [metainfos = std::move(metainfos)]
    {
        return metainfos;
    };
}

[[nodiscard]] std::unique_ptr<interop::StartupCoordinator> make_coordinator(
    std::string config_dir,
    Script* const script = nullptr,
    FakeTransport** const transport_out = nullptr)
{
    auto transport = std::make_unique<FakeTransport>(script);
    if (transport_out != nullptr)
    {
        *transport_out = transport.get();
    }

    return std::make_unique<interop::StartupCoordinator>(std::move(config_dir), std::move(transport));
}

} // namespace

using InteropTest = libtransmission::test::SandboxedTest;

TEST_F(InteropTest, startsWhenNoInstanceIsRunning)
{
    auto coordinator = make_coordinator(sandboxDir());
    EXPECT_FALSE(coordinator->delegate(Intent::Present, {}).has_value());
}

TEST_F(InteropTest, contendingLaunchDelegatesToThePublisher)
{
    auto publisher = make_coordinator(sandboxDir());

    auto script = Script{ sandboxDir() };
    auto launcher = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(0, launcher->delegate(Intent::Present, {}));
    EXPECT_EQ(1, script.present_asks);
}

TEST_F(InteropTest, presentsTheInstanceServingTheConfigDir)
{
    auto script = Script{ sandboxDir() };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(0, coordinator->delegate(Intent::Present, {}));
    EXPECT_EQ(1, script.present_asks);
    EXPECT_EQ(1, script.config_dir_asks);
    EXPECT_TRUE(std::empty(script.adds));
}

TEST_F(InteropTest, handsTorrentsToTheInstanceServingTheConfigDir)
{
    auto script = Script{ sandboxDir() };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    auto const metainfos = std::vector<std::string>{ "alpha", "beta" };
    EXPECT_EQ(0, coordinator->delegate(Intent::AddTorrents, offering(metainfos)));
    EXPECT_EQ(metainfos, script.adds);
    EXPECT_EQ(0, script.present_asks);
}

// A launch carrying a remote host, or asked to open minimized, wants something no running instance can give it.
// Taking it would drop what it asked for and report success, so it is never offered anywhere.
TEST_F(InteropTest, keepsALaunchNoInstanceCanServe)
{
    auto script = Script{ sandboxDir() };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_FALSE(coordinator->delegate(Intent::Standalone, offering({ "alpha" })).has_value());
    EXPECT_TRUE(std::empty(script.adds));
    EXPECT_EQ(0, script.present_asks);
}

TEST_F(InteropTest, ignoresAnInstanceServingAnotherConfigDir)
{
    auto const other_dir = sandboxDir() + "/other";
    ASSERT_TRUE(tr_sys_dir_create(other_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));

    auto script = Script{ other_dir };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_FALSE(coordinator->delegate(Intent::AddTorrents, offering({ "alpha" })).has_value());
    EXPECT_TRUE(std::empty(script.adds));
    EXPECT_EQ(0, script.present_asks);
}

TEST_F(InteropTest, twoSpellingsOfOneConfigDirNameOneInstance)
{
    auto script = Script{ sandboxDir() + "/./" };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(0, coordinator->delegate(Intent::Present, {}));
}

#ifndef _WIN32
TEST_F(InteropTest, aSymlinkAndItsTargetNameOneInstance)
{
    auto const real_dir = sandboxDir() + "/real";
    ASSERT_TRUE(tr_sys_dir_create(real_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));
    auto const link_dir = sandboxDir() + "/link";
    ASSERT_EQ(0, symlink(real_dir.c_str(), link_dir.c_str()));

    auto script = Script{ real_dir };
    auto coordinator = make_coordinator(link_dir, &script);

    EXPECT_EQ(0, coordinator->delegate(Intent::Present, {}));
}
#endif

TEST_F(InteropTest, comparesConfigDirsThatDoNotResolveAsSpelled)
{
    auto const missing = sandboxDir() + "/missing";

    auto script = Script{ missing };
    auto coordinator = make_coordinator(missing, &script);

    EXPECT_EQ(0, coordinator->delegate(Intent::Present, {}));
}

// A release too old to have ConfigDir() names no dir, and neither does one whose call went wrong.
TEST_F(InteropTest, takesAnInstanceThatNamesNoConfigDir)
{
    auto script = Script{};
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(0, coordinator->delegate(Intent::AddTorrents, offering({ "alpha" })));

    EXPECT_EQ(std::vector<std::string>{ "alpha" }, script.adds);
}

// A refusal still takes the launch. Preventing a second window is the point of all this.
// The launch came to nothing, though, and only this side can say which file it was.
TEST_F(InteropTest, reportsWhenTheInstanceDeclinesTheTorrents)
{
    auto script = Script{ sandboxDir() };
    script.add_answer = interop::Reply::No;
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(1, coordinator->delegate(Intent::AddTorrents, offering({ "alpha" })));
}

TEST_F(InteropTest, reportsWhenTheInstanceDeclinesToPresent)
{
    auto script = Script{ sandboxDir() };
    script.present_answer = interop::Reply::No;
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(1, coordinator->delegate(Intent::Present, {}));
}

// We still offer every file. One the instance cannot use tells us nothing about the next.
// An instance is there, but every argument failed to encode, so there is nothing to
// hand over and nothing to start for. The provider already reported each argument,
// so the exit code is the only thing left to get right.
TEST_F(InteropTest, failsTheLaunchWhenNoArgumentIsUsable)
{
    auto script = Script{ sandboxDir() };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(1, coordinator->delegate(Intent::AddTorrents, offering({})));
    EXPECT_TRUE(std::empty(script.adds));
    EXPECT_EQ(0, script.present_asks);
}

// One refusal decides the launch. A file the user named went nowhere.
TEST_F(InteropTest, reportsARefusalAmongTorrentsThatLanded)
{
    auto script = Script{ sandboxDir() };
    script.add_answers = { interop::Reply::Yes, interop::Reply::No, interop::Reply::Yes };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(1, coordinator->delegate(Intent::AddTorrents, offering({ "alpha", "beta", "gamma" })));

    EXPECT_EQ((std::vector<std::string>{ "alpha", "beta", "gamma" }), script.adds);
}

TEST_F(InteropTest, acceptsWhenEveryTorrentLands)
{
    auto script = Script{ sandboxDir() };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(0, coordinator->delegate(Intent::AddTorrents, offering({ "alpha", "beta" })));
}

TEST_F(InteropTest, startsWhenAMatchedInstanceNeverAnswers)
{
    auto script = Script{ sandboxDir() };
    script.present_answer = interop::Reply::Unanswered;
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_FALSE(coordinator->delegate(Intent::Present, {}).has_value());
}

TEST_F(InteropTest, startsWhenNoTorrentIsAnswered)
{
    auto script = Script{ sandboxDir() };
    script.add_answer = interop::Reply::Unanswered;
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_FALSE(coordinator->delegate(Intent::AddTorrents, offering({ "alpha", "beta" })).has_value());

    // the first unanswered offer settles it, so we never offer the rest
    EXPECT_EQ(std::vector<std::string>{ "alpha" }, script.adds);
}

// A dead instance holds no config dir lock, so the launch starts and keeps the files.
// The one file the instance already took may come back as a duplicate, which the
// session detects. A dropped file would just be gone.
TEST_F(InteropTest, startsWhenTheInstanceDiesMidHandoff)
{
    auto script = Script{ sandboxDir() };
    script.add_answers = { interop::Reply::Yes, interop::Reply::Gone };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_FALSE(coordinator->delegate(Intent::AddTorrents, offering({ "alpha", "beta", "gamma" })).has_value());

    // the files after the death are not offered to a process that is no longer there
    EXPECT_EQ((std::vector<std::string>{ "alpha", "beta" }), script.adds);
}

// An instance that answered once is still there, so starting would fail on its config
// dir lock. Exiting zero would claim every file landed. Reporting the interruption is
// all that is left.
TEST_F(InteropTest, reportsAHandoffTheInstanceStoppedAnswering)
{
    auto script = Script{ sandboxDir() };
    script.add_answers = { interop::Reply::Yes, interop::Reply::Unanswered };
    auto coordinator = make_coordinator(sandboxDir(), &script);

    EXPECT_EQ(1, coordinator->delegate(Intent::AddTorrents, offering({ "alpha", "beta", "gamma" })));
    EXPECT_EQ((std::vector<std::string>{ "alpha", "beta" }), script.adds);
}

TEST_F(InteropTest, encodesLinksAsThemselves)
{
    auto const magnet = std::string{ "magnet:?xt=urn:btih:00000000000000000000000000000000000000aa" };
    EXPECT_EQ(magnet, interop::encode_metainfo_arg(magnet));

    auto const url = std::string{ "https://example.com/example.torrent" };
    EXPECT_EQ(url, interop::encode_metainfo_arg(url));
}

TEST_F(InteropTest, encodesFilesAsTheirContentsBase64d)
{
    static auto constexpr Contents = "torrent-file-payload"sv;
    auto const filename = sandboxDir() + "/example one.torrent"; // space: %20 in URI form
    ASSERT_TRUE(tr_file_save(filename, Contents));

    auto const from_path = interop::encode_metainfo_arg(filename);
    ASSERT_TRUE(from_path.has_value());
    EXPECT_EQ(Contents, tr_base64_decode(*from_path));

    // The file:// URI a desktop's Exec=%U launch hands over, percent-encoded.
    auto uri = std::string{ "file://" };
    for (auto const ch : filename)
    {
        if (ch == ' ')
        {
            uri += "%20";
        }
        else
        {
            uri += ch;
        }
    }

    EXPECT_EQ(from_path, interop::encode_metainfo_arg(uri));
}

TEST_F(InteropTest, encodesNothingFromAnUnreadableArg)
{
    EXPECT_FALSE(interop::encode_metainfo_arg(sandboxDir() + "/no-such-file.torrent").has_value());
}

TEST_F(InteropTest, canonicalConfigDirIsAbsolute)
{
    auto const missing = sandboxDir() + "/not-created-yet";
    EXPECT_EQ(missing, interop::canonical_config_dir(missing));

    auto const relative = std::string{ "some-relative-dir" };
    auto const canonical = interop::canonical_config_dir(relative);
    EXPECT_NE(relative, canonical);
    EXPECT_FALSE(tr_sys_path_is_relative(canonical));
}

// The Windows lookup key, tested on every platform because every platform can.
// A launch on Windows finds a running client only when the two build this string identically,
// and nothing on that path re-canonicalizes it the way the D-Bus comparison does.
TEST_F(InteropTest, comMonikerItemCarriesTheSharedPrefix)
{
    auto const item = interop::com_config_moniker_item(sandboxDir());
    EXPECT_EQ(0U, item.find(interop::ComConfigMonikerPrefix));
    EXPECT_NE(std::string{ interop::ComConfigMonikerPrefix }, item);
}

TEST_F(InteropTest, comMonikerItemNamesTheCanonicalDir)
{
    // The dir is identified by what the path resolves to, not by how the caller spelled it.
    EXPECT_EQ(
        std::string{ interop::ComConfigMonikerPrefix } + interop::canonical_config_dir(sandboxDir()),
        interop::com_config_moniker_item(sandboxDir()));
}

TEST_F(InteropTest, oneConfigDirHasOneComMonikerItem)
{
    auto const dir = sandboxDir() + "/com-one";
    ASSERT_TRUE(tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0700));

    // The spellings a user can arrive with, all naming one running client.
    EXPECT_EQ(interop::com_config_moniker_item(dir), interop::com_config_moniker_item(dir + "/"));
    EXPECT_EQ(interop::com_config_moniker_item(dir), interop::com_config_moniker_item(dir + "/./"));
}

TEST_F(InteropTest, twoConfigDirsHaveDifferentComMonikerItems)
{
    auto const one = sandboxDir() + "/com-a";
    auto const two = sandboxDir() + "/com-b";
    ASSERT_TRUE(tr_sys_dir_create(one, TR_SYS_DIR_CREATE_PARENTS, 0700));
    ASSERT_TRUE(tr_sys_dir_create(two, TR_SYS_DIR_CREATE_PARENTS, 0700));

    EXPECT_NE(interop::com_config_moniker_item(one), interop::com_config_moniker_item(two));
}

#ifndef _WIN32
TEST_F(InteropTest, aSymlinkedConfigDirHasTheComMonikerItemOfItsTarget)
{
    auto const real_dir = sandboxDir() + "/com-real";
    ASSERT_TRUE(tr_sys_dir_create(real_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));
    auto const link_dir = sandboxDir() + "/com-link";
    ASSERT_EQ(0, symlink(real_dir.c_str(), link_dir.c_str()));

    EXPECT_EQ(interop::com_config_moniker_item(real_dir), interop::com_config_moniker_item(link_dir));
}
#endif

// The first launch on a config dir has to reach the same spelling as every launch after it.
// It cannot resolve a dir that is not there yet, so it creates the dir first.
TEST_F(InteropTest, canonicalConfigDirCreatedResolvesADirThatWasNotThere)
{
    auto const missing = sandboxDir() + "/made-on-demand";
    ASSERT_FALSE(tr_sys_path_exists(missing));

    auto const first = interop::canonical_config_dir_created(missing);
    EXPECT_TRUE(tr_sys_path_exists(missing));

    // What a later launch computes, now that the dir exists.
    EXPECT_EQ(interop::canonical_config_dir(missing), first);
}

#ifndef _WIN32
// The creation is what makes the difference. An unresolved dir keeps the caller's
// spelling, and a resolved one does not. A first launch that skipped the creation would
// name the instance one way and every launch after it another.
TEST_F(InteropTest, canonicalConfigDirCreatedResolvesSymlinksAnUncreatedDirCannot)
{
    auto const real_dir = sandboxDir() + "/real3";
    ASSERT_TRUE(tr_sys_dir_create(real_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));
    auto const link_dir = sandboxDir() + "/link3";
    ASSERT_EQ(0, symlink(real_dir.c_str(), link_dir.c_str()));

    auto const via_link = link_dir + "/inner";
    EXPECT_EQ(via_link, interop::canonical_config_dir(via_link));
    EXPECT_EQ(real_dir + "/inner", interop::canonical_config_dir_created(via_link));
}
#endif

#ifndef _WIN32
TEST_F(InteropTest, canonicalConfigDirResolvesSymlinks)
{
    auto const real_dir = sandboxDir() + "/real2";
    ASSERT_TRUE(tr_sys_dir_create(real_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));
    auto const link_dir = sandboxDir() + "/link2";
    ASSERT_EQ(0, symlink(real_dir.c_str(), link_dir.c_str()));

    EXPECT_EQ(interop::canonical_config_dir(real_dir), interop::canonical_config_dir(link_dir));
}
#endif

// Encoding reads torrent files, so the launch must only do it once it has somewhere to hand them.
TEST_F(InteropTest, encodesTheMetainfosOnlyWhenAnInstanceIsFound)
{
    auto calls = 0;
    auto const provider = [&calls]
    {
        ++calls;
        return std::vector<std::string>{ "alpha" };
    };

    auto* transport = static_cast<FakeTransport*>(nullptr);
    auto coordinator = make_coordinator(sandboxDir(), nullptr, &transport);
    EXPECT_FALSE(coordinator->delegate(Intent::AddTorrents, provider).has_value());
    EXPECT_EQ(0, calls);

    auto script = Script{ sandboxDir() };
    transport->other = &script;
    EXPECT_EQ(0, coordinator->delegate(Intent::AddTorrents, provider));
    EXPECT_EQ(1, calls);
    EXPECT_EQ(std::vector<std::string>{ "alpha" }, script.adds);
}

TEST_F(InteropTest, contendedLaunchWaitsOutThePublisherThenDelegates)
{
    using namespace std::chrono_literals;

    auto publisher = make_coordinator(sandboxDir());

    auto script = Script{};
    script.config_dir = sandboxDir();
    auto* transport = static_cast<FakeTransport*>(nullptr);
    auto launcher_coordinator = make_coordinator(sandboxDir(), nullptr, &transport);

    auto exit_code = std::optional<int>{};
    auto launcher = std::thread{ [&]
                                 {
                                     exit_code = launcher_coordinator->delegate(Intent::Present, {}, 5s);
                                 } };

    // The publisher publishes, then releases the startup lock. The launcher's final
    // delegation attempt, made once it acquires that lock, must find the published instance.
    std::this_thread::sleep_for(100ms);
    transport->other = &script;
    auto published = ScriptedInstance{ script };
    publisher->publish(published);
    launcher.join();

    EXPECT_EQ(0, exit_code);
    EXPECT_EQ(1, script.present_asks);
}

TEST_F(InteropTest, busyWhenThePublisherNeverYields)
{
    using namespace std::chrono_literals;

    auto publisher = make_coordinator(sandboxDir());
    auto launcher = make_coordinator(sandboxDir());

    EXPECT_EQ(1, launcher->delegate(Intent::Present, {}, 200ms));
}

// A refused handoff must not read as success anywhere between the delegation and the exit code.
TEST_F(InteropTest, carriesARefusalThroughToTheExitCode)
{
    auto script = Script{ sandboxDir() };
    script.add_answer = interop::Reply::No;
    auto coordinator = make_coordinator(sandboxDir(), &script);

    auto const provider = interop::MetainfoProvider{ []
                                                     {
                                                         return std::vector<std::string>{ "alpha" };
                                                     } };
    EXPECT_EQ(1, coordinator->delegate(Intent::AddTorrents, provider));
}

} // namespace tr::test
