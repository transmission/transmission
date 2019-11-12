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
    Prefs& myPrefs;
    TorrentModel const& myTorrents;
    TorrentFilter const& myFilter;

    FilterBarComboBox* myActivityCombo;
    FilterBarComboBox* myTrackerCombo;
    QLabel* myCountLabel;
    QStandardItemModel* myTrackerModel;
    QTimer* myRecountTimer;
    bool myIsBootstrapping;
    QLineEdit* myLineEdit;
    std::map<QString, int> myTrackerCounts;
};
