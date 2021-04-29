/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include "BaseDialog.h"
#include "Macros.h"
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
