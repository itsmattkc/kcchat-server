#include "overlaymessage.h"

QString OverlayMessage::getTypeName(Type type)
{
  switch (type) {
  case MSG_ALERT: return QStringLiteral("alert");
  case MSG_JOKE: return QStringLiteral("joke");
  case MSG_COMMAND: return QStringLiteral("command");
  case MSG_NONE:
    break;
  }

  return QString();
}

QJsonDocument OverlayMessage::toJson() const
{
  QJsonObject outer;

  outer.insert(QStringLiteral("type"), getTypeName(m_type));
  outer.insert(QStringLiteral("data"), getDataObject());

  return QJsonDocument(outer);
}

QJsonObject OverlayMessage::getDataObject() const
{
  QJsonObject j;

  switch (m_type) {
  case MSG_ALERT:
    j.insert(QStringLiteral("title"), m_alertTitle);
    j.insert(QStringLiteral("subtitle"), m_alertSubtitle);
    break;
  case MSG_JOKE:
    j.insert(QStringLiteral("name"), m_jokeName);
    break;
  case MSG_COMMAND:
    j.insert(QStringLiteral("name"), getCommandName(m_command));
    break;
  case MSG_NONE:
    break;
  }

  return j;
}

QString OverlayMessage::getCommandName(CommandType c)
{
  switch (c) {
  case CMD_SKIP_TTS: return QStringLiteral("skiptts");
  case CMD_PAUSE_TTS: return QStringLiteral("pausetts");
  case CMD_PURGE_TTS: return QStringLiteral("purgetts");
  case CMD_AUTO_TTS: return QStringLiteral("autotts");
  case CMD_NEXT_TTS: return QStringLiteral("nexttts");
  }

  return QString();
}
