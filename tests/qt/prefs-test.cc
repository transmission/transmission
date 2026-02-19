// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>
#include <type_traits>

#include <QApplication>
#include <QDateTime>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <fmt/format.h>

#include <libtransmission/quark.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/variant.h>

#include "Prefs.h"
#include "TrQtInit.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
#define QCOMPARE_EQ(actual, expected) QCOMPARE(actual, expected)
#define QCOMPARE_NE(actual, expected) QVERIFY((actual) != (expected))
#endif

using namespace std::literals;

class PrefsTest : public QObject
{
    Q_OBJECT

    template<typename T>
    static void compare_eq(T const& actual, T const& expected)
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            QVERIFY(qFuzzyCompare(actual, expected));
        }
        else
        {
            QCOMPARE_EQ(actual, expected);
        }
    }

    template<typename T>
    static void compare_ne(T const& actual, T const& expected)
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            QVERIFY(!qFuzzyCompare(actual, expected));
        }
        else
        {
            QCOMPARE_NE(actual, expected);
        }
    }

    [[nodiscard]] static std::string get_json_member_str(tr_quark const key, std::string_view const valstr)
    {
        auto const json_key = tr_quark_get_string_view(key);
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
    void verify_get_set_by_property(Prefs& prefs, int const idx, T const& val1, T const& val2)
    {
        compare_ne(val1, val2);

        prefs.set(idx, val1);
        compare_eq(prefs.get<T>(idx), val1);
        compare_ne(prefs.get<T>(idx), val2);

        prefs.set(idx, val2);
        compare_ne(prefs.get<T>(idx), val1);
        compare_eq(prefs.get<T>(idx), val2);
    }

    template<typename T>
    void verify_get_by_json(Prefs& prefs, int const idx, T const& val, std::string_view const valstr)
    {
        prefs.set(idx, val);
        compare_eq(prefs.get<T>(idx), val);
        verify_json_contains(prefs.current_settings(), prefs.keyval(idx).first, valstr);
    }

    template<typename T>
    void verify_set_by_json(Prefs& prefs, int const idx, T const& val, std::string_view const valstr)
    {
        auto const json_object_str = fmt::format(R"({{{:s}}})", get_json_member_str(prefs.keyval(idx).first, valstr));
        auto serde = tr_variant_serde::json();
        auto const var = serde.parse(json_object_str);
        QVERIFY(var.has_value());
        // IDK why clang-tidy doesn't see the QVERIFY check above?
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        auto const* const map = var->get_if<tr_variant::Map>();
        QVERIFY(map != nullptr);
        prefs.load(*map);
        compare_eq(prefs.get<T>(idx), val);
    }

    static void verify_variant_json(tr_variant const& var, std::string_view const expected)
    {
        auto serde = tr_variant_serde::json();
        serde.compact();
        auto const str = serde.to_string(var);
        QCOMPARE_EQ(str, std::string{ expected });
    }

private slots:
    void handles_bool()
    {
        auto constexpr Idx = Prefs::SORT_REVERSED;
        auto constexpr ValA = false;
        auto constexpr ValAStr = "false"sv;
        auto constexpr ValB = true;
        auto constexpr ValBStr = "true"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    void handles_int()
    {
        auto constexpr Idx = Prefs::MAIN_WINDOW_HEIGHT;
        auto constexpr ValA = 4242;
        auto constexpr ValAStr = "4242"sv;
        auto constexpr ValB = 2323;
        auto constexpr ValBStr = "2323"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    void handles_double()
    {
        auto constexpr Idx = Prefs::RATIO;
        auto constexpr ValA = 1.234;
        auto constexpr ValB = 5.678;
        auto const val_a_str = fmt::format("{}", ValA);
        auto const val_b_str = fmt::format("{}", ValB);

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, val_a_str);
        verify_get_by_json(prefs, Idx, ValB, val_b_str);
    }

    void handles_qstring()
    {
        auto constexpr Idx = Prefs::DOWNLOAD_DIR;
        auto constexpr ValAStr = R"("/tmp/transmission-test-download-dir")"sv;
        auto constexpr ValBStr = R"("/tmp/transmission-test-download-dir-b")"sv;
        auto const val_a = QStringLiteral("/tmp/transmission-test-download-dir");
        auto const val_b = QStringLiteral("/tmp/transmission-test-download-dir-b");

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, val_a, val_b);
        verify_set_by_json(prefs, Idx, val_a, ValAStr);
        verify_get_by_json(prefs, Idx, val_b, ValBStr);
    }

    void handles_qstringlist()
    {
        auto constexpr Idx = Prefs::COMPLETE_SOUND_COMMAND;
        auto constexpr ValAStr = R"(["one","two","three"])"sv;
        auto constexpr ValBStr = R"(["alpha","beta"])"sv;
        auto const val_a = QStringList{ QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three") };
        auto const val_b = QStringList{ QStringLiteral("alpha"), QStringLiteral("beta") };

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, val_a, val_b);
        verify_set_by_json(prefs, Idx, val_a, ValAStr);
        verify_get_by_json(prefs, Idx, val_b, ValBStr);
    }

    void handles_qdatetime()
    {
        auto constexpr Idx = Prefs::BLOCKLIST_DATE;
        auto const val_a = QDateTime::fromMSecsSinceEpoch(1700000000000LL).toUTC();
        auto const val_a_str = fmt::format("{}", val_a.toSecsSinceEpoch());
        auto const val_b = QDateTime::fromMSecsSinceEpoch(1700000000000LL + 123000LL).toUTC();
        auto const val_b_str = fmt::format("{}", val_b.toSecsSinceEpoch());

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, val_a, val_b);
        verify_set_by_json(prefs, Idx, val_a, val_a_str);
        verify_get_by_json(prefs, Idx, val_b, val_b_str);
    }

    void handles_sortmode()
    {
        auto constexpr Idx = Prefs::SORT_MODE;
        auto constexpr ValA = SortMode::SortBySize;
        auto constexpr ValAStr = R"("sort_by_size")"sv;
        auto constexpr ValB = SortMode::SortByName;
        auto constexpr ValBStr = R"("sort_by_name")"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    void handles_showmode()
    {
        auto constexpr Idx = Prefs::FILTER_MODE;
        auto constexpr ValA = ShowMode::ShowAll;
        auto constexpr ValAStr = R"("show_all")"sv;
        auto constexpr ValB = ShowMode::ShowActive;
        auto constexpr ValBStr = R"("show_active")"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    void handles_encryptionmode()
    {
        auto constexpr Idx = Prefs::ENCRYPTION;
        auto constexpr ValA = TR_ENCRYPTION_REQUIRED;
        auto constexpr ValAStr = R"("required")"sv;
        auto constexpr ValB = TR_ENCRYPTION_PREFERRED;
        auto constexpr ValBStr = R"("preferred")"sv;

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    static void keyval_returns_key_and_value()
    {
        auto prefs = Prefs{};

        {
            auto constexpr Idx = Prefs::MAIN_WINDOW_HEIGHT;
            auto constexpr Val = 4242;
            prefs.set(Idx, Val);

            auto const [key, var] = prefs.keyval(Idx);
            QCOMPARE_EQ(key, TR_KEY_main_window_height);
            verify_variant_json(var, fmt::format("{}", Val));
        }

        {
            auto constexpr Idx = Prefs::DOWNLOAD_DIR;
            auto const val = QStringLiteral("/tmp/transmission-test-download-dir");
            prefs.set(Idx, val);

            auto const [key, var] = prefs.keyval(Idx);
            QCOMPARE_EQ(key, TR_KEY_download_dir);
            verify_variant_json(var, fmt::format(R"("{}")", val.toStdString()));
        }
    }

    // ---

    static void changed_signal_emits_when_change()
    {
        static auto constexpr Idx = Prefs::SORT_REVERSED;

        auto prefs = Prefs{};
        auto const spy = QSignalSpy{ &prefs, &Prefs::changed };

        auto const old_value = prefs.get<bool>(Idx);
        auto const new_value = !old_value;
        prefs.set(Idx, new_value);
        QCOMPARE(spy.count(), 1);
        auto const& signal_args = spy.first();
        QCOMPARE(signal_args.at(0).toInt(), Idx);
    }

    static void changed_signal_does_not_emit_when_unchanged()
    {
        static auto constexpr Idx = Prefs::SORT_REVERSED;

        auto prefs = Prefs{};
        auto const spy = QSignalSpy{ &prefs, &Prefs::changed };

        auto const current_value = prefs.get<bool>(Idx);
        prefs.set(Idx, current_value);
        QCOMPARE(spy.count(), 0);
    }
};

int main(int argc, char** argv)
{
    trqt::trqt_init();
    auto const app = QApplication{ argc, argv };
    auto test = PrefsTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "prefs-test.moc"
