/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FREESPACE_LABEL_H
#define QTR_FREESPACE_LABEL_H

#include <stdint.h>

#include <QString>
#include <QTimer>
#include <QLabel>

class Session;

extern "C"
{
  struct tr_variant;
}

class FreespaceLabel: public QLabel
{
    Q_OBJECT

  public:
    FreespaceLabel (Session&, const QString& path, QWidget *parent=0);
    virtual ~FreespaceLabel () {}
    void setPath (const QString& folder);

  private:
    Session& mySession;
    int64_t myTag;
    QString myPath;
    QTimer myTimer;

  private slots:
    void onSessionExecuted (int64_t tag, const QString& result, tr_variant * arguments);
    void onTimer ();
};

#endif // QTR_FREESPACE_LABEL_H

