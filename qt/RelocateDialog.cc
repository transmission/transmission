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

bool RelocateDialog::move_flag_ = true;

void RelocateDialog::onSetLocation()
{
    session_.torrentSetLocation(ids_, newLocation(), move_flag_);
    close();
}

void RelocateDialog::onMoveToggled(bool b)
{
    move_flag_ = b;
}

RelocateDialog::RelocateDialog(Session& session, TorrentModel const& model, torrent_ids_t const& ids, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    ids_(ids)
{
    ui.setupUi(this);

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
        ui.newLocationStack->setCurrentWidget(ui.newLocationButton);
        ui.newLocationButton->setMode(PathButton::DirectoryMode);
        ui.newLocationButton->setTitle(tr("Select Location"));
        ui.newLocationButton->setPath(path);
    }
    else
    {
        ui.newLocationStack->setCurrentWidget(ui.newLocationEdit);
        ui.newLocationEdit->setText(path);
        ui.newLocationEdit->selectAll();
    }

    ui.newLocationStack->setFixedHeight(ui.newLocationStack->currentWidget()->sizeHint().height());
    ui.newLocationLabel->setBuddy(ui.newLocationStack->currentWidget());

    if (move_flag_)
    {
        ui.moveDataRadio->setChecked(true);
    }
    else
    {
        ui.findDataRadio->setChecked(true);
    }

    connect(ui.moveDataRadio, SIGNAL(toggled(bool)), this, SLOT(onMoveToggled(bool)));
    connect(ui.dialogButtons, SIGNAL(rejected()), this, SLOT(close()));
    connect(ui.dialogButtons, SIGNAL(accepted()), this, SLOT(onSetLocation()));
}

QString RelocateDialog::newLocation() const
{
    return ui.newLocationStack->currentWidget() == ui.newLocationButton ? ui.newLocationButton->path() :
        ui.newLocationEdit->text();
}
