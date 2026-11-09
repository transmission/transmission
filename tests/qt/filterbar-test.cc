// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QComboBox>
#include <QTest>

#include "FilterBar.h"
#include "Prefs.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "qt-test-fixtures.h"

class FilterBarTest
    : public QObject
    , BasicTest
{
    Q_OBJECT

private slots:
    static void activity_combo_keeps_selected_mode()
    {
        auto prefs = Prefs{};
        auto const torrents = TorrentModel{ prefs };
        auto const filter = TorrentFilter{ prefs };
        auto const bar = FilterBar{ prefs, torrents, filter };

        auto* const combo = bar.findChildren<QComboBox*>().front();
        auto const row = combo->findText(QStringLiteral("Active"));
        combo->setCurrentIndex(row);

        QCOMPARE(prefs.get<ShowMode>(TR_KEY_filter_mode), ShowMode::ShowActive);
        QCOMPARE(combo->currentIndex(), row);
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };
    auto test = FilterBarTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "filterbar-test.moc"
