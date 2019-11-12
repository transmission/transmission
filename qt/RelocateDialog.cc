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

bool RelocateDialog::myMoveFlag = true;

void RelocateDialog::onSetLocation()
{
    mySession.torrentSetLocation(myIds, newLocation(), myMoveFlag);
    close();
}

void RelocateDialog::onMoveToggled(bool b)
{
    myMoveFlag = b;
}

RelocateDialog::RelocateDialog(Session& session, TorrentModel const& model, torrent_ids_t const& ids, QWidget* parent) :
    BaseDialog(parent),
    mySession(session),
    myIds(ids)
{
    ui.setupUi(this);

    QString path;

    for (int const id : myIds)
    {
        Torrent const* tor = model.getTorrentFromId(id);

        if (path.isEmpty())
        {
            path = tor->getPath();
        }
        else if (path != tor->getPath())
        {
            if (mySession.isServer())
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

    if (mySession.isServer())
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

    if (myMoveFlag)
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
