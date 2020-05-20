/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QStylePainter>

#include "PathButton.h"
#include "Utils.h"

PathButton::PathButton(QWidget* parent) :
    QToolButton(parent),
    mode_(DirectoryMode),
    title_(),
    name_filter_(),
    path_()
{
    setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setText(tr("(None)")); // for minimum width

    updateAppearance();

    connect(this, SIGNAL(clicked()), this, SLOT(onClicked()));
}

void PathButton::setMode(Mode mode)
{
    if (mode_ == mode)
    {
        return;
    }

    mode_ = mode;

    updateAppearance();
}

void PathButton::setTitle(QString const& title)
{
    title_ = title;
}

void PathButton::setNameFilter(QString const& nameFilter)
{
    name_filter_ = nameFilter;
}

void PathButton::setPath(QString const& path)
{
    if (path_ == path)
    {
        return;
    }

    path_ = QDir::toNativeSeparators(Utils::removeTrailingDirSeparator(path));

    updateAppearance();

    emit pathChanged(path_);
}

QString const& PathButton::path() const
{
    return path_;
}

QSize PathButton::sizeHint() const
{
    QSize const sh(QToolButton::sizeHint());
    return QSize(qMin(sh.width(), 150), sh.height());
}

void PathButton::paintEvent(QPaintEvent* /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);

    QSize const fakeContentSize(qMax(100, qApp->globalStrut().width()), qMax(100, qApp->globalStrut().height()));
    QSize const fakeSizeHint = style()->sizeFromContents(QStyle::CT_ToolButton, &option, fakeContentSize, this);

    int textWidth = width() - (fakeSizeHint.width() - fakeContentSize.width()) - iconSize().width() - 6;

    if (popupMode() == MenuButtonPopup)
    {
        textWidth -= style()->pixelMetric(QStyle::PM_MenuButtonIndicator, &option, this);
    }

    QFileInfo const pathInfo(path_);
    option.text = path_.isEmpty() ? tr("(None)") : (pathInfo.fileName().isEmpty() ? path_ : pathInfo.fileName());
    option.text = fontMetrics().elidedText(option.text, Qt::ElideMiddle, textWidth);

    painter.drawComplexControl(QStyle::CC_ToolButton, option);
}

void PathButton::onClicked()
{
    auto* dialog = new QFileDialog(window(), effectiveTitle());
    dialog->setFileMode(isDirMode() ? QFileDialog::Directory : QFileDialog::ExistingFile);

    if (isDirMode())
    {
        dialog->setOption(QFileDialog::ShowDirsOnly);
    }

    if (!name_filter_.isEmpty())
    {
        dialog->setNameFilter(name_filter_);
    }

    QFileInfo const pathInfo(path_);

    if (!path_.isEmpty() && pathInfo.exists())
    {
        if (pathInfo.isDir())
        {
            dialog->setDirectory(pathInfo.absoluteFilePath());
        }
        else
        {
            dialog->setDirectory(pathInfo.absolutePath());
            dialog->selectFile(pathInfo.fileName());
        }
    }

    connect(dialog, SIGNAL(fileSelected(QString)), this, SLOT(onFileSelected(QString)));

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void PathButton::onFileSelected(QString const& path)
{
    if (!path.isEmpty())
    {
        setPath(path);
    }
}

void PathButton::updateAppearance()
{
    QFileInfo const pathInfo(path_);

    int const iconSize(style()->pixelMetric(QStyle::PM_SmallIconSize));
    QFileIconProvider const iconProvider;

    QIcon icon;

    if (!path_.isEmpty() && pathInfo.exists())
    {
        icon = iconProvider.icon(path_);
    }

    if (icon.isNull())
    {
        icon = iconProvider.icon(isDirMode() ? QFileIconProvider::Folder : QFileIconProvider::File);
    }

    setIconSize(QSize(iconSize, iconSize));
    setIcon(icon);
    setToolTip(path_ == text() ? QString() : path_);

    update();
}

bool PathButton::isDirMode() const
{
    return mode_ == DirectoryMode;
}

QString PathButton::effectiveTitle() const
{
    if (!title_.isEmpty())
    {
        return title_;
    }

    return isDirMode() ? tr("Select Folder") : tr("Select File");
}
