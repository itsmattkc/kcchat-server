cmake_minimum_required(VERSION 3.14)

project(kcbot LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt5 REQUIRED COMPONENTS Core Network WebSockets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network WebSockets Sql)

add_executable(kcbot
  src/auth/authlevel.h
  src/auth/authmodule.cpp
  src/auth/authmodule.h
  src/auth/googleauth.cpp
  src/auth/googleauth.h
  src/chatcommands.cpp
  src/chatserver.cpp
  src/chatserver.h
  src/main.cpp
  src/overlaydispatch.cpp
  src/overlaydispatch.h
  src/overlaymessage.cpp
  src/overlaymessage.h
  src/startupconfig.cpp
  src/startupconfig.h
  src/usersocketmap.cpp
  src/usersocketmap.h
  src/util.cpp
  src/util.h
)

target_link_libraries(kcbot Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets Qt${QT_VERSION_MAJOR}::Sql)

install(TARGETS kcbot
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
