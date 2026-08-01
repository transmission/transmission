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
#include "InteropObject.h"

// ComInteropHelper answers one question: is a Qt client already running, and if
// so, how do I reach it? A helper that claims a connection it does not have
// sends torrents nowhere, and one that starts a client in order to answer
// leaves a stray process holding a torrent the user cannot see.
//
// No client is registered until the last case, which registers one and never
// unregisters it. These run in declaration order.
class ComInteropTest : public QObject
{
    Q_OBJECT

private slots:
    static void initTestCase()
    {
        ComInteropHelper::initialize();
    }

    static void cleanupTestCase()
    {
        ::CoUninitialize();
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
        // An invalid variant is what the caller tests to decide whether the
        // torrent was delegated. A default-constructed `true` would make it
        // drop the torrent and exit.
        auto const response = ComInteropHelper{}.addMetainfo(
            QStringLiteral("magnet:?xt=urn:btih:00000000000000000000000000000000000000ff"));

        QVERIFY(!response.isValid());
    }

    static void registers_and_then_finds_a_running_client()
    {
        ComInteropHelper::registerObject(nullptr);

        QVERIFY(ComInteropHelper{}.isConnected());
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = ComInteropTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "com-interop-test.moc"
