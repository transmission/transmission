/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QToolButton>

class PathButton : public QToolButton
{
    Q_OBJECT

public:
    enum Mode
    {
        DirectoryMode,
        FileMode
    };

public:
    PathButton(QWidget* parent = nullptr);

    void setMode(Mode mode);
    void setTitle(QString const& title);
    void setNameFilter(QString const& nameFilter);

    void setPath(QString const& path);
    QString const& path() const;

    // QWidget
    QSize sizeHint() const override;

signals:
    void pathChanged(QString const& path);

protected:
    // QWidget
    void paintEvent(QPaintEvent* event) override;

private:
    void updateAppearance();

    bool isDirMode() const;
    QString effectiveTitle() const;

private slots:
    void onClicked();
    void onFileSelected(QString const& path);

private:
    Mode myMode;
    QString myTitle;
    QString myNameFilter;
    QString myPath;
};
