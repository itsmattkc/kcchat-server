#ifndef GOOGLEAUTH_H
#define GOOGLEAUTH_H

#include "authmodule.h"

class GoogleAuth : public AuthModule
{
  Q_OBJECT
public:
  GoogleAuth(QObject *parent) : AuthModule(parent){}

  virtual QString id() const override { return QStringLiteral("google"); }

  virtual void authenticate(QSqlDatabase db, const QString &token, const QString &redirect_uri, std::function<void(qint64)> callback, std::function<void()> failure) const override;

private:
  void lookupUserFromSub(QSqlDatabase db, const QString &sub, std::function<void(qint64)> callback, std::function<void()> failure) const;

  void handleNewToken(QSqlDatabase db, const QString &token, const QString &redirect_uri, const QString &existingRefresh, std::function<void(qint64)> callback, std::function<void()> failure) const;

};

#endif // GOOGLEAUTH_H
