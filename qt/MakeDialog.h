/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_MAKE_DIALOG_H
#define QTR_MAKE_DIALOG_H

#include <memory>

#include <QDialog>

#include "ui_MakeDialog.h"

class QAbstractButton;

class Session;

extern "C"
{
  struct tr_metainfo_builder;
}

class MakeDialog: public QDialog
{
    Q_OBJECT

  private slots:
    void onSourceChanged ();
    void makeTorrent ();

  private:
    QString getSource () const;

  private:
    Session& mySession;
    Ui::MakeDialog ui;
    std::unique_ptr<tr_metainfo_builder, void(*)(tr_metainfo_builder*)> myBuilder;

  protected:
    virtual void dragEnterEvent (QDragEnterEvent *);
    virtual void dropEvent (QDropEvent *);

  public:
    MakeDialog (Session&, QWidget * parent = 0);
    virtual ~MakeDialog ();
};

#endif // QTR_MAKE_DIALOG_H
