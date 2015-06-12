/*
 * This file Copyright (C) 2013-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FREE_SPACE_LABEL_H
#define QTR_FREE_SPACE_LABEL_H

#include <cstdint>

#include <QLabel>
#include <QString>
#include <QTimer>

class Session;

extern "C"
{
  struct tr_variant;
}

class FreeSpaceLabel: public QLabel
{
    Q_OBJECT

  public:
    FreeSpaceLabel (QWidget * parent = nullptr);
    virtual ~FreeSpaceLabel () {}

    void setSession (Session& session);
    void setPath (const QString& folder);

  private slots:
    void onSessionExecuted (int64_t tag, const QString& result, tr_variant * arguments);
    void onTimer ();

  private:
    Session * mySession;
    int64_t myTag;
    QString myPath;
    QTimer myTimer;
};

#endif // QTR_FREE_SPACE_LABEL_H
