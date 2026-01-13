// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTest>
#include <QUrl>

#include <libtransmission/rpcimpl.h>
#include <libtransmission/quark.h>

#include "RpcClient.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
#define QCOMPARE_EQ(actual, expected) QCOMPARE(actual, expected)
#define QCOMPARE_NE(actual, expected) QVERIFY((actual) != (expected))
#endif

namespace
{
class FakeReply final : public QNetworkReply
{
    Q_OBJECT

public:
    explicit FakeReply(QNetworkAccessManager::Operation op, QNetworkRequest const& req, QObject* parent = nullptr)
        : QNetworkReply{ parent }
    {
        setOperation(op);
        setRequest(req);
        setUrl(req.url());
        open(QIODevice::ReadOnly);
    }

    void setHttpStatus(int const code)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code);
    }

    void addRawHeader(QByteArray const& name, QByteArray const& value)
    {
        setRawHeader(name, value);
    }

    void abort() override
    {
    }

protected:
    qint64 readData(char* /*data*/, qint64 /*maxSize*/) override
    {
        return 0;
    }
};

class FakeNetworkAccessManager final : public QNetworkAccessManager
{
    Q_OBJECT

public:
    int create_count = 0;
    QNetworkAccessManager::Operation last_operation = QNetworkAccessManager::UnknownOperation;
    QNetworkRequest last_request;
    QByteArray last_body;

protected:
    QNetworkReply* createRequest(Operation op, QNetworkRequest const& req, QIODevice* outgoing_data) override
    {
        ++create_count;
        last_operation = op;
        last_request = req;

        if (outgoing_data != nullptr)
        {
            last_body = outgoing_data->readAll();
            (void)outgoing_data->seek(0);
        }

        return new FakeReply{ op, req, this };
    }
};

auto const tr4_session_get_payload_re = QRegularExpression{ R"(^\{"method":"session-get","tag":[0-9]+\}$)" };
auto const tr5_session_get_payload_re = QRegularExpression{ R"(^\{"id":[0-9]+,"jsonrpc":"2\.0","method":"session_get"\}$)" };

} // namespace

class RpcClientTest : public QObject
{
    Q_OBJECT

    static void QVERIFY_re_matches(QRegularExpression const& re, QByteArray const& bytes)
    {
        auto const str = QString::fromUtf8(bytes);
        QVERIFY2(re.match(str).hasMatch(), bytes.constData());
    }

    static void QVERIFY_is_tr4_session_get_payload(QByteArray const& body)
    {
        QVERIFY_re_matches(tr4_session_get_payload_re, body);
    }

    static void QVERIFY_is_tr5_session_get_payload(QByteArray const& body)
    {
        QVERIFY_re_matches(tr5_session_get_payload_re, body);
    }

    static void invoke_network_finished(RpcClient& client, QNetworkReply* reply)
    {
        QMetaObject::invokeMethod(&client, "networkRequestFinished", Qt::DirectConnection, Q_ARG(QNetworkReply*, reply));
    }

private slots:
    static void start_sets_url_and_exec_posts_tr4()
    {
        auto nam = FakeNetworkAccessManager{};
        auto client = RpcClient{ nam };

        auto const url = QUrl{ "http://example.invalid:9091/transmission/rpc" };
        client.start(url);
        QCOMPARE_EQ(client.url(), url);

        (void)client.exec(TR_KEY_session_get, nullptr);

        QVERIFY(nam.create_count >= 1);
        QCOMPARE_EQ(nam.last_operation, QNetworkAccessManager::PostOperation);
        QCOMPARE_EQ(nam.last_request.url(), url);

        QCOMPARE_NE(nam.last_request.rawHeader("Content-Type"), QByteArray{});
        QVERIFY(nam.last_request.rawHeader("Content-Type").contains("application/json"));
        QVERIFY(nam.last_request.rawHeader("User-Agent").startsWith("Transmisson/"));

        // TR4 is legacy style: method + tag, not jsonrpc + id.
        QVERIFY_is_tr4_session_get_payload(nam.last_body);
    }

    static void exec_posts_tr5_after_409_sets_style()
    {
        auto nam = FakeNetworkAccessManager{};
        auto client = RpcClient{ nam };
        client.start(QUrl{ "http://example.invalid:9091/transmission/rpc" });

        // request TR4 by default.
        // TODO(TR5) expect Tr5 by default.
        (void)client.exec(TR_KEY_session_get, nullptr);
        QVERIFY_is_tr4_session_get_payload(nam.last_body);

        // Simulate a 409 response that includes both the Session-Id and the RPC version.
        // This updates the client's network_style_ to Tr5.
        auto* reply = new FakeReply{ QNetworkAccessManager::PostOperation, QNetworkRequest{ client.url() }, &nam };
        reply->setHttpStatus(409);
        reply->addRawHeader(TR_RPC_SESSION_ID_HEADER, "fake-session-id");
        reply->addRawHeader(
            TR_RPC_RPC_VERSION_HEADER,
            QByteArray{ std::data(TrRpcVersionSemver), static_cast<int>(std::size(TrRpcVersionSemver)) });

        // networkRequestFinished expects these properties to exist.
        auto promise = QFutureInterface<RpcResponse>{};
        promise.reportStarted();
        reply->setProperty("requestReplyFutureInterface", QVariant::fromValue(promise));
        reply->setProperty("requestBody", QByteArray{ "{}" });

        invoke_network_finished(client, reply);

        // Second request should now be TR5.
        (void)client.exec(TR_KEY_session_get, nullptr);
        QVERIFY_is_tr5_session_get_payload(nam.last_body);
    }

    static void exec_post_tr4_after_409_without_rpc_version_header()
    {
        auto nam = FakeNetworkAccessManager{};
        auto client = RpcClient{ nam };
        client.start(QUrl{ "http://example.invalid:9091/transmission/rpc" });

        // request TR4 by default.
        // TODO(TR5) expect Tr5 by default.
        (void)client.exec(TR_KEY_session_get, nullptr);
        QVERIFY_is_tr4_session_get_payload(nam.last_body);

        // Simulate a 409 response that includes the Session-Id but *not* the RPC version.
        // This should not change the client's network_style_.
        auto* reply = new FakeReply{ QNetworkAccessManager::PostOperation, QNetworkRequest{ client.url() }, &nam };
        reply->setHttpStatus(409);
        reply->addRawHeader(TR_RPC_SESSION_ID_HEADER, "fake-session-id");

        // networkRequestFinished expects these properties to exist.
        auto promise = QFutureInterface<RpcResponse>{};
        promise.reportStarted();
        reply->setProperty("requestReplyFutureInterface", QVariant::fromValue(promise));
        reply->setProperty("requestBody", QByteArray{ "{}" });

        invoke_network_finished(client, reply);

        // Second request should still be TR4.
        (void)client.exec(TR_KEY_session_get, nullptr);
        QVERIFY_is_tr4_session_get_payload(nam.last_body);
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = RpcClientTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "rpcclient-test.moc"
