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
#include <QSet>
#include "prefs.h"

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
    void refreshPref (int key);
    void encryptionEdited (int);
    void altSpeedDaysEdited (int);
    void sessionUpdated ();
    void onWatchClicked ();
    void onScriptClicked ();
    void onIncompleteClicked ();
    void onDestinationClicked ();
    void onLocationSelected (const QString&, int key);
    void onPortTested (bool);
    void onPortTest ();
    void onIdleLimitChanged ();

    void onUpdateBlocklistClicked ();
    void onUpdateBlocklistCancelled ();
    void onBlocklistDialogDestroyed (QObject *);
    void onBlocklistUpdated (int n);

  private:
    QDoubleSpinBox * doubleSpinBoxNew (int key, double low, double high, double step, int decimals);
    QCheckBox * checkBoxNew (const QString& text, int key);
    QSpinBox * spinBoxNew (int key, int low, int high, int step);
    QTimeEdit * timeEditNew (int key);
    QLineEdit * lineEditNew (int key, int mode = 0);
    void enableBuddyWhenChecked (QCheckBox *, QWidget *);
    void updateBlocklistLabel ();

  public:
    PrefsDialog (Session&, Prefs&, QWidget * parent = 0);
    ~PrefsDialog ();

  private:
    void setPref (int key, const QVariant& v);
    bool isAllowed (int key) const;
    QWidget * createDownloadingTab ();
    QWidget * createSeedingTab ();
    QWidget * createSpeedTab ();
    QWidget * createPrivacyTab ();
    QWidget * createNetworkTab ();
    QWidget * createDesktopTab ();
    QWidget * createRemoteTab (Session&);

  private:
    typedef QMap<int,QWidget*> key2widget_t;
    key2widget_t myWidgets;
    const bool myIsServer;
    Session& mySession;
    Prefs& myPrefs;
    QVBoxLayout * myLayout;
    QLabel * myPortLabel;
    QPushButton * myPortButton;
    QPushButton * myWatchButton;
    QPushButton * myTorrentDoneScriptButton;
    QCheckBox * myTorrentDoneScriptCheckbox;
    QCheckBox * myIncompleteCheckbox;
    QPushButton * myIncompleteButton;
    QPushButton * myDestinationButton;
    QWidgetList myWebWidgets;
    QWidgetList myWebAuthWidgets;
    QWidgetList myWebWhitelistWidgets;
    QWidgetList myProxyWidgets;
    QWidgetList myProxyAuthWidgets;
    QWidgetList mySchedWidgets;
    QWidgetList myBlockWidgets;
    QWidgetList myUnsupportedWhenRemote;
    FreespaceLabel * myFreespaceLabel;
    QSpinBox * myIdleLimitSpin;

    int myBlocklistHttpTag;
    QHttp * myBlocklistHttp;
    QMessageBox * myBlocklistDialog;
    QLabel * myBlocklistLabel;
};

#endif
