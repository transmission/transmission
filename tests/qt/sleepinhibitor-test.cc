// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <functional>

#include <QCoreApplication>
#include <QObject>
#include <QTest>

#include "SleepInhibitor.h"

namespace
{
// Stands in for the OS so the tests never keep the test machine awake.
struct FakeOs
{
    int inhibit_count = 0;
    int release_count = 0;
    bool fail = false;

    [[nodiscard]] SleepInhibitor::Inhibit inhibit()
    {
        return [this]() -> std::function<void()>
        {
            ++inhibit_count;

            if (fail)
            {
                return {};
            }

            return [this]()
            {
                ++release_count;
            };
        };
    }
};
} // namespace

class SleepInhibitorTest : public QObject
{
    Q_OBJECT

private slots:
    static void inhibits_once_per_inhibited_period()
    {
        auto os = FakeOs{};
        auto inhibitor = SleepInhibitor{ os.inhibit() };

        inhibitor.setInhibited(true);
        inhibitor.setInhibited(true);
        inhibitor.setInhibited(true);

        QCOMPARE(os.inhibit_count, 1);
        QCOMPARE(os.release_count, 0);
    }

    static void releases_once_when_no_longer_inhibited()
    {
        auto os = FakeOs{};
        auto inhibitor = SleepInhibitor{ os.inhibit() };

        inhibitor.setInhibited(true);
        inhibitor.setInhibited(false);
        inhibitor.setInhibited(false);

        QCOMPARE(os.inhibit_count, 1);
        QCOMPARE(os.release_count, 1);
    }

    static void releases_on_destruction()
    {
        auto os = FakeOs{};

        {
            auto inhibitor = SleepInhibitor{ os.inhibit() };
            inhibitor.setInhibited(true);
        }

        QCOMPARE(os.release_count, 1);
    }

    static void does_nothing_if_never_inhibited()
    {
        auto os = FakeOs{};

        {
            auto inhibitor = SleepInhibitor{ os.inhibit() };
            inhibitor.setInhibited(false);
        }

        QCOMPARE(os.inhibit_count, 0);
        QCOMPARE(os.release_count, 0);
    }

    static void retries_after_failure_only_in_next_inhibited_period()
    {
        auto os = FakeOs{};
        os.fail = true;
        auto inhibitor = SleepInhibitor{ os.inhibit() };

        inhibitor.setInhibited(true);
        inhibitor.setInhibited(true);
        QCOMPARE(os.inhibit_count, 1);

        os.fail = false;
        inhibitor.setInhibited(true);
        QCOMPARE(os.inhibit_count, 1);

        inhibitor.setInhibited(false);
        QCOMPARE(os.release_count, 0);

        inhibitor.setInhibited(true);
        QCOMPARE(os.inhibit_count, 2);

        inhibitor.setInhibited(false);
        QCOMPARE(os.release_count, 1);
    }
};

int main(int argc, char** argv)
{
    auto const app = QCoreApplication{ argc, argv };
    auto test = SleepInhibitorTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "sleepinhibitor-test.moc"
