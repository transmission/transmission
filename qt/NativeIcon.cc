// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "NativeIcon.h"

#include <bitset>
#include <optional>
#include <string_view>

#include <QFontDatabase>
#include <QOperatingSystemVersion>
#include <QStyle>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtGui/QPalette>
#include <QtGui/QPixmap>

#include <small/set.hpp>

#if defined(Q_OS_MAC)
extern QPixmap loadSFSymbol(QString symbol_name, int pixel_size);
#endif

namespace icons
{
namespace
{

// https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
auto const Win10IconFamily = QStringLiteral("Segoe MDL2 Assets");

// https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
auto const Win11IconFamily = QStringLiteral("Segoe Fluent Icons");

// Define these two macros to force a specific icon icon during development.
// Their EULA doesn't allow redistribution but does allow using them
// during design/develop/testing.
// 1. Snag the ttf you want to use (Win 10 uses https://aka.ms/SegoeFonts,
//    Win 11 uses https://aka.ms/SegoeFluentIcons).
// 2. Add it to application.qrc
// 3. Set these two macros accordingly
// #define DEV_FORCE_FONT_FAMILY Win11IconFamily
// #define DEV_FORCE_FONT_RESOURCE QStringLiteral(":devonly/segoe_fluent_icons.ttf")

QString getWindowsFontFamily()
{
#ifdef DEV_FORCE_FONT_FAMILY
    return DEV_FORCE_FONT_FAMILY;
#else
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 11))
        return Win11IconFamily;

    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10))
        return Win10IconFamily;

    return {};
#endif
}

void ensureFontsLoaded()
{
#ifdef DEV_FORCE_FONT_RESOURCE
    [[maybe_unused]] static auto const font_id = QFontDatabase::addApplicationFont(DEV_FORCE_FONT_RESOURCE);
#endif
}

QPixmap makeIconFromCodepoint(QString const family, QChar const codepoint, int const point_size)
{
    auto const font = QFont{ family, point_size - 8 };
    if (!QFontMetrics{ font }.inFont(codepoint))
        return {};

    // FIXME: HDPI, pixel size vs point size?
    auto const rect = QRect{ 0, 0, point_size, point_size };
    auto pixmap = QPixmap{ rect.size() };
    pixmap.fill(Qt::transparent);
    auto painter = QPainter{ &pixmap };
    painter.setFont(font);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(qApp->palette().color(QPalette::ButtonText));
    painter.setRenderHint(QPainter::TextAntialiasing);
    auto br = QRect{};
    painter.drawText(rect, Qt::AlignCenter, QString{ codepoint }, &br);
    painter.end();

    return pixmap;
}

using MenuMode = std::bitset<3U>;
constexpr auto Standard = MenuMode{ 1 << 0U };
constexpr auto Noun = MenuMode{ 1 << 1U };
constexpr auto Verb = MenuMode{ 1 << 2U };

struct Info
{
    std::string_view sf_symbol_name;
    char16_t segoe_codepoint;
    std::string_view xdg_icon_name;
    MenuMode mode;
};

/**
 * # Choosing Icons
 *
 * ## Windows
 *
 * https://learn.microsoft.com/en-us/windows/apps/design/controls/menus
 * Consider providing menu item icons for:
 * - The most commonly used items.
 * - Menu items whose icon is standard or well known.
 * - Menu items whose icon well illustrates what the command does.
 * Don't feel obligated to provide icons for commands that don't have
 * a standard visualization. Cryptic icons aren't helpful, create visual
 * clutter, and prevent users from focusing on the important menu items.
 *
 * ## macOS
 *
 * https://developer.apple.com/design/human-interface-guidelines/menus
 * Represent menu item actions with familiar icons. Icons help people
 * recognize common actions throughout your app. Use the same icons as
 * the system to represent actions such as Copy, Share, and Delete,
 * wherever they appear. For a list of icons that represent common
 * actions, see Standard icons.
 * Don’t display an icon if you can’t find one that clearly represents
 * the menu item. Not all menu items need an icon. Be careful when adding
 * icons for custom menu items to avoid confusion with other existing
 * actions, and don’t add icons just for the sake of ornamentation.
 *
 * ## GNOME
 *
 * https://discourse.gnome.org/t/principle-of-icons-in-menus/4803
 * We do not “block” icons in menus; icons are generally reserved
 * for “nouns”, or “objects”—for instance: website favicons in
 * bookmark menus, or file-type icons—instead of having them for
 * “verbs”, or “actions”—for instance: save, copy, print, etc.
 *
 * ## KDE
 *
 * https://develop.kde.org/hig/icons/#icons-for-menu-items-and-buttons-with-text
 * Set an icon on every button and menu item, making sure not to
 * use the same icon for multiple visible buttons or menu items.
 * Choose different icons, or use more specific ones to disambiguate.
 */
[[nodiscard]] constexpr Info getInfo(Type const type)
{
    auto sf_symbol_name = std::string_view{};
    auto xdg_icon_name = std::string_view{};
    auto segoe_codepoint = char16_t{};
    auto mode = MenuMode{};

    switch (type)
    {
    case Type::AddTracker:
        sf_symbol_name = "plus";
        segoe_codepoint = 0xE710U; // Add
        xdg_icon_name = "list-add";
        break;

    case Type::EditTrackers:
        sf_symbol_name = "pencil";
        segoe_codepoint = 0xE70FU; // Edit
        xdg_icon_name = "document-edit";
        break;

    case Type::RemoveTracker:
        sf_symbol_name = "minus";
        segoe_codepoint = 0xE738U; // Remove
        xdg_icon_name = "list-remove";
        break;

    case Type::AddTorrentFromFile:
        sf_symbol_name = "folder";
        segoe_codepoint = 0xE8E5U; // OpenFile
        xdg_icon_name = "document-open";
        mode = Standard | Verb;
        break;

    case Type::AddTorrentFromURL:
        sf_symbol_name = "network";
        segoe_codepoint = 0xE774U; // Globe
        xdg_icon_name = "network-workgroup";
        mode = Standard | Verb;
        break;

    case Type::CreateNewTorrent:
        sf_symbol_name = "plus";
        segoe_codepoint = 0xE710U; // Add
        xdg_icon_name = "document-new";
        mode = Standard | Verb;
        break;

    case Type::OpenTorrentDetails:
        sf_symbol_name = "doc.text.magnifyingglass";
        segoe_codepoint = 0xE946U; // Info
        xdg_icon_name = "document-properties";
        mode = Standard | Verb;
        break;

    case Type::OpenTorrentLocalFolder:
        sf_symbol_name = "folder";
        segoe_codepoint = 0xED25U; // OpenFolderHorizontal
        xdg_icon_name = "folder-open";
        mode = Standard | Verb;
        break;

    case Type::StartTorrent:
        sf_symbol_name = "play";
        segoe_codepoint = 0xE768U; // Play
        xdg_icon_name = "media-playback-start";
        mode = Standard | Verb;
        break;

    case Type::StartTorrentNow:
        sf_symbol_name = "forward";
        segoe_codepoint = 0xEB9DU; // FastForward
        xdg_icon_name = "media-seek-forward";
        mode = Standard | Verb;
        break;

    case Type::RemoveTorrent:
        sf_symbol_name = "minus";
        segoe_codepoint = 0xE738U; // Remove
        xdg_icon_name = "list-remove";
        mode = Verb;
        break;

    case Type::RemoveTorrentAndDeleteData:
        sf_symbol_name = "trash";
        segoe_codepoint = 0xE74DU; // Delete
        xdg_icon_name = "edit-delete";
        mode = Verb;
        break;

    case Type::SetTorrentLocation:
        sf_symbol_name = "arrow.up.and.down.and.arrow.left.and.right";
        segoe_codepoint = 0xE7C2U; // Move
        xdg_icon_name = "edit-find";
        mode = Standard | Verb;
        break;

    case Type::CopyMagnetLinkToClipboard:
        sf_symbol_name = "clipboard";
        segoe_codepoint = 0xE8C8U; // Copy
        xdg_icon_name = "edit-copy";
        mode = Standard | Verb;
        break;

    case Type::SelectAll:
        sf_symbol_name = "checkmark.square";
        segoe_codepoint = 0xE8B3U; // SelectAll
        xdg_icon_name = "edit-select-all";
        mode = Verb;
        break;

    case Type::DeselectAll:
        sf_symbol_name = "square";
        segoe_codepoint = 0xE739U; // Checkbox
        xdg_icon_name = "edit-select-none";
        mode = Verb;
        break;

    case Type::Statistics:
        sf_symbol_name = "chart.bar";
        segoe_codepoint = 0xED5EU; // Ruler
        xdg_icon_name = "info";
        mode = Noun;
        break;

    case Type::Donate:
        sf_symbol_name = "heart";
        segoe_codepoint = 0xEB51U; // Heart
        xdg_icon_name = "donate";
        mode = Standard | Verb;
        break;

    case Type::Settings:
        sf_symbol_name = "gearshape";
        segoe_codepoint = 0xE713U; // Settings
        xdg_icon_name = "preferences-system";
        mode = Standard | Noun;
        break;

    case Type::QuitApp:
        sf_symbol_name = "power";
        segoe_codepoint = 0xE7E8U; // PowerButton
        xdg_icon_name = "application-exit";
        mode = Standard | Verb;
        break;

    case Type::About:
        sf_symbol_name = "info.circle";
        segoe_codepoint = 0xE946U; // Info
        xdg_icon_name = "help-about";
        mode = Standard | Noun;
        break;

    case Type::Help:
        sf_symbol_name = "questionmark.circle";
        segoe_codepoint = 0xE897U; // Help
        xdg_icon_name = "help-faq";
        mode = Standard | Noun;
        break;

    case Type::QueueMoveTop:
        sf_symbol_name = "arrow.up.to.line";
        segoe_codepoint = 0xEDDBU; // CaretUpSolid8
        xdg_icon_name = "go-top";
        mode = Verb;
        break;

    case Type::QueueMoveUp:
        sf_symbol_name = "arrow.up";
        segoe_codepoint = 0xEDD7U; // CaretUp8
        xdg_icon_name = "go-up";
        mode = Verb;
        break;

    case Type::QueueMoveDown:
        sf_symbol_name = "arrow.down";
        segoe_codepoint = 0xEDD8U; // CaretDown8
        xdg_icon_name = "go-down";
        mode = Verb;
        break;

    case Type::QueueMoveBottom:
        sf_symbol_name = "arrow.down.to.line";
        segoe_codepoint = 0xEDDCU; // CaretDownSolid8
        xdg_icon_name = "go-bottom";
        mode = Verb;
        break;

    case Type::NetworkIdle:
        xdg_icon_name = "network-idle";
        break;

    case Type::NetworkReceive:
        sf_symbol_name = "arrow.down.circle";
        segoe_codepoint = 0xE896U; // Download
        xdg_icon_name = "network-receive";
        break;

    case Type::NetworkTransmit:
        sf_symbol_name = "arrow.up.circle";
        segoe_codepoint = 0xE898U; // Upload
        xdg_icon_name = "network-transmit";
        break;

    case Type::NetworkTransmitReceive:
        sf_symbol_name = "arrow.up.arrow.down.circle";
        segoe_codepoint = 0xE174U; // UploadDownload
        xdg_icon_name = "network-transmit-receive";
        break;

    case Type::NetworkError:
        sf_symbol_name = "wifi.exclamationmark";
        segoe_codepoint = 0xE783U; // Error
        xdg_icon_name = "network-error";
        break;

    case Type::TorrentStateActive:
        sf_symbol_name = "play";
        segoe_codepoint = 0xE768U; // Play
        xdg_icon_name = "media-playback-start";
        break;

    case Type::TorrentStateSeeding:
        sf_symbol_name = "chevron.up";
        segoe_codepoint = 0xE70EU; // ChevronUp
        xdg_icon_name = "go-up";
        break;

    case Type::TorrentStateDownloading:
        sf_symbol_name = "chevron.down";
        segoe_codepoint = 0xE70DU; // ChevronDown
        xdg_icon_name = "go-down";
        break;

    case Type::PauseTorrent:
        mode = Standard | Verb;
        [[fallthrough]];

    case Type::TorrentStatePaused:
        sf_symbol_name = "pause";
        segoe_codepoint = 0xE769U; // Pause
        xdg_icon_name = "media-playback-pause";
        break;

    case Type::VerifyTorrent:
        mode = Standard | Verb;
        [[fallthrough]];

    case Type::TorrentStateVerifying:
        sf_symbol_name = "arrow.clockwise";
        segoe_codepoint = 0xE72CU; // Refresh
        xdg_icon_name = "view-refresh";
        break;

    case Type::TorrentErrorEmblem:
        [[fallthrough]];

    case Type::TorrentStateError:
        sf_symbol_name = "xmark.circle";
        segoe_codepoint = 0xEB90U; // StatusErrorFull
        xdg_icon_name = "dialog-error";
        break;
    }

    return { sf_symbol_name, segoe_codepoint, xdg_icon_name, mode };
}

[[nodiscard]] MenuMode get_menu_mode()
{
    static auto value = std::optional<MenuMode>{};

    if (!value)
    {
        if (auto const env = qgetenv("TR_ICON_MODE").toLower(); !env.isEmpty())
        {
            auto mode = MenuMode{};
            if (env.contains("all"))
                mode.set();
            if (env.contains("noun"))
                mode |= Noun;
            if (env.contains("standard"))
                mode |= Standard;
            if (env.contains("verb"))
                mode |= Verb;
            value = mode;
        }
    }

#if defined(Q_OS_WIN)

    if (!value) {
        // https://learn.microsoft.com/en-us/windows/apps/design/controls/menus
        // Consider providing menu item icons for:
        // The most commonly used items.
        // Menu items whose icon is standard or well known.
        // Menu items whose icon well illustrates what the command does.
        // Don't feel obligated to provide icons for commands that don't have
        // a standard visualization. Cryptic icons aren't helpful, create visual
        // clutter, and prevent users from focusing on the important menu items.
        value = Standard;
    }

#elif defined(Q_OS_MAC)

    if (!value)
    {
        // https://developer.apple.com/design/human-interface-guidelines/menus
        // Represent menu item actions with familiar icons. Icons help people
        // recognize common actions throughout your app. Use the same icons as
        // the system to represent actions such as Copy, Share, and Delete,
        // wherever they appear. For a list of icons that represent common
        // actions, see Standard icons.
        // Don’t display an icon if you can’t find one that clearly represents
        // the menu item. Not all menu items need an icon. Be careful when adding
        // icons for custom menu items to avoid confusion with other existing
        // actions, and don’t add icons just for the sake of ornamentation.
        value = Standard;

        // Note: Qt 6.7.3 turned off menu icons by default on macOS.
        // https://github.com/qt/qtbase/commit/d671e1af3b736ee7d866323246fc2190fc5e076a
        // This seems too restrictive based on the Apple HIG guidance at
        // https://developer.apple.com/design/human-interface-guidelines/menus
        // Based on the HIG text above, this is probably too restrictive?
    }

#else

    if (!value)
    {
        auto const desktop = qgetenv("XDG_CURRENT_DESKTOP");

        // https://discourse.gnome.org/t/principle-of-icons-in-menus/4803
        // We do not “block” icons in menus; icons are generally reserved
        // for “nouns”, or “objects”—for instance: website favicons in
        // bookmark menus, or file-type icons—instead of having them for
        // “verbs”, or “actions”—for instance: save, copy, print, etc.
        if (desktop.contains("GNOME"))
        {
            value = Noun;
        }

        // https://develop.kde.org/hig/icons/#icons-for-menu-items-and-buttons-with-text
        // Set an icon on every button and menu item, making sure not to
        // use the same icon for multiple visible buttons or menu items.
        // Choose different icons, or use more specific ones to disambiguate.
        else if (desktop.contains("KDE"))
        {
            value = Standard | Noun | Verb;
        }

        // Unknown DE -- not GNOME or KDE, so probably no HIG.
        // Use best guess.
        else
        {
            value = Standard | Noun;
        }
    }

#endif

    return *value;
}
} // namespace

QIcon icon(Type const type, QStyle* style)
{
    ensureFontsLoaded();

    static auto const point_sizes = small::set<int>{
        style->pixelMetric(QStyle::PM_ButtonIconSize),   style->pixelMetric(QStyle::PM_LargeIconSize),
        style->pixelMetric(QStyle::PM_ListViewIconSize), style->pixelMetric(QStyle::PM_MessageBoxIconSize),
        style->pixelMetric(QStyle::PM_SmallIconSize),    style->pixelMetric(QStyle::PM_TabBarIconSize),
        style->pixelMetric(QStyle::PM_ToolBarIconSize)
    };

    auto const info = getInfo(type);

#if defined(Q_OS_MAC)
    if (auto const name = info.sf_symbol_name; !std::empty(name))
    {
        auto icon = QIcon{};
        auto const qname = QString::fromUtf8(std::data(name), std::size(name));
        for (int const point_size : point_sizes)
            if (auto const pixmap = loadSFSymbol(qname, point_size); !pixmap.isNull())
                icon.addPixmap(pixmap);
        if (!icon.isNull())
            return icon;
    }
#endif

    if (auto const codepoint = info.segoe_codepoint)
    {
        if (auto const font_family = getWindowsFontFamily(); !font_family.isEmpty())
        {
            auto icon = QIcon{};
            auto const ch = QChar{ codepoint };
            for (int const point_size : point_sizes)
                if (auto pixmap = makeIconFromCodepoint(font_family, ch, point_size); !pixmap.isNull())
                    icon.addPixmap(pixmap);
            if (!icon.isNull())
                return icon;
        }
    }

    if (auto const name = info.xdg_icon_name; !std::empty(name))
    {
        auto const qname = QString::fromUtf8(std::data(name), std::size(name));
        if (auto icon = QIcon::fromTheme(qname); !icon.isNull())
            return icon;
        if (auto icon = QIcon::fromTheme(qname + QStringLiteral("-symbolic")); !icon.isNull())
            return icon;
    }

    return {};
}

[[nodiscard]] bool shouldBeShownInMenu(Type type)
{
    auto const facet_mode = getInfo(type).mode;
    assert(facet_mode.any());
    return (get_menu_mode() & facet_mode).any();
}

} // namespace icons
