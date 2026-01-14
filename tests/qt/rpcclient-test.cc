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

#include <libtransmission/api-compat.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/quark.h>

#include "RpcClient.h"

namespace api_compat = libtransmission::api_compat;
using Style = api_compat::Style;

Q_DECLARE_METATYPE(Style)

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
#define QCOMPARE_EQ(actual, expected) QCOMPARE(actual, expected)
#define QCOMPARE_NE(actual, expected) QVERIFY((actual) != (expected))
#endif

namespace
{
template<typename String>
[[nodiscard]] QByteArray toQBA(String const& str)
{
    auto const sv = std::string_view{ str };
    return { sv.data(), static_cast<qsizetype>(sv.size()) };
}

class FakeReply final : public QNetworkReply
{
    Q_OBJECT

public:
    [[nodiscard]] static FakeReply* newPostReply(QUrl const& url, QObject* parent = nullptr)
    {
        return newPostReply(QNetworkRequest{ url }, parent);
    }

    [[nodiscard]] static FakeReply* newPostReply(QNetworkRequest const& req, QObject* parent = nullptr)
    {
        auto reply = new FakeReply{ QNetworkAccessManager::PostOperation, req, parent };

        // networkRequestFinished expects these properties to exist.
        auto promise = QFutureInterface<RpcResponse>{};
        promise.reportStarted();
        reply->setProperty("requestReplyFutureInterface", QVariant::fromValue(promise));
        reply->setProperty("requestBody", QByteArray{ "{}" });

        return reply;
    }

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

    template<typename StringA, typename StringB>
    void addRawHeader(StringA const& name, StringB const& value)
    {
        setRawHeader(toQBA(name), toQBA(value));
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
            outgoing_data->seek(0);
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

    static void QVERIFY_is_session_get_request(Style style, QByteArray const& bytes)
    {
        auto const payload_re = style == Style::Tr4 ? tr4_session_get_payload_re : tr5_session_get_payload_re;
        QVERIFY_re_matches(payload_re, bytes);
    }

    static void invoke_network_finished(RpcClient& client, QNetworkReply* reply)
    {
        QMetaObject::invokeMethod(&client, "networkRequestFinished", Qt::DirectConnection, Q_ARG(QNetworkReply*, reply));
    }

    static void add_style_data()
    {
        QTest::addColumn<Style>("initial_style");
        QTest::newRow("Tr4") << Style::Tr4;
        QTest::newRow("Tr5") << Style::Tr5;
    }

private slots:
    void init()
    {
        api_compat::set_default_style(initial_style_);
    }

    static void first_post_is_in_default_style_data()
    {
        add_style_data();
    }
    static void first_post_is_in_default_style()
    {
        // setup: set style
        QFETCH(Style const, initial_style);
        api_compat::set_default_style(initial_style);

        // setup: create & start `client`
        auto nam = FakeNetworkAccessManager{};
        auto client = RpcClient{ nam };
        auto const url = QUrl{ "http://example.invalid:9091/transmission/rpc" };
        client.start(url);
        QCOMPARE_EQ(client.url(), url);

        client.exec(TR_KEY_session_get, nullptr);

        // verify that a request to `url` was posted
        QVERIFY(nam.create_count >= 1);
        QCOMPARE_EQ(nam.last_operation, QNetworkAccessManager::PostOperation);
        QCOMPARE_EQ(nam.last_request.url(), url);

        // verify that the request's headers look right
        QCOMPARE_NE(nam.last_request.rawHeader("Content-Type"), QByteArray{});
        QVERIFY(nam.last_request.rawHeader("Content-Type").contains("application/json"));
        QVERIFY(nam.last_request.rawHeader("User-Agent").startsWith("Transmission/"));

        // verify that the request's payload looks right
        QVERIFY_is_session_get_request(initial_style, nam.last_body);
    }

    static void exec_posts_tr5_after_409_sets_style_data()
    {
        add_style_data();
    }
    static void exec_posts_tr5_after_409_sets_style()
    {
        // setup: set style
        QFETCH(Style const, initial_style);
        api_compat::set_default_style(initial_style);

        // setup: create & start `client`
        auto const url = QUrl{ "http://example.invalid:9091/transmission/rpc" };
        auto nam = FakeNetworkAccessManager{};
        auto client = RpcClient{ nam };
        client.start(url);

        // setup: post initial request
        client.exec(TR_KEY_session_get, nullptr);
        QVERIFY_is_session_get_request(initial_style, nam.last_body);

        // setup: make a 409 response that includes Session-Id *and* RPC version.
        auto* reply = FakeReply::newPostReply(url, &nam);
        reply->setHttpStatus(409);
        reply->addRawHeader(TrRpcSessionIdHeader, "fake-session-id");
        reply->addRawHeader(TrRpcVersionHeader, TrRpcVersionSemver);
        invoke_network_finished(client, reply);

        // action: make another request after receiving the 409
        auto const n_created = nam.create_count;
        client.exec(TR_KEY_session_get, nullptr);

        // verify subsequent request used Style::Tr5
        QVERIFY(nam.create_count > n_created);
        QVERIFY_is_session_get_request(Style::Tr5, nam.last_body);
    }

    static void exec_post_tr4_after_409_without_rpc_version_header_data()
    {
        add_style_data();
    }
    static void exec_post_tr4_after_409_without_rpc_version_header()
    {
        // setup: set style
        QFETCH(Style const, initial_style);
        api_compat::set_default_style(initial_style);

        // setup: create & start `client`
        auto const url = QUrl{ "http://example.invalid:9091/transmission/rpc" };
        auto nam = FakeNetworkAccessManager{};
        auto client = RpcClient{ nam };
        client.start(QUrl{ "http://example.invalid:9091/transmission/rpc" });

        // setup: post initial request
        client.exec(TR_KEY_session_get, nullptr);
        QVERIFY_is_session_get_request(initial_style, nam.last_body);

        // setup: make a 409 response with Session-Id but *not* RPC version.
        auto* reply = FakeReply::newPostReply(url, &nam);
        reply->setHttpStatus(409);
        reply->addRawHeader(TrRpcSessionIdHeader, "fake-session-id");
        invoke_network_finished(client, reply);

        // action: make another request after receiving the 409
        auto const n_created = nam.create_count;
        client.exec(TR_KEY_session_get, nullptr);

        // verify subsequent request used Style::Tr4
        QVERIFY(nam.create_count > n_created);
        QVERIFY_is_session_get_request(Style::Tr4, nam.last_body);
    }

    // previous declaration was `private slots:`
    // NOLINTNEXTLINE(readability-redundant-access-specifiers)
private:
    Style const initial_style_ = api_compat::default_style();
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = RpcClientTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "rpcclient-test.moc"
