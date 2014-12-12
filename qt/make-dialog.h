/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef MAKE_DIALOG_H
#define MAKE_DIALOG_H

#include <QDialog>
#include <QTimer>

class QAbstractButton;
class QPlainTextEdit;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class Session;
class QProgressBar;
class QDialogButtonBox;

extern "C"
{
  struct tr_metainfo_builder;
}

class MakeDialog: public QDialog
{
    Q_OBJECT

  private slots:
    void onSourceChanged ();
    void onButtonBoxClicked (QAbstractButton*);
    void onNewButtonBoxClicked (QAbstractButton*);
    void onNewDialogDestroyed (QObject*);
    void onProgress ();

    void onFolderClicked ();
    void onFolderSelected (const QString&);
    void onFolderSelected (const QStringList&);

    void onFileClicked ();
    void onFileSelected (const QString&);
    void onFileSelected (const QStringList&);

    void onDestinationClicked ();
    void onDestinationSelected (const QString&);
    void onDestinationSelected (const QStringList&);

  private:
    void makeTorrent ();
    QString getSource () const;
    void enableBuddyWhenChecked (QCheckBox *, QWidget *);
    void enableBuddyWhenChecked (QRadioButton *, QWidget *);

  private:
    Session& mySession;
    QString myDestination;
    QString myTarget;
    QString myFile;
    QString myFolder;
    QTimer myTimer;
    QRadioButton * myFolderRadio;
    QRadioButton * myFileRadio;
    QPushButton * myDestinationButton;
    QPushButton * myFileButton;
    QPushButton * myFolderButton;
    QPlainTextEdit * myTrackerEdit;
    QCheckBox * myCommentCheck;
    QLineEdit * myCommentEdit;
    QCheckBox * myPrivateCheck;
    QLabel * mySourceLabel;
    QDialogButtonBox * myButtonBox;
    QProgressBar * myNewProgress;
    QLabel * myNewLabel;
    QDialogButtonBox * myNewButtonBox;
    QDialog * myNewDialog;
    tr_metainfo_builder * myBuilder;

  protected:
    virtual void dragEnterEvent (QDragEnterEvent *);
    virtual void dropEvent (QDropEvent *);

  public:
    MakeDialog (Session&, QWidget * parent = 0);
    ~MakeDialog ();
};

#endif
