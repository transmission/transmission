// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t
#include <cstdint> // int64_t
#include <memory>
#include <string_view>

#include <libtransmission/transmission.h>

#include <libtransmission/log.h>
#include <libtransmission/net.h>
#include <libtransmission/open-files.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/quark.h>
#include <libtransmission/session.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"

using namespace std::literals;

using SettingsTest = ::testing::Test;

TEST_F(SettingsTest, canInstantiate)
{
    auto settings = tr_session::Settings{};

    auto var = settings.save();
    EXPECT_TRUE(var.has_value());
}

TEST_F(SettingsTest, canLoadBools)
{
    static auto constexpr Key = TR_KEY_seed_queue_enabled;

    auto settings = tr_session::Settings{};
    auto const expected_value = !settings.seed_queue_enabled;

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddBool(&var, Key, expected_value);
    settings.load(var);

    EXPECT_EQ(expected_value, settings.seed_queue_enabled);
}

TEST_F(SettingsTest, canSaveBools)
{
    static auto constexpr Key = TR_KEY_seed_queue_enabled;

    auto settings = tr_session::Settings{};
    auto const expected_value = !settings.seed_queue_enabled;
    settings.seed_queue_enabled = expected_value;

    auto var = settings.save();
    auto val = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&var, Key, &val));
    EXPECT_EQ(expected_value, val);
}

TEST_F(SettingsTest, canLoadDoubles)
{
    static auto constexpr Key = TR_KEY_ratio_limit;

    auto settings = tr_session::Settings{};
    auto const expected_value = settings.ratio_limit + 1.0;

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddReal(&var, Key, expected_value);
    settings.load(var);
    EXPECT_NEAR(expected_value, settings.ratio_limit, 0.001);
}

TEST_F(SettingsTest, canSaveDoubles)
{
    static auto constexpr Key = TR_KEY_seed_queue_enabled;

    auto settings = tr_session::Settings{};
    auto const default_value = settings.seed_queue_enabled;
    auto const expected_value = !default_value;
    settings.seed_queue_enabled = expected_value;

    auto var = settings.save();
    auto val = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&var, Key, &val));
    EXPECT_EQ(expected_value, val);
}

TEST_F(SettingsTest, canLoadEncryptionMode)
{
    static auto constexpr Key = TR_KEY_encryption;
    static auto constexpr ExpectedValue = TR_ENCRYPTION_REQUIRED;

    auto settings = std::make_unique<tr_session::Settings>();
    ASSERT_NE(ExpectedValue, settings->encryption_mode);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, ExpectedValue);
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->encryption_mode);
    var.clear();

    settings = std::make_unique<tr_session::Settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, "required");
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->encryption_mode);
}

TEST_F(SettingsTest, canSaveEncryptionMode)
{
    static auto constexpr Key = TR_KEY_encryption;
    static auto constexpr ExpectedValue = TR_ENCRYPTION_REQUIRED;

    auto settings = tr_session::Settings{};
    EXPECT_NE(ExpectedValue, settings.seed_queue_enabled);
    settings.encryption_mode = ExpectedValue;

    auto var = settings.save();
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&var, Key, &val));
    EXPECT_EQ(ExpectedValue, val);
}

TEST_F(SettingsTest, canLoadLogLevel)
{
    static auto constexpr Key = TR_KEY_message_level;

    auto settings = std::make_unique<tr_session::Settings>();
    auto const default_value = settings->log_level;
    auto constexpr ExpectedValue = TR_LOG_DEBUG;
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, ExpectedValue);
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->log_level);
    var.clear();

    settings = std::make_unique<tr_session::Settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, "debug");
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->log_level);
}

TEST_F(SettingsTest, canSaveLogLevel)
{
    static auto constexpr Key = TR_KEY_message_level;

    auto settings = tr_session::Settings{};
    auto const default_value = settings.log_level;
    auto constexpr ExpectedValue = TR_LOG_DEBUG;
    ASSERT_NE(ExpectedValue, default_value);

    settings.log_level = ExpectedValue;
    auto var = settings.save();
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&var, Key, &val));
    EXPECT_EQ(ExpectedValue, val);
}

TEST_F(SettingsTest, canLoadMode)
{
    static auto constexpr Key = TR_KEY_umask;

    auto settings = std::make_unique<tr_session::Settings>();
    auto const default_value = settings->umask;
    auto constexpr ExpectedValue = tr_mode_t{ 0777 };
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, ExpectedValue);
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->umask);
    var.clear();

    settings = std::make_unique<tr_session::Settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, "0777");
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->umask);
}

TEST_F(SettingsTest, canSaveMode)
{
    static auto constexpr Key = TR_KEY_umask;

    auto settings = tr_session::Settings{};
    auto const default_value = settings.log_level;
    auto constexpr ExpectedValue = tr_mode_t{ 0777 };
    ASSERT_NE(ExpectedValue, default_value);

    settings.umask = ExpectedValue;
    auto var = settings.save();
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&var, Key, &val));
    EXPECT_EQ("0777", val);
}

TEST_F(SettingsTest, canLoadPort)
{
    static auto constexpr Key = TR_KEY_peer_port;

    auto settings = tr_session::Settings{};
    auto const default_value = settings.peer_port;
    auto constexpr ExpectedValue = tr_port::from_host(8080);
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, ExpectedValue.host());
    settings.load(var);
    EXPECT_EQ(ExpectedValue, settings.peer_port);
}

TEST_F(SettingsTest, canSavePort)
{
    static auto constexpr Key = TR_KEY_peer_port;

    auto settings = tr_session::Settings{};
    auto const default_value = settings.peer_port;
    auto constexpr ExpectedValue = tr_port::from_host(8080);
    ASSERT_NE(ExpectedValue, default_value);

    settings.peer_port = ExpectedValue;
    auto var = settings.save();
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&var, Key, &val));
    EXPECT_EQ(ExpectedValue.host(), val);
}

TEST_F(SettingsTest, canLoadPreallocation)
{
    static auto constexpr Key = TR_KEY_preallocation;

    auto settings = std::make_unique<tr_session::Settings>();
    auto const default_value = settings->preallocation_mode;
    auto constexpr ExpectedValue = tr_open_files::Preallocation::Full;
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, static_cast<int64_t>(ExpectedValue));
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->preallocation_mode);
    var.clear();

    settings = std::make_unique<tr_session::Settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, "full");
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->preallocation_mode);
}

TEST_F(SettingsTest, canSavePreallocation)
{
    static auto constexpr Key = TR_KEY_preallocation;

    auto settings = tr_session::Settings{};
    auto const default_value = settings.preallocation_mode;
    auto constexpr ExpectedValue = tr_open_files::Preallocation::Full;
    ASSERT_NE(ExpectedValue, default_value);

    settings.preallocation_mode = ExpectedValue;
    auto var = settings.save();
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&var, Key, &val));
    EXPECT_EQ(static_cast<int64_t>(ExpectedValue), val);
}

TEST_F(SettingsTest, canLoadSizeT)
{
    static auto constexpr Key = TR_KEY_queue_stalled_minutes;

    auto settings = tr_session::Settings{};
    auto const expected_value = settings.queue_stalled_minutes + 5U;

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, expected_value);
    settings.load(var);
    EXPECT_EQ(expected_value, settings.queue_stalled_minutes);
}

TEST_F(SettingsTest, canSaveSizeT)
{
    static auto constexpr Key = TR_KEY_queue_stalled_minutes;

    auto settings = tr_session::Settings{};
    auto const expected_value = settings.queue_stalled_minutes + 5U;

    settings.queue_stalled_minutes = expected_value;
    auto var = settings.save();
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&var, Key, &val));
    EXPECT_EQ(expected_value, static_cast<size_t>(val));
}

TEST_F(SettingsTest, canLoadString)
{
    static auto constexpr Key = TR_KEY_bind_address_ipv4;
    static auto constexpr ChangedValue = std::string_view{ "127.0.0.1" };

    auto settings = tr_session::Settings{};
    EXPECT_NE(ChangedValue, tr_session::Settings{}.bind_address_ipv4);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, ChangedValue);
    settings.load(var);
    EXPECT_EQ(ChangedValue, settings.bind_address_ipv4);
}

TEST_F(SettingsTest, canSaveString)
{
    static auto constexpr Key = TR_KEY_bind_address_ipv4;
    static auto constexpr ChangedValue = std::string_view{ "127.0.0.1" };

    auto settings = tr_session::Settings{};
    EXPECT_NE(ChangedValue, tr_session::Settings{}.bind_address_ipv4);

    settings.bind_address_ipv4 = ChangedValue;
    auto var = settings.save();
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&var, Key, &val));
    EXPECT_EQ(ChangedValue, val);
}

TEST_F(SettingsTest, canLoadTos)
{
    static auto constexpr Key = TR_KEY_peer_socket_tos;
    static auto constexpr ChangedValue = tr_tos_t{ 0x20 };

    auto settings = std::make_unique<tr_session::Settings>();
    auto const default_value = settings->peer_socket_tos;
    ASSERT_NE(ChangedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, 0x20);
    settings->load(var);
    EXPECT_EQ(ChangedValue, settings->peer_socket_tos);
    var.clear();

    settings = std::make_unique<tr_session::Settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, "cs1");
    settings->load(var);
    EXPECT_EQ(ChangedValue, settings->peer_socket_tos);
}

TEST_F(SettingsTest, canSaveTos)
{
    static auto constexpr Key = TR_KEY_peer_socket_tos;
    static auto constexpr ChangedValue = tr_tos_t{ 0x20 };

    auto settings = tr_session::Settings{};
    ASSERT_NE(ChangedValue, settings.peer_socket_tos);

    settings.peer_socket_tos = tr_tos_t(0x20);
    auto var = settings.save();
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&var, Key, &val));
    EXPECT_EQ(ChangedValue.toString(), val);
}

TEST_F(SettingsTest, canLoadVerify)
{
    static auto constexpr Key = TR_KEY_torrent_added_verify_mode;
    static auto constexpr ChangedValue = TR_VERIFY_ADDED_FULL;

    auto settings = std::make_unique<tr_session::Settings>();
    auto const default_value = settings->torrent_added_verify_mode;
    ASSERT_NE(ChangedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, "full");
    settings->load(var);
    EXPECT_EQ(ChangedValue, settings->torrent_added_verify_mode);
    var.clear();

    settings = std::make_unique<tr_session::Settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, ChangedValue);
    settings->load(var);
    EXPECT_EQ(ChangedValue, settings->torrent_added_verify_mode);
}

TEST_F(SettingsTest, canSaveVerify)
{
    static auto constexpr Key = TR_KEY_torrent_added_verify_mode;
    static auto constexpr ChangedValue = TR_VERIFY_ADDED_FULL;

    auto settings = tr_session::Settings{};
    ASSERT_NE(ChangedValue, settings.torrent_added_verify_mode);

    settings.torrent_added_verify_mode = ChangedValue;
    auto var = settings.save();
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&var, Key, &val));
    EXPECT_EQ("full", val);
}

TEST_F(SettingsTest, canLoadPreferredTransport)
{
    static auto constexpr Key = TR_KEY_preferred_transport;
    auto constexpr ExpectedValue = TR_PREFER_TCP;

    auto settings = std::make_unique<tr_session::Settings>();
    auto const default_value = settings->preferred_transport;
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, ExpectedValue);
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->preferred_transport);
    var.clear();

    settings = std::make_unique<tr_session::Settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddStrView(&var, Key, "tcp");
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->preferred_transport);
}

TEST_F(SettingsTest, canSavePreferredTransport)
{
    static auto constexpr Key = TR_KEY_preferred_transport;
    static auto constexpr ExpectedValue = TR_PREFER_TCP;

    auto settings = tr_session::Settings{};
    auto const default_value = settings.preferred_transport;
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 100);
    settings.preferred_transport = ExpectedValue;
    var = settings.save();
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&var, Key, &val));
    EXPECT_EQ("tcp", val);
}

TEST_F(SettingsTest, canLoadSleepPerSecondsDuringVerify)
{
    static auto constexpr Key = TR_KEY_sleep_per_seconds_during_verify;
    auto constexpr ExpectedValue = 90ms;

    auto settings = std::make_unique<tr_session_settings>();
    auto const default_value = settings->sleep_per_seconds_during_verify;
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, ExpectedValue.count());
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->sleep_per_seconds_during_verify);
    var.clear();

    settings = std::make_unique<tr_session_settings>();
    tr_variantInitDict(&var, 1);
    tr_variantDictAddInt(&var, Key, 90);
    settings->load(var);
    EXPECT_EQ(ExpectedValue, settings->sleep_per_seconds_during_verify);
}

TEST_F(SettingsTest, canSaveSleepPerSecondsDuringVerify)
{
    static auto constexpr Key = TR_KEY_sleep_per_seconds_during_verify;
    static auto constexpr ExpectedValue = 90ms;

    auto settings = tr_session_settings{};
    auto const default_value = settings.sleep_per_seconds_during_verify;
    ASSERT_NE(ExpectedValue, default_value);

    auto var = tr_variant{};
    tr_variantInitDict(&var, 100);
    settings.sleep_per_seconds_during_verify = ExpectedValue;
    var = settings.settings();

    int64_t val_raw;
    EXPECT_TRUE(tr_variantDictFindInt(&var, Key, &val_raw));
    EXPECT_EQ(ExpectedValue, std::chrono::milliseconds{ val_raw });
}