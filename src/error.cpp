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
    const QJsonObject obj = value.toObject();
    code = static_cast<error_code>(obj.value("code").toInt());
    message = obj.value("message").toString();
    data = obj.value("data");
    return *this;
  }
}
