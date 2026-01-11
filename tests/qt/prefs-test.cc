#include <string_view>

#include <QApplication>
#include <QDateTime>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <fmt/format.h>

#include <libtransmission/quark.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include "CustomVariantType.h"
#include "Filters.h"
#include "Prefs.h"
#include "TrQtInit.h"

using namespace std::literals;

class PrefsTest : public QObject
{
    Q_OBJECT

    [[nodiscard]] static std::string get_json_member_str(int const idx, std::string_view const valstr)
    {
        auto const json_key = tr_quark_get_string_view(Prefs::getKey(idx));
        return fmt::format(R"("{:s}":{:s})", json_key, valstr);
    }

    void verify_json_contains(tr_variant const& var, std::string_view const substr)
    {
        auto serde = tr_variant_serde::json();
        serde.compact();
        auto const str = serde.to_string(var);
        QVERIFY2(tr_strv_contains(str, substr), str.c_str());
    }

    void verify_json_contains(tr_variant const& var, int const idx, std::string_view const val)
    {
        auto serde = tr_variant_serde::json();
        serde.compact();
        auto const str = serde.to_string(var);
        auto const substr = get_json_member_str(idx, val);
        QVERIFY2(tr_strv_contains(str, substr), str.c_str());
    }

    template<typename T>
    void verify_get_set_by_property(Prefs& prefs, int const idx, T const& val1, T const& val2)
    {
        QCOMPARE_NE(val1, val2);

        prefs.set(idx, val1);
        QCOMPARE_EQ(prefs.get<T>(idx), val1);
        QCOMPARE_NE(prefs.get<T>(idx), val2);

        prefs.set(idx, val2);
        QCOMPARE_NE(prefs.get<T>(idx), val1);
        QCOMPARE_EQ(prefs.get<T>(idx), val2);
    }

    template<typename T>
    void verify_get_by_json(Prefs& prefs, int const idx, T const& val, std::string_view const valstr)
    {
        prefs.set(idx, val);
        QCOMPARE_EQ(prefs.get<T>(idx), val);
        verify_json_contains(prefs.current_settings(), idx, valstr);
    }

    template<typename T>
    void verify_set_by_json(Prefs& prefs, int const idx, T const& val, std::string_view const valstr)
    {
        auto const json_object_str = fmt::format(R"({{{:s}}})", get_json_member_str(idx, valstr));
        auto serde = tr_variant_serde::json();
        auto var = serde.parse(json_object_str);
        QVERIFY(var.has_value());
        auto const* const map = var->get_if<tr_variant::Map>();
        QVERIFY(map != nullptr);
        prefs.load(*map);
        QCOMPARE_EQ(prefs.get<T>(idx), val);
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
        auto const ValAStr = fmt::format("{}", ValA);
        auto const ValBStr = fmt::format("{}", ValB);

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    void handles_qstring()
    {
        auto constexpr Idx = Prefs::DOWNLOAD_DIR;
        auto constexpr ValAStr = R"("/tmp/transmission-test-download-dir")"sv;
        auto constexpr ValBStr = R"("/tmp/transmission-test-download-dir-b")"sv;
        auto const ValA = QStringLiteral("/tmp/transmission-test-download-dir");
        auto const ValB = QStringLiteral("/tmp/transmission-test-download-dir-b");

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    void handles_qstringlist()
    {
        auto constexpr Idx = Prefs::COMPLETE_SOUND_COMMAND;
        auto constexpr ValAStr = R"(["one","two","three"])"sv;
        auto constexpr ValBStr = R"(["alpha","beta"])"sv;
        auto const ValA = QStringList{ QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three") };
        auto const ValB = QStringList{ QStringLiteral("alpha"), QStringLiteral("beta") };

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
    }

    void handles_qdatetime()
    {
        auto constexpr Idx = Prefs::BLOCKLIST_DATE;
        auto const ValA = QDateTime::fromMSecsSinceEpoch(1700000000000LL).toUTC();
        auto const ValAStr = fmt::format("{}", ValA.toSecsSinceEpoch());
        auto const ValB = QDateTime::fromMSecsSinceEpoch(1700000000000LL + 123000LL).toUTC();
        auto const ValBStr = fmt::format("{}", ValB.toSecsSinceEpoch());

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Idx, ValA, ValB);
        verify_set_by_json(prefs, Idx, ValA, ValAStr);
        verify_get_by_json(prefs, Idx, ValB, ValBStr);
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

    // ---

    void load_sets_download_dir()
    {
        auto const expected_str = "/tmp/foo/bar"sv;

        auto prefs = Prefs{};
        QCOMPARE_NE(prefs.get<QString>(Prefs::DOWNLOAD_DIR), expected_str);

        auto map = tr_variant::Map{};
        map.try_emplace(TR_KEY_download_dir, expected_str);
        prefs.load(map);

        QCOMPARE_EQ(prefs.get<QString>(Prefs::DOWNLOAD_DIR), expected_str);
    }

    void emit_changed_signal_for_sort_reversed()
    {
        auto prefs = Prefs{};
        QSignalSpy spy(&prefs, &Prefs::changed);

        auto const old_value = prefs.get<bool>(Prefs::SORT_REVERSED);
        prefs.set(Prefs::SORT_REVERSED, !old_value);

        QCOMPARE(spy.count(), 1);
        auto const signal_args = spy.takeFirst();
        QCOMPARE(signal_args.at(0).toInt(), Prefs::SORT_REVERSED);
    }

    void ignore_changed_signal_if_value_unchanged()
    {
        auto prefs = Prefs{};
        QSignalSpy spy(&prefs, &Prefs::changed);

        auto const current_value = prefs.get<bool>(Prefs::SORT_REVERSED);
        prefs.set(Prefs::SORT_REVERSED, current_value);

        QCOMPARE(spy.count(), 0);
    }
};

int main(int argc, char** argv)
{
    trqt::trqt_init();
    QApplication app{ argc, argv };
    PrefsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "prefs-test.moc"
