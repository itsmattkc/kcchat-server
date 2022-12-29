#include "startupconfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QSslKey>

StartupConfig CONFIG;

bool StartupConfig::load()
{
  QFile f(QStringLiteral("config.json"));
  if (!f.open(QFile::ReadOnly)) {
    return false;
  }

  QJsonObject d = QJsonDocument::fromJson(f.readAll()).object();

  for (auto it = d.constBegin(); it != d.constEnd(); it++) {
    m_config.insert(it.key(), it.value().toVariant());
  }

  f.close();

  return true;
}

QSslConfiguration StartupConfig::getSslConfiguration() const
{
  QSslConfiguration s;

  QString sslKeyFile = m_config.value(QStringLiteral("ssl_key")).toString();
  QString sslCrtFile = m_config.value(QStringLiteral("ssl_crt")).toString();
  QString sslCaFile = m_config.value(QStringLiteral("ssl_ca")).toString();

  if (sslKeyFile.isEmpty() || sslCrtFile.isEmpty()) {
    return s;
  }

  QFile sslKey(sslKeyFile);
  if (!sslKey.open(QFile::ReadOnly)) {
    qWarning() << "Failed to open SSL private key file" << sslKeyFile;
    return s;
  }

  QFile sslCrt(sslCrtFile);
  if (!sslCrt.open(QFile::ReadOnly)) {
    qWarning() << "Failed to open SSL certificate file" << sslCrtFile;
    return s;
  }

  QSslKey k(&sslKey, QSsl::Rsa);
  if (k.isNull()) {
    qWarning() << "Failed to read or decode SSL private key";
    return s;
  }
  sslKey.close();

  QSslCertificate c(&sslCrt);
  if (c.isNull()) {
    qWarning() << "Failed to read or decode SSL certificate";
    return s;
  }
  sslCrt.close();

  s.setPrivateKey(k);
  s.setLocalCertificate(c);
  s.setPeerVerifyMode(QSslSocket::VerifyNone);

  if (!sslCaFile.isEmpty()) {
    QFile ca(sslCaFile);
    if (ca.open(QFile::ReadOnly)) {
      s.setCaCertificates(QSslCertificate::fromDevice(&ca));
    } else {
      qWarning() << "Failed to load SSL CA certificates";
    }
  }

  return s;
}
