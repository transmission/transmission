// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "NativeIcon.h"

#include <bitset>
#include <optional>
#include <string_view>
#include <tuple>

#include <QFontInfo>
#include <QOperatingSystemVersion>
#include <QPainterPath>
#include <QtGui/QIcon>
#include <QtGui/QPixmap>
#include <QtGui/QPainter>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>

#include <small/set.hpp>

#if defined(Q_OS_MAC)
extern QPixmap loadSFSymbol(QString symbol_name, int pixel_size);
#endif

namespace icons
{
namespace
{
auto const Win10IconFamily = QStringLiteral("Segoe MDL2 Assets");
auto const Win11IconFamily = QStringLiteral("Segoe Fluent Icons");

// Define these two macros to force a specific icon during development.
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
    // FIXME: light mode vs. dark mode?
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

using MenuMode = std::bitset<4U>;
constexpr auto Standard = MenuMode{ 1 << 0U };
constexpr auto Noun = MenuMode{ 1 << 1U };
constexpr auto Verb = MenuMode{ 1 << 2U };
constexpr auto Other = MenuMode{ 1 << 3U };

using Key = std::tuple<std::string_view, QChar, std::string_view, std::optional<QStyle::StandardPixmap>, MenuMode>;

[[nodiscard]] Key getKey(Facet const facet)
{
    // https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    // https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
    constexpr auto SegoeAdd = QChar{ 0xE710 };
    constexpr auto SegoeCaretDown8 = QChar{ 0xEDD8 };
    constexpr auto SegoeCaretDownSolid8 = QChar{ 0xEDDC };
    constexpr auto SegoeCaretUp8 = QChar{ 0xEDD7 };
    constexpr auto SegoeCaretUpSolid8 = QChar{ 0xEDDB };
    constexpr auto SegoeCheckbox = QChar{ 0xE739 };
    constexpr auto SegoeChevronDown = QChar{ 0xE70D };
    constexpr auto SegoeChevronUp = QChar{ 0xE70E };
    constexpr auto SegoeCopy = QChar{ 0xE8C8 };
    constexpr auto SegoeDelete = QChar{ 0xE74D };
    constexpr auto SegoeDownload = QChar{ 0xE896 };
    constexpr auto SegoeEdit = QChar{ 0xE70F };
    constexpr auto SegoeError = QChar{ 0xE783 };
    constexpr auto SegoeFastForward = QChar{ 0xEB9D };
    constexpr auto SegoeGlobe = QChar{ 0xE774 };
    constexpr auto SegoeHeart = QChar{ 0xEB51 };
    constexpr auto SegoeHelp = QChar{ 0xE897 };
    constexpr auto SegoeInfo = QChar{ 0xE946 };
    constexpr auto SegoeMove = QChar{ 0xE7C2 };
    constexpr auto SegoeOpenFile = QChar{ 0xE8E5 };
    constexpr auto SegoeOpenFolder = QChar{ 0xED25 };
    constexpr auto SegoePause = QChar{ 0xE769 };
    constexpr auto SegoePlay = QChar{ 0xE768 };
    constexpr auto SegoePowerButton = QChar{ 0xE7E8 };
    constexpr auto SegoeRefresh = QChar{ 0xE72C };
    constexpr auto SegoeRemove = QChar{ 0xE738 };
    constexpr auto SegoeRuler = QChar{ 0xED5E };
    constexpr auto SegoeSelectAll = QChar{ 0xE8B3 };
    constexpr auto SegoeSettings = QChar{ 0xE713 };
    constexpr auto SegoeStatusErrorFull = QChar{ 0xEB90 };
    constexpr auto SegoeUpload = QChar{ 0xE898 };
    constexpr auto SegoeUploadDownload = QChar{ 0xE174 };

    auto sf_symbol_name = std::string_view{};
    auto xdg_icon_name = std::string_view{};
    auto segoe_codepoint = QChar{};
    auto fallback = std::optional<QStyle::StandardPixmap>{};
    auto mode = MenuMode{};

    switch (facet)
    {
    case Facet::AddTracker:
        sf_symbol_name = "plus";
        segoe_codepoint = SegoeAdd;
        xdg_icon_name = "list-add";
        break;

    case Facet::EditTrackers:
        sf_symbol_name = "pencil";
        segoe_codepoint = SegoeEdit;
        xdg_icon_name = "document-edit";
        break;

    case Facet::RemoveTracker:
        sf_symbol_name = "minus";
        segoe_codepoint = SegoeRemove;
        xdg_icon_name = "list-remove";
        break;

    case Facet::AddTorrentFromFile:
        sf_symbol_name = "folder.open";
        segoe_codepoint = SegoeOpenFile;
        xdg_icon_name = "folder";
        fallback = QStyle::SP_DirOpenIcon;
        mode = Standard | Verb;
        break;

    case Facet::AddTorrentFromURL:
        sf_symbol_name = "globe";
        segoe_codepoint = SegoeGlobe;
        xdg_icon_name = "globe";
        fallback = QStyle::SP_DirOpenIcon;
        mode = Standard | Verb;
        break;

    case Facet::CreateNewTorrent:
        sf_symbol_name = "plus";
        segoe_codepoint = SegoeAdd;
        xdg_icon_name = "document-net";
        fallback = QStyle::SP_FileIcon;
        mode = Standard | Verb;
        break;

    case Facet::OpenTorrentDetails:
        sf_symbol_name = "doc.text.magnifyingglass";
        segoe_codepoint = SegoeInfo;
        xdg_icon_name = "document-properties";
        fallback = QStyle::SP_FileIcon;
        break;

    case Facet::OpenTorrentLocalFolder:
        sf_symbol_name = "folder";
        segoe_codepoint = SegoeOpenFolder;
        xdg_icon_name = "folder-open";
        fallback = QStyle::SP_DirOpenIcon;
        break;

    case Facet::StartTorrent:
        sf_symbol_name = "play";
        segoe_codepoint = SegoePlay;
        xdg_icon_name = "media-playback-start";
        fallback = QStyle::SP_MediaPlay;
        mode = Standard | Verb;
        break;

    case Facet::StartTorrentNow:
        sf_symbol_name = "FIXME";
        segoe_codepoint = SegoeFastForward;
        xdg_icon_name = "media-seek-forward";
        fallback = QStyle::SP_MediaPlay;
        mode = Standard | Verb;
        break;

    case Facet::RemoveTorrent:
        sf_symbol_name = "minus";
        segoe_codepoint = SegoeRemove;
        xdg_icon_name = "list-remove";
        fallback = QStyle::SP_DialogCancelButton;
        mode = Verb;
        break;

    case Facet::RemoveTorrentAndDeleteData:
        sf_symbol_name = "trash";
        segoe_codepoint = SegoeDelete;
        xdg_icon_name = "edit-delet";
        fallback = QStyle::SP_TrashIcon;
        mode = Verb;
        break;

    case Facet::SetTorrentLocation:
        sf_symbol_name = "arrow.up.and.down.and.arrow.left.and.right";
        segoe_codepoint = SegoeMove;
        xdg_icon_name = "edit-copy";
        mode = Standard | Verb;
        break;

    case Facet::CopyMagnetLinkToClipboard:
        sf_symbol_name = "clipboard";
        segoe_codepoint = SegoeCopy;
        xdg_icon_name = "edit-copy";
        mode = Standard | Verb;
        break;

    case Facet::SelectAll:
        sf_symbol_name = "checkmark.square";
        segoe_codepoint = SegoeSelectAll;
        xdg_icon_name = "edit-select-all";
        mode = Verb;
        break;

    case Facet::DeselectAll:
        sf_symbol_name = "checkmark";
        segoe_codepoint = SegoeCheckbox;
        xdg_icon_name = "edit-select-none";
        mode = Verb;
        break;

    case Facet::Statistics:
        sf_symbol_name = "chart.bar";
        segoe_codepoint = SegoeRuler;
        xdg_icon_name = "info";
        mode = Noun;
        break;

    case Facet::Donate:
        sf_symbol_name = "heart";
        segoe_codepoint = SegoeHeart;
        xdg_icon_name = "donate";
        break;

    case Facet::Settings:
        sf_symbol_name = "gearshape";
        segoe_codepoint = SegoeSettings;
        xdg_icon_name = "perferences-system";
        mode = Standard | Noun;
        break;

    case Facet::QuitApp:
        sf_symbol_name = "power";
        segoe_codepoint = SegoePowerButton;
        xdg_icon_name = "application-exit";
        mode = Standard | Verb;
        break;

    case Facet::About:
        sf_symbol_name = "info.circle";
        segoe_codepoint = SegoeInfo;
        xdg_icon_name = "help-about";
        fallback = QStyle::SP_MessageBoxInformation;
        mode = Standard | Noun;
        break;

    case Facet::Help:
        sf_symbol_name = "questionmark.circle";
        segoe_codepoint = SegoeHelp;
        xdg_icon_name = "help-faq";
        fallback = QStyle::SP_DialogHelpButton;
        mode = Standard | Noun;
        break;

    case Facet::QueueMoveTop:
        sf_symbol_name = "arrow.up.to.line";
        segoe_codepoint = SegoeCaretUpSolid8;
        xdg_icon_name = "go-top";
        mode = Verb;
        break;

    case Facet::QueueMoveUp:
        sf_symbol_name = "arrow.up";
        segoe_codepoint = SegoeCaretUp8;
        xdg_icon_name = "go-top";
        mode = Verb;
        break;

    case Facet::QueueMoveDown:
        sf_symbol_name = "arrow.down";
        segoe_codepoint = SegoeCaretDown8;
        xdg_icon_name = "go-down";
        mode = Verb;
        break;

    case Facet::QueueMoveBottom:
        sf_symbol_name = "arrow.down.to.line";
        segoe_codepoint = SegoeCaretDownSolid8;
        xdg_icon_name = "go-bottom";
        mode = Verb;
        break;

    case Facet::NetworkIdle:
        xdg_icon_name = "network-idle";
        break;

    case Facet::NetworkReceive:
        sf_symbol_name = "arrow.down.circle";
        segoe_codepoint = SegoeDownload;
        xdg_icon_name = "network-receive";
        break;

    case Facet::NetworkTransmit:
        sf_symbol_name = "arrow.up.circle";
        segoe_codepoint = SegoeUpload;
        xdg_icon_name = "network-transmit";
        break;

    case Facet::NetworkTransmitReceive:
        sf_symbol_name = "arrow.up.arrow.down.circle";
        segoe_codepoint = SegoeUploadDownload;
        xdg_icon_name = "network-transmit-receive";
        break;

    case Facet::NetworkError:
        sf_symbol_name = "wifi.exclamationmark";
        segoe_codepoint = SegoeError;
        xdg_icon_name = "network-error";
        fallback = QStyle::SP_MessageBoxCritical;
        break;

    case Facet::TorrentStateActive:
        sf_symbol_name = "play";
        segoe_codepoint = SegoePlay;
        xdg_icon_name = "media-playback-start";
        fallback = QStyle::SP_MediaPlay;
        break;

    case Facet::TorrentStateSeeding:
        sf_symbol_name = "chevron.up";
        segoe_codepoint = SegoeChevronUp;
        xdg_icon_name = "go-up";
        fallback = QStyle::SP_ArrowUp;
        break;

    case Facet::TorrentStateDownloading:
        sf_symbol_name = "chevron.down";
        segoe_codepoint = SegoeChevronDown;
        xdg_icon_name = "go-down";
        fallback = QStyle::SP_ArrowDown;
        break;

    case Facet::PauseTorrent:
        mode = Standard | Verb;
        [[fallthrough]];

    case Facet::TorrentStatePaused:
        sf_symbol_name = "pause";
        segoe_codepoint = SegoePause;
        xdg_icon_name = "media-playback-pause";
        fallback = QStyle::SP_MediaPause;
        break;

    case Facet::VerifyTorrent:
        mode = Standard | Verb;
        [[fallthrough]];

    case Facet::TorrentStateVerifying:
        sf_symbol_name = "arrow.clockwise";
        segoe_codepoint = SegoeRefresh;
        xdg_icon_name = "view-refresh";
        fallback = QStyle::SP_BrowserReload;
        break;

    case Facet::TorrentErrorEmblem:
        [[fallthrough]];

    case Facet::TorrentStateError:
        sf_symbol_name = "xmark.circle";
        segoe_codepoint = SegoeStatusErrorFull;
        xdg_icon_name = "dialog-error";
        fallback = QStyle::SP_MessageBoxWarning;
        break;
    }

    return { sf_symbol_name, segoe_codepoint, xdg_icon_name, fallback, mode };
}

[[nodiscard]] MenuMode get_menu_mode()
{
    static auto value = std::optional<MenuMode>{};

    if (!value)
    {
        auto const override = qgetenv("TR_ICON_MODE").toLower();
        if (override.contains("all"))
            value = Noun | Standard | Verb | Other;
        if (override.contains("noun"))
            value = Noun;
        if (override.contains("standard"))
            value = Standard;
        if (override.contains("verb"))
            value = Verb;
    }

#if defined(Q_OS_WIN)

    if (!value)
    {
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
            value = Standard | Noun | Verb | Other;
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

QIcon icon(Facet const facet, QStyle* style)
{
    auto const [sf_symbol_name, segoe_codepoint, xdg_icon_name, fallback, mode] = getKey(facet);

    ensureFontsLoaded();

    static auto const point_sizes = small::set<int>{
        style->pixelMetric(QStyle::PM_ButtonIconSize),   style->pixelMetric(QStyle::PM_LargeIconSize),
        style->pixelMetric(QStyle::PM_ListViewIconSize), style->pixelMetric(QStyle::PM_MessageBoxIconSize),
        style->pixelMetric(QStyle::PM_SmallIconSize),    style->pixelMetric(QStyle::PM_TabBarIconSize),
        style->pixelMetric(QStyle::PM_ToolBarIconSize)
    };

#if defined(Q_OS_MAC)
    if (!std::empty(sf_symbol_name))
    {
        auto icon = QIcon{};
        auto const name = QString::fromUtf8(std::data(xdg_icon_name), std::size(xdg_icon_name));
        for (int const point_size : point_sizes)
            if (auto const pixmap = loadSFSymbol(name, point_size); !pixmap.isNull())
                icon.addPixmap(pixmap);
        if (!icon.isNull())
            return icon;
    }
#endif

    if (!segoe_codepoint.isNull())
    {
        if (auto const font_family = getWindowsFontFamily(); !font_family.isEmpty())
        {
            auto icon = QIcon{};
            for (int const point_size : point_sizes)
                if (auto const pixmap = makeIconFromCodepoint(font_family, segoe_codepoint, point_size); !pixmap.isNull())
                    icon.addPixmap(pixmap);
            if (!icon.isNull())
                return icon;
        }
    }

    if (!std::empty(xdg_icon_name))
    {
        auto const name = QString::fromUtf8(std::data(xdg_icon_name), std::size(xdg_icon_name));

        if (auto icon = QIcon::fromTheme(name); !icon.isNull())
            return icon;
        if (auto icon = QIcon::fromTheme(name + QStringLiteral("-symbolic")); !icon.isNull())
            return icon;
    }

    if (fallback)
        return style->standardIcon(*fallback);

    return {};
}

[[nodiscard]] bool shouldBeShownInMenu(Facet facet)
{
    auto const [sf_symbol_name, segoe_codepoint, xdg_icon_name, fallback, facet_mode] = getKey(facet);
    assert(facet_mode.any());
    return (get_menu_mode() & facet_mode).any();
}

} // namespace icons
