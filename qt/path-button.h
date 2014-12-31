/*
 * This file Copyright (C) 2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_PATH_BUTTON_H
#define QTR_PATH_BUTTON_H

#include <QToolButton>

class TrPathButton: public QToolButton
{
    Q_OBJECT

  public:
    enum Mode
    {
      DirectoryMode,
      FileMode
    };

  public:
    TrPathButton (QWidget * parent = nullptr);

    void setMode (Mode mode);
    void setTitle (const QString& title);
    void setNameFilter (const QString& nameFilter);

    void setPath (const QString& path);
    const QString& path () const;

  signals:
    void pathChanged (const QString& path);

  private slots:
    void onClicked ();
    void onFileSelected (const QString& path);

  private:
    void updateAppearance ();

    bool isDirMode () const;
    QString effectiveTitle () const;

  private:
    Mode myMode;
    QString myTitle;
    QString myNameFilter;
    QString myPath;
};

#endif // QTR_PATH_BUTTON_H
