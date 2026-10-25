#ifndef LSP_JSONRPC_H
#define LSP_JSONRPC_H

#include "error.h"
#include "json.h"

#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <string>
#include <variant>

namespace jsonrpc {

inline const QString kJsonrpcVersion = "2.0";

/// A JSON-RPC request id. The spec permits string or number; we preserve the
/// original kind on round-trip so a client that emits integer ids gets back
/// integer ids, and likewise for strings.
///
/// The value is the QJsonValue itself (the struct inherits QJsonValue), so
/// no custom metatype registration or extra QVariant boxing is needed.
/// Conversion operators expose the string and integer views; assignment
/// accepts a wire value and validates the kind.
struct RequestId : QJsonValue {
  RequestId(void) : QJsonValue(QJsonValue::Null) {}
  RequestId(QString s) : QJsonValue(std::move(s)) {}
  RequestId(const char *s) : QJsonValue(QString::fromUtf8(s)) {}
  RequestId(std::int64_t i) : QJsonValue(static_cast<qint64>(i)) {}
  RequestId(int i) : QJsonValue(static_cast<qint64>(i)) {}

  [[nodiscard]] bool is_string(void) const noexcept { return isString(); }
  [[nodiscard]] bool is_integer(void) const noexcept { return isDouble(); }

  [[nodiscard]] QString as_string(void) const { return toString(); }
  [[nodiscard]] std::int64_t as_integer(void) const { return toInteger(); }

  /// Canonical string form used for hash/map keys. String ids are returned
  /// as-is with a leading 's:' marker; integer ids get an 'i:' marker. This
  /// guarantees that the integer 5 and the string "5" do not collide.
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

/// Tagged union over every JSON-RPC frame we accept off the wire. Use
/// `std::visit` to dispatch on the concrete kind.
using JsonRpcMessage =
    std::variant<JsonRpcRequest, JsonRpcNotification, JsonRpcResponse>;

/// Parse a single JSON-RPC frame. Returns the variant; throws
/// `jsonrpc::Error(parse_error)` on parse error or envelope-shape errors that
/// prevent classification.
[[nodiscard]] JsonRpcMessage parse_message(const QJsonValue &j);

/// Serialize a JSON-RPC frame.
[[nodiscard]] QJsonValue serialize_message(const JsonRpcMessage &m);

} // namespace jsonrpc

#endif // LSP_JSONRPC_H
