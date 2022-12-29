#ifndef OVERLAYDISPATCH_H
#define OVERLAYDISPATCH_H

#include <QWebSocket>
#include <QWebSocketServer>

#include "overlaymessage.h"

class OverlayDispatch : public QObject
{
  Q_OBJECT
public:
  OverlayDispatch(QObject *parent = nullptr);

public slots:
  void start();

  void stop();

  void sendMessage(const OverlayMessage &msg);

private:
  QWebSocketServer *m_webSocket;

  QVector<QWebSocket*> m_clients;

private slots:
  void handleNewConnection();

  void clientDisconnected();

};

#endif // OVERLAYDISPATCH_H
