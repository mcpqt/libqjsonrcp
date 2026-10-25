#ifndef LSP_QT_TCP_TRANSPORT_H
#define LSP_QT_TCP_TRANSPORT_H

#include "transport.h"

#include <QByteArray>
#include <QObject>
#include <QTcpSocket>

#include <memory>

namespace jsonrpc {
namespace rpc {

/// Qt-based TCP transport implementing the rpc::Transport interface.
/// Reads Content-Length framed JSON-RPC messages from a QTcpSocket and
/// writes similarly framed messages back. The transport takes ownership of
/// the socket and deletes it on close/destruction.
class QtTcpTransport : public QObject, public Transport {
  Q_OBJECT
public:
  explicit QtTcpTransport(QTcpSocket *socket);
  ~QtTcpTransport() override;

  void on_message(MessageCallback cb) override;
  void on_error(ErrorCallback cb) override;
  void on_close(CloseCallback cb) override;

  void start(void) override;
  void close(void) override;

  [[nodiscard]] std::error_code send(QByteArrayView frame) override;
  [[nodiscard]] bool is_open(void) const noexcept override;

private slots:
  void handleReadyRead();
  void handleDisconnected();
  void handleError(QAbstractSocket::SocketError error);

private:
  QTcpSocket *m_socket = nullptr;
  QByteArray m_buffer;
  bool m_running = false;
  bool m_closed = false;

  MessageCallback m_on_message;
  ErrorCallback m_on_error;
  CloseCallback m_on_close;
};

} // namespace rpc
} // namespace jsonrpc

#endif // LSP_QT_TCP_TRANSPORT_H
