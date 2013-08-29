/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id:$
 */

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "formatter.h"
#include "freespace-label.h"
#include "session.h"

namespace
{
  static const int INTERVAL_MSEC = 15000;
}

FreespaceLabel :: FreespaceLabel (Session        & session,
                                  const QString  & path,
                                  QWidget        * parent):
  QLabel (parent),
  mySession (session),
  myTag (-1),
  myTimer (this)
{
  myTimer.setSingleShot (false);
  myTimer.setInterval (INTERVAL_MSEC);
  myTimer.start ();

  connect (&myTimer, SIGNAL(timeout()), this, SLOT(onTimer()));

  connect (&mySession, SIGNAL(executed(int64_t, const QString&, struct tr_variant*)),
           this,       SLOT(onSessionExecuted(int64_t, const QString&, struct tr_variant*)));

  setPath (path);
}

void
FreespaceLabel :: setPath (const QString& path)
{
  if (myPath != path)
    {
      setText (tr("<i>Calculating Free Space...</i>"));
      myPath = path;
      onTimer ();
    }
}

void
FreespaceLabel :: onTimer ()
{
  const int64_t tag = mySession.getUniqueTag ();
  const QByteArray myPathUtf8 = myPath.toUtf8 ();

  myTag = tag;
  tr_variant top;
  tr_variantInitDict (&top, 3);
  tr_variantDictAddStr (&top, TR_KEY_method, "free-space");
  tr_variantDictAddInt (&top, TR_KEY_tag, tag);
  tr_variant * args = tr_variantDictAddDict (&top, TR_KEY_arguments, 1);
  tr_variantDictAddStr (args, TR_KEY_path, myPathUtf8.constData());
  mySession.exec (&top);
  tr_variantFree (&top);
}

void
FreespaceLabel :: onSessionExecuted (int64_t tag, const QString& result, struct tr_variant * arguments)
{
  Q_UNUSED (result);

  if (tag != myTag)
    return;

  QString str;

  // update the label
  int64_t bytes = -1;
  tr_variantDictFindInt (arguments, TR_KEY_size_bytes, &bytes);
  if (bytes >= 0)
    setText (tr("%1 free").arg(Formatter::sizeToString (bytes)));
  else
    setText ("");

  // update the tooltip
  size_t len = 0;
  const char * path = 0;
  tr_variantDictFindStr (arguments, TR_KEY_path, &path, &len);
  str = QString::fromUtf8 (path, len);
  setToolTip (str);
}
