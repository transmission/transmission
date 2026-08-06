// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>
#include <QTest>

#include <libtransmission-app/interop.h>

#include "AddData.h"

using namespace std::string_view_literals;

// What AddMetainfo carries. This is a wire contract with clients built from other sources.
class AddDataTest : public QObject
{
    Q_OBJECT

private slots:
    void readsATorrentFileAsItsContents();
    void readsALinkAsItself();
    void refusesAPathNamingAFileOnThisHost();
    void refusesWhatIsNeitherFormAtAll();
};

namespace
{

// The smallest thing tr_torrent_metainfo will accept: one 1-byte file, one 20-byte piece hash.
[[nodiscard]] QByteArray minimalTorrent()
{
    return QByteArray{ "d4:infod6:lengthi1e4:name1:a12:piece lengthi16384e6:pieces20:" } + QByteArray(20, '\x01') +
        QByteArray{ "ee" };
}

} // namespace

// The round trip both ends of the wire perform. The launch encodes the file it read,
// and the client that took the launch reads the contents back out.
void AddDataTest::readsATorrentFileAsItsContents()
{
    auto dir = QTemporaryDir{};
    QVERIFY(dir.isValid());

    auto const path = dir.path() + QStringLiteral("/ok.torrent");
    auto file = QFile{ path };
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(minimalTorrent());
    file.close();

    auto const encoded = tr::interop::encode_metainfo_arg(path.toStdString());
    QVERIFY(encoded.has_value());

    auto const parsed = AddData::fromWireMetainfo(*encoded);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->type, static_cast<int>(AddData::METAINFO));
    QCOMPARE(parsed->metainfo, minimalTorrent());
}

void AddDataTest::readsALinkAsItself()
{
    auto const url = AddData::fromWireMetainfo("https://example.com/x.torrent"sv);
    QVERIFY(url.has_value());
    QCOMPARE(url->type, static_cast<int>(AddData::URL));

    auto const magnet = AddData::fromWireMetainfo(
        "magnet:?xt=urn:btih:"
        "0000000000000000000000000000000000000000"sv);
    QVERIFY(magnet.has_value());
    QCOMPARE(magnet->type, static_cast<int>(AddData::MAGNET));
}

// A bare path is not a form AddMetainfo defines. Reading one would open a file the sender
// named and this process can reach. Across a bus, the sender need not even own it.
void AddDataTest::refusesAPathNamingAFileOnThisHost()
{
    auto dir = QTemporaryDir{};
    QVERIFY(dir.isValid());

    auto const path = dir.path() + QStringLiteral("/secret.torrent");
    auto file = QFile{ path };
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("d4:infod6:lengthi1e4:name1:a12:piece lengthi1e6:pieces0:ee");
    file.close();

    QVERIFY(!AddData::fromWireMetainfo(path.toStdString()).has_value());

    // Nor one resolved against wherever this process happens to be running.
    auto const previous = QDir::currentPath();
    QVERIFY(QDir::setCurrent(dir.path()));
    QVERIFY(!AddData::fromWireMetainfo("secret.torrent"sv).has_value());
    QVERIFY(QDir::setCurrent(previous));
}

void AddDataTest::refusesWhatIsNeitherFormAtAll()
{
    QVERIFY(!AddData::fromWireMetainfo(""sv).has_value());
    QVERIFY(!AddData::fromWireMetainfo("!!! not base64 !!!"sv).has_value());
}

QTEST_MAIN(AddDataTest)

#include "add-data-test.moc"
