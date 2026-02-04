// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

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

PathButton::PathButton(QWidget* parent)
    : QToolButton{ parent }
{
    setSizePolicy(QSizePolicy{ QSizePolicy::Preferred, QSizePolicy::Fixed });
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setText(tr("(None)")); // for minimum width

    update_appearance();

    connect(this, &QAbstractButton::clicked, this, &PathButton::on_clicked);
}

void PathButton::set_mode(Mode mode)
{
    if (mode_ == mode)
    {
        return;
    }

    mode_ = mode;

    update_appearance();
}

void PathButton::set_title(QString const& title)
{
    title_ = title;
}

void PathButton::set_name_filter(QString const& name_filter)
{
    name_filter_ = name_filter;
}

void PathButton::set_path(QString const& path)
{
    if (path_ == path)
    {
        return;
    }

    path_ = QDir::toNativeSeparators(Utils::remove_trailing_dir_separator(path));

    update_appearance();

    emit path_changed(path_);
}

QSize PathButton::sizeHint() const
{
    auto const sh = QToolButton::sizeHint();
    return { qMin(sh.width(), 150), sh.height() };
}

void PathButton::paintEvent(QPaintEvent* /*event*/)
{
    auto painter = QStylePainter{ this };
    QStyleOptionToolButton option;
    initStyleOption(&option);

    QSize const fake_content_size(100, 100);
    QSize const fake_size_hint = style()->sizeFromContents(QStyle::CT_ToolButton, &option, fake_content_size, this);

    int text_width = width() - (fake_size_hint.width() - fake_content_size.width()) - iconSize().width() - 6;

    if (popupMode() == MenuButtonPopup)
    {
        text_width -= style()->pixelMetric(QStyle::PM_MenuButtonIndicator, &option, this);
    }

    if (path_.isEmpty())
    {
        option.text = tr("(None)");
    }
    else if (auto const info = QFileInfo{ path_ }; !info.fileName().isEmpty())
    {
        option.text = info.fileName();
    }
    else
    {
        option.text = path_;
    }

    option.text = fontMetrics().elidedText(option.text, Qt::ElideMiddle, text_width);

    painter.drawComplexControl(QStyle::CC_ToolButton, option);
}

void PathButton::on_clicked() const
{
    auto* dialog = new QFileDialog{ window(), effective_title() };
    dialog->setFileMode(is_dir_mode() ? QFileDialog::Directory : QFileDialog::ExistingFile);

    if (is_dir_mode())
    {
        dialog->setOption(QFileDialog::ShowDirsOnly);
    }

    if (!name_filter_.isEmpty())
    {
        dialog->setNameFilter(name_filter_);
    }

    if (auto const path_info = QFileInfo{ path_ }; !path_.isEmpty() && path_info.exists())
    {
        if (path_info.isDir())
        {
            dialog->setDirectory(path_info.absoluteFilePath());
        }
        else
        {
            dialog->setDirectory(path_info.absolutePath());
            dialog->selectFile(path_info.fileName());
        }
    }

    connect(dialog, &QFileDialog::fileSelected, this, &PathButton::on_file_selected);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void PathButton::on_file_selected(QString const& path)
{
    if (!path.isEmpty())
    {
        set_path(path);
    }
}

void PathButton::update_appearance()
{
    QFileInfo const path_info{ path_ };

    int const icon_size(style()->pixelMetric(QStyle::PM_SmallIconSize));
    QFileIconProvider const icon_provider;

    QIcon icon;

    if (!path_.isEmpty() && path_info.exists())
    {
        icon = icon_provider.icon(QFileInfo{ path_ });
    }

    if (icon.isNull())
    {
        icon = icon_provider.icon(is_dir_mode() ? QFileIconProvider::Folder : QFileIconProvider::File);
    }

    setIconSize(QSize{ icon_size, icon_size });
    setIcon(icon);
    setToolTip(path_ == text() ? QString{} : path_);

    update();
}

bool PathButton::is_dir_mode() const
{
    return mode_ == DirectoryMode;
}

QString PathButton::effective_title() const
{
    if (!title_.isEmpty())
    {
        return title_;
    }

    return is_dir_mode() ? tr("Select Folder") : tr("Select File");
}
