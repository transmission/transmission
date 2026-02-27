// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <utility>

#include <QDir>

#include "RelocateDialog.h"
#include "Session.h"
#include "Torrent.h"
#include "TorrentModel.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool RelocateDialog::move_flag = true;

void RelocateDialog::on_set_location()
{
    session_.torrent_set_location(ids_, new_location(), move_flag);
    close();
}

void RelocateDialog::on_move_toggled(bool b) const
{
    move_flag = b;
}

RelocateDialog::RelocateDialog(Session& session, TorrentModel const& model, torrent_ids_t ids, QWidget* parent)
    : BaseDialog{ parent }
    , session_{ session }
    , ids_{ std::move(ids) }
{
    ui_.setupUi(this);

    QString path;

    for (int const id : ids_)
    {
        Torrent const* tor = model.get_torrent_from_id(id);

        if (path.isEmpty())
        {
            path = tor->get_path();
        }
        else if (path != tor->get_path())
        {
            if (session_.is_local())
            {
                path = QDir::homePath();
            }
            else
            {
                path = QDir::rootPath();
            }

            break;
        }
    }

    if (session_.is_local())
    {
        ui_.newLocationStack->setCurrentWidget(ui_.newLocationButton);
        ui_.newLocationButton->set_mode(PathButton::DirectoryMode);
        ui_.newLocationButton->set_title(tr("Select Location"));
        ui_.newLocationButton->set_path(path);
    }
    else
    {
        ui_.newLocationStack->setCurrentWidget(ui_.newLocationEdit);
        ui_.newLocationEdit->setText(path);
        ui_.newLocationEdit->selectAll();
    }

    ui_.newLocationStack->setFixedHeight(ui_.newLocationStack->currentWidget()->sizeHint().height());
    ui_.newLocationLabel->setBuddy(ui_.newLocationStack->currentWidget());

    if (move_flag)
    {
        ui_.moveDataRadio->setChecked(true);
    }
    else
    {
        ui_.findDataRadio->setChecked(true);
    }

    connect(ui_.moveDataRadio, &QAbstractButton::toggled, this, &RelocateDialog::on_move_toggled);
    connect(ui_.dialogButtons, &QDialogButtonBox::rejected, this, &RelocateDialog::close);
    connect(ui_.dialogButtons, &QDialogButtonBox::accepted, this, &RelocateDialog::on_set_location);
}

QString RelocateDialog::new_location() const
{
    return ui_.newLocationStack->currentWidget() == ui_.newLocationButton ? ui_.newLocationButton->path() :
                                                                            ui_.newLocationEdit->text();
}
