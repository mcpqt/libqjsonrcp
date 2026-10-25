#include "error.h"

namespace jsonrpc
{
  ErrorObject::operator QJsonValue() const
  {
    QJsonObject obj;
    obj.insert("code", static_cast<int>(code));
    obj.insert("message", message);
    if (!data.isNull())
      obj.insert("data", data);
    return obj;
  }

  ErrorObject& ErrorObject::operator=(const QJsonValue& value)
  {
    if(!value.isObject())
    {
      throw Error(error_code::invalid_request,
                  "JSON-RPC error must be an object");
    }

    const QJsonObject obj = value.toObject();
    if(!obj.contains("code") || !obj.value("code").isDouble())
    {
      throw Error(error_code::invalid_request,
                  "JSON-RPC error.code must be an integer");
    }
    if(!obj.contains("message") || !obj.value("message").isString())
    {
      throw Error(error_code::invalid_request,
                  "JSON-RPC error.message must be a string");
    }

    const double rawCode = obj.value("code").toDouble();
    const qint64 whole = static_cast<qint64>(rawCode);
    if(static_cast<double>(whole) != rawCode)
    {
      throw Error(error_code::invalid_request,
                  "JSON-RPC error.code must be an integer");
    }

    // Unknown/application-defined codes are valid JSON-RPC. Preserve them
    // as-is instead of collapsing invalid values to 0.
    code = static_cast<error_code>(static_cast<int32_t>(whole));
    message = obj.value("message").toString();
    data = obj.contains("data") ? obj.value("data")
                                : QJsonValue(QJsonValue::Null);
    return *this;
  }
}
