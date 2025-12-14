// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "NativeIcon.h"

#include <optional>
#include <string_view>

#include <QChar>
#include <QFontDatabase>
#include <QOperatingSystemVersion>
#include <QStyle>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication> // qApp
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

QPixmap makeIconFromCodepoint(QString const family, QChar const codepoint, int const pixel_size)
{
    auto font = QFont{ family };
    if (!QFontMetrics{ font }.inFont(codepoint))
        return {};

    font.setPixelSize(pixel_size);

    // FIXME: HDPI, pixel size vs point size?
    auto const rect = QRect{ 0, 0, pixel_size, pixel_size };
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

struct Info
{
    std::string_view sf_symbol_name;
    char16_t segoe_codepoint = {};
    std::string_view xdg_icon_name;
    std::optional<QStyle::StandardPixmap> fallback;
    bool ok_in_gnome_menus = false;
};

/**
 * # Choosing Icons
 *
 * Use icons that follow the per-platform guidelines below.
 * If there's not a match, don't give that Type an icon on that platform.
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
 *
 * ## QStyle::StandardPixmap
 *
 * This is an extremely limited icon set. Use with caution.
 * https://github.com/transmission/transmission/pull/2283 has galleries.
 * This is used as a fallback to ensure all toolbar acitions have icons,
 * even on very old Windows / macOS systems lacking Segoe / SF Symbols.
 */
[[nodiscard]] constexpr Info getInfo(Type const type)
{
    auto sf_symbol_name = std::string_view{};
    auto xdg_icon_name = std::string_view{};
    auto segoe_codepoint = char16_t{};
    auto fallback = std::optional<QStyle::StandardPixmap>{};
    auto ok_in_gnome_menus = false;

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
        fallback = QStyle::SP_FileIcon;
        break;

    case Type::AddTorrentFromURL:
        sf_symbol_name = "network";
        segoe_codepoint = 0xE774U; // Globe
        xdg_icon_name = "network-workgroup";
        fallback = QStyle::SP_DriveNetIcon;
        break;

    case Type::CreateNewTorrent:
        sf_symbol_name = "plus";
        segoe_codepoint = 0xE710U; // Add
        xdg_icon_name = "document-new";
        fallback = QStyle::SP_FileIcon;
        break;

    case Type::OpenTorrentDetails:
        sf_symbol_name = "doc.text.magnifyingglass";
        segoe_codepoint = 0xE946U; // Info
        xdg_icon_name = "document-properties";
        ok_in_gnome_menus = true;
        fallback = QStyle::SP_FileDialogContentsView;
        break;

    case Type::OpenTorrentLocalFolder:
        sf_symbol_name = "folder";
        segoe_codepoint = 0xED25U; // OpenFolderHorizontal
        xdg_icon_name = "folder-open";
        fallback = QStyle::SP_DirOpenIcon;
        break;

    case Type::StartTorrent:
        sf_symbol_name = "play";
        segoe_codepoint = 0xE768U; // Play
        xdg_icon_name = "media-playback-start";
        fallback = QStyle::SP_MediaPlay;
        break;

    case Type::StartTorrentNow:
        sf_symbol_name = "forward";
        segoe_codepoint = 0xEB9DU; // FastForward
        xdg_icon_name = "media-seek-forward";
        fallback = QStyle::SP_MediaSeekForward;
        break;

    case Type::RemoveTorrent:
        sf_symbol_name = "minus";
        segoe_codepoint = 0xE738U; // Remove
        xdg_icon_name = "list-remove";
        fallback = QStyle::SP_DialogCancelButton;
        break;

    case Type::RemoveTorrentAndDeleteData:
        sf_symbol_name = "trash";
        segoe_codepoint = 0xE74DU; // Delete
        xdg_icon_name = "edit-delete";
        fallback = QStyle::SP_TrashIcon;
        break;

    case Type::SetTorrentLocation:
        sf_symbol_name = "arrow.up.and.down.and.arrow.left.and.right";
        segoe_codepoint = 0xE7C2U; // Move
        xdg_icon_name = "edit-find";
        break;

    case Type::CopyMagnetLinkToClipboard:
        sf_symbol_name = "clipboard";
        segoe_codepoint = 0xE8C8U; // Copy
        xdg_icon_name = "edit-copy";
        break;

    case Type::SelectAll:
        sf_symbol_name = "checkmark.square";
        segoe_codepoint = 0xE8B3U; // SelectAll
        xdg_icon_name = "edit-select-all";
        break;

    case Type::DeselectAll:
        sf_symbol_name = "square";
        segoe_codepoint = 0xE739U; // Checkbox
        xdg_icon_name = "edit-select-none";
        break;

    case Type::Statistics:
        sf_symbol_name = "chart.bar";
        segoe_codepoint = 0xED5EU; // Ruler
        xdg_icon_name = "info";
        ok_in_gnome_menus = true;
        break;

    case Type::Donate:
        sf_symbol_name = "heart";
        segoe_codepoint = 0xEB51U; // Heart
        xdg_icon_name = "donate";
        break;

    case Type::Settings:
        sf_symbol_name = "gearshape";
        segoe_codepoint = 0xE713U; // Settings
        xdg_icon_name = "preferences-system";
        ok_in_gnome_menus = true;
        break;

    case Type::QuitApp:
        sf_symbol_name = "power";
        segoe_codepoint = 0xE7E8U; // PowerButton
        xdg_icon_name = "application-exit";
        break;

    case Type::About:
        sf_symbol_name = "info.circle";
        segoe_codepoint = 0xE946U; // Info
        xdg_icon_name = "help-about";
        fallback = QStyle::SP_MessageBoxInformation;
        ok_in_gnome_menus = true;
        break;

    case Type::Help:
        sf_symbol_name = "questionmark.circle";
        segoe_codepoint = 0xE897U; // Help
        xdg_icon_name = "help-faq";
        fallback = QStyle::SP_DialogHelpButton;
        ok_in_gnome_menus = true;
        break;

    case Type::QueueMoveTop:
        sf_symbol_name = "arrow.up.to.line";
        segoe_codepoint = 0xEDDBU; // CaretUpSolid8
        xdg_icon_name = "go-top";
        break;

    case Type::QueueMoveUp:
        sf_symbol_name = "arrow.up";
        segoe_codepoint = 0xEDD7U; // CaretUp8
        xdg_icon_name = "go-up";
        break;

    case Type::QueueMoveDown:
        sf_symbol_name = "arrow.down";
        segoe_codepoint = 0xEDD8U; // CaretDown8
        xdg_icon_name = "go-down";
        break;

    case Type::QueueMoveBottom:
        sf_symbol_name = "arrow.down.to.line";
        segoe_codepoint = 0xEDDCU; // CaretDownSolid8
        xdg_icon_name = "go-bottom";
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
        fallback = QStyle::SP_MessageBoxCritical;
        break;

    case Type::TorrentStateActive:
        sf_symbol_name = "play";
        segoe_codepoint = 0xE768U; // Play
        xdg_icon_name = "media-playback-start";
        fallback = QStyle::SP_MediaPlay;
        break;

    case Type::TorrentStateSeeding:
        sf_symbol_name = "chevron.up";
        segoe_codepoint = 0xE70EU; // ChevronUp
        xdg_icon_name = "go-up";
        fallback = QStyle::SP_ArrowUp;
        break;

    case Type::TorrentStateDownloading:
        sf_symbol_name = "chevron.down";
        segoe_codepoint = 0xE70DU; // ChevronDown
        xdg_icon_name = "go-down";
        fallback = QStyle::SP_ArrowDown;
        break;

    case Type::PauseTorrent:
        [[fallthrough]];

    case Type::TorrentStatePaused:
        sf_symbol_name = "pause";
        segoe_codepoint = 0xE769U; // Pause
        xdg_icon_name = "media-playback-pause";
        fallback = QStyle::SP_MediaPause;
        break;

    case Type::VerifyTorrent:
        [[fallthrough]];

    case Type::TorrentStateVerifying:
        sf_symbol_name = "arrow.clockwise";
        segoe_codepoint = 0xE72CU; // Refresh
        xdg_icon_name = "view-refresh";
        fallback = QStyle::SP_BrowserReload;
        break;

    case Type::TorrentErrorEmblem:
        [[fallthrough]];

    case Type::TorrentStateError:
        sf_symbol_name = "xmark.circle";
        segoe_codepoint = 0xEB90U; // StatusErrorFull
        xdg_icon_name = "dialog-error";
        fallback = QStyle::SP_MessageBoxWarning;
        break;
    }

    return { sf_symbol_name, segoe_codepoint, xdg_icon_name, fallback, ok_in_gnome_menus };
}
} // namespace

QIcon icon(Type const type, QStyle const* const style)
{
    ensureFontsLoaded();

    auto const pixel_sizes = small::max_size_set<int, 7U>{
        style->pixelMetric(QStyle::PM_ButtonIconSize),   style->pixelMetric(QStyle::PM_LargeIconSize),
        style->pixelMetric(QStyle::PM_ListViewIconSize), style->pixelMetric(QStyle::PM_MessageBoxIconSize),
        style->pixelMetric(QStyle::PM_SmallIconSize),    style->pixelMetric(QStyle::PM_TabBarIconSize),
        style->pixelMetric(QStyle::PM_ToolBarIconSize)
    };

    auto const info = getInfo(type);

#if defined(Q_OS_MAC)
    if (auto const key = info.sf_symbol_name; !std::empty(key))
    {
        auto icon = QIcon{};
        auto const name = QString::fromUtf8(std::data(key), std::size(key));
        for (int const pixel_size : pixel_sizes)
            if (auto const pixmap = loadSFSymbol(name, pixel_size); !pixmap.isNull())
                icon.addPixmap(pixmap);
        if (!icon.isNull())
            return icon;
    }
#endif

    if (auto const key = info.segoe_codepoint)
    {
        if (auto const family = getWindowsFontFamily(); !family.isEmpty())
        {
            auto icon = QIcon{};
            auto const ch = QChar{ key };
            for (int const pixel_size : pixel_sizes)
                if (auto pixmap = makeIconFromCodepoint(family, ch, pixel_size); !pixmap.isNull())
                    icon.addPixmap(pixmap);
            if (!icon.isNull())
                return icon;
        }
    }

    if (auto const key = info.xdg_icon_name; !std::empty(key))
    {
        auto const name = QString::fromUtf8(std::data(key), std::size(key));

        if (auto icon = QIcon::fromTheme(name); !icon.isNull())
            return icon;
        if (auto icon = QIcon::fromTheme(name + QStringLiteral("-symbolic")); !icon.isNull())
            return icon;
    }

    if (info.fallback)
        return style->standardIcon(*info.fallback);

    return {};
}

[[nodiscard]] bool shouldBeShownInMenu(Type type)
{
    static bool const force_icons = !qgetenv("TR_SHOW_MENU_ICONS").isEmpty();
    static bool const is_gnome = qgetenv("XDG_CURRENT_DESKTOP").contains("GNOME");
    return force_icons || !is_gnome || getInfo(type).ok_in_gnome_menus;
}

} // namespace icons
