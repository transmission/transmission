/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_ABOUT_DIALOG_H
#define QTR_ABOUT_DIALOG_H

#include <QDialog>

#include "ui_AboutDialog.h"

class AboutDialog: public QDialog
{
    Q_OBJECT

  public:
    AboutDialog (QWidget * parent = 0);
    ~AboutDialog () {}

  public slots:
    void showCredits ();

  private:
    QDialog * myLicenseDialog;
    Ui::AboutDialog ui;
};

#endif // QTR_ABOUT_DIALOG_H
