#ifndef AUTHMODULE_H
#define AUTHMODULE_H

#include <functional>
#include <QDebug>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>

#include "authlevel.h"

class AuthModule : public QObject
{
  Q_OBJECT
public:
  AuthModule(QObject *parent);

  virtual QString id() const = 0;

  virtual void authenticate(QSqlDatabase db, const QString &token, const QString &redirect_uri, std::function<void(qint64)> callback, std::function<void()> failure) const = 0;

  qint64 createNewUser(QSqlDatabase db) const;

protected:
  QNetworkAccessManager *netMan() const { return m_netMan; }

private:
  QNetworkAccessManager *m_netMan;

};

#endif // AUTHMODULE_H
