#include "util.h"

QString leftpad(qint64 v)
{
  QString s = QString::number(v);

  while (s.size() < 2) {
    s.prepend('0');
  }

  return s;
}

QString secsToHHMMSS(qint64 time)
{
  qint64 hours = time / 3600;
  time -= hours * 3600;

  qint64 minutes = time / 60;
  time -= minutes * 60;

  return QStringLiteral("%1:%2:%3").arg(leftpad(hours), leftpad(minutes), leftpad(time));
}

QString secsToLongElapsed(qint64 time)
{
  qint64 days = time / 86400;
  time -= days * 86400;

  qint64 hours = time / 3600;
  time -= hours * 3600;

  qint64 minutes = time / 60;
  time -= minutes * 60;

  return QCoreApplication::translate("Util", "%1 days, %2 hours, %3 minutes, and %4 seconds").arg(
        QString::number(days), QString::number(hours), QString::number(minutes), QString::number(time));
}

bool strequals(const QString &a, const QString &b)
{
  return !a.compare(b, Qt::CaseInsensitive);
}
