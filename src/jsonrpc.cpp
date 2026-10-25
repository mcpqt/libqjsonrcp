#include "jsonrpc.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <utility>
#include <variant>

namespace jsonrpc {

// =====================================================================
// RequestId
// =====================================================================

QString RequestId::canonical(void) const {
  if (is_string()) {
    return "s:" + as_string();
  }
  return "i:" + QString::number(as_integer());
}

RequestId &RequestId::operator=(const QJsonValue &j) {
  if (j.isString()) {
    *this = RequestId{j.toString()};
    return *this;
  }
  if (j.isDouble()) {
    // The spec types `id` as `string | number`; some JS clients
    // emit small whole numbers as floats (`1.0`) because JS has
    // no integer literal. Accept whole values; reject fractional.
    std::int64_t v{};
    from_json(j, v);
    *this = RequestId{v};
    return *this;
  }
  throw Error(error_code::parse_error, "JSON-RPC id must be a string or integer");
}

// =====================================================================
// JSON-RPC envelopes
// =====================================================================

JsonRpcRequest::operator QJsonValue() const {
  QJsonObject o;
  o.insert("jsonrpc", to_json(kJsonrpcVersion));
  o.insert("id", to_json(id));
  o.insert("method", to_json(method));
  if (!params.isNull())
    o.insert("params", params);
  return o;
}

JsonRpcRequest &JsonRpcRequest::operator=(const QJsonValue &j) {
  const QJsonObject o = as_object(j);
  if (require<QString>(o, "jsonrpc") != QString{kJsonrpcVersion}) {
    throw Error(error_code::invalid_request, "jsonrpc must be \"2.0\"");
  }
  id = require<RequestId>(o, "id");
  method = require<QString>(o, "method");
  params = QJsonValue(QJsonValue::Null);
  take_optional_json(o, "params", params);

  return *this;
}

JsonRpcNotification::operator QJsonValue() const {
  QJsonObject o;
  o.insert("jsonrpc", to_json(kJsonrpcVersion));
  o.insert("method", to_json(method));
  if (!params.isNull())
    o.insert("params", params);
  return o;
}

JsonRpcNotification &JsonRpcNotification::operator=(const QJsonValue &j) {
  const QJsonObject o = as_object(j);
  if (require<QString>(o, "jsonrpc") != QString{kJsonrpcVersion}) {
    throw Error(error_code::invalid_request, "jsonrpc must be \"2.0\"");
  }
  method = require<QString>(o, "method");
  params = QJsonValue(QJsonValue::Null);
  take_optional_json(o, "params", params);

  return *this;
}

JsonRpcResponse::operator QJsonValue() const {
  QJsonObject o;
  o.insert("jsonrpc", to_json(kJsonrpcVersion));
  if (!id.isNull())
    o.insert("id", to_json(id));
  else
    o.insert("id", QJsonValue(QJsonValue::Null));
  if (is_success())
    o.insert("result", std::get<0>(outcome));
  else
    o.insert("error", to_json(std::get<1>(outcome)));
  return o;
}

JsonRpcResponse &JsonRpcResponse::operator=(const QJsonValue &j) {
  const QJsonObject o = as_object(j);
  if (require<QString>(o, "jsonrpc") != QString{kJsonrpcVersion}) {
    throw Error(error_code::invalid_request, "jsonrpc must be \"2.0\"");
  }
  const auto idIt = o.constFind("id");
  if (idIt != o.constEnd() && !idIt->isNull()) {
    id = from_json<RequestId>(*idIt);
  } else {
    id = RequestId();
  }
  const bool has_result = o.contains("result");
  const bool has_error = o.contains("error");
  if (has_result == has_error) {
    throw Error(error_code::invalid_request,
                "JSON-RPC response must carry exactly one of "
                "\"result\" or \"error\"");
  }
  if (has_result) {
    outcome = o.value("result");
  } else {
    outcome = from_json<ErrorObject>(o.value("error"));
  }

  return *this;
}

JsonRpcMessage parse_message(const QJsonValue &j) {
  const QJsonObject o = as_object(j);
  const bool has_id = o.contains("id");
  const bool has_method = o.contains("method");
  const bool has_result = o.contains("result");
  const bool has_error = o.contains("error");

  if (has_method && has_id)
    return from_json<JsonRpcRequest>(j);
  if (has_method)
    return from_json<JsonRpcNotification>(j);
  if (has_result || has_error)
    return from_json<JsonRpcResponse>(j);

  throw Error(error_code::invalid_request,
              "JSON-RPC frame has neither \"method\" nor "
              "\"result\"/\"error\"");
}

QJsonValue serialize_message(const JsonRpcMessage &m) {
  return std::visit([](const auto &msg) { return to_json(msg); }, m);
}

} // namespace jsonrpc
