// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "RenameTorrentDialog.h"
#include "Session.h"
#include "Torrent.h"
#include "TorrentModel.h"

RenameTorrentDialog::RenameTorrentDialog(Session& session, TorrentModel const& model, torrent_ids_t ids, QWidget* parent)
    : BaseDialog{ parent }
    , session_{ session }
{
    if (ids.size() == 1)
    {
        auto const id = *ids.begin();
        if (auto const* const tor = model.getTorrentFromId(id); tor != nullptr && tor->hasMetadata() && tor->hasName())
        {
            torrent_id_ = id;
            old_name_ = tor->name();
        }
    }

    setWindowTitle(tr("Rename Torrent"));

    if (torrent_id_ > 0)
    {
        auto* const layout = new QVBoxLayout{ this };
        auto* const label = new QLabel{ tr("Rename \"%1\":").arg(old_name_), this };
        line_edit_ = new QLineEdit{ old_name_, this };
        button_box_ = new QDialogButtonBox{ QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this };

        label->setBuddy(line_edit_);

        layout->addWidget(label);
        layout->addWidget(line_edit_);
        layout->addWidget(button_box_);

        connect(button_box_, &QDialogButtonBox::accepted, this, &RenameTorrentDialog::onAccepted);
        connect(button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(line_edit_, &QLineEdit::textChanged, this, [this]() { updateButtons(); });

        QTimer::singleShot(
            0,
            this,
            [this]()
            {
                // Keep width resizable for long names, but lock vertical resize
                adjustSize();
                int const h = sizeHint().height();
                setMinimumHeight(h);
                setMaximumHeight(h);

                selectBaseName();
            });

        updateButtons();
    }
    else
    {
        QTimer::singleShot(0, this, &QDialog::reject);
    }
}

QString RenameTorrentDialog::newName() const
{
    return line_edit_->text();
}

void RenameTorrentDialog::onAccepted()
{
    auto const new_name = newName();
    if (torrent_id_ > 0 && !new_name.isEmpty() && new_name != old_name_)
    {
        session_.torrentRenamePath({ torrent_id_ }, old_name_, new_name);
    }

    accept();
}

void RenameTorrentDialog::updateButtons() const
{
    auto* const ok_button = button_box_->button(QDialogButtonBox::Ok);
    if (ok_button == nullptr)
    {
        return;
    }

    auto const name = newName();
    ok_button->setEnabled(torrent_id_ > 0 && !name.isEmpty() && name != old_name_);
}

void RenameTorrentDialog::selectBaseName() const
{
    int const dot = old_name_.lastIndexOf(QLatin1Char('.'));
    int const base_name_len = dot > 0 ? dot : old_name_.size();

    line_edit_->setFocus();
    if (base_name_len > 0)
    {
        line_edit_->setSelection(0, base_name_len);
    }
    else
    {
        line_edit_->selectAll();
    }
}
