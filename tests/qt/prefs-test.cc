// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <fmt/format.h>

#include <libtransmission/api-compat.h>
#include <libtransmission/quark.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/variant.h>

#include "Prefs.h"
#include "TrQtInit.h"
#include "qt-test-fixtures.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
#define QCOMPARE_EQ(actual, expected) QCOMPARE(actual, expected)
#define QCOMPARE_NE(actual, expected) QVERIFY((actual) != (expected))
#endif

using namespace std::literals;

Q_DECLARE_METATYPE(tr_quark)

class PrefsTest
    : public QObject
    , SandboxedTest
{
    Q_OBJECT

    [[nodiscard]] static std::string get_json_member_str(tr_quark const key, std::string_view const valstr)
    {
        auto const json_key = tr_quark_get_string_view<char>(key);
        return fmt::format(R"("{:s}":{:s})", json_key, valstr);
    }

    static void verify_json_contains(tr_variant const& var, std::string_view const substr)
    {
        auto serde = tr_variant_serde::json();
        serde.compact();
        auto const str = serde.to_string(var);
        QVERIFY2(tr_strv_contains(str, substr), str.c_str());
    }

    static void verify_json_contains(tr_variant const& var, tr_quark const key, std::string_view const val)
    {
        auto serde = tr_variant_serde::json();
        serde.compact();
        auto const str = serde.to_string(var);
        auto const substr = get_json_member_str(key, val);
        QVERIFY2(tr_strv_contains(str, substr), str.c_str());
    }

    template<typename T>
    void verify_get_set_by_property(Prefs& prefs, tr_quark const key, T const& val1, T const& val2)
    {
        QCOMPARE_NE(val1, val2);

        prefs.set(key, val1);
        QCOMPARE_EQ(prefs.get<T>(key), val1);
        QCOMPARE_NE(prefs.get<T>(key), val2);

        prefs.set(key, val2);
        QCOMPARE_NE(prefs.get<T>(key), val1);
        QCOMPARE_EQ(prefs.get<T>(key), val2);
    }

    template<typename T>
    void verify_get_by_json(Prefs& prefs, tr_quark const key, T const& val, std::string_view const valstr)
    {
        prefs.set(key, val);
        QCOMPARE_EQ(prefs.get<T>(key), val);
        verify_json_contains(prefs.current_settings(), prefs.keyval(key).first, valstr);
    }

    template<typename T>
    static void verify_set_by_json(tr_quark const key, T const& val, std::string_view const valstr)
    {
        auto const json_object_str = fmt::format(R"({{{:s}}})", get_json_member_str(key, valstr));
        auto serde = tr_variant_serde::json();
        auto const var = serde.parse(json_object_str);
        QVERIFY(var.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        auto const prefs = Prefs{ *var };
        QCOMPARE_EQ(prefs.get<T>(key), val);
    }

    static void verify_variant_json(tr_variant const& var, std::string_view const expected)
    {
        auto serde = tr_variant_serde::json();
        serde.compact();
        auto const str = serde.to_string(var);
        QCOMPARE_EQ(str, std::string{ expected });
    }

private slots:
    void initializes_typed_defaults()
    {
        auto prefs = Prefs{};

        QCOMPARE_EQ(prefs.get<ShowMode>(TR_KEY_filter_mode), DefaultShowMode);
        QCOMPARE_EQ(prefs.get<SortMode>(TR_KEY_sort_mode), DefaultSortMode);
        QCOMPARE_EQ(prefs.get<StatsMode>(TR_KEY_statusbar_stats), DefaultStatsMode);
        QCOMPARE_EQ(prefs.get<QDateTime>(TR_KEY_blocklist_date), QDateTime::fromSecsSinceEpoch(0));
        QCOMPARE_EQ(prefs.get<bool>(TR_KEY_torrent_complete_sound_enabled), true);
        QCOMPARE_EQ(prefs.get<bool>(TR_KEY_show_statusbar), true);
    }

    void handles_bool()
    {
        auto constexpr Key = TR_KEY_sort_reversed;
        auto constexpr ValA = false;
        auto constexpr ValAStr = "false"sv;
        auto constexpr ValB = true;
        auto constexpr ValBStr = "true"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, ValA, ValB);
        verify_set_by_json(Key, ValA, ValAStr);
        verify_get_by_json(prefs, Key, ValB, ValBStr);
    }

    void handles_int()
    {
        auto constexpr Key = TR_KEY_main_window_height;
        auto constexpr ValA = 4242;
        auto constexpr ValAStr = "4242"sv;
        auto constexpr ValB = 2323;
        auto constexpr ValBStr = "2323"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, ValA, ValB);
        verify_set_by_json(Key, ValA, ValAStr);
        verify_get_by_json(prefs, Key, ValB, ValBStr);
    }

    void handles_double()
    {
        auto constexpr Key = TR_KEY_seed_ratio_limit;
        auto constexpr ValA = 1.234;
        auto constexpr ValB = 5.678;
        auto const val_a_str = fmt::format("{}", ValA);
        auto const val_b_str = fmt::format("{}", ValB);

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, ValA, ValB);
        verify_set_by_json(Key, ValA, val_a_str);
        verify_get_by_json(prefs, Key, ValB, val_b_str);
    }

    void handles_qstring()
    {
        auto constexpr Key = TR_KEY_download_dir;
        auto constexpr ValAStr = R"("/tmp/transmission-test-download-dir")"sv;
        auto constexpr ValBStr = R"("/tmp/transmission-test-download-dir-b")"sv;
        auto const val_a = QStringLiteral("/tmp/transmission-test-download-dir");
        auto const val_b = QStringLiteral("/tmp/transmission-test-download-dir-b");

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, val_a, val_b);
        verify_set_by_json(Key, val_a, ValAStr);
        verify_get_by_json(prefs, Key, val_b, ValBStr);
    }

    void handles_qstringlist()
    {
        auto constexpr Key = TR_KEY_torrent_complete_sound_command;
        auto constexpr ValAStr = R"(["one","two","three"])"sv;
        auto constexpr ValBStr = R"(["alpha","beta"])"sv;
        auto const val_a = QStringList{ QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three") };
        auto const val_b = QStringList{ QStringLiteral("alpha"), QStringLiteral("beta") };

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, val_a, val_b);
        verify_set_by_json(Key, val_a, ValAStr);
        verify_get_by_json(prefs, Key, val_b, ValBStr);
    }

    void handles_qdatetime()
    {
        auto constexpr Key = TR_KEY_blocklist_date;
        auto const val_a = QDateTime::fromMSecsSinceEpoch(1700000000000LL).toUTC();
        auto const val_a_str = fmt::format("{}", val_a.toSecsSinceEpoch());
        auto const val_b = QDateTime::fromMSecsSinceEpoch(1700000000000LL + 123000LL).toUTC();
        auto const val_b_str = fmt::format("{}", val_b.toSecsSinceEpoch());

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, val_a, val_b);
        verify_set_by_json(Key, val_a, val_a_str);
        verify_get_by_json(prefs, Key, val_b, val_b_str);
    }

    void handles_sortmode()
    {
        auto constexpr Key = TR_KEY_sort_mode;
        auto constexpr ValA = SortMode::SortBySize;
        auto constexpr ValAStr = R"("sort_by_size")"sv;
        auto constexpr ValB = SortMode::SortByName;
        auto constexpr ValBStr = R"("sort_by_name")"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, ValA, ValB);
        verify_set_by_json(Key, ValA, ValAStr);
        verify_get_by_json(prefs, Key, ValB, ValBStr);
    }

    void handles_showmode()
    {
        auto constexpr Key = TR_KEY_filter_mode;
        auto constexpr ValA = ShowMode::ShowAll;
        auto constexpr ValAStr = R"("show_all")"sv;
        auto constexpr ValB = ShowMode::ShowActive;
        auto constexpr ValBStr = R"("show_active")"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, ValA, ValB);
        verify_set_by_json(Key, ValA, ValAStr);
        verify_get_by_json(prefs, Key, ValB, ValBStr);
    }

    void handles_encryptionmode()
    {
        auto constexpr Key = TR_KEY_encryption;
        auto constexpr ValA = TR_ENCRYPTION_REQUIRED;
        auto constexpr ValAStr = R"("required")"sv;
        auto constexpr ValB = TR_ENCRYPTION_PREFERRED;
        auto constexpr ValBStr = R"("preferred")"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, ValA, ValB);
        verify_set_by_json(Key, ValA, ValAStr);
        verify_get_by_json(prefs, Key, ValB, ValBStr);
    }

    static void keyval_returns_key_and_value()
    {
        auto prefs = Prefs{};

        {
            auto constexpr Key = TR_KEY_main_window_height;
            auto constexpr Val = 4242;
            prefs.set(Key, Val);

            auto const [key, var] = prefs.keyval(Key);
            QCOMPARE_EQ(key, TR_KEY_main_window_height);
            verify_variant_json(var, fmt::format("{}", Val));
        }

        {
            auto constexpr Key = TR_KEY_download_dir;
            auto const val = QStringLiteral("/tmp/transmission-test-download-dir");
            prefs.set(Key, val);

            auto const [key, var] = prefs.keyval(Key);
            QCOMPARE_EQ(key, TR_KEY_download_dir);
            verify_variant_json(var, fmt::format(R"("{}")", val.toStdString()));
        }
    }

    // ---

    static void changed_signal_emits_when_change()
    {
        static auto constexpr Key = TR_KEY_sort_reversed;

        auto prefs = Prefs{};
        auto const spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const old_value = prefs.get<bool>(Key);
        auto const new_value = !old_value;
        prefs.set(Key, new_value);
        QCOMPARE(spy.count(), 1);
        auto const& signal_args = spy.first();
        QCOMPARE(signal_args.at(0).value<tr_quark>(), Key);
    }

    static void changed_signal_does_not_emit_when_unchanged()
    {
        static auto constexpr Key = TR_KEY_sort_reversed;

        auto prefs = Prefs{};
        auto const spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const current_value = prefs.get<bool>(Key);
        prefs.set(Key, current_value);
        QCOMPARE(spy.count(), 0);
    }

    static void quark_api_uses_raw_keys_and_emits_quark_signal()
    {
        static auto constexpr QuarkKey = TR_KEY_sort_reversed;

        auto prefs = Prefs{};
        auto const quark_spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const old_value = prefs.get<bool>(QuarkKey);
        auto const new_value = !old_value;
        prefs.set(QuarkKey, new_value);

        QCOMPARE_EQ(prefs.get<bool>(QuarkKey), new_value);
        QCOMPARE(quark_spy.count(), 1);
        QCOMPARE(quark_spy.first().at(0).value<tr_quark>(), static_cast<tr_quark>(QuarkKey));

        prefs.set(QuarkKey, new_value);
        QCOMPARE(quark_spy.count(), 1);
    }

    static void quark_variant_set_ignores_wrong_type()
    {
        static auto constexpr QuarkKey = TR_KEY_sort_reversed;

        auto prefs = Prefs{};
        auto const quark_spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const current_value = prefs.get<bool>(QuarkKey);
        prefs.set(QuarkKey, tr_variant{ "wrong-type"sv });

        QCOMPARE_EQ(prefs.get<bool>(QuarkKey), current_value);
        QCOMPARE(quark_spy.count(), 0);
    }

    static void current_settings_excludes_non_savable_keys()
    {
        auto prefs = Prefs{};

        prefs.set(TR_KEY_filter_text, QStringLiteral("needle"));
        prefs.set(TR_KEY_sort_reversed, true);

        auto const settings = prefs.current_settings();
        QVERIFY(!settings.contains(TR_KEY_filter_text));
        QVERIFY(settings.contains(TR_KEY_sort_reversed));
    }

    void save_merges_existing_file_and_round_trips_serializer_backed_values()
    {
        auto const sandbox_dir = sandboxDir();

        auto const settings_file = QDir{ sandbox_dir }.filePath(QStringLiteral("settings.json"));
        auto constexpr CustomKeyName = "custom-setting"sv;
        auto const custom_key = tr_quark_new(CustomKeyName);

        auto existing_settings = tr_variant::Map{};
        existing_settings.insert_or_assign(custom_key, 123);
        existing_settings.insert_or_assign(TR_KEY_download_dir, "/tmp/stale-download-dir"sv);
        existing_settings.insert_or_assign(TR_KEY_filter_text, "stale-filter"sv);
        tr_variant_serde::json().to_file(tr_variant{ std::move(existing_settings) }, settings_file.toStdString());

        auto const download_dir = QDir{ sandbox_dir }.filePath(QStringLiteral("Downloads"));
        auto const blocklist_date = QDateTime::fromSecsSinceEpoch(1700000000).toUTC();

        auto prefs = Prefs{};
        prefs.set(TR_KEY_download_dir, download_dir);
        prefs.set(TR_KEY_filter_text, QStringLiteral("volatile-filter"));
        prefs.set(TR_KEY_sort_mode, SortMode::SortByQueue);
        prefs.set(TR_KEY_blocklist_date, blocklist_date);
        prefs.save(settings_file);

        auto saved = tr_variant_serde::json().parse_file(settings_file.toStdString());
        QVERIFY(saved.has_value());

        auto const* const saved_map = saved->get_if<tr_variant::Map>();
        QVERIFY(saved_map != nullptr);
        QVERIFY(!saved_map->contains(TR_KEY_filter_text));

        auto const custom_value = saved_map->value_if<int64_t>(custom_key);
        QVERIFY(custom_value.has_value());
        QCOMPARE_EQ(*custom_value, 123);

        tr::api_compat::convert_incoming_data(*saved);
        auto round_tripped = Prefs{ *saved };
        QCOMPARE_EQ(round_tripped.get<QString>(TR_KEY_download_dir), download_dir);
        QCOMPARE_EQ(round_tripped.get<SortMode>(TR_KEY_sort_mode), SortMode::SortByQueue);
        QCOMPARE_EQ(round_tripped.get<QDateTime>(TR_KEY_blocklist_date), blocklist_date);
        QCOMPARE_EQ(round_tripped.get<QString>(TR_KEY_filter_text), QString{});
    }

    static void is_core_classifies_core_keys()
    {
        QVERIFY(Prefs::isCore(TR_KEY_download_dir));
        QVERIFY(Prefs::isCore(TR_KEY_rpc_enabled));
        QVERIFY(!Prefs::isCore(TR_KEY_filter_text));
        QVERIFY(!Prefs::isCore(TR_KEY_show_statusbar));
    }
};

int main(int argc, char** argv)
{
    qRegisterMetaType<tr_quark>("tr_quark");
    trqt::trqt_init();
    auto const app = QApplication{ argc, argv };
    auto test = PrefsTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "prefs-test.moc"
