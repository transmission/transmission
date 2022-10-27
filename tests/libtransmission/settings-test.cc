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
    using Setting = libtransmission::Setting;
    using Settings = libtransmission::SessionSettings;
    using SessionSettings = libtransmission::SessionSettings;
};

TEST_F(SettingsTest, canInstantiate)
{
    auto settings = SessionSettings{};
}

TEST_F(SettingsTest, canGetValues)
{
    auto settings = SessionSettings{};

    EXPECT_EQ(false, settings.get<bool>(Settings::TrashOriginalTorrentFiles));
    EXPECT_NEAR(2.0, settings.get<double>(Settings::RatioLimit), 0.01);
    EXPECT_EQ(TR_ENCRYPTION_PREFERRED, settings.get<tr_encryption_mode>(Settings::Encryption));
    EXPECT_EQ(4, settings.get<int>(Settings::PeerSocketTos));
    EXPECT_EQ(TR_LOG_INFO, settings.get<tr_log_level>(Settings::MessageLevel));
    EXPECT_EQ(022, settings.get<mode_t>(Settings::Umask));
    EXPECT_EQ(tr_port::fromHost(51413), settings.get<tr_port>(Settings::PeerPort));
    EXPECT_EQ(TR_PREALLOCATE_SPARSE, settings.get<tr_preallocation_mode>(Settings::Preallocation));
    EXPECT_EQ(size_t{ 100U }, settings.get<size_t>(Settings::SpeedLimitDown));
    EXPECT_EQ("0.0.0.0"sv, settings.get<std::string>(Settings::RpcBindAddress));
}

TEST_F(SettingsTest, canSetValues)
{
    auto settings = SessionSettings{};

    EXPECT_EQ(false, settings.get<bool>(Settings::TrashOriginalTorrentFiles));

    auto changed = settings.set<bool>(Settings::TrashOriginalTorrentFiles, true);
    EXPECT_TRUE(changed);
    EXPECT_EQ(true, settings.get<bool>(Settings::TrashOriginalTorrentFiles));

    changed = settings.set<bool>(Settings::TrashOriginalTorrentFiles, true);
    EXPECT_FALSE(changed);
}

TEST_F(SettingsTest, canImportBools)
{
    static auto constexpr Field = Settings::SeedQueueEnabled;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<bool>(Field);
    auto const expected_value = !default_value;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddBool(&dict, settings.key(Field), expected_value);
    auto const changed = settings.import(&dict);
    tr_variantClear(&dict);

    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(expected_value, settings.get<bool>(Field));
}

TEST_F(SettingsTest, canImportDoubles)
{
    static auto constexpr Field = Settings::RatioLimit;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<double>(Field);
    auto const expected_value = default_value + 1.0;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddReal(&dict, settings.key(Field), expected_value);
    auto const changed = settings.import(&dict);
    tr_variantClear(&dict);

    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_NEAR(expected_value, settings.get<double>(Field), 0.001);
}

TEST_F(SettingsTest, canImportEncryptionMode)
{
    static auto constexpr Field = Settings::Encryption;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<tr_encryption_mode>(Field);
    auto constexpr ExpectedValue = TR_ENCRYPTION_REQUIRED;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), ExpectedValue);
    auto changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_encryption_mode>(Field));

    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings.key(Field), "required");
    changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_encryption_mode>(Field));
}

TEST_F(SettingsTest, canImportInt)
{
    static auto constexpr Field = Settings::PeerSocketTos;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<int>(Field);
    auto const expected_value = default_value + 1;

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), expected_value);
    auto const changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(expected_value, settings.get<int>(Field));
}

TEST_F(SettingsTest, canImportLogLevel)
{
    static auto constexpr Field = Settings::MessageLevel;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<tr_log_level>(Field);
    auto constexpr ExpectedValue = TR_LOG_DEBUG;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), ExpectedValue);
    auto changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_log_level>(Field));

    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings.key(Field), "debug");
    changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_log_level>(Field));
}

TEST_F(SettingsTest, canImportMode)
{
    static auto constexpr Field = Settings::Umask;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<mode_t>(Field);
    auto constexpr ExpectedValue = mode_t{ 0777 };
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), ExpectedValue);
    auto changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<mode_t>(Field));

    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings.key(Field), "0777");
    changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<mode_t>(Field));
}

TEST_F(SettingsTest, canImportPort)
{
    static auto constexpr Field = Settings::PeerPort;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<tr_port>(Field);
    auto constexpr ExpectedValue = tr_port::fromHost(8080);
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), ExpectedValue.host());
    auto const changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_port>(Field));
}

TEST_F(SettingsTest, canImportPreallocation)
{
    static auto constexpr Field = Settings::Preallocation;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<tr_preallocation_mode>(Field);
    auto constexpr ExpectedValue = TR_PREALLOCATE_FULL;
    ASSERT_NE(ExpectedValue, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), ExpectedValue);
    auto changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_preallocation_mode>(Field));

    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings.key(Field), "full");
    changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(ExpectedValue, settings.get<tr_preallocation_mode>(Field));
}

TEST_F(SettingsTest, canImportSizeT)
{
    static auto constexpr Field = Settings::SeedQueueSize;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<size_t>(Field);
    auto const expected_value = default_value + 5U;
    ASSERT_NE(expected_value, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddInt(&dict, settings.key(Field), expected_value);
    auto changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(expected_value, settings.get<size_t>(Field));
}

TEST_F(SettingsTest, canImportString)
{
    static auto constexpr Field = Settings::BlocklistUrl;

    auto settings = SessionSettings{};
    auto const default_value = settings.get<std::string>(Field);
    auto const expected_value = default_value + "/subpath";
    ASSERT_NE(expected_value, default_value);

    auto dict = tr_variant{};
    tr_variantInitDict(&dict, 1);
    tr_variantDictAddStrView(&dict, settings.key(Field), expected_value);
    auto changed = settings.import(&dict);
    tr_variantClear(&dict);
    EXPECT_EQ(1U, changed.count());
    EXPECT_EQ(true, changed.test(Field));
    EXPECT_EQ(expected_value, settings.get<std::string>(Field));
}
