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

#include <QPointer>

#include "BaseDialog.h"

#include "ui_AboutDialog.h"

class LicenseDialog;

class AboutDialog: public BaseDialog
{
    Q_OBJECT

  public:
    AboutDialog (QWidget * parent = nullptr);
    virtual ~AboutDialog () {}

  private slots:
    void showCredits ();
    void showLicense ();

  private:
    Ui::AboutDialog ui;

    QPointer<LicenseDialog> myLicenseDialog;
};

#endif // QTR_ABOUT_DIALOG_H
