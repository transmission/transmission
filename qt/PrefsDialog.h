/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_PREFS_DIALOG_H
#define QTR_PREFS_DIALOG_H

#include <QMap>
#include <QSet>

#include "BaseDialog.h"
#include "Prefs.h"

#include "ui_PrefsDialog.h"

class QHttp;
class QMessageBox;
class QString;

class Prefs;
class Session;

class PrefsDialog: public BaseDialog
{
    Q_OBJECT

  public:
    PrefsDialog (Session&, Prefs&, QWidget * parent = nullptr);
    virtual ~PrefsDialog ();

  private:
    typedef QMap<int, QWidget*> key2widget_t;

  private:
    bool updateWidgetValue (QWidget * widget, int prefKey);
    void linkWidgetToPref (QWidget * widget, int prefKey);
    void updateBlocklistLabel ();

    void setPref (int key, const QVariant& v);

    void initDownloadingTab ();
    void initSeedingTab ();
    void initSpeedTab ();
    void initPrivacyTab ();
    void initNetworkTab ();
    void initDesktopTab ();
    void initRemoteTab ();

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
    Session& mySession;
    Prefs& myPrefs;

    Ui::PrefsDialog ui;

    key2widget_t myWidgets;
    const bool myIsServer;
    QWidgetList myWebWidgets;
    QWidgetList myWebAuthWidgets;
    QWidgetList myWebWhitelistWidgets;
    QWidgetList myProxyWidgets;
    QWidgetList myProxyAuthWidgets;
    QWidgetList mySchedWidgets;
    QWidgetList myBlockWidgets;
    QWidgetList myUnsupportedWhenRemote;

    int myBlocklistHttpTag;
    QHttp * myBlocklistHttp;
    QMessageBox * myBlocklistDialog;
};

#endif // QTR_PREFS_DIALOG_H
