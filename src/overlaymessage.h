#ifndef OVERLAYMESSAGE_H
#define OVERLAYMESSAGE_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

class OverlayMessage
{
public:
  enum Type {
    MSG_NONE,
    MSG_ALERT,
    MSG_JOKE,
    MSG_COMMAND
  };

  enum CommandType {
    CMD_SKIP_TTS,
    CMD_PAUSE_TTS,
    CMD_PURGE_TTS,
    CMD_AUTO_TTS,
    CMD_NEXT_TTS
  };

  OverlayMessage(Type type = MSG_NONE)
  {
    m_type = type;
  }

  static OverlayMessage Alert(const QString &title, const QString &subtitle = QString())
  {
    OverlayMessage m(MSG_ALERT);
    m.setAlertTitle(title);
    m.setAlertSubtitle(subtitle);
    return m;
  }

  static OverlayMessage Joke(const QString &joke)
  {
    OverlayMessage m(MSG_JOKE);
    m.setJokeName(joke);
    return m;
  }

  static OverlayMessage Command(const CommandType &cmd)
  {
    OverlayMessage m(MSG_COMMAND);
    m.setCommand(cmd);
    return m;
  }

  static QString getTypeName(Type type);

  QJsonDocument toJson() const;

  Type type() const { return m_type; }

  const QString &alertTitle() const { return m_alertTitle; }
  void setAlertTitle(const QString &title) { m_alertTitle = title; }

  const QString &alertSubtitle() const { return m_alertSubtitle; }
  void setAlertSubtitle(const QString &subtitle) { m_alertSubtitle = subtitle; }

  const QString &jokeName() const { return m_jokeName; }
  void setJokeName(const QString &jokeName) { m_jokeName = jokeName; }

  CommandType command() const { return m_command; }
  void setCommand(CommandType c) { m_command = c; }

private:
  QJsonObject getDataObject() const;

  static QString getCommandName(CommandType c);

  Type m_type;

  QString m_alertTitle;
  QString m_alertSubtitle;

  QString m_jokeName;

  CommandType m_command;

};

Q_DECLARE_METATYPE(OverlayMessage);

#endif // OVERLAYMESSAGE_H
