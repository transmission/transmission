/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <vector>

#include <QDir>
#include <QFile>
#include <QMap>
#include <QString>
#include <QTimer>

#include "AddData.h" // AddData
#include "BaseDialog.h"
#include "Macros.h"
#include "Torrent.h" // FileList
#include "ui_OptionsDialog.h"

class Prefs;
class Session;

extern "C"
{
    struct tr_variant;
}

class OptionsDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(OptionsDialog)

public:
    OptionsDialog(Session& session, Prefs const& prefs, AddData addme, QWidget* parent = nullptr);
    ~OptionsDialog() override;

private slots:
    void onAccepted();
    void onPriorityChanged(QSet<int> const& file_indices, int);
    void onWantedChanged(QSet<int> const& file_indices, bool);

    void onSourceChanged();
    void onDestinationChanged();

    void onSessionUpdated();

private:
    using mybins_t = QMap<uint32_t, int32_t>;

    void reload();
    void updateWidgetsLocality();
    void clearInfo();

    AddData add_;
    FileList files_;

    QDir local_destination_;
    QTimer edit_timer_;
    std::vector<bool> wanted_;
    std::vector<int> priorities_;
    Session& session_;
    Ui::OptionsDialog ui_ = {};
    tr_info info_ = {};
    bool have_info_ = {};
    bool is_local_ = {};
};
