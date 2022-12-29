#ifndef UTIL_H
#define UTIL_H

#include <QCoreApplication>
#include <QString>

QString leftpad(qint64 v);
QString secsToHHMMSS(qint64 time);
QString secsToLongElapsed(qint64 time);
bool strequals(const QString &a, const QString &b);

#endif // UTIL_H
