/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <windows.h>
#include <objbase.h>

#include <QAxFactory>
#include <QAxObject>
#include <QString>
#include <QVariant>

#include "ComInteropHelper.h"
#include "InteropObject.h"

QAXFACTORY_BEGIN("{1e405fc2-1a3a-468b-8bd6-bfbb58770390}", "{792d1aac-53cc-4dc9-bc29-e5295fdb93a9}")
  QAXCLASS(InteropObject)
QAXFACTORY_END()

// These are ActiveQt internals; declaring here as I don't like their WinMain much...
extern HANDLE qAxInstance;
extern bool qAxOutProcServer;
extern wchar_t qAxModuleFilename[MAX_PATH];
extern QString qAxInit();

ComInteropHelper::ComInteropHelper ():
  m_client (new QAxObject (QLatin1String ("Transmission.QtClient")))
{
}

ComInteropHelper::~ComInteropHelper ()
{
}

bool
ComInteropHelper::isConnected () const
{
  return !m_client->isNull ();
}
    
QVariant
ComInteropHelper::addMetainfo (const QString& metainfo)
{
  return m_client->dynamicCall ("AddMetainfo(QString)", metainfo);
}

void
ComInteropHelper::initialize ()
{
  qAxOutProcServer = true;
  ::GetModuleFileNameW (0, qAxModuleFilename, MAX_PATH);
  qAxInstance = ::GetModuleHandleW (NULL);

  ::CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
  qAxInit ();
}

void
ComInteropHelper::registerObject (QObject * parent)
{
  QAxFactory::startServer();
  QAxFactory::registerActiveObject(new InteropObject (parent));
}
