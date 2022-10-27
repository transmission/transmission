// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "session-settings.h"

#include "test-fixtures.h"

using namespace std::literals;

class SettingsTest : public ::testing::Test
{
protected:
    using SessionSettings = libtransmission::SessionSettings;
};

TEST_F(SettingsTest, canInstantiate)
{
    auto settings = SessionSettings{};

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.save(&dict);
    tr_variantClear(&dict);
}

TEST_F(SettingsTest, canLoadBools)
{
    static auto constexpr Key = TR_KEY_seed_queue_enabled;

    auto settings = SessionSettings{};
    auto const default_value = settings.seed_queue_enabled;
    auto const expected_value = !default_value;

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

    auto settings = SessionSettings{};
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

TEST_F(SettingsTest, canLoadDoubles)
{
    static auto constexpr Key = TR_KEY_ratio_limit;

    auto settings = SessionSettings{};
    auto const default_value = settings.ratio_limit;
    auto const expected_value = default_value + 1.0;

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

    auto settings = SessionSettings{};
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

    auto settings = std::make_unique<SessionSettings>();
    ASSERT_NE(ExpectedValue, settings->encryption_mode);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, Key, ExpectedValue);
    settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(ExpectedValue, settings->encryption_mode);

    settings = std::make_unique<SessionSettings>();
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

    auto settings = SessionSettings{};
    EXPECT_NE(ExpectedValue, settings.seed_queue_enabled);
    settings.encryption_mode = ExpectedValue;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 100);
    settings.save(&dict);
    auto val = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&dict, Key, &val));
    EXPECT_EQ("required"sv, val);
    tr_variantClear(&dict);
}

#if 0
TEST_F(SettingsTest, canLoadInt)
{
    static auto constexpr Field = Settings::PeerSocketTos;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<int>(Field);
    auto const expected_value = default_value + 1;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), expected_value);
    auto const changed = settings.load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(expected_value, settings.get<int>(Field));
}

TEST_F(SettingsTest, canLoadLogLevel)
{
    static auto constexpr Field = Settings::MessageLevel;

    auto settings = std::make_unique<SessionSettings>();
    auto const default_value = settings->get<tr_log_level>(Field);
    auto constexpr ExpectedValue = TR_LOG_DEBUG;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings->key(Field), ExpectedValue);
    auto changed = settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings->get<tr_log_level>(Field));

    settings = std::make_unique<SessionSettings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings->key(Field), "debug");
    changed = settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings->get<tr_log_level>(Field));
}

TEST_F(SettingsTest, canLoadMode)
{
    static auto constexpr Field = Settings::Umask;

    auto settings = std::make_unique<SessionSettings>();
    auto const default_value = settings->get<mode_t>(Field);
    auto constexpr ExpectedValue = mode_t{ 0777 };
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings->key(Field), ExpectedValue);
    auto changed = settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings->get<mode_t>(Field));

    settings = std::make_unique<SessionSettings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings->key(Field), "0777");
    changed = settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings->get<mode_t>(Field));
}

TEST_F(SettingsTest, canLoadPort)
{
    static auto constexpr Field = Settings::PeerPort;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<tr_port>(Field);
    auto constexpr ExpectedValue = tr_port::fromHost(8080);
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), ExpectedValue.host());
    auto const changed = settings.load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_port>(Field));
}

TEST_F(SettingsTest, canLoadPreallocation)
{
    static auto constexpr Field = Settings::Preallocation;

    auto settings = std::make_unique<SessionSettings>();
    auto const default_value = settings->get<tr_preallocation_mode>(Field);
    auto constexpr ExpectedValue = TR_PREALLOCATE_FULL;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings->key(Field), ExpectedValue);
    auto changed = settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings->get<tr_preallocation_mode>(Field));

    settings = std::make_unique<SessionSettings>();
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings->key(Field), "full");
    changed = settings->load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings->get<tr_preallocation_mode>(Field));
}

TEST_F(SettingsTest, canLoadSizeT)
{
    static auto constexpr Field = Settings::SeedQueueSize;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<size_t>(Field);
    auto const expected_value = default_value + 5U;
    ASSERT_NE(expected_value, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), expected_value);
    auto changed = settings.load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(expected_value, settings.get<size_t>(Field));
}

TEST_F(SettingsTest, canLoadString)
{
    static auto constexpr Field = Settings::BlocklistUrl;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<std::string>(Field);
    auto const expected_value = default_value + "/subpath";
    ASSERT_NE(expected_value, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings.key(Field), expected_value);
    auto changed = settings.load(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(expected_value, settings.get<std::string>(Field));
}
#endif
