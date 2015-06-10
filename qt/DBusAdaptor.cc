/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "AddData.h"
#include "Application.h"
#include "DBusAdaptor.h"

DBusAdaptor::DBusAdaptor (Application* app):
  QDBusAbstractAdaptor (app),
  myApp (app)
{
}

bool
DBusAdaptor::PresentWindow ()
{
  myApp->raise ();
  return true;
}

bool
DBusAdaptor::AddMetainfo (const QString& key)
{
  AddData addme (key);

  if (addme.type != addme.NONE)
    myApp->addTorrent (addme);

  return true;
}
