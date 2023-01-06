#include "googleauth.h"

#include <QDateTime>
#include <QDebug>

#include "../startupconfig.h"

void GoogleAuth::authenticate(QSqlDatabase db, const QString &token, std::function<void(qint64)> callback, std::function<void ()> failure) const
{
  // See if we already have this token ID/sub in the database
  {
    QSqlQuery lookupToken(db);
    lookupToken.prepare(QStringLiteral("DELETE FROM google_ids WHERE expiry < ?;"
                                       "SELECT sub FROM google_ids WHERE id_token = ?"));
    lookupToken.addBindValue(QDateTime::currentSecsSinceEpoch());
    lookupToken.addBindValue(token);
    if (!lookupToken.exec()) {
      qCritical() << "Failed to look up sub from Google ID:" << lookupToken.lastError();
      failure();
      return;
    }

    lookupToken.nextResult();

    if (lookupToken.next()) {
      // Found the sub for this token!
      lookupUserFromSub(db, lookupToken.value(QStringLiteral("sub")).toString(), callback, failure);
      return;
    }
  }

  // Failed to find sub from token, this token must be new. Look it up...
  QNetworkRequest req(QStringLiteral("https://oauth2.googleapis.com/tokeninfo?id_token=%1").arg(token));
  QNetworkReply *idTokenLookup = netMan()->get(req);
  connect(idTokenLookup, &QNetworkReply::finished, this, [this, db, callback, failure, token]{
    QNetworkReply *r = static_cast<QNetworkReply *>(sender());
    QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();

    QString exp = o.value(QStringLiteral("exp")).toString();
    QString aud = o.value(QStringLiteral("aud")).toString();
    QString iss = o.value(QStringLiteral("iss")).toString();
    QString sub = o.value(QStringLiteral("sub")).toString();

    if (exp.toLongLong() > QDateTime::currentSecsSinceEpoch()
        && aud == CONFIG[QStringLiteral("youtube_client_id")].toString()
        && (iss == QStringLiteral("https://accounts.google.com") || iss == QStringLiteral("accounts.google.com"))) {
      // Seems legit, pair the ID token to their sub
      {
        QSqlQuery tokenSubLink(db);
        tokenSubLink.prepare(QStringLiteral("INSERT INTO google_ids (id_token, sub, expiry) VALUES (?, ?, ?)"));
        tokenSubLink.addBindValue(token);
        tokenSubLink.addBindValue(sub);
        tokenSubLink.addBindValue(exp);
        if (!tokenSubLink.exec()) {
          qCritical() << "Failed to link ID token to sub:" << tokenSubLink.lastError();
        }
      }

      lookupUserFromSub(db, sub, callback, failure);
    } else {
      qWarning() << "Invalid ID token:"
                 << (exp.toLongLong() > QDateTime::currentSecsSinceEpoch())
                 << (aud == CONFIG[QStringLiteral("youtube_client_id")].toString())
                 << (iss == QStringLiteral("https://accounts.google.com") || iss == QStringLiteral("accounts.google.com"));
      failure();
    }
  });
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
