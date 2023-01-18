// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#include <libtransmission/session-settings.h>

#include "test-fixtures.h"

using namespace std::literals;

class SettingsTest : public ::testing::Test
{
protected:
    using SessionSettings = tr_session_settings;
};

TEST_F(SettingsTest, canInstantiate)
{
    auto settings = tr_session_settings{};

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.save(&dict);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadBools)
{
    static auto constexpr Key = TR_KEY_seed_queue_enabled;

    auto settings = tr_session_settings{};
    auto const expected_value = !settings.seed_queue_enabled;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddBool(&dict, Key, expected_value);
    settings.load(&dict);
    tr_variantClear(&dict);

    EXPECT_EQ(expected_value, settings.seed_queue_enabled);
}

TEST_F(SettingsTest, canSaveBools)
{
    static auto constexpr Key = TR_KEY_seed_queue_enabled;

    auto settings = tr_session_settings{};
    auto const expected_value = !settings.seed_queue_enabled;
    settings.seed_queue_enabled = expected_value;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.save(&dict);
    auto val = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&dict, Key, &val));
    EXPECT_EQ(expected_value, val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadDoubles)
{
    static auto constexpr Key = TR_KEY_ratio_limit;

    auto settings = tr_session_settings{};
    auto const expected_value = settings.ratio_limit + 1.0;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddReal(&dict, Key, expected_value);
    settings.load(&dict);
    EXPECT_NEAR(expected_value, settings.ratio_limit, 0.001);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canSaveDoubles)
{
    static auto constexpr Key = TR_KEY_seed_queue_enabled;

    auto settings = tr_session_settings{};
    auto const default_value = settings.seed_queue_enabled;
    auto const expected_value = !default_value;
    settings.seed_queue_enabled = expected_value;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.save(&dict);
    auto val = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&dict, Key, &val));
    EXPECT_EQ(expected_value, val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadEncryptionMode)
{
    static auto constexpr Key = TR_KEY_encryption;
    static auto constexpr ExpectedValue = TR_ENCRYPTION_REQUIRED;

    auto settings = std::make_unique<tr_session_settings>();
    ASSERT_NE(ExpectedValue, settings->encryption_mode);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, ExpectedValue);
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->encryption_mode);

    settings = std::make_unique<tr_session_settings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, Key, "required");
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->encryption_mode);
}

TEST_F(SettingsTest, canSaveEncryptionMode)
{
    static auto constexpr Key = TR_KEY_encryption;
    static auto constexpr ExpectedValue = TR_ENCRYPTION_REQUIRED;

    auto settings = tr_session_settings{};
    EXPECT_NE(ExpectedValue, settings.seed_queue_enabled);
    settings.encryption_mode = ExpectedValue;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.save(&dict);
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&dict, Key, &val));
    EXPECT_EQ(ExpectedValue, val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadLogLevel)
{
    static auto constexpr Key = TR_KEY_message_level;

    auto settings = std::make_unique<tr_session_settings>();
    auto const default_value = settings->log_level;
    auto constexpr ExpectedValue = TR_LOG_DEBUG;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, ExpectedValue);
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->log_level);

    settings = std::make_unique<tr_session_settings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, Key, "debug");
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->log_level);
}

TEST_F(SettingsTest, canSaveLogLevel)
{
    static auto constexpr Key = TR_KEY_message_level;

    auto settings = tr_session_settings{};
    auto const default_value = settings.log_level;
    auto constexpr ExpectedValue = TR_LOG_DEBUG;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.log_level = ExpectedValue;
    settings.save(&dict);
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&dict, Key, &val));
    EXPECT_EQ(ExpectedValue, val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadMode)
{
    static auto constexpr Key = TR_KEY_umask;

    auto settings = std::make_unique<tr_session_settings>();
    auto const default_value = settings->umask;
    auto constexpr ExpectedValue = tr_mode_t{ 0777 };
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, ExpectedValue);
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->umask);

    settings = std::make_unique<tr_session_settings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, Key, "0777");
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->umask);
}

TEST_F(SettingsTest, canSaveMode)
{
    static auto constexpr Key = TR_KEY_umask;

    auto settings = tr_session_settings{};
    auto const default_value = settings.log_level;
    auto constexpr ExpectedValue = tr_mode_t{ 0777 };
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.umask = ExpectedValue;
    settings.save(&dict);
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&dict, Key, &val));
    EXPECT_EQ("0777", val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadPort)
{
    static auto constexpr Key = TR_KEY_peer_port;

    auto settings = tr_session_settings{};
    auto const default_value = settings.peer_port;
    auto constexpr ExpectedValue = tr_port::fromHost(8080);
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, ExpectedValue.host());
    settings.load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings.peer_port);
}

TEST_F(SettingsTest, canSavePort)
{
    static auto constexpr Key = TR_KEY_peer_port;

    auto settings = tr_session_settings{};
    auto const default_value = settings.peer_port;
    auto constexpr ExpectedValue = tr_port::fromHost(8080);
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.peer_port = ExpectedValue;
    settings.save(&dict);
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&dict, Key, &val));
    EXPECT_EQ(ExpectedValue.host(), val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadPreallocation)
{
    static auto constexpr Key = TR_KEY_preallocation;

    auto settings = std::make_unique<tr_session_settings>();
    auto const default_value = settings->preallocation_mode;
    auto constexpr ExpectedValue = TR_PREALLOCATE_FULL;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, ExpectedValue);
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->preallocation_mode);

    settings = std::make_unique<tr_session_settings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, Key, "full");
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->preallocation_mode);
}

TEST_F(SettingsTest, canSavePreallocation)
{
    static auto constexpr Key = TR_KEY_preallocation;

    auto settings = tr_session_settings{};
    auto const default_value = settings.preallocation_mode;
    auto constexpr ExpectedValue = TR_PREALLOCATE_FULL;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.preallocation_mode = ExpectedValue;
    settings.save(&dict);
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&dict, Key, &val));
    EXPECT_EQ(ExpectedValue, val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadSizeT)
{
    static auto constexpr Key = TR_KEY_queue_stalled_minutes;

    auto settings = tr_session_settings{};
    auto const expected_value = settings.queue_stalled_minutes + 5U;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, expected_value);
    settings.load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(expected_value, settings.queue_stalled_minutes);
}

TEST_F(SettingsTest, canSaveSizeT)
{
    static auto constexpr Key = TR_KEY_queue_stalled_minutes;

    auto settings = tr_session_settings{};
    auto const expected_value = settings.queue_stalled_minutes + 5U;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.queue_stalled_minutes = expected_value;
    settings.save(&dict);
    auto val = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&dict, Key, &val));
    EXPECT_EQ(expected_value, static_cast<size_t>(val));
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadString)
{
    static auto constexpr Key = TR_KEY_bind_address_ipv4;
    static auto constexpr ChangedValue = std::string_view{ "127.0.0.1" };

    auto settings = tr_session_settings{};
    EXPECT_NE(ChangedValue, tr_session_settings{}.bind_address_ipv4);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, Key, ChangedValue);
    settings.load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ChangedValue, settings.bind_address_ipv4);
}

TEST_F(SettingsTest, canSaveString)
{
    static auto constexpr Key = TR_KEY_bind_address_ipv4;
    static auto constexpr ChangedValue = std::string_view{ "127.0.0.1" };

    auto settings = tr_session_settings{};
    EXPECT_NE(ChangedValue, tr_session_settings{}.bind_address_ipv4);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.bind_address_ipv4 = ChangedValue;
    settings.save(&dict);
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&dict, Key, &val));
    EXPECT_EQ(ChangedValue, val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadTos)
{
    static auto constexpr Key = TR_KEY_peer_socket_tos;
    static auto constexpr ChangedValue = tr_tos_t{ 0x20 };

    auto settings = std::make_unique<tr_session_settings>();
    auto const default_value = settings->peer_socket_tos;
    ASSERT_NE(ChangedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, 0x20);
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ChangedValue, settings->peer_socket_tos);

    settings = std::make_unique<tr_session_settings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, Key, "cs1");
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ChangedValue, settings->peer_socket_tos);
}

TEST_F(SettingsTest, canSaveTos)
{
    static auto constexpr Key = TR_KEY_peer_socket_tos;
    static auto constexpr ChangedValue = tr_tos_t{ 0x20 };

    auto settings = tr_session_settings{};
    ASSERT_NE(ChangedValue, settings.peer_socket_tos);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.peer_socket_tos = tr_tos_t(0x20);
    settings.save(&dict);
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&dict, Key, &val));
    EXPECT_EQ(ChangedValue.toString(), val);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadVerify)
{
    static auto constexpr Key = TR_KEY_torrent_added_verify_mode;
    static auto constexpr ChangedValue = TR_VERIFY_ADDED_FULL;

    auto settings = std::make_unique<tr_session_settings>();
    auto const default_value = settings->torrent_added_verify_mode;
    ASSERT_NE(ChangedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, Key, "full");
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ChangedValue, settings->torrent_added_verify_mode);

    settings = std::make_unique<tr_session_settings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, ChangedValue);
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ChangedValue, settings->torrent_added_verify_mode);
}

TEST_F(SettingsTest, canSaveVerify)
{
    static auto constexpr Key = TR_KEY_torrent_added_verify_mode;
    static auto constexpr ChangedValue = TR_VERIFY_ADDED_FULL;

    auto settings = tr_session_settings{};
    ASSERT_NE(ChangedValue, settings.torrent_added_verify_mode);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.torrent_added_verify_mode = ChangedValue;
    settings.save(&dict);
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&dict, Key, &val));
    EXPECT_EQ("full", val);
    tr_variantClear(&dict);
}
