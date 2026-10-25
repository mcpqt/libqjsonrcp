#include "tcp_transport.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <system_error>

namespace jsonrpc {

/// Extract Content-Length from the header bytes. Returns -1 when absent or
/// malformed. Header lines are CRLF-terminated per the LSP wire format.
static qint64 parse_content_length(const QByteArray &header) {
  for (const QByteArray &rawLine : header.split('\n')) {
    const QByteArray line = rawLine.trimmed();
    const int colon = line.indexOf(':');
    if (colon < 0)
      continue;
    if (line.left(colon).trimmed().toLower() != "content-length")
      continue;
    bool ok = false;
    const qint64 length = line.mid(colon + 1).trimmed().toLongLong(&ok);
    return ok ? length : -1;
  }
  return -1;
}

QtTcpTransport::QtTcpTransport(QTcpSocket *socket) : m_socket(socket) {
  if (m_socket) {
    connect(m_socket, &QTcpSocket::readyRead, this,
            &QtTcpTransport::handleReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this,
            &QtTcpTransport::handleDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            &QtTcpTransport::handleError);
  }
}

QtTcpTransport::~QtTcpTransport() { close(); }

void QtTcpTransport::on_message(MessageCallback cb) {
  m_on_message = std::move(cb);
}

void QtTcpTransport::on_error(ErrorCallback cb) { m_on_error = std::move(cb); }

void QtTcpTransport::on_close(CloseCallback cb) { m_on_close = std::move(cb); }

void QtTcpTransport::start(void) {
  if (m_running)
    return;
  m_running = true;
}

void QtTcpTransport::close(void) {
  if (m_closed)
    return;
  m_closed = true;
  m_running = false;
  if (m_socket) {
    m_socket->disconnectFromHost();
    m_socket->deleteLater();
    m_socket = nullptr;
  }
}

std::error_code QtTcpTransport::send(QByteArrayView frame) {
  if (!m_running || m_closed || !m_socket)
    return std::make_error_code(std::errc::not_connected);

  const QByteArray header = QByteArray("Content-Length: ") +
                            QByteArray::number(frame.size()) + "\r\n\r\n";
  const qint64 headerWritten = m_socket->write(header);
  if (headerWritten < 0)
    return std::make_error_code(std::errc::io_error);
  const qint64 bodyWritten = m_socket->write(frame.data(), frame.size());
  if (bodyWritten < 0)
    return std::make_error_code(std::errc::io_error);
  return {};
}

bool QtTcpTransport::is_open(void) const noexcept {
  return m_running && !m_closed;
}

void QtTcpTransport::handleReadyRead() {
  if (!m_running || !m_socket)
    return;
  m_buffer.append(m_socket->readAll());
  while (true) {
    int headerEnd = m_buffer.indexOf("\r\n\r\n");
    int separatorLength = 4;
    if (headerEnd < 0) {
      headerEnd = m_buffer.indexOf("\n\n");
      separatorLength = 2;
    }
    if (headerEnd < 0)
      return;

    const QByteArray header = m_buffer.left(headerEnd);
    const qint64 contentLength = parse_content_length(header);
    if (contentLength < 0) {
      // Malformed header: drop it and keep reading.
      m_buffer.remove(0, headerEnd + separatorLength);
      continue;
    }

    const qint64 total = headerEnd + separatorLength + contentLength;
    if (m_buffer.size() < total)
      return;

    const QByteArray body = m_buffer.mid(
        headerEnd + separatorLength, static_cast<int>(contentLength));
    m_buffer.remove(0, static_cast<int>(total));

    if (m_on_message)
      m_on_message(body);
  }
}

void QtTcpTransport::handleDisconnected() {
  if (m_closed)
    return;
  m_closed = true;
  m_running = false;
  if (m_on_close)
    m_on_close();
}

void QtTcpTransport::handleError(QAbstractSocket::SocketError error) {
  if (error == QAbstractSocket::RemoteHostClosedError)
    return;
  if (m_on_error)
    m_on_error(std::make_error_code(std::errc::io_error));
}

} // namespace jsonrpc
