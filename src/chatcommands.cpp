#include "chatserver.h"

void ChatServer::initCommands()
{
  insertCommand(QStringLiteral("addcom"), &ChatServer::commandAddCom, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("alert"), &ChatServer::commandAlert, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("editcom"), &ChatServer::commandEditCom, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("delcom"), &ChatServer::commandDelCom, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("commands"), &ChatServer::commandHelp, Authorization::AUTH_USER);
  insertCommand(QStringLiteral("help"), &ChatServer::commandHelp, Authorization::AUTH_USER);
  insertCommand(QStringLiteral("autotts"), &ChatServer::commandAutoTTS, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("nexttts"), &ChatServer::commandNextTTS, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("pausetts"), &ChatServer::commandPauseTTS, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("purgetts"), &ChatServer::commandPurgeTTS, Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("say"), &ChatServer::commandSay, Authorization::AUTH_MOD);
  //insertCommand(QStringLiteral("shoutout"), &ChatServer::commandShoutout, Authorization::AUTH_USER);
  insertCommand(QStringLiteral("skiptts"), &ChatServer::commandSkipTTS, Authorization::AUTH_MOD);
  //insertCommand(QStringLiteral("so"), &ChatServer::commandShoutout, Authorization::AUTH_USER);
  insertCommand(QStringLiteral("time"), &ChatServer::commandTime, Authorization::AUTH_USER);
  insertCommand(QStringLiteral("timer"), &ChatServer::commandTimer, Authorization::AUTH_USER);
  insertCommand(QStringLiteral("info"), &ChatServer::commandInfo, Authorization::AUTH_USER);
  insertCommand(QStringLiteral("followmode"), &ChatServer::commandFollowMode, Authorization::AUTH_MOD);

  insertCommand(QStringLiteral("ban"), static_cast<CommandHandler_t>(&ChatServer::commandBan), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("unban"), static_cast<CommandHandler_t>(&ChatServer::commandUnban), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("ipban"), static_cast<CommandHandler_t>(&ChatServer::commandIpBan), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("ip"), static_cast<CommandHandler_t>(&ChatServer::commandIpBan), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("slowmode"), static_cast<CommandHandler_t>(&ChatServer::commandSlowMode), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("slow"), static_cast<CommandHandler_t>(&ChatServer::commandSlowMode), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("mod"), static_cast<CommandHandler_t>(&ChatServer::commandMod), Authorization::AUTH_ADMIN);
  insertCommand(QStringLiteral("unmod"), static_cast<CommandHandler_t>(&ChatServer::commandUnmod), Authorization::AUTH_ADMIN);
  insertCommand(QStringLiteral("delete"), static_cast<CommandHandler_t>(&ChatServer::commandDelMsg), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("del"), static_cast<CommandHandler_t>(&ChatServer::commandDelMsg), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("rm"), static_cast<CommandHandler_t>(&ChatServer::commandDelMsg), Authorization::AUTH_MOD);
  insertCommand(QStringLiteral("video"), static_cast<CommandHandler_t>(&ChatServer::commandVideo), Authorization::AUTH_ADMIN);
}

ChatServer::Response ChatServer::commandAddCom(const Request &r)
{
  if (r.args().size() >= 3) {
    QString newcom = r.args().at(1).toLower();

    if (m_commandMap.contains(newcom)) {
      return Response(r, tr("Command \"%1\" already exists").arg(newcom));
    } else {
      QString response = r.args().mid(2).join(' ');
      m_simpleResponses.insert(newcom, response);
      insertCommand(newcom, &ChatServer::commandSimpleResponse, Authorization::AUTH_USER);

      // Add to database
      QSqlQuery q(m_db);
      q.prepare(QStringLiteral("INSERT INTO responses (command, response) VALUES (?, ?)"));
      q.addBindValue(newcom);
      q.addBindValue(response);
      if (!q.exec()) {
        qCritical() << "Failed to add simple response:" << q.lastError();
      }

      return Response(r, tr("Command \"%1\" added").arg(newcom));
    }
  } else {
    return Response(r, tr("Usage: %1 <command> <reply>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandAlert(const Request &r)
{
  if (r.args().size() == 2 || r.args().size() == 3) {
    emit requestOverlayMessage(OverlayMessage::Alert(r.args().at(1), r.args().size() == 3 ? r.args().at(2) : QString()));
    return Response(r, tr("Alert submitted successfully"));
  } else {
    return Response(r, tr("Usage: %1 <title> [subtitle]").arg(r.command()));
  }
}

ChatServer::Response ChatServer::doMention(const Request &r)
{
  static const QStringList helloWords = {
    "hello",
    "hi",
    "hey",
    "salutations",
    "greetings",
    "sup",
    "wassup",
    "whats up",
    "what's up"
  };

  bool isGreeting = false;

  QStringList words = r.line().split(' ');
  for (const QString &h : helloWords) {
    if (h.contains(' ')) {
      if (r.line().contains(h)) {
        isGreeting = true;
        break;
      }
    } else if (words.contains(h, Qt::CaseInsensitive)) {
      isGreeting = true;
      break;
    }
  }

  if (isGreeting) {
    if (r.authorization() >= Authorization::AUTH_MEMBER) {
      return Response(r, tr("Hey @%1!").arg(r.author()), true);
    } else {
      return Response(r, tr("I only say hello to subscribers"), true);
    }
  } else if (r.line().startsWith(QStringLiteral("@%1").arg(CONFIG[QStringLiteral("bot_name")].toString()), Qt::CaseInsensitive) && r.line().endsWith('?')) {
    static const QStringList random_replies = {
      "It is certain.",
      "It is decidedly so.",
      "Without a doubt.",
      "Yes definitely.",
      "You may rely on it.",
      "As I see it, yes.",
      "Most likely.",
      "Outlook good.",
      "Yes.",
      "Signs point to yes.",
      "Reply hazy, try again.",
      "Ask again later.",
      "Better not tell you now.",
      "Cannot predict now.",
      "Concentrate and ask again.",
      "Don't count on it.",
      "My reply is no.",
      "My sources say no.",
      "Outlook not so good.",
      "Very doubtful."
    };

    return Response(r, random_replies.at(QRandomGenerator::global()->bounded(random_replies.size())), true);
  }

  return Response();
}

ChatServer::Response ChatServer::commandEditCom(const Request &r)
{
  if (r.args().size() >= 3) {
    QString editcom = r.args().at(1).toLower();

    if (m_commandMap.contains(editcom)) {
      if (m_commandMap.value(editcom).handler == &ChatServer::commandSimpleResponse) {
        QString response = r.args().mid(2).join(' ');
        m_simpleResponses.insert(editcom, response);

        // Edit command in database
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("UPDATE responses SET response = ? WHERE command = ?"));
        q.addBindValue(response);
        q.addBindValue(editcom);
        if (!q.exec()) {
          qCritical() << "Failed to edit simple response:" << q.lastError();
        }

        return Response(r, tr("Command \"%1\" edited").arg(editcom));
      } else {
        return Response(r, tr("Command \"%1\" cannot be edited").arg(editcom));
      }
    } else {
      return Response(r, tr("Command \"%1\" does not exist").arg(editcom));
    }
  } else {
    return Response(r, tr("Usage: %1 <command> <reply>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandDelCom(const Request &r)
{
  if (r.args().size() == 2) {
    QString delcom = r.args().at(1).toLower();

    if (m_commandMap.contains(delcom)) {
      if (m_commandMap.value(delcom).handler == &ChatServer::commandSimpleResponse) {
        // Delete command from response and command maps
        m_simpleResponses.remove(delcom);
        m_commandMap.remove(delcom);

        // Delete command from database
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("DELETE FROM responses WHERE command = ?"));
        q.addBindValue(delcom);
        if (!q.exec()) {
          qCritical() << "Failed to delete simple response:" << q.lastError();
        }

        return Response(r, tr("Command \"%1\" deleted").arg(delcom));
      } else {
        return Response(r, tr("Command \"%1\" cannot be deleted").arg(delcom));
      }
    } else {
      return Response(r, tr("Command \"%1\" does not exist").arg(delcom));
    }
  } else {
    return Response(r, tr("Usage: %1 <command>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandHelp(const Request &r)
{
  QString s;

  for (auto it=m_commandMap.cbegin(); it!=m_commandMap.cend(); it++) {
    if (r.authorization() >= it->authorization) {
      if (!s.isEmpty()) {
        s.append(QStringLiteral(", "));
      }
      s.append(it.key());
    }
  }

  return Response(r, tr("Available commands: %1").arg(s));
}

ChatServer::Response ChatServer::commandPauseTTS(const Request &r)
{
  emit requestOverlayMessage(OverlayMessage::Command(OverlayMessage::CMD_PAUSE_TTS));
  return Response(r, tr("TTS paused"));
}

ChatServer::Response ChatServer::commandPurgeTTS(const Request &r)
{
  emit requestOverlayMessage(OverlayMessage::Command(OverlayMessage::CMD_PURGE_TTS));
  return Response(r, tr("TTS purged"));
}

ChatServer::Response ChatServer::commandSay(const Request &r)
{
  if (r.args().size() != 2) {
    return Response(r, tr("Usage: %1 <message>").arg(r.command()));
  } else {
    return Response(Request(), r.args().at(1), true);
  }
}

ChatServer::Response ChatServer::commandShoutout(const Request &r)
{
  if (r.args().size() == 2) {
    QString so_user = r.args().at(1);

    while (!so_user.isEmpty() && so_user.at(0) == '@') {
      so_user.remove(0, 1);
    }

    return Response(r, tr("I don't even know who @%1 is").arg(so_user));
  } else {
    return Response(r, tr("Usage: %1 <user>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandSkipTTS(const Request &r)
{
  emit requestOverlayMessage(OverlayMessage::Command(OverlayMessage::CMD_SKIP_TTS));
  return Response(r, tr("TTS skipped"));
}

ChatServer::Response ChatServer::commandTime(const Request &r)
{
  return Response(r, tr("The time for the streamer is: %1").arg(QDateTime::currentDateTime().toString()), true);
}

ChatServer::Response ChatServer::commandTimer(const Request &r)
{
  if (r.args().size() == 3) {
    QString action = r.args().at(1).toLower();
    QString name = r.args().at(2).toLower();

    if (action == QStringLiteral("start")) {
      if (m_timers.contains(name)) {
        return Response(r, tr("Timer \"%1\" already exists").arg(name), true);
      } else {
        m_timers.insert(name, QDateTime::currentSecsSinceEpoch());
        return Response(r, tr("Timer \"%1\" created").arg(name), true);
      }
    } else if (action == QStringLiteral("check") || action == QStringLiteral("stop")) {
      if (m_timers.contains(name)) {
        qint64 old = m_timers.value(name);
        qint64 elapsed = QDateTime::currentSecsSinceEpoch() - old;

        QString old_str = QDateTime::fromSecsSinceEpoch(old).toString();
        QString elapsed_str = secsToHHMMSS(elapsed);

        if (action == QStringLiteral("check")) {
          return Response(r, tr("Timer \"%1\" has been running for %2 (started %3)").arg(name, elapsed_str, old_str), true);
        } else {
          m_timers.remove(name);
          return Response(r, tr("Timer \"%1\" stopped at %2 (started %3)").arg(name, elapsed_str, old_str), true);
        }
      } else {
        return Response(r, tr("Timer \"%1\" does not exist").arg(name), true);
      }
    }
  }

  return Response(r, tr("Usage: %1 <start/check/stop> <name>").arg(r.command()), true);
}

ChatServer::Response ChatServer::commandSimpleResponse(const Request &r)
{
  return Response(r, m_simpleResponses.value(r.command()), true);
}

ChatServer::Response ChatServer::commandAutoTTS(const Request &r)
{
  emit requestOverlayMessage(OverlayMessage::Command(OverlayMessage::CMD_AUTO_TTS));
  return Response(r, tr("Auto TTS toggled"));
}

ChatServer::Response ChatServer::commandNextTTS(const Request &r)
{
  emit requestOverlayMessage(OverlayMessage::Command(OverlayMessage::CMD_NEXT_TTS));
  return Response(r, tr("Requested next TTS"));
}

ChatServer::Response ChatServer::commandBan(const Request &r)
{
  return ban(r, false);
}

ChatServer::Response ChatServer::commandUnban(const Request &r)
{
  if (r.args().size() == 2) {
    QSqlQuery userUpdate(m_db);
    userUpdate.prepare(QStringLiteral("UPDATE users SET banned_until = 0 WHERE display_name = ?;"
                                      "SELECT id FROM users WHERE display_name = ?;"));

    QString unbannedUser = stripAtSymbols(r.args().at(1));
    userUpdate.addBindValue(unbannedUser);
    userUpdate.addBindValue(unbannedUser);

    if (!userUpdate.exec()) {
      qCritical() << "Failed to update user details for unban:" << userUpdate.lastError();
      return Response::Error(r);
    }

    if (userUpdate.numRowsAffected() == 1) {
      userUpdate.nextResult();
      userUpdate.next();

      qint64 bannedId = userUpdate.value(0).toLongLong();

      const QList<QWebSocket*> bannedClient = m_clients.socketsForAuthor(bannedId);
      for (auto skt : bannedClient) {
        // Send banned status to user
        sendUserState(skt, bannedId);
      }

      return Response(r, tr("%1 unbanned").arg(unbannedUser));
    } else {
      // Let sender know that user was banned
      return Response(r, tr("Couldn't find user %1").arg(unbannedUser));
    }
  } else {
    return Response(r, tr("Usage: %1 <name>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandIpBan(const Request &r)
{
  return ban(r, true);
}

ChatServer::Response ChatServer::commandSlowMode(const Request &r)
{
  if (r.args().size() == 2) {
    m_slowMode = r.args().at(1).toInt();
    return Response(r, tr("Slow mode set to %1 seconds").arg(m_slowMode));
  } else {
    return Response(r, tr("Usage: %1 <seconds>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandMod(const Request &r)
{
  return setUserAuthLevelCommand(r, Authorization::AUTH_MOD);
}

ChatServer::Response ChatServer::commandUnmod(const Request &r)
{
  return setUserAuthLevelCommand(r, Authorization::AUTH_USER);
}

ChatServer::Response ChatServer::commandDelMsg(const Request &r)
{
  if (r.args().size() >= 2) {
    QVector<qint64> msgs;
    for (size_t i = 1; i < r.args().size(); i++) {
      bool ok;
      qint64 msg = r.args().at(i).toLongLong(&ok);
      if (ok) {
        msgs.append(msg);
      }
    }
    dropMessages(msgs, true);
    return Response(r, tr("%1 message(s) deleted").arg(msgs.size()));
  } else {
    return Response(r, tr("Usage: %1 <messages-to-delete>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandVideo(const ChatServer::Request &r)
{
  if (r.args().size() >= 2) {
    const QString &id = r.args().at(1);
    QSqlQuery updateVideoQuery(m_db);
    updateVideoQuery.prepare(QStringLiteral("UPDATE config SET value = ? WHERE name = 'video'"));
    updateVideoQuery.addBindValue(id);
    if (!updateVideoQuery.exec()) {
      qCritical() << "Failed to update video query:" << updateVideoQuery.lastError();
    }
    return Response(r, tr("Video updated to %1 successfully").arg(id));
  } else {
    return Response(r, tr("Usage: %1 <video-id>").arg(r.command()));
  }
}

ChatServer::Response ChatServer::commandInfo(const Request &r)
{
  return Response(r, tr("Version: %1<br>Slow Mode: %2 seconds<br>Duplicate Slow Mode: %3 seconds<br>Follow Mode: %4 seconds").arg(
                    QStringLiteral("0.1"),
                    QString::number(m_slowMode),
                    QString::number(m_duplicateSlowMode),
                    QString::number(m_followMode)
                    ));
}

ChatServer::Response ChatServer::commandFollowMode(const Request &r)
{
  if (r.args().size() == 2) {
    const QString &s = r.args().at(1);
    bool ok;
    int newFollowMode = s.toInt(&ok);
    if (ok) {
      m_followMode = newFollowMode;
      return Response(r, tr("Follow mode set to %1 seconds").arg(m_followMode));
    } else {
      return Response(r, tr("Failed to parse seconds '%1'").arg(s));
    }
  } else {
    return Response(r, tr("Usage: %1 <seconds>").arg(r.command()));
  }
}
