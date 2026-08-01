// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <windows.h>
#include <objbase.h>

#include <QApplication>
#include <QString>
#include <QTest>
#include <QVariant>

#include "ComInteropHelper.h"

// ComInteropHelper answers one question: is a Qt client already running, and if
// so, how do I reach it? Nothing in this file registers a client, so every
// lookup here has to come back empty.
//
// A helper that claims a connection it does not have sends torrents nowhere. A
// helper that starts a client in order to answer the question leaves a stray
// process behind and hands the torrent to it, which looks to the user like the
// torrent opened a second copy of the app.
class ComInteropTest : public QObject
{
    Q_OBJECT

private slots:
    static void initTestCase()
    {
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }

    static void cleanupTestCase()
    {
        ::CoUninitialize();
    }

    static void does_not_connect_when_no_client_is_running()
    {
        auto const helper = ComInteropHelper{};

        QVERIFY(!helper.isConnected());
    }

    static void does_not_start_a_client_to_answer_the_query()
    {
        // Asking twice catches a lookup that answers itself by starting a
        // client: the second helper would find what the first one launched.
        auto const first = ComInteropHelper{};
        QVERIFY(!first.isConnected());

        auto const second = ComInteropHelper{};
        QVERIFY(!second.isConnected());
    }

    static void add_metainfo_reports_failure_when_not_connected()
    {
        auto const helper = ComInteropHelper{};
        QVERIFY(!helper.isConnected());

        auto const response = helper.addMetainfo(
            QStringLiteral("magnet:?xt=urn:btih:00000000000000000000000000000000000000ff"));

        // An invalid variant is what the caller tests to decide whether the
        // torrent was delegated. A default-constructed `true` would make it
        // drop the torrent and exit.
        QVERIFY(!response.isValid());
        QVERIFY(!response.toBool());
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = ComInteropTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "com-interop-test.moc"
