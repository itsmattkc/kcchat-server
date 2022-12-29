#include "overlaydispatch.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "startupconfig.h"

OverlayDispatch::OverlayDispatch(QObject *parent) :
  QObject(parent)
{
}

void OverlayDispatch::start()
{
  const quint16 wssPort = 2001;

  QSslConfiguration ssl = CONFIG.getSslConfiguration();

  m_webSocket = new QWebSocketServer(QStringLiteral("Event Dispatch"), ssl.isNull() ? QWebSocketServer::NonSecureMode : QWebSocketServer::SecureMode, this);
  m_webSocket->setSslConfiguration(ssl);
  connect(m_webSocket, &QWebSocketServer::newConnection, this, &OverlayDispatch::handleNewConnection);
  if (m_webSocket->listen(QHostAddress::Any, wssPort)) {
    qDebug() << "Listening for WebSocket event dispatch on port" << wssPort;
  } else {
    qCritical() << "Failed to bind WebSocket server to port" << wssPort;
  }
}

void OverlayDispatch::stop()
{
  qDeleteAll(m_clients);
  m_webSocket->close();
}

void OverlayDispatch::sendMessage(const OverlayMessage &msg)
{
  if (msg.type() == OverlayMessage::MSG_NONE) {
    return;
  }

  qInfo() << "On-screen alert:" << msg.alertTitle() << "-" << msg.alertSubtitle();

  QString doc = msg.toJson().toJson();
  for (QWebSocket *s : qAsConst(m_clients)) {
    s->sendTextMessage(doc);
  }
}

void OverlayDispatch::handleNewConnection()
{
  QWebSocket *skt = m_webSocket->nextPendingConnection();

  connect(skt, &QWebSocket::disconnected, this, &OverlayDispatch::clientDisconnected);

  qDebug() << "Overlay" << skt << "connected" << skt->peerAddress();

  m_clients.append(skt);
}

void OverlayDispatch::clientDisconnected()
{
  QWebSocket *s = static_cast<QWebSocket*>(sender());

  qDebug() << "Overlay" << s << "disconnected" << s->peerAddress();

  m_clients.removeOne(s);
}
