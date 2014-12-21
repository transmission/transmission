/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "add-data.h"
#include "app.h"
#include "dbus-adaptor.h"

TrDBusAdaptor::TrDBusAdaptor (MyApp* app):
  QDBusAbstractAdaptor (app),
  myApp (app)
{
}

bool
TrDBusAdaptor::PresentWindow ()
{
  myApp->raise ();
  return true;
}

bool
TrDBusAdaptor::AddMetainfo (const QString& key)
{
  AddData addme (key);

  if (addme.type != addme.NONE)
    myApp->addTorrent (addme);

  return true;
}
