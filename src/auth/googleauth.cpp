#include "googleauth.h"

#include <QDateTime>
#include <QDebug>
#include <QUrlQuery>

#include "../startupconfig.h"

void GoogleAuth::authenticate(QSqlDatabase db, const QString &token, std::function<void(qint64)> callback, std::function<void ()> failure) const
{
  // Look up access token in database
  QSqlQuery lookupToken(db);
  lookupToken.prepare(QStringLiteral("SELECT * FROM google_tokens WHERE auth_token = ?"));
  lookupToken.addBindValue(token);
  if (!lookupToken.exec()) {
    qCritical() << "Failed to look up Google token:" << lookupToken.lastError();
    failure();
    return;
  }

  if (lookupToken.next()) {
    qint64 expireTime = lookupToken.value(QStringLiteral("expires_at")).toLongLong();

    if (expireTime < QDateTime::currentSecsSinceEpoch()) {
      handleNewToken(db, token, lookupToken.value(QStringLiteral("refresh_token")).toString(), callback, failure);
    } else {
      lookupUserFromSub(db, lookupToken.value(QStringLiteral("google_id")).toString(), callback, failure);
    }
  } else {
    // We've never seen this token before, try exchanging it for an access token
    handleNewToken(db, token, QString(), callback, failure);
  }
}

void GoogleAuth::lookupUserFromSub(QSqlDatabase db, const QString &sub, std::function<void (qint64)> callback, std::function<void ()> failure) const
{
  qint64 userId = 0;

  // Determine if we already have one
  QSqlQuery lookupToken(db);
  lookupToken.prepare(QStringLiteral("SELECT user_id FROM google_users WHERE sub = ?"));
  lookupToken.addBindValue(sub);
  if (!lookupToken.exec()) {
    qCritical() << "Failed to look up Google token:" << lookupToken.lastError();
    failure();
    return;
  }

  if (lookupToken.next()) {
    // Found user ID! Proceed with it:
    userId = lookupToken.value(QStringLiteral("user_id")).toLongLong();
  } else {
    // User has never logged in before, we'll create a new user for them
    userId = createNewUser(db);

    if (userId != 0) {
      // Link Google sub with our ID
      QSqlQuery linkUser(db);
      linkUser.prepare(QStringLiteral("INSERT INTO google_users (sub, user_id) VALUES (?, ?)"));
      linkUser.addBindValue(sub);
      linkUser.addBindValue(userId);
      if (!linkUser.exec()) {
        qCritical() << "Failed to insert link between Google sub and user ID:" << linkUser.lastError();
        failure();
        return;
      }
    }
  }

  if (userId != 0) {
    callback(userId);
  }
}

void GoogleAuth::handleNewToken(QSqlDatabase db, const QString &token, const QString &existingRefresh, std::function<void (qint64)> callback, std::function<void ()> failure) const
{
  QNetworkRequest req(QStringLiteral("https://oauth2.googleapis.com/token"));
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

  QUrlQuery q;
  q.addQueryItem(QStringLiteral("client_id"), CONFIG[QStringLiteral("youtube_client_id")].toString());
  q.addQueryItem(QStringLiteral("client_secret"), CONFIG[QStringLiteral("youtube_client_secret")].toString());

  bool isRefresh = !existingRefresh.isEmpty();

  if (isRefresh) {
    q.addQueryItem(QStringLiteral("refresh_token"), existingRefresh);
    q.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
  } else {
    q.addQueryItem(QStringLiteral("code"), token);
    q.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    q.addQueryItem(QStringLiteral("redirect_uri"), QStringLiteral("https://stream.mattkc.com"));
  }

  QNetworkReply *reply = netMan()->post(req, q.toString(QUrl::FullyEncoded).toUtf8());
  connect(reply, &QNetworkReply::finished, this, [this, db, token, existingRefresh, isRefresh, callback, failure]{
    QNetworkReply *sender = static_cast<QNetworkReply *>(this->sender());
    if (sender->error()) {
      if (isRefresh) {
        qCritical() << "Failed to refresh access token:" << sender->errorString() << sender->readAll();
      } else {
        qCritical() << "Failed to exchange auth token for access token:" << sender->errorString() << sender->readAll();
      }
      failure();
      return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(sender->readAll());
    QJsonObject o = doc.object();

    QString accessToken = o.value(QStringLiteral("access_token")).toString();
    QString refreshToken = isRefresh ? existingRefresh : o.value(QStringLiteral("refresh_token")).toString();
    qint64 expiresIn = o.value(QStringLiteral("expires_in")).toInt();
    qint64 expiresAt = QDateTime::currentSecsSinceEpoch() + expiresIn;

    // Use access token to retrieve Google ID
    QNetworkRequest idLookup(QStringLiteral("https://www.googleapis.com/oauth2/v2/userinfo"));
    idLookup.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ").append(accessToken.toUtf8()));
    QNetworkReply *reply = netMan()->get(idLookup);
    connect(reply, &QNetworkReply::finished, this, [this, db, token, callback, failure, accessToken, refreshToken, expiresAt]{
      QNetworkReply *sender = static_cast<QNetworkReply *>(this->sender());
      if (sender->error()) {
        qCritical() << "Failed to read Google user information:" << sender->errorString() << sender->readAll();
        failure();
        return;
      }

      QJsonDocument doc = QJsonDocument::fromJson(sender->readAll());
      QJsonObject o = doc.object();

      QString googleId = o.value(QStringLiteral("id")).toString();

      QSqlQuery insertQuery(db);
      insertQuery.prepare(QStringLiteral("INSERT INTO google_tokens (auth_token, access_token, refresh_token, expires_at, google_id) VALUES (?, ?, ?, ?, ?)"
                                         "ON DUPLICATE KEY UPDATE access_token = ?, expires_at = ?"));
      insertQuery.addBindValue(token);
      insertQuery.addBindValue(accessToken);
      insertQuery.addBindValue(refreshToken);
      insertQuery.addBindValue(expiresAt);
      insertQuery.addBindValue(googleId);

      insertQuery.addBindValue(accessToken);
      insertQuery.addBindValue(expiresAt);
      if (!insertQuery.exec()) {
        qCritical() << "Failed to insert Google token:" << insertQuery.lastError();
        failure();
      } else {
        lookupUserFromSub(db, googleId, callback, failure);
      }
    });
  });
}
