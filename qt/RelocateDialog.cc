/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QDir>

#include "RelocateDialog.h"
#include "Session.h"
#include "Torrent.h"
#include "TorrentModel.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool RelocateDialog::move_flag = true;

void RelocateDialog::onSetLocation()
{
    session_.torrentSetLocation(ids_, newLocation(), move_flag);
    close();
}

void RelocateDialog::onMoveToggled(bool b) const
{
    move_flag = b;
}

RelocateDialog::RelocateDialog(Session& session, TorrentModel const& model, torrent_ids_t ids, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    ids_(std::move(ids))
{
    ui_.setupUi(this);

    QString path;

    for (int const id : ids_)
    {
        Torrent const* tor = model.getTorrentFromId(id);

        if (path.isEmpty())
        {
            path = tor->getPath();
        }
        else if (path != tor->getPath())
        {
            if (session_.isServer())
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

    if (session_.isServer())
    {
        ui_.newLocationStack->setCurrentWidget(ui_.newLocationButton);
        ui_.newLocationButton->setMode(PathButton::DirectoryMode);
        ui_.newLocationButton->setTitle(tr("Select Location"));
        ui_.newLocationButton->setPath(path);
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

    connect(ui_.moveDataRadio, &QAbstractButton::toggled, this, &RelocateDialog::onMoveToggled);
    connect(ui_.dialogButtons, &QDialogButtonBox::rejected, this, &RelocateDialog::close);
    connect(ui_.dialogButtons, &QDialogButtonBox::accepted, this, &RelocateDialog::onSetLocation);
}

QString RelocateDialog::newLocation() const
{
    return ui_.newLocationStack->currentWidget() == ui_.newLocationButton ? ui_.newLocationButton->path() :
        ui_.newLocationEdit->text();
}
