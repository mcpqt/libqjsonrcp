#ifndef LSP_JSONRPC_H
#define LSP_JSONRPC_H

#include "error.h"
#include "json.h"

#include <QJsonValue>
#include <QList>
#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace jsonrpc {

inline const QString kJsonrpcVersion = "2.0";

/// A JSON-RPC request id. The spec permits String, Number, or Null; we
/// preserve the original kind on round-trip so a client that emits integer
/// ids gets back integer ids, and likewise for strings and fractional
/// numbers. Null ids are permitted (though discouraged by the spec).
///
/// The value is the QJsonValue itself (the struct inherits QJsonValue), so
/// no custom metatype registration or extra QVariant boxing is needed.
/// Conversion operators expose the string, integer, and number views;
/// assignment accepts a wire value and validates the kind.
struct RequestId : QJsonValue {
  RequestId(void) : QJsonValue(QJsonValue::Null) {}
  RequestId(QString s) : QJsonValue(std::move(s)) {}
  RequestId(const char *s) : QJsonValue(QString::fromUtf8(s)) {}
  RequestId(std::int64_t i) : QJsonValue(static_cast<qint64>(i)) {}
  RequestId(int i) : QJsonValue(static_cast<qint64>(i)) {}
  RequestId(double d) : QJsonValue(d) {}

  [[nodiscard]] bool is_string(void) const noexcept { return isString(); }
  [[nodiscard]] bool is_number(void) const noexcept { return isDouble(); }
  [[nodiscard]] bool is_integer(void) const noexcept
  {
    return isDouble() && static_cast<double>(toInteger()) == toDouble();
  }
  [[nodiscard]] bool is_fractional(void) const noexcept
  {
    return isDouble() && static_cast<double>(toInteger()) != toDouble();
  }

  [[nodiscard]] QString as_string(void) const { return toString(); }
  [[nodiscard]] std::int64_t as_integer(void) const { return toInteger(); }
  [[nodiscard]] double as_double(void) const { return toDouble(); }

  /// Canonical string form used for hash/map keys. String ids are returned
  /// as-is with a leading 's:' marker; whole-number ids get an 'i:' marker;
  /// fractional ids get a 'd:' marker; null ids get 'n:'. This guarantees
  /// that the integer 5 and the string "5" do not collide.
  [[nodiscard]] QString canonical(void) const;

  friend bool operator==(const RequestId &, const RequestId &) = default;

  inline operator std::int64_t(void) const { return as_integer(); }
  inline operator QString(void) const { return as_string(); }
  RequestId &operator=(const QJsonValue &);
};

/// A JSON-RPC 2.0 request. Always has an id.
struct JsonRpcRequest {
  RequestId id;
  QString method;
  QJsonValue params = {};

  [[nodiscard]] operator QJsonValue() const;
  JsonRpcRequest &operator=(const QJsonValue &);
};

/// A JSON-RPC 2.0 notification. Has no id; sender expects no response.
struct JsonRpcNotification {
  QString method;
  QJsonValue params = {};

  [[nodiscard]] operator QJsonValue() const;
  JsonRpcNotification &operator=(const QJsonValue &);
};

/// A JSON-RPC 2.0 response. Carries either a `result` or an `error`. Per
/// the spec the id is REQUIRED on success, and `null`/absent only on parse
/// errors that prevented identifying the request — we model that with a
/// null RequestId.
struct JsonRpcResponse {
  RequestId id;
  std::variant<QJsonValue /*result*/, ErrorObject /*error*/> outcome;

  [[nodiscard]] bool is_error(void) const noexcept
  {
    return outcome.index() == 1;
  }
  [[nodiscard]] bool is_success(void) const noexcept
  {
    return outcome.index() == 0;
  }

  [[nodiscard]] const QJsonValue &result(void) const
  {
    return std::get<0>(outcome);
  }
  [[nodiscard]] const ErrorObject &error(void) const
  {
    return std::get<1>(outcome);
  }

  [[nodiscard]] operator QJsonValue() const;
  JsonRpcResponse &operator=(const QJsonValue &);
};

/// Explicit decoders for wire structs that only provide
/// `operator=(const QJsonValue&)`. Without these, the generic `from_json`
/// template detects itself via ADL and recurses until the stack overflows.
inline void from_json(const QJsonValue &v, RequestId &out) { out = v; }
inline void from_json(const QJsonValue &v, ErrorObject &out) { out = v; }
inline void from_json(const QJsonValue &v, JsonRpcRequest &out) { out = v; }
inline void from_json(const QJsonValue &v, JsonRpcNotification &out) { out = v; }
inline void from_json(const QJsonValue &v, JsonRpcResponse &out) { out = v; }

struct JsonRpcBatch;

/// Tagged union over every JSON-RPC frame we accept off the wire. Use
/// `std::visit` to dispatch on the concrete kind. A batch is represented by a
/// shared pointer so the variant remains finite and copyable.
using JsonRpcMessage = std::variant<JsonRpcRequest, JsonRpcNotification,
                                    JsonRpcResponse,
                                    std::shared_ptr<JsonRpcBatch>>;

/// One element of a JSON-RPC batch. `valid` is false for an element that could
/// not be decoded; in that case `message` is an error response with a null id
/// that the Session must send back in the batch response array.
struct JsonRpcBatchItem {
  JsonRpcMessage message;
  bool valid = true;
};

/// A JSON-RPC batch: a non-empty array of request/notification/response
/// objects. Empty arrays are rejected by `parse_message`.
struct JsonRpcBatch {
  QList<JsonRpcBatchItem> items;
};

/// Parse a single JSON-RPC frame or a batch array. Returns the variant; throws
/// `jsonrpc::Error(parse_error)` on malformed JSON or
/// `jsonrpc::Error(invalid_request)` on envelope-shape errors that prevent
/// classification. Empty arrays throw `invalid_request` (the Session turns
/// that into a single error object per the spec).
[[nodiscard]] JsonRpcMessage parse_message(const QJsonValue &j);

/// Serialize a JSON-RPC frame.
[[nodiscard]] QJsonValue serialize_message(const JsonRpcMessage &m);

} // namespace jsonrpc

#endif // LSP_JSONRPC_H
