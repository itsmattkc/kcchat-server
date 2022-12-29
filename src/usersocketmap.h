#ifndef USERSOCKETMAP_H
#define USERSOCKETMAP_H

#include <QWebSocket>

/**
 * @brief Convenience class for pairing sockets with their user IDs
 */
class UserSocketMap
{
public:
  UserSocketMap() = default;

  QList<QWebSocket*> sockets() const { return m_socketId.keys(); }
  QList<qint64> authors() const { return m_idSocket.keys(); }
  QList<QWebSocket*> socketsForAuthor(qint64 author) { return m_idSocket.value(author); }
  qint64 authorForSocket (QWebSocket *skt) const { return m_socketId.value(skt); }

  bool insertSocket(qint64 author, QWebSocket *skt)
  {
    bool joined = false;

    if (author != 0) {
      joined = !m_idSocket.contains(author);
      if (!m_idSocket[author].contains(skt)) {
        m_idSocket[author].append(skt);
      }
    }

    m_socketId.insert(skt, author);

    return joined;
  }

  qint64 removeSocket(QWebSocket *skt)
  {
    if (m_socketId.contains(skt)) {
      qint64 a = m_socketId.take(skt);
      if (a != 0) {
        QList<QWebSocket*> &skts = m_idSocket[a];
        skts.removeOne(skt);
        if (skts.empty()) {
          m_idSocket.remove(a);
          return a;
        }
      }
    }

    return 0;
  }

  void broadcastTextMessage(const QString &s)
  {
    for (auto it = m_socketId.cbegin(); it != m_socketId.cend(); it++) {
      it.key()->sendTextMessage(s);
    }
  }

private:
  QHash< qint64, QList<QWebSocket*> > m_idSocket;
  QHash<QWebSocket*, qint64> m_socketId;

};

#endif // USERSOCKETMAP_H
