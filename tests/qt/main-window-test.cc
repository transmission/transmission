// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>

#include <QApplication>
#include <QTemporaryDir>
#include <QTest>

#include "MainWindow.h"
#include "Prefs.h"
#include "Session.h"
#include "TorrentModel.h"

// This client shows the window on someone else's behalf. A launch handed here
// asked for a window, and only a visible one answers it.
class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void presentsAWindowThatStartedInTheTray();
    void presentsAWindowThatIsAlreadyShowing();
    void placesTheActivationTokenWhereTheWaylandBackendLooks();

private:
    // Built per test so neither one inherits the other's window state.
    struct Client
    {
        explicit Client(QString const& config_dir)
            : prefs{ config_dir }
            , session{ config_dir, prefs }
            , model{ prefs }
            , window{ session, prefs, model, /*minimized=*/true }
        {
            // Nothing here is a session worth persisting. Writable prefs need type
            // registrations that Application makes, and no test builds an Application.
            prefs.disableSaveOnExit();
        }

        Prefs prefs;
        Session session;
        TorrentModel model;
        MainWindow window;
    };
};

void MainWindowTest::presentsAWindowThatStartedInTheTray()
{
    auto dir = QTemporaryDir{};
    QVERIFY(dir.isValid());

    auto client = std::make_unique<Client>(dir.path());

    // `minimized` starts the client with no window on screen at all.
    QVERIFY(!client->window.isVisible());

    client->window.presentWindow();

    // Alerting the taskbar entry of a window nobody can see is not presenting it.
    QVERIFY(client->window.isVisible());
    QVERIFY(!client->window.isMinimized());
}

void MainWindowTest::presentsAWindowThatIsAlreadyShowing()
{
    auto dir = QTemporaryDir{};
    QVERIFY(dir.isValid());

    auto client = std::make_unique<Client>(dir.path());
    client->window.presentWindow();
    QVERIFY(client->window.isVisible());

    // A second launch arriving at a client already on screen leaves it on screen.
    client->window.presentWindow();
    QVERIFY(client->window.isVisible());
    QVERIFY(!client->window.isMinimized());
}

// The Wayland backend takes the token from this environment variable during
// activateWindow() and clears it. The offscreen platform consumes nothing, which is
// what lets this test observe the value in place.
void MainWindowTest::placesTheActivationTokenWhereTheWaylandBackendLooks()
{
    auto dir = QTemporaryDir{};
    QVERIFY(dir.isValid());

    auto client = std::make_unique<Client>(dir.path());

    qunsetenv("XDG_ACTIVATION_TOKEN");
    client->window.presentWindow(QStringLiteral("launch-token"));
    QCOMPARE(qgetenv("XDG_ACTIVATION_TOKEN"), QByteArray{ "launch-token" });

    // A token is single-use. presentWindow() must not plant one it was not given,
    // where the next activation would spend it a second time.
    qunsetenv("XDG_ACTIVATION_TOKEN");
    client->window.presentWindow();
    QCOMPARE(qgetenv("XDG_ACTIVATION_TOKEN"), QByteArray{});
}

QTEST_MAIN(MainWindowTest)

#include "main-window-test.moc"
