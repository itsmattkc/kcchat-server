#ifndef CHATSERVER_H
#define CHATSERVER_H

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QObject>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslKey>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlRecord>
#include <QtSql/QSqlResult>
#include <QtSql/QSqlQuery>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#include "auth/authmodule.h"
#include "overlaymessage.h"
#include "startupconfig.h"
#include "usersocketmap.h"
#include "util.h"

class ChatServer : public QObject
{
  Q_OBJECT
public:
  class Request
  {
  public:
    Request(const QString &line, const QString &author, qint64 authorId, Authorization auth)
    {
      m_line = line;
      m_args = m_line.split(QRegExp("\\s+(?=([^\"]*\"[^\"]*\")*[^\"]*$)"));
      for (int i=0; i<m_args.size(); i++) {
        QString &s = m_args[i];
        if (s.startsWith('"')) {
          s.remove(0, 1);
        }
        if (s.endsWith('"')) {
          s.chop(1);
        }
      }
      m_command = m_args.at(0).toLower();
      m_author = author;
      m_authorId = authorId;
      m_authorization = auth;
    }

    Request(const QString &line) :
      Request(line, QString(), 0, Authorization::AUTH_ADMIN)
    {}

    Request() : Request(QString()){}

    bool hasAuthor() const { return !m_author.isEmpty(); }

    bool equals(const char *command) const
    {
      return m_command.compare(command, Qt::CaseInsensitive) == 0;
    }

    const QString &line() const { return m_line; }
    const QStringList &args() const { return m_args; }
    const QString &author() const { return m_author; }
    const QString &command() const { return m_command; }
    qint64 authorId() const { return m_authorId; }
    Authorization authorization() const { return m_authorization; }

  private:
    QString m_line;
    QStringList m_args;
    QString m_command;
    QString m_author;
    qint64 m_authorId;
    Authorization m_authorization;

  };

  class Response
  {
  public:
    Response() = default;

    Response(const Request &request, const QString &message, bool publicly = false)
    {
      m_request = request;
      m_message = message;
      m_publicly = publicly;
    }

    static Response Error(const Request &request)
    {
      return Response(request, tr("Internal server error"));
    }

    const Request &request() const { return m_request; }
    const QString &message() const { return m_message; }
    bool isPublic() const { return m_publicly; }

    static const QString INTERNAL_SERVER_ERROR;

    bool isValid() const { return !m_message.isEmpty(); }

  private:
    Request m_request;
    QString m_message;
    bool m_publicly;
  };

  enum Status
  {
    STATUS_UNAUTHENTICATED,
    STATUS_AUTHENTICATED,
    STATUS_BANNED,
    STATUS_RENAME,
    STATUS_NAME_EXISTS,
    STATUS_NAME_TIMEOUT,
    STATUS_NAME_INVALID,
    STATUS_NAME_LENGTH,
    STATUS_CONFIG_SUCCESS
  };

  explicit ChatServer(QObject *parent = nullptr);

public slots:
  void start();

  void stop();

signals:
  void requestOverlayMessage(const OverlayMessage &msg);

protected:
  void reply(const Response &reply);

  typedef Response(ChatServer::*CommandHandler_t)(const Request &r);

  void insertCommand(const QString &command, CommandHandler_t handler, Authorization minimumAuth)
  {
    m_commandMap.insert(command, {handler, minimumAuth});
  }

  void insertSimpleResponse(const QString &command, const QString &response);

  void publish(const QString &author, qint64 id, QString msg, const QString &color, const QHostAddress &ip, Authorization auth);

  Response doMention(const Request &r);

private:
  struct CommandHandler
  {
    CommandHandler_t handler;
    Authorization authorization;
  };

  void initCommands();

  Response commandAddCom(const Request &r);
  Response commandAlert(const Request &r);
  Response commandEditCom(const Request &r);
  Response commandDelCom(const Request &r);
  Response commandHelp(const Request &r);
  Response commandPauseTTS(const Request &r);
  Response commandPurgeTTS(const Request &r);
  Response commandSay(const Request &r);
  Response commandShoutout(const Request &r);
  Response commandSkipTTS(const Request &r);
  Response commandTime(const Request &r);
  Response commandTimer(const Request &r);
  Response commandSimpleResponse(const Request &r);
  Response commandAutoTTS(const Request &r);
  Response commandNextTTS(const Request &r);
  Response commandBan(const Request &r);
  Response commandUnban(const Request &r);
  Response commandIpBan(const Request &r);
  Response commandSlowMode(const Request &r);
  Response commandMod(const Request &r);
  Response commandUnmod(const Request &r);
  Response commandDelMsg(const Request &r);
  Response commandVideo(const Request &r);
  Response commandInfo(const Request &r);
  Response commandFollowMode(const Request &r);

  Status getUserStateFromID(qint64 id);
  static QString getStatusString(Status s);

  static QJsonDocument generateClientPacket(const QString &type, const QJsonObject &data);

  void processAuthenticatedMessage(QWebSocket *client, const QString &type, const QJsonValue &data, qint64 authorId);
  void handleAuthFailure(QWebSocket *client);

  void sendUserStatusMessage(QWebSocket *skt, const Status &status);
  void sendServerMessage(QWebSocket *skt, const QString &status);
  void sendInternalServerError(QWebSocket *skt)
  {
    sendServerMessage(skt, tr("Internal server error"));
  }

  void sendUserState(QWebSocket *skt, qint64 id);

  QString getDisplayNameFromUserId(qint64 id);

  void dropMessages(const QVector<qint64> &msgIds, bool updateDb);

  Response ban(const Request &r, bool andIP);

  Response setUserAuthLevelCommand(const Request &r, Authorization auth);
  void loadResponses();

  bool isMessageAcceptable(const QString &msg);

  qint64 createNewUser();

  static QString stripAtSymbols(QString name);

  static QJsonDocument generateChatMessageForClient(qint64 msgId, const QString &author, qint64 authorId, const QString &authorColor, QString msg, Authorization auth);

  void insertSocket(qint64 author, QWebSocket *skt);
  void removeSocket(QWebSocket *skt);

  QJsonDocument generateJoinPacket(const QString &name);
  QJsonDocument generateAuthLevelPacket(Authorization auth);

  AuthModule *getAuthModuleById(const QString &id) const;

  QMap<QString, CommandHandler> m_commandMap;
  QMap<QString, QString> m_simpleResponses;
  QMap<QString, qint64> m_timers;

  QWebSocketServer *m_server;

  quint64 m_slowMode;
  quint64 m_duplicateSlowMode;
  quint64 m_displayNameChangeTime;
  quint64 m_followMode;

  QSqlDatabase m_db;

  UserSocketMap m_clients;

  QNetworkAccessManager *m_netMan;

  QVector<AuthModule*> m_authModules;

private slots:
  void handleNewConnection();

  void clientDisconnected();

  void processClientMessage(const QString &s);
  void processChatMessage(QWebSocket *client, qint64 authorId, const QJsonValue &data);
  void processGetUserConfig(QWebSocket *client, qint64 id);
  void processSetUserConfig(QWebSocket *client, qint64 id, const QJsonValue &data);
  void processPayPal(qint64 id, const QJsonValue &data);
  void processHello(QWebSocket *client, const QJsonValue &data);

  void handleSslError(const QList<QSslError> &errs);
  void handlePeerVerifyError(const QSslError &err);

  void checkApiError(QNetworkReply *r);

};

#endif // CHATSERVER_H
