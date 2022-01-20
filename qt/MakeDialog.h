// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include <libtransmission/tr-macros.h>

#include "BaseDialog.h"
#include "ui_MakeDialog.h"

class QAbstractButton;

class Session;

extern "C"
{
    struct tr_metainfo_builder;
}

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

private:
    QString getSource() const;

    Session& session_;

    Ui::MakeDialog ui_ = {};

    std::unique_ptr<tr_metainfo_builder, void (*)(tr_metainfo_builder*)> builder_;
};
