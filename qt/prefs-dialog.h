/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef PREFS_DIALOG_H
#define PREFS_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QSet>

#include "prefs.h"
#include "ui_prefs-dialog.h"

class QAbstractButton;
class QCheckBox;
class QDoubleSpinBox;
class QHttp;
class QLabel;
class QLineEdit;
class QMessageBox;
class QPushButton;
class QSpinBox;
class QString;
class QTime;
class QTimeEdit;
class QVBoxLayout;
class QWidget;

class FreespaceLabel;
class Prefs;
class Session;

class PrefsDialog: public QDialog
{
    Q_OBJECT

  private slots:
    void checkBoxToggled (bool checked);
    void spinBoxEditingFinished ();
    void timeEditingFinished ();
    void lineEditingFinished ();
    void pathChanged (const QString& path);
    void refreshPref (int key);
    void encryptionEdited (int);
    void altSpeedDaysEdited (int);
    void sessionUpdated ();
    void onPortTested (bool);
    void onPortTest ();
    void onIdleLimitChanged ();
    void onQueueStalledMinutesChanged ();

    void onUpdateBlocklistClicked ();
    void onUpdateBlocklistCancelled ();
    void onBlocklistDialogDestroyed (QObject *);
    void onBlocklistUpdated (int n);

  private:
    bool updateWidgetValue (QWidget * widget, int prefKey);
    void linkWidgetToPref (QWidget * widget, int prefKey);
    void updateBlocklistLabel ();

  public:
    PrefsDialog (Session&, Prefs&, QWidget * parent = 0);
    ~PrefsDialog ();

  private:
    void setPref (int key, const QVariant& v);

    void initDownloadingTab ();
    void initSeedingTab ();
    void initSpeedTab ();
    void initPrivacyTab ();
    void initNetworkTab ();
    void initDesktopTab ();
    void initRemoteTab ();

  private:
    typedef QMap<int,QWidget*> key2widget_t;
    key2widget_t myWidgets;
    const bool myIsServer;
    Session& mySession;
    Prefs& myPrefs;
    QWidgetList myWebWidgets;
    QWidgetList myWebAuthWidgets;
    QWidgetList myWebWhitelistWidgets;
    QWidgetList myProxyWidgets;
    QWidgetList myProxyAuthWidgets;
    QWidgetList mySchedWidgets;
    QWidgetList myBlockWidgets;
    QWidgetList myUnsupportedWhenRemote;
    Ui::PrefsDialog ui;

    int myBlocklistHttpTag;
    QHttp * myBlocklistHttp;
    QMessageBox * myBlocklistDialog;
};

#endif
