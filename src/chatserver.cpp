#include "chatserver.h"

#include "auth/googleauth.h"
#include "startupconfig.h"

static const QString SQL_CONNECTION_NAME = QStringLiteral("kcchat");

ChatServer::ChatServer(QObject *parent) :
  QObject{parent},
  m_slowMode(0),
  m_duplicateSlowMode(30),          // 30 seconds
  m_displayNameChangeTime(2592000), // 30 days
  m_followMode(600)                 // 10 minutes
{
  m_netMan = new QNetworkAccessManager(this);
  connect(m_netMan, &QNetworkAccessManager::finished, this, &ChatServer::checkApiError);

  m_authModules.append(new GoogleAuth(this));

  initCommands();
}

void ChatServer::start()
{
  const quint16 wssPort = 2002;

  m_db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), SQL_CONNECTION_NAME);

  m_db.setHostName(CONFIG[QStringLiteral("db_host")].toString());
  m_db.setPort(CONFIG[QStringLiteral("db_port")].toInt());
  m_db.setDatabaseName(CONFIG[QStringLiteral("db_name")].toString());
  m_db.setUserName(CONFIG[QStringLiteral("db_user")].toString());
  m_db.setPassword(CONFIG[QStringLiteral("db_pass")].toString());
  m_db.setConnectOptions("MYSQL_OPT_RECONNECT=1");
  if (m_db.open()) {
    qDebug() << "Successfully connected to database";
    loadResponses();
  } else {
    qCritical() << "Failed to connect to database:" << m_db.lastError();
  }

  // Use SSL if available
  QSslConfiguration ssl = CONFIG.getSslConfiguration();

  // Start hosting websocket
  m_server = new QWebSocketServer(QStringLiteral("Chat"), ssl.isNull() ? QWebSocketServer::NonSecureMode : QWebSocketServer::SecureMode, this);
  m_server->setSslConfiguration(ssl);
  connect(m_server, &QWebSocketServer::newConnection, this, &ChatServer::handleNewConnection);
  connect(m_server, &QWebSocketServer::sslErrors, this, &ChatServer::handleSslError);
  connect(m_server, &QWebSocketServer::peerVerifyError, this, &ChatServer::handlePeerVerifyError);

  if (m_server->listen(QHostAddress::Any, wssPort)) {
    qDebug() << "Listening for chat server on port" << wssPort;
  } else {
    qCritical() << "Failed to bind chat server to port" << wssPort;
  }
}

void ChatServer::stop()
{
  auto copy = m_clients.sockets();
  for (auto it=copy.cbegin(); it!=copy.cend(); it++) {
    (*it)->close();
  }
  m_server->close();

  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(SQL_CONNECTION_NAME);
}

void ChatServer::reply(const Response &r)
{
  const Request &req = r.request();
  if (req.hasAuthor() || r.isPublic()) {
    if (r.isPublic()) {
      QString s = r.message();
      if (req.hasAuthor()) {
        s.prepend(QStringLiteral("@%1 ").arg(req.author()));
      }
      publish(CONFIG[QStringLiteral("bot_name")].toString(), 0, s, CONFIG[QStringLiteral("bot_color")].toString(), QHostAddress::LocalHost, Authorization::AUTH_MOD);
    } else {
      // Send status message
      const QList<QWebSocket*> skts = m_clients.socketsForAuthor(req.authorId());
      for (QWebSocket *s : skts) {
        sendServerMessage(s, r.message());
      }
    }
  } else {
    // Fallback to printing
    fprintf(stdout, "%s\n", r.message().toUtf8().constData());

#ifdef Q_OS_WINDOWS
    // Windows still seems to buffer stderr and we want to see debug messages immediately, so here we make sure each line
    // is flushed
    fflush(stderr);
#endif
  }
}

ChatServer::Status ChatServer::getUserStateFromID(qint64 id)
{
  QSqlQuery userLookupQuery(m_db);
  userLookupQuery.prepare(QStringLiteral("SELECT display_name, banned_until FROM users WHERE id = ?"));
  userLookupQuery.addBindValue(id);

  if (!userLookupQuery.exec()) {
    qCritical() << "Failed to look up user state:" << userLookupQuery.lastError();
    return STATUS_UNAUTHENTICATED;
  }

  if (!userLookupQuery.next()) {
    return STATUS_UNAUTHENTICATED;
  }

  if (userLookupQuery.value(QStringLiteral("banned_until")).toLongLong() > QDateTime::currentSecsSinceEpoch()) {
    return STATUS_BANNED;
  }

  if (userLookupQuery.value(QStringLiteral("display_name")).toString().isEmpty()) {
    return STATUS_RENAME;
  }

  return STATUS_AUTHENTICATED;
}

QString ChatServer::getStatusString(Status s)
{
  switch (s) {
  case STATUS_UNAUTHENTICATED:
    return QStringLiteral("unauthenticated");
  case STATUS_AUTHENTICATED:
    return QStringLiteral("authenticated");
  case STATUS_BANNED:
    return QStringLiteral("banned");
  case STATUS_RENAME:
    return QStringLiteral("rename");
  case STATUS_NAME_EXISTS:
    return QStringLiteral("nameexists");
  case STATUS_NAME_TIMEOUT:
    return QStringLiteral("nametimeout");
  case STATUS_NAME_INVALID:
    return QStringLiteral("nameinvalid");
  case STATUS_CONFIG_SUCCESS:
    return QStringLiteral("setuserconf");
  case STATUS_NAME_LENGTH:
    return QStringLiteral("namelength");
  }

  return QString();
}

void ChatServer::publish(const QString &author, qint64 id, QString msg, const QString &color, const QHostAddress &ip, Authorization auth, const QString &donateValue)
{
  // Trim string and check for empty. Client will have done this, but we can't trust it.
  msg = msg.replace(QRegExp(QStringLiteral("["
                                             "\\xAD\\xA0\\x09\\x34F\\x61C\\x115F"
                                             "\\x1160\\x17B4\\x17B5\\x180E"
                                             "\\x2000-\\x200F"
                                             "\\x202F\\x205F"
                                             "\\x2060-\\x2064"
                                             "\\x206A-\\x206F"
                                             "\\x3000"
                                             "\\x2800\\x3164\\xFEFF\\xFFA0"
                                           "]")), QStringLiteral(" "));
  msg = msg.trimmed();
  if (msg.isEmpty() || msg.size() > CONFIG[QStringLiteral("max_chat_length")].toInt()) {
    return;
  }

  bool dropped = !isMessageAcceptable(msg);
  qint64 now = QDateTime::currentMSecsSinceEpoch();

  QSqlQuery insertQuery(m_db);
  insertQuery.prepare(QStringLiteral("INSERT INTO history (user_id, time, message, dropped, host, donate_value) VALUES (?, ?, ?, ?, ?, ?); SELECT LAST_INSERT_ID();"));
  insertQuery.addBindValue(id);
  insertQuery.addBindValue(now);
  insertQuery.addBindValue(msg);
  insertQuery.addBindValue(dropped);
  insertQuery.addBindValue(ip.toString());
  insertQuery.addBindValue(donateValue.isEmpty() ? QStringLiteral("") : donateValue);
  if (!insertQuery.exec()) {
    qCritical() << "Failed to insert chat message into history:" << insertQuery.lastError();
  }

  if (dropped) {
    return;
  }

  insertQuery.nextResult();
  insertQuery.next();
  qint64 msgId = insertQuery.value(0).toLongLong();

  m_clients.broadcastTextMessage(generateChatMessageForClient(msgId, now, author, id, color, msg, auth, donateValue).toJson());
}

QJsonDocument ChatServer::generateClientPacket(const QString &type, const QJsonObject &data)
{
  QJsonObject o;
  o.insert(QStringLiteral("type"), type);
  o.insert(QStringLiteral("data"), data);
  return QJsonDocument(o);
}

void ChatServer::sendUserStatusMessage(QWebSocket *skt, const Status &status)
{
  QJsonObject data;
  data.insert(QStringLiteral("status"), getStatusString(status));
  skt->sendTextMessage(generateClientPacket(QStringLiteral("status"), data).toJson());
}

void ChatServer::sendServerMessage(QWebSocket *skt, const QString &text)
{
  QJsonObject o;
  o.insert(QStringLiteral("message"), text);
  skt->sendTextMessage(generateClientPacket(QStringLiteral("servermsg"), o).toJson());
}

void ChatServer::sendUserState(QWebSocket *skt, qint64 id)
{
  sendUserStatusMessage(skt, getUserStateFromID(id));
}

bool ChatServer::getUserInfoFromUserId(qint64 id, UserInfo *out)
{
  QSqlQuery userLookupQuery(m_db);
  userLookupQuery.prepare(QStringLiteral("SELECT * FROM users WHERE id = ?"));
  userLookupQuery.addBindValue(id);
  if (!userLookupQuery.exec()) {
    qCritical() << "Failed to look up user information:" << userLookupQuery.lastError();
    return false;
  }

  if (!userLookupQuery.next()) {
    return false;
  }

  out->name = userLookupQuery.value(QStringLiteral("display_name")).toString();
  out->lastMessage = userLookupQuery.value(QStringLiteral("last_message")).toString();
  out->lastMessageTime = userLookupQuery.value(QStringLiteral("last_message_time")).toLongLong();
  out->bannedUntil = userLookupQuery.value(QStringLiteral("banned_until")).toLongLong();
  out->auth = static_cast<Authorization>(userLookupQuery.value(QStringLiteral("auth_level")).toInt());
  out->color = userLookupQuery.value(QStringLiteral("display_color")).toString();
  out->createdAt = userLookupQuery.value(QStringLiteral("created_at")).toLongLong();

  return true;
}

void ChatServer::dropMessages(const QVector<qint64> &msgIds, bool updateDb)
{
  QJsonArray a;

  for (int i = 0; i < msgIds.size(); i++) {
    qint64 id = msgIds.at(i);
    a.append(id);

    if (updateDb) {
      QSqlQuery rmQuery(m_db);
      rmQuery.prepare(QStringLiteral("UPDATE history SET dropped = 1 WHERE id = ?"));
      rmQuery.addBindValue(id);
      if (!rmQuery.exec()) {
        qCritical() << "Failed to set message to dropped:" << rmQuery.lastError();
      }
    }
  }

  QJsonObject o;
  o.insert(QStringLiteral("messages"), a);
  m_clients.broadcastTextMessage(generateClientPacket(QStringLiteral("delete"), o).toJson());
}

ChatServer::Response ChatServer::ban(const Request &r, bool andIP)
{
  if (r.args().size() == 2 || r.args().size() == 3) {
    QSqlQuery userUpdate(m_db);
    userUpdate.prepare(QStringLiteral("UPDATE users SET banned_at = ?, banned_until = ? WHERE display_name = ? AND auth_level != ?;"
                                      "SELECT id FROM users WHERE display_name = ?;"));

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 banEnd;
    if (r.args().size() == 2) {
      // Perma-ban

      // Defined as Number.MAX_SAFE_INTEGER
      // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Number/MAX_SAFE_INTEGER
      const uint64_t MAX_JAVASCRIPT_NUMBER = 9007199254740991;

      banEnd = MAX_JAVASCRIPT_NUMBER;
    } else {
      // Ban for seconds
      QString timeframe = r.args().at(2);

      bool ok;

      // Attempt seconds first
      int timeframeSecs = timeframe.toInt(&ok);

      if (!ok) {
        // Failed to parse as-is, assume there is a unit indicator at the end
        QChar lastChar = timeframe.at(timeframe.size()-1).toLower();
        timeframe.chop(1);
        timeframeSecs = timeframe.toInt(&ok);

        if (ok) {
          if (lastChar == 'y') {
            // Convert years to seconds
            timeframeSecs *= 31536000;
          } else if (lastChar == 'd') {
            // Convert days to seconds
            timeframeSecs *= 86400;
          } else if (lastChar == 'h') {
            // Convert hours to seconds
            timeframeSecs *= 3600;
          } else if (lastChar == 'm') {
            // Convert minutes to seconds
            timeframeSecs *= 60;
          } else if (lastChar == 's') {
            // Do nothing, already in seconds
          } else {
            // Don't know what unit this is
            ok = false;
          }
        }

        if (!ok) {
          return Response(r, tr("Failed to parse ban timeframe: %1").arg(r.args().at(2)));
        }
      }

      banEnd = now + timeframeSecs;
    }

    QString bannedUser = stripAtSymbols(r.args().at(1));
    userUpdate.addBindValue(now);
    userUpdate.addBindValue(banEnd);
    userUpdate.addBindValue(bannedUser);
    userUpdate.addBindValue(int(Authorization::AUTH_ADMIN));
    userUpdate.addBindValue(bannedUser);

    if (!userUpdate.exec()) {
      qCritical() << "Failed to update user details for ban:" << userUpdate.lastError();
      return Response::Error(r);
    }

    if (userUpdate.numRowsAffected() == 1) {
      // Get ID
      userUpdate.nextResult();
      userUpdate.next();
      qint64 bannedId = userUpdate.value(0).toLongLong();

      // Drop the messages
      {
        QSqlQuery dropMsgQuery(m_db);
        dropMsgQuery.prepare(QStringLiteral("SELECT id FROM history WHERE user_id = ? AND dropped = 0;"
                                            "UPDATE history SET dropped = 1 WHERE user_id = ?"));
        dropMsgQuery.addBindValue(bannedId);
        dropMsgQuery.addBindValue(bannedId);
        if (!dropMsgQuery.exec()) {
          qCritical() << "Failed to drop messages from banned user:" << dropMsgQuery.lastError();
        } else {
          QVector<qint64> msgs;
          while (dropMsgQuery.next()) {
            msgs.append(dropMsgQuery.value(0).toLongLong());
          }
          dropMessages(msgs, false);
        }
      }

      QString msg = tr("%1 banned until <span class='timestamp'>%2</span>").arg(bannedUser, QString::number(banEnd));

      // Use socket to send ban message (and ban the IP if necessary)
      const QList<QWebSocket *> bannedClient = m_clients.socketsForAuthor(bannedId);
      for (QWebSocket *s : bannedClient) {
        // Send banned status to user
        sendUserStatusMessage(s, STATUS_BANNED);

        if (andIP) {
          QSqlQuery banIpQuery(m_db);
          banIpQuery.prepare(QStringLiteral("INSERT INTO banned_hosts (host, started, until) VALUES (?, ?, ?)"));
          banIpQuery.addBindValue(s->peerAddress().toString());
          banIpQuery.addBindValue(now);
          banIpQuery.addBindValue(banEnd);
          if (!banIpQuery.exec()) {
            qCritical() << "Failed to insert IP into banned hosts:" << banIpQuery.lastError();
          }
        }
      }

      if (andIP) {
        msg.append('\n');
        if (bannedClient.empty()) {
          msg.append(tr("But IP address could not be determined"));
        } else {
          msg.append(tr("And %1 IP(s) also banned").arg(bannedClient.size()));
        }
      }

      return Response(r, msg);
    } else {
      // Let sender know that user was not banned
      return Response(r, tr("Couldn't find user %1").arg(bannedUser));
    }
  } else {
    // Return usage
    return Response(r, tr("Usage: %1 <name> [length-of-ban]").arg(r.command()));
  }
}

ChatServer::Response ChatServer::setUserAuthLevelCommand(const Request &r, Authorization auth)
{
  if (r.args().size() == 2) {
    QString userToMod = stripAtSymbols(r.args().at(1));

    QSqlQuery modQuery(m_db);
    modQuery.prepare(QStringLiteral("UPDATE users SET auth_level = ? WHERE display_name = ? AND auth_level != ?"));
    modQuery.addBindValue(int(auth));
    modQuery.addBindValue(userToMod);
    modQuery.addBindValue(int(Authorization::AUTH_ADMIN));

    if (!modQuery.exec()) {
      qCritical() << "Failed to set auth level:" << modQuery.lastError();
      return Response::Error(r);
    }

    if (modQuery.numRowsAffected() == 0) {
      return Response(r, tr("Failed to find user '%1'").arg(userToMod));
    } else {
      auto skts = m_clients.socketsForAuthor(userToMod.toLongLong());
      for (auto skt : skts) {
        skt->sendTextMessage(generateAuthLevelPacket(auth).toJson());
      }
      return Response(r, tr("%1 auth level set to %2 successfully").arg(userToMod, QString::number(int(auth))));
    }
  } else {
    return Response(r, tr("Usage: %1 <username>").arg(r.command()));
  }
}

void ChatServer::loadResponses()
{
  // Read commands from database
  QSqlQuery commandRetrieve(m_db);
  if (!commandRetrieve.exec(QStringLiteral("SELECT * FROM responses"))) {
    qCritical() << "Failed to query responses:" << commandRetrieve.lastError();
    return;
  }

  while (commandRetrieve.next()) {
    QString command = commandRetrieve.value(QStringLiteral("command")).toString();
    QString response = commandRetrieve.value(QStringLiteral("response")).toString();
    insertSimpleResponse(command, response);
    qDebug() << "Loaded simple response" << command;
  }
}

bool ChatServer::isMessageAcceptable(const QString &msg)
{
  QSqlQuery blockedWordQuery(QStringLiteral("SELECT word FROM banned_words"), m_db);

  if (!blockedWordQuery.exec()) {
    qCritical() << "Failed to look up banned word list";
    return false;
  }

  while (blockedWordQuery.next()) {
    QString word = blockedWordQuery.value(QStringLiteral("word")).toString();
    if (msg.contains(word, Qt::CaseInsensitive)) {
      return false;
    }
  }

  return true;
}

QString ChatServer::stripAtSymbols(QString name)
{
  while (!name.isEmpty() && name.startsWith('@')) {
    name.remove(0, 1);
  }

  return name;
}

QJsonDocument ChatServer::generateChatMessageForClient(qint64 msgId, qint64 time, const QString &author, qint64 authorId, const QString &authorColor, QString msg, Authorization auth, const QString &donateValue)
{
  // Escape HTML
  msg = msg.toHtmlEscaped();

  // Message is valid, create JSON data to send to clients
  QJsonObject data;
  data.insert(QStringLiteral("id"), msgId);
  data.insert(QStringLiteral("time"), time);
  data.insert(QStringLiteral("author"), author);
  data.insert(QStringLiteral("author_id"), authorId);
  data.insert(QStringLiteral("author_color"), authorColor);
  data.insert(QStringLiteral("author_level"), int(auth));
  data.insert(QStringLiteral("message"), msg);
  data.insert(QStringLiteral("auth"), int(auth));
  data.insert(QStringLiteral("donate_value"), donateValue);

  return generateClientPacket(QStringLiteral("chat"), data);
}

void ChatServer::insertSocket(qint64 author, QWebSocket *skt)
{
  bool just_joined = m_clients.insertSocket(author, skt);
  if (just_joined) {
    UserInfo info;
    if (getUserInfoFromUserId(author, &info) && !info.name.isEmpty()) {
      m_clients.broadcastTextMessage(generateJoinPacket(info.name).toJson());
      qDebug() << "Chatter" << info.name << author << "joined";
    }
  }
}

void ChatServer::removeSocket(QWebSocket *skt)
{
  qint64 a = m_clients.removeSocket(skt);
  if (a != 0) {
    UserInfo info;
    if (getUserInfoFromUserId(a, &info) && !info.name.isEmpty()) {
      m_clients.broadcastTextMessage(generatePartPacket(info.name).toJson());
      qDebug() << "Chatter" << info.name << a << "parted";
    }
  }
}

QJsonDocument ChatServer::generateJoinPacket(const QString &name)
{
  QJsonObject d;
  d.insert(QStringLiteral("name"), name);
  return generateClientPacket(QStringLiteral("join"), d);
}

QJsonDocument ChatServer::generatePartPacket(const QString &name)
{
  QJsonObject d;
  d.insert(QStringLiteral("name"), name);
  return generateClientPacket(QStringLiteral("part"), d);
}

QJsonDocument ChatServer::generateAuthLevelPacket(Authorization auth)
{
  QJsonObject o;
  o.insert(QStringLiteral("value"), int(auth));
  return generateClientPacket(QStringLiteral("authlevel"), o);
}

AuthModule *ChatServer::getAuthModuleById(const QString &id) const
{
  for (AuthModule *a : m_authModules) {
    if (a->id() == id) {
      return a;
    }
  }

  return nullptr;
}

void ChatServer::handleNewConnection()
{
  QWebSocket *skt = m_server->nextPendingConnection();

  connect(skt, &QWebSocket::textMessageReceived, this, &ChatServer::processClientMessage);
  connect(skt, &QWebSocket::disconnected, this, &ChatServer::clientDisconnected);
}

void ChatServer::clientDisconnected()
{
  QWebSocket *s = static_cast<QWebSocket*>(sender());

  removeSocket(s);

  // FIXME: Not deleting may be a memory leak, but some event-based functions continue to refer to
  //        sockets that may get deleted between callbacks.
  //s->deleteLater();
}

void ChatServer::processAuthenticatedMessage(QWebSocket *client, const QString &type, const QJsonValue &data, qint64 id)
{
  insertSocket(id, client);

  if (type == QStringLiteral("status")) {
    sendUserState(client, id);

    {
      // Send user auth level
      QSqlQuery authLevelQuery(m_db);
      authLevelQuery.prepare(QStringLiteral("SELECT auth_level FROM users WHERE id = ?"));
      authLevelQuery.addBindValue(id);
      if (!authLevelQuery.exec()) {
        qCritical() << "Failed to get auth_level" << authLevelQuery.lastError();
      } else if (!authLevelQuery.next()) {
        qCritical() << "Failed to get auth_level, user" << id << "didn't exist";
      } else {
        client->sendTextMessage(generateAuthLevelPacket(static_cast<Authorization>(authLevelQuery.value(QStringLiteral("auth_level")).toInt())).toJson());
      }
    }
  } else if (type == QStringLiteral("getuserconf")) {
    processGetUserConfig(client, id);
  } else if (type == QStringLiteral("setuserconf")) {
    processSetUserConfig(client, id, data);
  } else if (type == QStringLiteral("message")) {
    processChatMessage(client, id, data);
  } else if (type == QStringLiteral("paypal")) {
    processPayPal(client, id, data);
  }
}

void ChatServer::handleAuthFailure(QWebSocket *client)
{
  sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
}

void ChatServer::processClientMessage(const QString &s)
{
  QWebSocket *client = static_cast<QWebSocket*>(sender());

  // Ignore packet if sent too recently to last packet (attempt to mitigate DDoS)
  {
    //
    // Enforce no more than 10 requests per second
    //
    const qint64 MAX_REQUEST_COUNT = 10;
    const qint64 MAX_REQUEST_INTERVAL = 1000;

    // Get access record
    std::list<qint64> accessRecord = client->property("access").value< std::list<qint64> >();

    // Push back now time
    accessRecord.push_back(QDateTime::currentMSecsSinceEpoch());

    // Keep list 10 elements long
    if (accessRecord.size() > MAX_REQUEST_COUNT) {
      accessRecord.pop_front();
    }

    // Store access history in socket
    client->setProperty("access", QVariant::fromValue(accessRecord));

    // If we have the maximum number of requests, make sure they happened in longer than the max
    // interval for that amount of requests
    if (accessRecord.size() == MAX_REQUEST_COUNT) {
      qint64 diff = accessRecord.back() - accessRecord.front();
      if (diff < MAX_REQUEST_INTERVAL) {
        // Too many requests per second. Discard.
        return;
      }
    }
  }

  QJsonDocument json = QJsonDocument::fromJson(s.toUtf8());
  QString type = json.object().value(QStringLiteral("type")).toString();
  QJsonValue data = json.object().value(QStringLiteral("data"));

  // Hello is processed before anything else
  if (type == QStringLiteral("hello")) {
    processHello(client, data);
    return;
  }

  // Check if IP is banned

  // First, check if IP is banned
  QSqlQuery bannedHostQuery(m_db);
  bannedHostQuery.prepare(QStringLiteral("SELECT * FROM banned_hosts WHERE host = ? AND until > ?"));
  bannedHostQuery.addBindValue(client->peerAddress().toString());
  bannedHostQuery.addBindValue(QDateTime::currentSecsSinceEpoch());
  if (!bannedHostQuery.exec()) {
    qCritical() << "Failed to check for banned host:" << bannedHostQuery.lastError();
    sendInternalServerError(client);
    return;
  }

  if (bannedHostQuery.next()) {
    // Banned host exists in the database
    sendUserStatusMessage(client, STATUS_BANNED);
    return;
  }

  // Ensure token is valid
  QString token = json.object().value(QStringLiteral("token")).toString();
  QString authType = json.object().value(QStringLiteral("auth")).toString();
  if (token.isEmpty() || authType.isEmpty()) {
    sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
    return;
  }

  if (AuthModule *a = getAuthModuleById(authType)) {
    a->authenticate(m_db, token, std::bind(&ChatServer::processAuthenticatedMessage, this, client, type, data, std::placeholders::_1), std::bind(&ChatServer::handleAuthFailure, this, client));
  } else {
    // Don't know how to handle this auth service
    sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
    return;
  }
}

void ChatServer::processChatMessage(QWebSocket *client, qint64 authorId, const QJsonValue &data)
{
  UserInfo info;

  // Using the user ID, try loading the user's details
  if (!getUserInfoFromUserId(authorId, &info)) {
    sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
    return;
  }

  // Get time now
  qint64 now = QDateTime::currentSecsSinceEpoch();

  // Check if user is banned
  if (info.bannedUntil > now) {
    sendUserStatusMessage(client, STATUS_BANNED);
    return;
  }

  if (info.name.isEmpty()) {
    sendUserStatusMessage(client, STATUS_RENAME);
    return;
  }

  QString msg = data.toString().trimmed();
  if (msg.isEmpty()) {
    // Ignore empty message
    return;
  }

  // Determine if message should be published or absorbed by bot
  const QHostAddress &ip = client->peerAddress();
  Response response;

  if (msg.startsWith('!') || msg.startsWith('/')) {
    // Strip ! or /
    QString strippedMsg = msg;
    strippedMsg.remove(0, 1);

    // Prevent possible backdoor to admin access
    if (!info.name.isEmpty() && authorId != 0) {
      Request r(strippedMsg, info.name, authorId, info.auth);
      qDebug() << info.name << "tried to use command" << r.command();

      if (r.command().isEmpty()) {
        return;
      } else if (m_commandMap.contains(r.command())) {
        const CommandHandler &hnd = m_commandMap.value(r.command());
        if (r.authorization() >= hnd.authorization) {
          response = (this->*hnd.handler)(r);
        } else {
          response = Response(r, tr("You don't have permission to use this command."));
        }
      } else {
        response = Response(r, tr("Don't know command \"%1\"").arg(r.command()));
      }
    }
  } else {
    // Handle a mention of the bot
    if (msg.contains(QStringLiteral("@%1").arg(CONFIG[QStringLiteral("bot_name")].toString()), Qt::CaseInsensitive)) {
      response = doMention(Request(msg, info.name, authorId, info.auth));
    }
  }

  if ((!response.isValid() || response.isPublic()) && info.auth < Authorization::AUTH_MOD) {
    // Check if user has violated slow mode
    if (m_slowMode > 0) {
      qint64 slowModeDelta = info.lastMessageTime + m_slowMode - now;
      if (slowModeDelta > 0) {
        sendServerMessage(client, tr("Chat is in slow mode, please wait %1 seconds to send another message.").arg(slowModeDelta));
        return;
      }
    }

    // If this is not a bot message, and it's a duplicate that happened too quickly, reject it
    if (m_duplicateSlowMode > 0 && msg == info.lastMessage) {
      qint64 slowModeDelta = info.lastMessageTime + m_duplicateSlowMode - now;
      if (slowModeDelta > 0) {
        sendServerMessage(client, tr("Your identical message was sent too quickly, please wait %1 seconds to send it again.").arg(slowModeDelta));
        return;
      }
    }

    // Check if user is allowed to speak yet
    if (m_followMode > 0) {
      qint64 followDelta = now - (info.createdAt + m_followMode);
      if (followDelta < 0) {
        sendServerMessage(client, tr("Your account must be at least %1 seconds old to message here. Please wait another %2 seconds.").arg(QString::number(m_followMode), QString::number(-followDelta)));
        return;
      }
    }

    // Chat is not rate limited, update client's last message & time
    {
      QSqlQuery updateLastMsgQuery(m_db);
      updateLastMsgQuery.prepare(QStringLiteral("UPDATE users SET last_message = ?, last_message_time = ? WHERE id = ?"));
      updateLastMsgQuery.addBindValue(msg);
      updateLastMsgQuery.addBindValue(now);
      updateLastMsgQuery.addBindValue(authorId);
      if (!updateLastMsgQuery.exec()) {
        qCritical() << "Failed to update last message information:" << updateLastMsgQuery.lastError();
      }
    }
  }

  if (!response.isValid() || response.isPublic()) {
    publish(info.name, authorId, msg, info.color, ip, info.auth);
  }

  if (response.isValid()) {
    reply(response);
  }

  // Let client know we accepted the message
  QJsonObject accept;
  accept.insert(QStringLiteral("message"), msg);
  client->sendTextMessage(generateClientPacket(QStringLiteral("accepted"), accept).toJson());
}

void ChatServer::processGetUserConfig(QWebSocket *client, qint64 id)
{
  QSqlQuery currentConfigLookup(m_db);
  currentConfigLookup.prepare(QStringLiteral("SELECT display_name, display_color FROM users WHERE id = ?"));
  currentConfigLookup.addBindValue(id);
  if (!currentConfigLookup.exec()) {
    qCritical() << "Failed to retrieve current user config:" << currentConfigLookup.lastError();
    return;
  }

  if (!currentConfigLookup.next()) {
    sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
    return;
  }

  QJsonObject data;
  data.insert(QStringLiteral("name"), currentConfigLookup.value(QStringLiteral("display_name")).toString());
  data.insert(QStringLiteral("color"), currentConfigLookup.value(QStringLiteral("display_color")).toString());
  client->sendTextMessage(generateClientPacket(QStringLiteral("getuserconf"), data).toJson());
}

void ChatServer::processSetUserConfig(QWebSocket *client, qint64 id, const QJsonValue &data)
{
  QString oldName;
  qint64 lastNameChangeTime;

  QJsonObject o = data.toObject();

  {
    QSqlQuery currentConfigLookup(m_db);
    currentConfigLookup.prepare(QStringLiteral("SELECT display_name, display_name_change_time FROM users WHERE id = ?"));
    currentConfigLookup.addBindValue(id);
    if (!currentConfigLookup.exec()) {
      qCritical() << "Failed to retrieve display name change time:" << currentConfigLookup.lastError();
      return;
    }

    if (!currentConfigLookup.next()) {
      sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
      return;
    }

    oldName = currentConfigLookup.value(QStringLiteral("display_name")).toString();
    lastNameChangeTime = currentConfigLookup.value(QStringLiteral("display_name_change_time")).toLongLong();
  }

  {
    // Update display color
    QSqlQuery updateColorQuery(m_db);
    updateColorQuery.prepare(QStringLiteral("UPDATE users SET display_color = ? WHERE id = ?"));
    updateColorQuery.addBindValue(o.value(QStringLiteral("color")));
    updateColorQuery.addBindValue(id);
    if (!updateColorQuery.exec()) {
      qCritical() << "Failed to update color:" << updateColorQuery.lastError();
    }
  }

  QString newName = o.value(QStringLiteral("name")).toString().trimmed();
  if (newName != oldName) {
    // Check name length
    if (newName.size() < 5 || newName.size() > 32) {
      sendUserStatusMessage(client, STATUS_NAME_LENGTH);
      return;
    }

    // Check if name is valid
    for (int i = 0; i < newName.size(); i++) {
      const QChar &c = newName.at(i);

      // Only allow numbers, lowercase letter, uppercase letters, and underscores
      bool valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_');

      if (!valid) {
        sendUserStatusMessage(client, STATUS_NAME_INVALID);
        return;
      }
    }

    if (QDateTime::currentSecsSinceEpoch() < lastNameChangeTime + m_displayNameChangeTime) {
      sendUserStatusMessage(client, STATUS_NAME_TIMEOUT);
      return;
    }

    // Try to update display name, return if name exists
    {
      QSqlQuery renameQuery(m_db);
      renameQuery.prepare(QStringLiteral("UPDATE users SET display_name = ?, display_name_change_time = ? WHERE id = ?"));
      renameQuery.addBindValue(newName);
      renameQuery.addBindValue(QDateTime::currentSecsSinceEpoch());
      renameQuery.addBindValue(id);
      if (!renameQuery.exec()) {
        // SQL error, determine whether it's a "duplicate entry" error or some other error
        QSqlError err = renameQuery.lastError();
        if (err.nativeErrorCode() == QStringLiteral("1062")) {
          // Duplicate entry, let client know
          sendUserStatusMessage(client, STATUS_NAME_EXISTS);
        } else {
          // Some other error, this is a developer issue
          qCritical() << "Failed to update display name:" << err;
        }
        return;
      }

      // If we're here, username changed successfully. Let all clients know.
      if (!oldName.isEmpty()) {
        m_clients.broadcastTextMessage(generatePartPacket(oldName).toJson());
      }
      m_clients.broadcastTextMessage(generateJoinPacket(newName).toJson());
    }
  }

  // Config saved successfully, let client know
  sendUserStatusMessage(client, STATUS_CONFIG_SUCCESS);
}

void ReportPayPalError(const QString &orderId, qint64 userId, const QString &userName, const QString &error)
{
  qWarning() << "Order" << orderId << "initiated by" << userName << "/" << userId << "rejected:" << error;
}

void ChatServer::processHello(QWebSocket *client, const QJsonValue &data)
{
  // Send last few messages as history
  const int HISTORY_LENGTH = 50;

  QJsonObject o = data.toObject();

  QSqlQuery historyQuery(m_db);
  historyQuery.prepare(QStringLiteral("SELECT id, user_id, message, donate_value, time FROM history WHERE dropped = 0 AND id > ? ORDER BY time DESC LIMIT ?"));
  historyQuery.addBindValue(o.value(QStringLiteral("last_message")).toInt());
  historyQuery.addBindValue(HISTORY_LENGTH);
  if (!historyQuery.exec()) {
    qCritical() << "Failed to retrieve chat messages for history:" << historyQuery.lastError();
  }

  struct Msg
  {
    QString author;
    qint64 authorId;
    QString authorColor;
    QString message;
    qint64 messageId;
    qint64 messageTime;
    Authorization auth;
    QString donateValue;
  };

  std::list<Msg> msgs;

  while (historyQuery.next()) {
    Msg m;

    m.authorId = historyQuery.value(QStringLiteral("user_id")).toLongLong();

    if (m.authorId == 0) {
      m.author = CONFIG[QStringLiteral("bot_name")].toString();
    } else {
      QSqlQuery authorQuery(m_db);
      authorQuery.prepare(QStringLiteral("SELECT display_name, display_color, auth_level FROM users WHERE id = ?"));
      authorQuery.addBindValue(m.authorId);
      if (!authorQuery.exec()) {
        qCritical() << "Failed to query author display name for chat:" << authorQuery.lastError();
        continue;
      }

      if (!authorQuery.next()) {
        qCritical() << "Failed to find author display name for chat, row did not exist";
        continue;
      }

      m.author = authorQuery.value(QStringLiteral("display_name")).toString();
      m.authorColor = authorQuery.value(QStringLiteral("display_color")).toString();
      m.auth = static_cast<Authorization>(authorQuery.value(QStringLiteral("auth_level")).toInt());
    }

    m.messageTime = historyQuery.value(QStringLiteral("time")).toLongLong();
    m.messageId = historyQuery.value(QStringLiteral("id")).toLongLong();
    m.message = historyQuery.value(QStringLiteral("message")).toString();
    m.donateValue = historyQuery.value(QStringLiteral("donate_value")).toString();

    msgs.push_back(m);
  }

  while (!msgs.empty()) {
    const Msg &m = msgs.back();
    client->sendTextMessage(generateChatMessageForClient(m.messageId, m.messageTime, m.author, m.authorId, m.authorColor, m.message, m.auth, m.donateValue).toJson());
    msgs.pop_back();
  }

  const QList<qint64> activeUsers = m_clients.authors();
  client->sendTextMessage(generateJoinPacket(CONFIG[QStringLiteral("bot_name")].toString()).toJson());
  for (qint64 a : activeUsers) {
    UserInfo info;
    if (getUserInfoFromUserId(a, &info) && !info.name.isEmpty()) {
      client->sendTextMessage(generateJoinPacket(info.name).toJson());
    }
  }

  insertSocket(0, client);
}

void ChatServer::processPayPal(QWebSocket *client, qint64 id, const QJsonValue &data)
{
  static QByteArray PP_ACCESS_TOKEN;

  static QString PP_API_URL;
  if (PP_API_URL.isEmpty()) {
    if (CONFIG[QStringLiteral("paypal_live")].toBool()) {
      PP_API_URL = QStringLiteral("https://api-m.paypal.com");
    } else {
      PP_API_URL = QStringLiteral("https://api-m.sandbox.paypal.com");
    }
  }

  UserInfo info;
  if (!getUserInfoFromUserId(id, &info)) {
    return;
  }

  if (info.bannedUntil > QDateTime::currentSecsSinceEpoch()) {
    qInfo() << "Banned user" << info.name << "attempted to make a donation";
    return;
  }

  if (info.name.isEmpty()) {
    qInfo() << "User with no name attempted to make a donation";
    return;
  }

  qDebug() << "User" << info.name << "made donation";

  QJsonObject o = data.toObject();

  QString message = o.value(QStringLiteral("message")).toString().trimmed();

  QJsonObject order = o.value(QStringLiteral("order")).toObject();

  QString orderId = order.value(QStringLiteral("id")).toString();

  // Validate order
  QString url = PP_API_URL;
  url.append(QStringLiteral("/v2/checkout/orders/%1").arg(orderId));

  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  req.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ").append(PP_ACCESS_TOKEN));

  auto reply = m_netMan->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, client, id, data, info, message, orderId, order]{
    auto reply = static_cast<QNetworkReply*>(sender());
    auto doc = QJsonDocument::fromJson(reply->readAll());

    if (reply->error() != QNetworkReply::NoError) {
      if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
        // Token is invalid, must create a new one
        QString url = PP_API_URL;
        url.append(QStringLiteral("/v1/oauth2/token"));

        QNetworkRequest tokenReq(url);
        tokenReq.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
        tokenReq.setRawHeader(QByteArrayLiteral("Accept-Language"), QByteArrayLiteral("en_US"));
        tokenReq.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));

        QByteArray ppClientId = CONFIG[QStringLiteral("paypal_client_id")].toString().toUtf8();
        QByteArray ppClientSecret = CONFIG[QStringLiteral("paypal_client_secret")].toString().toUtf8();
        QByteArray auth = ppClientId.append(':').append(ppClientSecret).toBase64();
        tokenReq.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Basic ").append(auth));

        QNetworkReply *tokenReply = m_netMan->post(tokenReq, QByteArrayLiteral("grant_type=client_credentials"));
        connect(tokenReply, &QNetworkReply::finished, this, [this, client, id, data]{
          auto reply = static_cast<QNetworkReply*>(sender());
          auto json = QJsonDocument::fromJson(reply->readAll());

          PP_ACCESS_TOKEN = json.object().value(QStringLiteral("access_token")).toString().toUtf8();
          processPayPal(client, id, data);
        });
      } else {
        // Handle unknown error
        qCritical() << "Error checking order" << reply->error() << doc;
      }
      return;
    }

    auto o = doc.object();
    const QString &name = info.name;

    {
      QSqlQuery recordQuery(m_db);
      recordQuery.prepare(QStringLiteral("INSERT INTO transactions (order_id, user_id, time_received, data, message, succeeded) VALUES (?, ?, ?, ?, ?, 0)"));
      recordQuery.addBindValue(orderId);
      recordQuery.addBindValue(id);
      recordQuery.addBindValue(QDateTime::currentSecsSinceEpoch());
      recordQuery.addBindValue(QJsonDocument(order).toJson());
      recordQuery.addBindValue(message);
      if (!recordQuery.exec()) {
        QSqlError err = recordQuery.lastError();
        if (err.nativeErrorCode() == QStringLiteral("1062")) { // Duplicate key
          ReportPayPalError(orderId, id, name, tr("transaction already exists in database"));
        } else {
          qCritical() << "Failed to record transaction in database:" << err;
        }
        return;
      }
    }

    auto orderId = o.value(QStringLiteral("id")).toString();

    auto createTime = QDateTime::fromString(o.value(QStringLiteral("create_time")).toString(), Qt::ISODate);
    auto fiveMinutesAgo = QDateTime::currentDateTime().addSecs(-300);
    if (createTime < fiveMinutesAgo) {
      ReportPayPalError(orderId, id, name, tr("order was created more than 5 minutes ago"));
      return;
    }

    if (o.value(QStringLiteral("intent")) != QStringLiteral("CAPTURE")) {
      ReportPayPalError(orderId, id, name, tr("intent was not CAPTURE"));
      return;
    }

    if (o.value(QStringLiteral("status")) != QStringLiteral("COMPLETED")) {
      ReportPayPalError(orderId, id, name, tr("status was not COMPLETED"));
      return;
    }

    auto purchaseUnits = o.value(QStringLiteral("purchase_units")).toArray();
    if (purchaseUnits.isEmpty()) {
      ReportPayPalError(orderId, id, name, tr("purchase units was empty"));
      return;
    }

    auto purchaseUnit = purchaseUnits.first().toObject();
    auto purchaseAmount = purchaseUnit.value(QStringLiteral("amount")).toObject();

    if (purchaseAmount.value(QStringLiteral("currency_code")) != QStringLiteral("USD")) {
      ReportPayPalError(orderId, id, name, tr("currency was not in USD"));
      return;
    }

    QString amountStr = purchaseAmount.value(QStringLiteral("value")).toString();
    double amount = amountStr.toDouble();
    if (amount < 1.00) {
      ReportPayPalError(orderId, id, name, tr("amount was less than 1.00 USD"));
      return;
    }

    if (message > CONFIG[QStringLiteral("max_chat_length")].toInt()) {
      ReportPayPalError(orderId, id, name, tr("message was too long"));
      return;
    }

    if (!isMessageAcceptable(message)) {
      ReportPayPalError(orderId, id, name, tr("message was unacceptable"));
      return;
    }

    emit requestOverlayMessage(OverlayMessage::Alert(tr("%1 donated $%2").arg(name, amountStr), message));
    if (!message.isEmpty()) {
      publish(name, id, message, info.color, client->peerAddress(), info.auth, amountStr);
    }
  });
}

void ChatServer::handleSslError(const QList<QSslError> &errs)
{
  qCritical() << "Got SSL errors:" << errs;
}

void ChatServer::handlePeerVerifyError(const QSslError &err)
{
  qCritical() << "Peer verify error:" << err;
}

/*
template <typename T>
void ChatServer::youtube_doApi(const QString &endpoint, const QString &access_token, const QString &refresh_token, T slot)
{
  // Determine user's channel ID and name
  QNetworkRequest r(QStringLiteral("https://youtube.googleapis.com/youtube/v3%1").arg(endpoint));
  r.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ").append(access_token.toUtf8()));
  r.setRawHeader(QByteArrayLiteral("Client-ID"), CONFIG[QStringLiteral("youtube_client_id")].toString().toUtf8());
  r.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));

  QNetworkReply *reply = m_netMan->get(r);
  QObject::connect(reply, &QNetworkReply::finished, this, slot);
}

template <typename T>
void ChatServer::youtube_lookupUser(const MessageData &msg, const QString &auth_code, T success)
{
  QWebSocket *client = msg.client;

  {
    // See if we can find the user ID from the auth code alone
    QSqlQuery tokenLookup(m_db);
    tokenLookup.prepare(QStringLiteral("SELECT user_id FROM youtube WHERE auth_code = ?"));
    tokenLookup.addBindValue(auth_code);
    if (!tokenLookup.exec()) {
      // Failed to execute query, this is a developer error
      qCritical() << "Failed to look up YouTube token:" << tokenLookup.lastError();
      return;
    }

    // FIXME: Get membership status, handle refresh tokens if necessary

    if (tokenLookup.next()) {
      // Found user ID, return it here
      qint64 userId = tokenLookup.value(QStringLiteral("user_id")).toLongLong();
      insertSocket(userId, client);
      (this->*success)(msg, userId);
      return;
    }
  }

  // Couldn't find user ID with auth code alone, we'll need to grab an access token so we can
  // retrieve the channel ID, and then use that to find the user ID
  QNetworkRequest req(QStringLiteral("https://oauth2.googleapis.com/token"));
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

  QUrlQuery postData;
  postData.addQueryItem(QStringLiteral("code"), auth_code);
  postData.addQueryItem(QStringLiteral("client_id"), CONFIG[QStringLiteral("youtube_client_id")].toString().toUtf8());
  postData.addQueryItem(QStringLiteral("client_secret"), CONFIG[QStringLiteral("youtube_client_secret")].toString().toUtf8());
  postData.addQueryItem(QStringLiteral("redirect_uri"), QStringLiteral("https://stream.mattkc.com/chat/auth"));
  postData.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));

  QNetworkReply *reply = m_netMan->post(req, postData.toString(QUrl::FullyEncoded).toUtf8());
  connect(reply, &QNetworkReply::finished, this, [this, reply, msg, auth_code, success]{
    QWebSocket *client = msg.client;

    if (reply->error() != QNetworkReply::NoError) {
      sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
      return;
    }

    QJsonDocument json = QJsonDocument::fromJson(reply->readAll());

    // Pull tokens from response
    QString access_token = json.object().value(QStringLiteral("access_token")).toString();
    QString refresh_token = json.object().value(QStringLiteral("refresh_token")).toString();
    qint64 expire_time = QDateTime::currentSecsSinceEpoch() + json.object().value(QStringLiteral("expires_in")).toInt();

    youtube_doApi(QStringLiteral("/channels?part=snippet&mine=true"), access_token, refresh_token,
                 [this, msg, auth_code, access_token, refresh_token, expire_time, success]{
      QWebSocket *client = msg.client;
      QNetworkReply *reply = static_cast<QNetworkReply*>(sender());
      QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
      QJsonArray items = json.object().value(QStringLiteral("items")).toArray();

      if (items.empty()) {
        sendUserStatusMessage(client, STATUS_UNAUTHENTICATED);
        return;
      }

      QJsonObject item = items.first().toObject();
      QString channelId = item.value(QStringLiteral("id")).toString();
      qint64 userId = 0;

      // Now that we have the channel ID, we can see if it's been ascribed to a user ID or not before
      {
        QSqlQuery channelIdLookup(m_db);
        channelIdLookup.prepare(QStringLiteral("SELECT user_id FROM youtube WHERE channel_id = ? LIMIT 1"));
        channelIdLookup.addBindValue(channelId);
        if (!channelIdLookup.exec()) {
          // Failed to execute query, this is a developer error
          qCritical() << "Failed to look up channel ID:" << channelIdLookup.lastError();
          return;
        }

        // FIXME: Get membership status, handle refresh tokens if necessary

        if (channelIdLookup.next()) {
          // Found user ID, return it here
          userId = channelIdLookup.value(QStringLiteral("user_id")).toLongLong();
        }
      }

      if (userId == 0) {
        userId = createNewUser();
      }

      if (userId != 0) {
        // User ID was found or created, proceed

        // Keep track of this code/token with user ID (may be 0 at this point if it doesn't yet exist)
        QSqlQuery insertQuery(m_db);

        // YouTube Token
        insertQuery.prepare(QStringLiteral("INSERT INTO youtube (auth_code, channel_id, access_token, expire_time, refresh_token, user_id) VALUES (?, ?, ?, ?, ?, ?);"));

        insertQuery.addBindValue(auth_code);
        insertQuery.addBindValue(channelId);
        insertQuery.addBindValue(access_token);
        insertQuery.addBindValue(expire_time);
        insertQuery.addBindValue(refresh_token);
        insertQuery.addBindValue(userId);

        if (!insertQuery.exec()) {
          qCritical() << "Failed to insert YouTube token into table:" << insertQuery.lastError();
          return;
        }

        // Insert this token/ID map into the table
        insertSocket(userId, client);

        // Check if user already exists (or has been banned lmao)
        Status state = getUserStateFromID(userId);
        if (state != STATUS_AUTHENTICATED) {
          // User exists, return existing state
          sendUserStatusMessage(client, state);
          return;
        }

        (this->*success)(msg, userId);
      } else {
        sendUserState(client, STATUS_UNAUTHENTICATED);
      }
    });
  });
}
*/

void ChatServer::insertSimpleResponse(const QString &command, const QString &response)
{
  insertCommand(command, &ChatServer::commandSimpleResponse, Authorization::AUTH_USER);
  m_simpleResponses.insert(command, response);
}

void ChatServer::checkApiError(QNetworkReply *r)
{
  if (r->error() != QNetworkReply::NoError) {
    qCritical() << "Encountered network error:" << r->errorString() << r->readAll();
  }

  r->deleteLater();
}
