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

#ifndef QTR_FREESPACE_LABEL_H
#define QTR_FREESPACE_LABEL_H

#include <QString>
#include <QTimer>

#include <QLabel>

class Session;

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
    void onSessionExecuted (int64_t tag, const QString& result, struct tr_variant * arguments);
    void onTimer ();
};

#endif // QTR_FREESPACE_LABEL_H

