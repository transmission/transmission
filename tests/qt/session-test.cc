// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <iostream>
#include <ranges>

#include <QApplication>
#include <QDir>
#include <QRegularExpression>
#include <QTest>

#include <libtransmission/transmission.h>
#include <libtransmission/api-compat.h>
#include <libtransmission/env.h>

#include "Prefs.h"
#include "Session.h"
#include "qt-test-fixtures.h"
#include "rpc-test-fixtures.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
#define QCOMPARE_EQ(actual, expected) QCOMPARE(actual, expected)
#define QCOMPARE_NE(actual, expected) QVERIFY((actual) != (expected))
#endif

namespace api_compat = tr::api_compat;
using Style = api_compat::Style;

Q_DECLARE_METATYPE(Style)

namespace
{
[[nodiscard]] QRegularExpression getSessionSetDownloadDirRegEx(Style const style, QString dir)
{
    dir = QRegularExpression::escape(dir);

    switch (style)
    {
    case Style::Tr4:
        return QRegularExpression{
            QStringLiteral(R"(^\{"arguments":\{"download-dir":"%1"\},"method":"session-set","tag":[0-9]+\}$)").arg(dir)
        };
    case Style::Tr5:
        return QRegularExpression{
            QStringLiteral(R"(^\{"id":[0-9]+,"jsonrpc":"2\.0","method":"session_set","params":\{"download_dir":"%1"\}\}$)")
                .arg(dir)
        };
    }

    abort();
    return {};
}
} // namespace

class SessionTest
    : public QObject
    , SandboxedTest
{
    Q_OBJECT

private slots:
    static void download_dir_change_posts_session_set_data()
    {
        QTest::addColumn<Style>("initial_style");
        QTest::newRow("Tr4") << Style::Tr4;
        QTest::newRow("Tr5") << Style::Tr5;
    }
    void download_dir_change_posts_session_set()
    {
        // setup: set api_compat style
        QFETCH(Style const, initial_style);
        api_compat::set_default_style(initial_style);

        // setup: the sandbox
        auto const sandbox_dir = sandboxDir();
        auto const downloads_dir = QDir{ sandbox_dir }.filePath(QStringLiteral("Downloads"));
        QDir{}.mkpath(downloads_dir);

        // setup: make a Prefs that points to a remote session
        auto prefs = Prefs{};
        prefs.set(TR_KEY_remote_session_enabled, true);
        prefs.set(TR_KEY_remote_session_host, QStringLiteral("example.invalid"));
        prefs.set(TR_KEY_remote_session_port, TrDefaultRpcPort);
        prefs.set(TR_KEY_download_dir, sandbox_dir);

        // setup: make a Session
        auto nam = FakeNetworkAccessManager{};
        auto rpc = RpcClient{ nam };
        auto session = Session{ sandbox_dir, prefs, rpc };
        session.restart();

        // action: set TR_KEY_download_dir to a new value
        auto const before = nam.create_count;
        prefs.set(TR_KEY_download_dir, downloads_dir);

        // verify that session_set::download_dir was POSTed
        QVERIFY(nam.create_count > before);
        auto const payload_re = getSessionSetDownloadDirRegEx(initial_style, downloads_dir);
        auto const has_session_set = std::ranges::any_of(
            std::as_const(nam.request_bodies),
            [&payload_re](QByteArray const& body)
            {
                auto const str = QString::fromUtf8(body);
                return payload_re.match(str).hasMatch();
            });
        QVERIFY(has_session_set);
    }

    static void main_window_torrent_refreshes_request_is_stalled_data()
    {
        QTest::addColumn<Style>("initial_style");
        QTest::addColumn<QString>("method");
        QTest::addColumn<QString>("field");
        QTest::newRow("Tr4") << Style::Tr4 << QStringLiteral(R"("method":"torrent-get")") << QStringLiteral(R"("isStalled")");
        QTest::newRow("Tr5") << Style::Tr5 << QStringLiteral(R"("method":"torrent_get")") << QStringLiteral(R"("is_stalled")");
    }
    void main_window_torrent_refreshes_request_is_stalled()
    {
        // setup: set api_compat style
        QFETCH(Style const, initial_style);
        QFETCH(QString const, method);
        QFETCH(QString const, field);
        api_compat::set_default_style(initial_style);

        // setup: make a Session that points to a remote session
        auto const sandbox_dir = sandboxDir();
        auto prefs = Prefs{};
        prefs.set(TR_KEY_remote_session_enabled, true);
        prefs.set(TR_KEY_remote_session_host, QStringLiteral("example.invalid"));
        prefs.set(TR_KEY_remote_session_port, TrDefaultRpcPort);
        auto nam = FakeNetworkAccessManager{};
        auto rpc = RpcClient{ nam };
        auto session = Session{ sandbox_dir, prefs, rpc };
        session.restart();

        // action: do the main window's initial and periodic torrent refreshes
        auto const before = nam.request_bodies.size();
        session.initTorrents();
        session.refreshAllTorrents();

        // verify that both torrent_get requests asked for is_stalled
        auto const bodies = nam.request_bodies.mid(before);
        auto const is_torrent_get = [&method](QByteArray const& body)
        {
            return QString::fromUtf8(body).contains(method);
        };
        auto const requests_field = [&field](QByteArray const& body)
        {
            return QString::fromUtf8(body).contains(field);
        };
        QCOMPARE(std::ranges::count_if(bodies, is_torrent_get), 2);
        QVERIFY(std::ranges::all_of(bodies | std::views::filter(is_torrent_get), requests_field));
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = SessionTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "session-test.moc"
