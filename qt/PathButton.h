/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QToolButton>

#include "Macros.h"

class PathButton : public QToolButton
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(PathButton)

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
    void setNameFilter(QString const& name_filter);

    void setPath(QString const& path);
    QString const& path() const;

    // QWidget
    QSize sizeHint() const override;

signals:
    void pathChanged(QString const& path);

protected:
    // QWidget
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onClicked();
    void onFileSelected(QString const& path);

private:
    void updateAppearance();

    bool isDirMode() const;
    QString effectiveTitle() const;

    QString name_filter_;
    QString path_;
    QString title_;
    Mode mode_;
};
