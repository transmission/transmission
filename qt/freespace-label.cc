/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
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

FreespaceLabel::FreespaceLabel (QWidget * parent):
  QLabel (parent),
  mySession (nullptr),
  myTag (-1),
  myTimer (this)
{
  myTimer.setSingleShot (true);
  myTimer.setInterval (INTERVAL_MSEC);

  connect (&myTimer, SIGNAL (timeout ()), this, SLOT (onTimer ()));
}

void
FreespaceLabel::setSession (Session& session)
{
  if (mySession == &session)
    return;

  if (mySession != nullptr)
    disconnect (mySession, nullptr, this, nullptr);

  mySession = &session;

  connect (mySession, SIGNAL (executed (int64_t, QString, tr_variant *)),
           this,      SLOT (onSessionExecuted (int64_t, QString, tr_variant *)));

  onTimer ();
}

void
FreespaceLabel::setPath (const QString& path)
{
  if (myPath != path)
    {
      setText (tr("<i>Calculating Free Space...</i>"));
      myPath = path;
      onTimer ();
    }
}

void
FreespaceLabel::onTimer ()
{
  myTimer.stop ();

  if (mySession == nullptr || myPath.isEmpty ())
    return;

  tr_variant args;
  tr_variantInitDict (&args, 1);
  tr_variantDictAddStr (&args, TR_KEY_path, myPath.toUtf8 ().constData());

  myTag = mySession->getUniqueTag ();
  mySession->exec ("free-space", &args, myTag);
}

void
FreespaceLabel::onSessionExecuted (int64_t tag, const QString& result, tr_variant * arguments)
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

  myTimer.start ();
}
