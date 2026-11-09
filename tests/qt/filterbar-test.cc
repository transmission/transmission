// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QComboBox>
#include <QTemporaryDir>
#include <QTest>

#include "FilterBar.h"
#include "Prefs.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"

class FilterBarTest : public QObject
{
    Q_OBJECT

private slots:
    void activityComboKeepsSelectedMode();
};

void FilterBarTest::activityComboKeepsSelectedMode()
{
    auto dir = QTemporaryDir{};
    QVERIFY(dir.isValid());

    auto prefs = Prefs{ dir.path() };
    // Writable prefs need type registrations that Application makes, and no test builds an Application.
    prefs.disableSaveOnExit();
    auto const torrents = TorrentModel{ prefs };
    auto const filter = TorrentFilter{ prefs };
    auto const bar = FilterBar{ prefs, torrents, filter };

    auto* const combo = bar.findChildren<QComboBox*>().front();
    auto const row = combo->findText(QStringLiteral("Active"));
    combo->setCurrentIndex(row);

    QCOMPARE(prefs.get<ShowMode>(Prefs::FILTER_MODE), ShowMode::ShowActive);
    QCOMPARE(combo->currentIndex(), row);
}

QTEST_MAIN(FilterBarTest)

#include "filterbar-test.moc"
