/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <unordered_map>

#include <QWidget>

class QLabel;
class QLineEdit;
class QStandardItemModel;
class QTimer;

class FilterBarComboBox;
class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterBar : public QWidget
{
    Q_OBJECT

public:
    FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent = nullptr);
    virtual ~FilterBar();

public slots:
    void clear();

private:
    FilterBarComboBox* createTrackerCombo(QStandardItemModel*);
    FilterBarComboBox* createActivityCombo();
    void refreshTrackers();

private slots:
    void recountSoon();
    void recount();
    void refreshPref(int key);
    void onActivityIndexChanged(int index);
    void onTrackerIndexChanged(int index);
    void onTextChanged(QString const&);

private:
    Prefs& prefs_;
    TorrentModel const& torrents_;
    TorrentFilter const& filter_;

    std::map<QString, int> tracker_counts_;
    FilterBarComboBox* activity_combo_ = {};
    FilterBarComboBox* tracker_combo_ = {};
    QLabel* count_label_ = {};
    QStandardItemModel* tracker_model_ = {};
    QTimer* recount_timer_ = {};
    QLineEdit* line_edit_ = {};
    bool is_bootstrapping_ = {};
};
