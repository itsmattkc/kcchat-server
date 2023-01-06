#ifndef AUTHLEVEL_H
#define AUTHLEVEL_H

#include <QVariant>

enum class Authorization
{
  AUTH_USER = 0,
  AUTH_MEMBER = 20,
  AUTH_MOD = 50,
  AUTH_ADMIN = 100
};

#endif // AUTHLEVEL_H
