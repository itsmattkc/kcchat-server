#include <iostream>
#include <QCoreApplication>
#include <QThread>
#include <signal.h>

#include "chatserver.h"
#include "overlaydispatch.h"
#include "startupconfig.h"

/**
 * @brief Custom handler for QDebug that prints messages to stderr
 */
void dbg(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
  QByteArray localMsg = msg.toLocal8Bit();

  const char* msg_type = "UNKNOWN";
  switch (type) {
  case QtDebugMsg:
    msg_type = "DEBUG";
    break;
  case QtInfoMsg:
    msg_type = "INFO";
    break;
  case QtWarningMsg:
    msg_type = "WARNING";
    break;
  case QtCriticalMsg:
    msg_type = "ERROR";
    break;
  case QtFatalMsg:
    msg_type = "FATAL";
    break;
  }

  fprintf(stderr, "[%s] %s\n", msg_type, localMsg.constData());

#ifdef Q_OS_WINDOWS
  // Windows still seems to buffer stderr and we want to see debug messages immediately, so here we
  // make sure each line is flushed
  fflush(stderr);
#endif
}

/**
 * @brief Exit QThreads in a safe way
 */
void threadExit(QThread *t)
{
  t->quit();
  t->wait();
}

/**
 * @brief Handle Ctrl+C in a safe way
 */
void safeExit(int s)
{
  qApp->quit();
}

int main(int argc, char *argv[])
{
  // Install custom handler for QDebug
  qInstallMessageHandler(dbg);

  // Handle Ctrl+C in a safe way
  signal(SIGINT, safeExit);

  // Load startup configuration
  if (!CONFIG.load()) {
    qCritical() << "Failed to open config.json";
    return 1;
  }

  // Create main application event loop
  QCoreApplication a(argc, argv);

  // Create threads for chat server and overlay
  QThread chatThread, overlayThread;

  // Register overlay message (so it can be sent between QThreads)
  qRegisterMetaType<OverlayMessage>();

  // Create chat server and move to its own thread
  ChatServer dispatch;
  chatThread.start();
  dispatch.moveToThread(&chatThread);

  // Create overlay handler and move to its own thread
  OverlayDispatch overlay;
  overlayThread.start();
  overlay.moveToThread(&overlayThread);

  // Tell chat server and overlay to start in their own threads
  QMetaObject::invokeMethod(&dispatch, &ChatServer::start, Qt::QueuedConnection);
  QMetaObject::invokeMethod(&overlay, &OverlayDispatch::start, Qt::QueuedConnection);

  // Connect signals between chat server and overlay
  QObject::connect(&dispatch, &ChatServer::requestOverlayMessage, &overlay, &OverlayDispatch::sendMessage);

  // Run main event loop
  int r = a.exec();

  // Tell chat server and overlay to stop
  QMetaObject::invokeMethod(&dispatch, &ChatServer::stop, Qt::QueuedConnection);
  QMetaObject::invokeMethod(&overlay, &OverlayDispatch::stop, Qt::QueuedConnection);

  // Safely join their threads
  threadExit(&overlayThread);
  threadExit(&chatThread);

  return r;
}
