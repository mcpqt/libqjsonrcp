// SPDX-License-Identifier: AGPL-3.0-only
//
// QtTest suite for libqjsonrpc.
//
// Covers the JSON codec, JSON-RPC envelope round-trips, id handling, error
// propagation, and Session dispatch/timeout/close behaviour using an
// in-memory mock Transport.

#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QMutex>
#include <QMutexLocker>

#include <chrono>
#include <future>
#include <memory>

#include "error.h"
#include "json.h"
#include "jsonrpc.h"
#include "session.h"
#include "transport.h"

using namespace jsonrpc;

// ---------------------------------------------------------------------------
// Mock transport
// ---------------------------------------------------------------------------

class MockTransport final : public Transport
{
public:
    void on_message(MessageCallback cb) override { m_message_cb = std::move(cb); }
    void on_error(ErrorCallback cb) override { m_error_cb = std::move(cb); }
    void on_close(CloseCallback cb) override { m_close_cb = std::move(cb); }

    void start(void) override { m_open = true; }
    void close(void) override
    {
        m_open = false;
        if(m_close_cb)
            m_close_cb();
    }

    std::error_code send(QByteArrayView frame) override
    {
        QMutexLocker locker(&m_mutex);
        m_sent.append(QByteArray(frame.data(), static_cast<int>(frame.size())));
        return {};
    }

    bool is_open(void) const noexcept override { return m_open; }

    void inject(QByteArrayView frame)
    {
        if(m_message_cb)
            m_message_cb(QByteArray(frame.data(), static_cast<int>(frame.size())));
    }

    QByteArray take_sent(void)
    {
        QMutexLocker locker(&m_mutex);
        QByteArray r = m_sent;
        m_sent.clear();
        return r;
    }

    bool has_sent(void)
    {
        QMutexLocker locker(&m_mutex);
        return !m_sent.isEmpty();
    }

private:
    MessageCallback m_message_cb;
    ErrorCallback m_error_cb;
    CloseCallback m_close_cb;
    bool m_open = false;
    QMutex m_mutex;
    QByteArray m_sent;
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class TestJsonRpc : public QObject
{
    Q_OBJECT

private slots:
    // JSON codec
    void testParseJson();
    void testDump();
    void testParseJsonErrors();

    // RequestId
    void testRequestIdString();
    void testRequestIdInteger();
    void testRequestIdFromJson();
    void testRequestIdInvalid();

    // Request
    void testSerializeRequest();
    void testParseRequest();
    void testRequestWithoutParams();

    // Notification
    void testSerializeNotification();
    void testParseNotification();

    // Response
    void testSerializeResponseSuccess();
    void testSerializeResponseError();
    void testParseResponseSuccess();
    void testParseResponseError();
    void testResponseMissingId();

    // ErrorObject
    void testErrorObjectRoundTrip();

    // Frame classification / invalid frames
    void testInvalidFrames();
    void testParseMessageDispatch();

    // Session: outbound request
    void testSessionSendRequest();
    void testSessionTypedRequest();
    void testSessionRequestError();
    void testSessionTimeout();

    // Session: inbound dispatch
    void testSessionReceiveRequest();
    void testSessionMethodNotFound();
    void testSessionNotification();
    void testSessionHandlerThrows();

    // Session: lifecycle
    void testSessionClose();
    void testSessionSendOnClosed();
};

// ---------------------------------------------------------------------------
// JSON codec
// ---------------------------------------------------------------------------

void TestJsonRpc::testParseJson()
{
    const QJsonValue v = parse_json(QByteArrayView("{\"a\":1}"));
    QVERIFY(v.isObject());
    QCOMPARE(v.toObject().value(QStringLiteral("a")).toInt(), 1);

    const QJsonValue arr = parse_json(QByteArrayView("[1,2,3]"));
    QVERIFY(arr.isArray());
    QCOMPARE(arr.toArray().size(), 3);
}

void TestJsonRpc::testDump()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), QStringLiteral("value"));
    obj.insert(QStringLiteral("n"), 42);

    const QString text = dump(obj);
    const QJsonValue reparsed = parse_json(QByteArrayView(text.toUtf8()));
    QCOMPARE(reparsed.toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("value"));
    QCOMPARE(reparsed.toObject().value(QStringLiteral("n")).toInt(), 42);
}

void TestJsonRpc::testParseJsonErrors()
{
    QVERIFY_THROWS_EXCEPTION(Error, parse_json(QByteArrayView("not json")));
    QVERIFY_THROWS_EXCEPTION(Error, parse_json(QByteArrayView("42")));
    QVERIFY_THROWS_EXCEPTION(Error, parse_json(QByteArrayView("\"str\"")));
}

// ---------------------------------------------------------------------------
// RequestId
// ---------------------------------------------------------------------------

void TestJsonRpc::testRequestIdString()
{
    RequestId id(QStringLiteral("abc"));
    QVERIFY(id.is_string());
    QVERIFY(!id.is_integer());
    QCOMPARE(id.as_string(), QStringLiteral("abc"));
    QCOMPARE(id.canonical(), QStringLiteral("s:abc"));
}

void TestJsonRpc::testRequestIdInteger()
{
    RequestId id(42);
    QVERIFY(id.is_integer());
    QVERIFY(!id.is_string());
    QCOMPARE(id.as_integer(), 42);
    QCOMPARE(id.canonical(), QStringLiteral("i:42"));
}

void TestJsonRpc::testRequestIdFromJson()
{
    const RequestId from_int = from_json<RequestId>(QJsonValue(123));
    QVERIFY(from_int.is_integer());
    QCOMPARE(from_int.as_integer(), 123);

    const RequestId from_str
        = from_json<RequestId>(QJsonValue(QStringLiteral("xyz")));
    QVERIFY(from_str.is_string());
    QCOMPARE(from_str.as_string(), QStringLiteral("xyz"));

    // A whole-number double (as emitted by some JS clients) is accepted.
    const RequestId from_double = from_json<RequestId>(QJsonValue(7.0));
    QVERIFY(from_double.is_integer());
    QCOMPARE(from_double.as_integer(), 7);
}

void TestJsonRpc::testRequestIdInvalid()
{
    QVERIFY_THROWS_EXCEPTION(Error, from_json<RequestId>(QJsonValue(true)));
    QVERIFY_THROWS_EXCEPTION(Error, from_json<RequestId>(QJsonValue(1.5)));
    QVERIFY_THROWS_EXCEPTION(
        Error, from_json<RequestId>(QJsonValue(QJsonValue::Null)));
}

// ---------------------------------------------------------------------------
// Request
// ---------------------------------------------------------------------------

void TestJsonRpc::testSerializeRequest()
{
    JsonRpcRequest req;
    req.id = RequestId(1);
    req.method = QStringLiteral("test.method");
    QJsonObject params;
    params.insert(QStringLiteral("key"), QStringLiteral("value"));
    req.params = params;

    const QJsonObject o = static_cast<QJsonValue>(req).toObject();
    QCOMPARE(o.value(QStringLiteral("jsonrpc")).toString(),
             QStringLiteral("2.0"));
    QCOMPARE(o.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(o.value(QStringLiteral("method")).toString(),
             QStringLiteral("test.method"));
    QCOMPARE(o.value(QStringLiteral("params"))
                 .toObject()
                 .value(QStringLiteral("key"))
                 .toString(),
             QStringLiteral("value"));
}

void TestJsonRpc::testParseRequest()
{
    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("id"), 1);
    o.insert(QStringLiteral("method"), QStringLiteral("test.method"));
    QJsonObject params;
    params.insert(QStringLiteral("a"), 1);
    o.insert(QStringLiteral("params"), params);

    const JsonRpcMessage msg = parse_message(o);
    QVERIFY(std::holds_alternative<JsonRpcRequest>(msg));
    const auto& req = std::get<JsonRpcRequest>(msg);
    QCOMPARE(req.id.as_integer(), 1);
    QCOMPARE(req.method, QStringLiteral("test.method"));
    QCOMPARE(req.params.toObject().value(QStringLiteral("a")).toInt(), 1);
}

void TestJsonRpc::testRequestWithoutParams()
{
    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("id"), QStringLiteral("r1"));
    o.insert(QStringLiteral("method"), QStringLiteral("noop"));

    const auto req = from_json<JsonRpcRequest>(o);
    QVERIFY(req.params.isNull());

    // Absent params must not be emitted on the wire.
    const QJsonObject serialized = static_cast<QJsonValue>(req).toObject();
    QVERIFY(!serialized.contains(QStringLiteral("params")));
}

// ---------------------------------------------------------------------------
// Notification
// ---------------------------------------------------------------------------

void TestJsonRpc::testSerializeNotification()
{
    JsonRpcNotification note;
    note.method = QStringLiteral("notify");
    note.params = QJsonArray{ 1, 2, 3 };

    const QJsonObject o = static_cast<QJsonValue>(note).toObject();
    QCOMPARE(o.value(QStringLiteral("jsonrpc")).toString(),
             QStringLiteral("2.0"));
    QVERIFY(!o.contains(QStringLiteral("id")));
    QCOMPARE(o.value(QStringLiteral("method")).toString(),
             QStringLiteral("notify"));
    QCOMPARE(o.value(QStringLiteral("params")).toArray().size(), 3);
}

void TestJsonRpc::testParseNotification()
{
    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("method"), QStringLiteral("notify"));
    o.insert(QStringLiteral("params"), QStringLiteral("payload"));

    const JsonRpcMessage msg = parse_message(o);
    QVERIFY(std::holds_alternative<JsonRpcNotification>(msg));
    const auto& note = std::get<JsonRpcNotification>(msg);
    QCOMPARE(note.method, QStringLiteral("notify"));
    QCOMPARE(note.params.toString(), QStringLiteral("payload"));
}

// ---------------------------------------------------------------------------
// Response
// ---------------------------------------------------------------------------

void TestJsonRpc::testSerializeResponseSuccess()
{
    JsonRpcResponse resp;
    resp.id = RequestId(QStringLiteral("abc"));
    resp.outcome = QJsonValue(QStringLiteral("result_value"));

    const QJsonObject o = static_cast<QJsonValue>(resp).toObject();
    QCOMPARE(o.value(QStringLiteral("jsonrpc")).toString(),
             QStringLiteral("2.0"));
    QCOMPARE(o.value(QStringLiteral("id")).toString(), QStringLiteral("abc"));
    QCOMPARE(o.value(QStringLiteral("result")).toString(),
             QStringLiteral("result_value"));
    QVERIFY(!o.contains(QStringLiteral("error")));
}

void TestJsonRpc::testSerializeResponseError()
{
    JsonRpcResponse resp;
    resp.id = RequestId(5);
    resp.outcome = ErrorObject{ error_code::method_not_found,
                                QStringLiteral("Not found"),
                                QJsonValue(QJsonValue::Null) };

    const QJsonObject o = static_cast<QJsonValue>(resp).toObject();
    QCOMPARE(o.value(QStringLiteral("id")).toInt(), 5);
    QVERIFY(o.contains(QStringLiteral("error")));
    QVERIFY(!o.contains(QStringLiteral("result")));
    const QJsonObject err = o.value(QStringLiteral("error")).toObject();
    QCOMPARE(err.value(QStringLiteral("code")).toInt(), -32601);
    QCOMPARE(err.value(QStringLiteral("message")).toString(),
             QStringLiteral("Not found"));
}

void TestJsonRpc::testParseResponseSuccess()
{
    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("id"), 1);
    o.insert(QStringLiteral("result"), QStringLiteral("ok"));

    const JsonRpcMessage msg = parse_message(o);
    QVERIFY(std::holds_alternative<JsonRpcResponse>(msg));
    const auto& resp = std::get<JsonRpcResponse>(msg);
    QVERIFY(resp.is_success());
    QVERIFY(!resp.is_error());
    QCOMPARE(resp.result().toString(), QStringLiteral("ok"));
}

void TestJsonRpc::testParseResponseError()
{
    QJsonObject err;
    err.insert(QStringLiteral("code"), -32601);
    err.insert(QStringLiteral("message"), QStringLiteral("Not found"));

    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("id"), 7);
    o.insert(QStringLiteral("error"), err);

    const auto resp = from_json<JsonRpcResponse>(o);
    QVERIFY(resp.is_error());
    QCOMPARE(resp.error().code, error_code::method_not_found);
    QCOMPARE(resp.error().message, QStringLiteral("Not found"));
}

void TestJsonRpc::testResponseMissingId()
{
    // Error responses without an id are legal on parse failures.
    QJsonObject err;
    err.insert(QStringLiteral("code"), -32700);
    err.insert(QStringLiteral("message"), QStringLiteral("Parse error"));

    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("id"), QJsonValue(QJsonValue::Null));
    o.insert(QStringLiteral("error"), err);

    const auto resp = from_json<JsonRpcResponse>(o);
    QVERIFY(resp.is_error());
    QVERIFY(resp.id.isNull());
}

// ---------------------------------------------------------------------------
// ErrorObject
// ---------------------------------------------------------------------------

void TestJsonRpc::testErrorObjectRoundTrip()
{
    ErrorObject original;
    original.code = error_code::invalid_params;
    original.message = QStringLiteral("bad params");
    QJsonObject data;
    data.insert(QStringLiteral("field"), QStringLiteral("x"));
    original.data = data;

    const QJsonValue wire = original;
    QCOMPARE(wire.toObject().value(QStringLiteral("code")).toInt(), -32602);
    QVERIFY(wire.toObject().contains(QStringLiteral("data")));

    ErrorObject decoded;
    decoded = wire;
    QCOMPARE(decoded.code, error_code::invalid_params);
    QCOMPARE(decoded.message, QStringLiteral("bad params"));
    QVERIFY(decoded.has_data());
    QCOMPARE(decoded.data.toObject().value(QStringLiteral("field")).toString(),
             QStringLiteral("x"));
}

// ---------------------------------------------------------------------------
// Frame classification / invalid frames
// ---------------------------------------------------------------------------

void TestJsonRpc::testInvalidFrames()
{
    // Missing jsonrpc version.
    QJsonObject no_version;
    no_version.insert(QStringLiteral("id"), 1);
    no_version.insert(QStringLiteral("method"), QStringLiteral("foo"));
    QVERIFY_THROWS_EXCEPTION(Error, parse_message(no_version));

    // Wrong jsonrpc version.
    QJsonObject bad_version;
    bad_version.insert(QStringLiteral("jsonrpc"), QStringLiteral("1.0"));
    bad_version.insert(QStringLiteral("id"), 1);
    bad_version.insert(QStringLiteral("method"), QStringLiteral("foo"));
    QVERIFY_THROWS_EXCEPTION(Error, parse_message(bad_version));

    // Response carrying both result and error.
    QJsonObject both;
    both.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    both.insert(QStringLiteral("id"), 1);
    both.insert(QStringLiteral("result"), QStringLiteral("x"));
    QJsonObject err;
    err.insert(QStringLiteral("code"), -1);
    err.insert(QStringLiteral("message"), QStringLiteral("err"));
    both.insert(QStringLiteral("error"), err);
    QVERIFY_THROWS_EXCEPTION(Error, parse_message(both));

    // Frame with neither method nor result/error.
    QJsonObject neither;
    neither.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    neither.insert(QStringLiteral("id"), 1);
    QVERIFY_THROWS_EXCEPTION(Error, parse_message(neither));
}

void TestJsonRpc::testParseMessageDispatch()
{
    // Request: method + id.
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("method"), QStringLiteral("m"));
    QVERIFY(std::holds_alternative<JsonRpcRequest>(parse_message(req)));

    // Notification: method without id.
    QJsonObject note;
    note.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    note.insert(QStringLiteral("method"), QStringLiteral("m"));
    QVERIFY(std::holds_alternative<JsonRpcNotification>(parse_message(note)));

    // Response: result without method.
    QJsonObject resp;
    resp.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    resp.insert(QStringLiteral("id"), 1);
    resp.insert(QStringLiteral("result"), QJsonValue());
    QVERIFY(std::holds_alternative<JsonRpcResponse>(parse_message(resp)));
}

// ---------------------------------------------------------------------------
// Session: outbound request
// ---------------------------------------------------------------------------

void TestJsonRpc::testSessionSendRequest()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));
    session.start();

    auto fut = session.send_request(QStringLiteral("test.method"),
                                    QJsonValue(QStringLiteral("params")));
    QVERIFY(mock_ptr->has_sent());

    const QByteArray frame = mock_ptr->take_sent();
    const QJsonObject sent = QJsonDocument::fromJson(frame).object();
    QCOMPARE(sent.value(QStringLiteral("jsonrpc")).toString(),
             QStringLiteral("2.0"));
    QCOMPARE(sent.value(QStringLiteral("method")).toString(),
             QStringLiteral("test.method"));
    QVERIFY(sent.contains(QStringLiteral("id")));

    QJsonObject resp;
    resp.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    resp.insert(QStringLiteral("id"), sent.value(QStringLiteral("id")));
    resp.insert(QStringLiteral("result"), QStringLiteral("response_value"));
    mock_ptr->inject(QJsonDocument(resp).toJson(QJsonDocument::Compact));

    QVERIFY(fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    QCOMPARE(fut.get().toString(), QStringLiteral("response_value"));

    session.close();
}

void TestJsonRpc::testSessionTypedRequest()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));
    session.start();

    auto fut = session.send_request_for<int>(
        QStringLiteral("add"), QJsonObject(), [](QJsonValue j) {
            return j.toInt();
        });
    QVERIFY(mock_ptr->has_sent());
    const QJsonObject sent
        = QJsonDocument::fromJson(mock_ptr->take_sent()).object();

    QJsonObject resp;
    resp.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    resp.insert(QStringLiteral("id"), sent.value(QStringLiteral("id")));
    resp.insert(QStringLiteral("result"), 99);
    mock_ptr->inject(QJsonDocument(resp).toJson(QJsonDocument::Compact));

    QVERIFY(fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    QCOMPARE(fut.get(), 99);

    session.close();
}

void TestJsonRpc::testSessionRequestError()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));
    session.start();

    auto fut = session.send_request(QStringLiteral("fail"));
    QVERIFY(mock_ptr->has_sent());
    const QJsonObject sent
        = QJsonDocument::fromJson(mock_ptr->take_sent()).object();

    QJsonObject err;
    err.insert(QStringLiteral("code"), -32601);
    err.insert(QStringLiteral("message"), QStringLiteral("Not found"));
    QJsonObject resp;
    resp.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    resp.insert(QStringLiteral("id"), sent.value(QStringLiteral("id")));
    resp.insert(QStringLiteral("error"), err);
    mock_ptr->inject(QJsonDocument(resp).toJson(QJsonDocument::Compact));

    QVERIFY(fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    try
    {
        (void)fut.get();
        QFAIL("expected jsonrpc::Error");
    }
    catch(const Error& e)
    {
        QCOMPARE(e.code(), static_cast<int>(error_code::method_not_found));
        QCOMPARE(e.message(), QStringLiteral("Not found"));
    }

    session.close();
}

void TestJsonRpc::testSessionTimeout()
{
    auto mock = std::make_unique<MockTransport>();
    Session session(std::move(mock));
    session.start();

    auto fut = session.send_request(QStringLiteral("timeout.method"),
                                    QJsonValue(),
                                    std::chrono::milliseconds(50));
    QVERIFY(fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    QVERIFY_THROWS_EXCEPTION(Error, fut.get());

    session.close();
}

// ---------------------------------------------------------------------------
// Session: inbound dispatch
// ---------------------------------------------------------------------------

void TestJsonRpc::testSessionReceiveRequest()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));

    QJsonValue received_params;
    session.set_request_handler(
        QStringLiteral("test.method"), [&](const QJsonValue& params) {
            received_params = params;
            return QJsonValue(QStringLiteral("handled"));
        });
    session.start();

    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("method"), QStringLiteral("test.method"));
    req.insert(QStringLiteral("params"), QStringLiteral("incoming"));
    mock_ptr->inject(QJsonDocument(req).toJson(QJsonDocument::Compact));

    QTRY_VERIFY_WITH_TIMEOUT(mock_ptr->has_sent(), 2000);
    const QJsonObject sent
        = QJsonDocument::fromJson(mock_ptr->take_sent()).object();
    QCOMPARE(sent.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(sent.value(QStringLiteral("result")).toString(),
             QStringLiteral("handled"));
    QCOMPARE(received_params.toString(), QStringLiteral("incoming"));

    session.close();
}

void TestJsonRpc::testSessionMethodNotFound()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));
    session.start();

    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), 3);
    req.insert(QStringLiteral("method"), QStringLiteral("unknown"));
    mock_ptr->inject(QJsonDocument(req).toJson(QJsonDocument::Compact));

    QTRY_VERIFY_WITH_TIMEOUT(mock_ptr->has_sent(), 2000);
    const QJsonObject sent
        = QJsonDocument::fromJson(mock_ptr->take_sent()).object();
    QCOMPARE(sent.value(QStringLiteral("id")).toInt(), 3);
    QVERIFY(sent.contains(QStringLiteral("error")));
    QCOMPARE(sent.value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("code"))
                 .toInt(),
             -32601);

    session.close();
}

void TestJsonRpc::testSessionNotification()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));

    QAtomicInt call_count = 0;
    QJsonValue received_params;
    session.set_notification_handler(
        QStringLiteral("note"), [&](const QJsonValue& params) {
            received_params = params;
            call_count.fetchAndAddOrdered(1);
        });
    session.start();

    QJsonObject note;
    note.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    note.insert(QStringLiteral("method"), QStringLiteral("note"));
    note.insert(QStringLiteral("params"), QStringLiteral("notif"));
    mock_ptr->inject(QJsonDocument(note).toJson(QJsonDocument::Compact));

    QTRY_VERIFY_WITH_TIMEOUT(call_count.loadAcquire() == 1, 2000);
    QCOMPARE(received_params.toString(), QStringLiteral("notif"));
    QVERIFY(!mock_ptr->has_sent()); // notifications produce no response

    session.close();
}

void TestJsonRpc::testSessionHandlerThrows()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));

    session.set_request_handler(
        QStringLiteral("boom"), [](const QJsonValue&) -> QJsonValue {
            throw std::runtime_error("kaboom");
        });
    session.start();

    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), 9);
    req.insert(QStringLiteral("method"), QStringLiteral("boom"));
    mock_ptr->inject(QJsonDocument(req).toJson(QJsonDocument::Compact));

    QTRY_VERIFY_WITH_TIMEOUT(mock_ptr->has_sent(), 2000);
    const QJsonObject sent
        = QJsonDocument::fromJson(mock_ptr->take_sent()).object();
    QCOMPARE(sent.value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("code"))
                 .toInt(),
             -32603); // internal_error

    session.close();
}

// ---------------------------------------------------------------------------
// Session: lifecycle
// ---------------------------------------------------------------------------

void TestJsonRpc::testSessionClose()
{
    auto mock = std::make_unique<MockTransport>();
    Session session(std::move(mock));

    bool closed = false;
    session.set_on_closed([&]() { closed = true; });
    session.start();
    QVERIFY(session.is_open());

    auto fut = session.send_request(QStringLiteral("pending.method"));
    session.close();

    QVERIFY(!session.is_open());
    QVERIFY(closed);
    QVERIFY_THROWS_EXCEPTION(Error, fut.get());
}

void TestJsonRpc::testSessionSendOnClosed()
{
    auto mock = std::make_unique<MockTransport>();
    MockTransport* mock_ptr = mock.get();
    Session session(std::move(mock));
    session.start();
    session.close();

    const QByteArray before = mock_ptr->take_sent();
    auto fut = session.send_request(QStringLiteral("after.close"));
    QVERIFY(fut.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    QVERIFY_THROWS_EXCEPTION(Error, fut.get());
    QVERIFY(mock_ptr->take_sent().isEmpty());
}

QTEST_GUILESS_MAIN(TestJsonRpc)

#include "tst_jsonrpc.moc"
