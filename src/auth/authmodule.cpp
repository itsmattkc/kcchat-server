#include "authmodule.h"

AuthModule::AuthModule(QObject *parent)
 : QObject(parent)
{
  m_netMan = new QNetworkAccessManager(this);
}

qint64 AuthModule::createNewUser(QSqlDatabase db) const
{
  // Failed to find user ID from channel ID. This must be a new user! Create an account for them...
  QSqlQuery insertQuery(db);
  insertQuery.prepare(QStringLiteral("INSERT INTO users (display_name_change_time, last_message, last_message_time, banned_at, banned_until, auth_level, created_at) VALUES (?, ?, ?, ?, ?, ?, ?); SELECT LAST_INSERT_ID();"));

  insertQuery.addBindValue(0);
  insertQuery.addBindValue(QLatin1String(""));
  insertQuery.addBindValue(0);
  insertQuery.addBindValue(0);
  insertQuery.addBindValue(0);
  insertQuery.addBindValue(int(Authorization::AUTH_USER));
  insertQuery.addBindValue(QDateTime::currentSecsSinceEpoch());

  if (!insertQuery.exec()) {
    qCritical() << "Failed to insert user auth into table:" << insertQuery.lastError();
    return 0;
  }

  insertQuery.nextResult();

  if (insertQuery.next()) {
    return insertQuery.value(0).toLongLong();
  } else {
    qCritical() << "Failed to retrieve new user ID for user";
    return 0;
  }
}
