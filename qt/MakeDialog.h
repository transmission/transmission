// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>

#include <libtransmission/tr-macros.h>
#include <libtransmission/makemeta.h>

#include "BaseDialog.h"
#include "ui_MakeDialog.h"

class QAbstractButton;
class Session;

class MakeDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(MakeDialog)

public:
    MakeDialog(Session&, QWidget* parent = nullptr);

protected:
    // QWidget
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private slots:
    void onSourceChanged();
    void makeTorrent();
    void onPieceSizeUpdated(int);

private:
    QString getSource() const;
    void updatePiecesLabel();
    Session& session_;

    Ui::MakeDialog ui_ = {};

    std::optional<tr_metainfo_builder> builder_;
};
