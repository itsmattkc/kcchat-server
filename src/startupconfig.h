#ifndef STARTUPCONFIG_H
#define STARTUPCONFIG_H

#include <QJsonObject>
#include <QSslConfiguration>

/**
 * @brief Global read-only config loaded at startup
 */
class StartupConfig
{
public:
  StartupConfig() = default;

  bool load();

  QSslConfiguration getSslConfiguration() const;

  QVariant operator[](const QString &key) const
  {
    return m_config.value(key);
  }

private:
  QMap<QString, QVariant> m_config;

};

extern StartupConfig CONFIG;

#endif // STARTUPCONFIG_H
