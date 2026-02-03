// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <iostream>

#include <QApplication>
#include <QDir>
#include <QRegularExpression>
#include <QTest>

#include <libtransmission/api-compat.h>
#include <libtransmission/transmission.h>

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
        prefs.set(Prefs::SESSION_IS_REMOTE, true);
        prefs.set(Prefs::SESSION_REMOTE_HOST, QStringLiteral("example.invalid"));
        prefs.set(Prefs::SESSION_REMOTE_PORT, TrDefaultRpcPort);
        prefs.set(Prefs::DOWNLOAD_DIR, sandbox_dir);

        // setup: make a Session
        auto nam = FakeNetworkAccessManager{};
        auto rpc = RpcClient{ nam };
        auto session = Session{ sandbox_dir, prefs, rpc };
        session.restart();

        // action: set Prefs::DOWNLOAD_DIR to a new value
        auto const before = nam.create_count;
        prefs.set(Prefs::DOWNLOAD_DIR, downloads_dir);

        // verify that session_set::download_dir was POSTed
        QVERIFY(nam.create_count > before);
        auto const payload_re = getSessionSetDownloadDirRegEx(initial_style, downloads_dir);
        auto const has_session_set = std::any_of(
            nam.request_bodies.cbegin(),
            nam.request_bodies.cend(),
            [&payload_re](QByteArray const& body)
            {
                auto const str = QString::fromUtf8(body);
                return payload_re.match(str).hasMatch();
            });
        QVERIFY(has_session_set);
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = SessionTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "session-test.moc"
