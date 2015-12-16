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
#include "InteropObject.h"

InteropObject::InteropObject (QObject * parent):
  QObject (parent)
{
}

bool
InteropObject::PresentWindow ()
{
  qApp->raise ();
  return true;
}

bool
InteropObject::AddMetainfo (const QString& metainfo)
{
  AddData addme (metainfo);

  if (addme.type != addme.NONE)
    qApp->addTorrent (addme);

  return true;
}
