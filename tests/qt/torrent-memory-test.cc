// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <libtransmission/transmission.h>

#include "Prefs.h"
#include "Torrent.h"
#include "VariantHelpers.h"
#include "rpc-test-fixtures.h"

#define private public
#include "Application.h"
#undef private

using ::trqt::variant_helpers::change;
using ::trqt::variant_helpers::dictAdd;
using ::trqt::variant_helpers::variantInit;

class TorrentMemoryLeakTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(trApp != nullptr);
        trApp->model_timer_.stop();
        trApp->stats_timer_.stop();
        trApp->session_timer_.stop();
    }

    void mutableTorrentStringsDoNotGrowInternPool()
    {
        QVERIFY(trApp != nullptr);
        auto* const app = trApp;
        auto const before = app->interned_strings_.size();

        auto prefs = Prefs{};
        auto tor = Torrent{ prefs, 1 };

        for (int i = 0; i < 200; ++i)
        {
            auto const comment = QStringLiteral("leak-comment-%1").arg(i);
            auto const creator = QStringLiteral("leak-creator-%1").arg(i);
            auto const download_dir = QStringLiteral("/tmp/leak-dir-%1").arg(i);
            auto const error_string = QStringLiteral("leak-error-%1").arg(i);

            auto comment_var = tr_variant{};
            auto creator_var = tr_variant{};
            auto download_dir_var = tr_variant{};
            auto error_var = tr_variant{};
            variantInit(&comment_var, comment);
            variantInit(&creator_var, creator);
            variantInit(&download_dir_var, download_dir);
            variantInit(&error_var, error_string);

            auto const keys = std::array{ TR_KEY_comment, TR_KEY_creator, TR_KEY_download_dir, TR_KEY_error_string };
            auto const values = std::array<tr_variant const*, 4U>{ &comment_var, &creator_var, &download_dir_var, &error_var };
            auto const changed = tor.update(std::data(keys), std::data(values), std::size(keys));
            QVERIFY(changed.test(Torrent::COMMENT));
            QVERIFY(changed.test(Torrent::CREATOR));
            QVERIFY(changed.test(Torrent::DOWNLOAD_DIR));
            QVERIFY(changed.test(Torrent::TORRENT_ERROR_STRING));
        }

        QCOMPARE(app->interned_strings_.size(), before);
    }

    void trackerAnnounceDoesNotGrowInternPool()
    {
        QVERIFY(trApp != nullptr);
        auto* const app = trApp;
        auto const before = app->interned_strings_.size();

        auto stat = TrackerStat{};
        for (int i = 0; i < 200; ++i)
        {
            auto const announce = QStringLiteral("https://leak-%1.invalid/announce").arg(i);
            auto tracker_var = tr_variant::make_map(1U);
            dictAdd(&tracker_var, TR_KEY_announce, announce);
            QVERIFY(change(stat, &tracker_var));
            QCOMPARE(stat.announce, announce);
        }

        QCOMPARE(app->interned_strings_.size(), before);
    }
};

int main(int argc, char** argv)
{
    auto sandbox = QTemporaryDir{};
    if (!sandbox.isValid())
    {
        return 1;
    }

    auto const config_dir = sandbox.path();
    QDir{}.mkpath(config_dir);
    auto settings_json = QFile{ QDir{ config_dir }.filePath(QStringLiteral("settings.json")) };
    if (!settings_json.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return 1;
    }
    settings_json.write("{}");
    settings_json.close();

    auto prefs = Prefs{};
    prefs.set(Prefs::SESSION_IS_REMOTE, true);
    prefs.set(Prefs::SESSION_REMOTE_HOST, QStringLiteral("example.invalid"));
    prefs.set(Prefs::SESSION_REMOTE_PORT, TrDefaultRpcPort);

    auto nam = FakeNetworkAccessManager{};
    auto rpc = RpcClient{ nam };
    auto app = Application{ prefs, rpc, true, config_dir, {}, argc, argv };

    auto test = TorrentMemoryLeakTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "torrent-memory-test.moc"
