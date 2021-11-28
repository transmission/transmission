/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <limits.h> /* INT_MAX */

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_truncd() */

#include "HigWorkarea.h"
#include "IconCache.h"
#include "TorrentCellRenderer.h"
#include "Utils.h"

/* #define TEST_RTL */

#define DEFAULT_BAR_HEIGHT 12
#define SMALL_SCALE 0.9
#define COMPACT_ICON_SIZE Gtk::ICON_SIZE_MENU
#define FULL_ICON_SIZE Gtk::ICON_SIZE_DND

/***
****
***/

namespace
{

Glib::ustring getProgressString(tr_torrent const* tor, tr_info const* info, tr_stat const* st)
{
    Glib::ustring gstr;

    bool const isDone = st->leftUntilDone == 0;
    uint64_t const haveTotal = st->haveUnchecked + st->haveValid;
    bool const isSeed = st->haveValid >= info->totalSize;
    double seedRatio;
    bool const hasSeedRatio = tr_torrentGetSeedRatio(tor, &seedRatio);

    if (!isDone) /* downloading */
    {
        gstr += gtr_sprintf(
            /* %1$s is how much we've got,
               %2$s is how much we'll have when done,
               %3$s%% is a percentage of the two */
            _("%1$s of %2$s (%3$s%%)"),
            tr_strlsize(haveTotal),
            tr_strlsize(st->sizeWhenDone),
            tr_strlpercent(st->percentDone * 100.0));
    }
    else if (!isSeed) /* partial seeds */
    {
        if (hasSeedRatio)
        {
            gstr += gtr_sprintf(
                /* %1$s is how much we've got,
                   %2$s is the torrent's total size,
                   %3$s%% is a percentage of the two,
                   %4$s is how much we've uploaded,
                   %5$s is our upload-to-download ratio,
                   %6$s is the ratio we want to reach before we stop uploading */
                _("%1$s of %2$s (%3$s%%), uploaded %4$s (Ratio: %5$s Goal: %6$s)"),
                tr_strlsize(haveTotal),
                tr_strlsize(info->totalSize),
                tr_strlpercent(st->percentComplete * 100.0),
                tr_strlsize(st->uploadedEver),
                tr_strlratio(st->ratio),
                tr_strlratio(seedRatio));
        }
        else
        {
            gstr += gtr_sprintf(
                /* %1$s is how much we've got,
                   %2$s is the torrent's total size,
                   %3$s%% is a percentage of the two,
                   %4$s is how much we've uploaded,
                   %5$s is our upload-to-download ratio */
                _("%1$s of %2$s (%3$s%%), uploaded %4$s (Ratio: %5$s)"),
                tr_strlsize(haveTotal),
                tr_strlsize(info->totalSize),
                tr_strlpercent(st->percentComplete * 100.0),
                tr_strlsize(st->uploadedEver),
                tr_strlratio(st->ratio));
        }
    }
    else /* seeding */
    {
        if (hasSeedRatio)
        {
            gstr += gtr_sprintf(
                /* %1$s is the torrent's total size,
                   %2$s is how much we've uploaded,
                   %3$s is our upload-to-download ratio,
                   %4$s is the ratio we want to reach before we stop uploading */
                _("%1$s, uploaded %2$s (Ratio: %3$s Goal: %4$s)"),
                tr_strlsize(info->totalSize),
                tr_strlsize(st->uploadedEver),
                tr_strlratio(st->ratio),
                tr_strlratio(seedRatio));
        }
        else /* seeding w/o a ratio */
        {
            gstr += gtr_sprintf(
                /* %1$s is the torrent's total size,
                   %2$s is how much we've uploaded,
                   %3$s is our upload-to-download ratio */
                _("%1$s, uploaded %2$s (Ratio: %3$s)"),
                tr_strlsize(info->totalSize),
                tr_strlsize(st->uploadedEver),
                tr_strlratio(st->ratio));
        }
    }

    /* add time when downloading */
    if (st->activity == TR_STATUS_DOWNLOAD || (hasSeedRatio && st->activity == TR_STATUS_SEED))
    {
        int const eta = st->eta;
        gstr += " - ";

        if (eta < 0)
        {
            gstr += _("Remaining time unknown");
        }
        else
        {
            /* time remaining */
            gstr += gtr_sprintf(_("%s remaining"), tr_strltime(eta));
        }
    }

    return gstr;
}

Glib::ustring getShortTransferString(
    tr_torrent const* tor,
    tr_stat const* st,
    double uploadSpeed_KBps,
    double downloadSpeed_KBps)
{
    Glib::ustring buf;

    bool const haveMeta = tr_torrentHasMetadata(tor);
    bool const haveUp = haveMeta && st->peersGettingFromUs > 0;
    bool const haveDown = haveMeta && (st->peersSendingToUs > 0 || st->webseedsSendingToUs > 0);

    if (haveDown)
    {
        char dnStr[32];
        char upStr[32];
        tr_formatter_speed_KBps(dnStr, downloadSpeed_KBps, sizeof(dnStr));
        tr_formatter_speed_KBps(upStr, uploadSpeed_KBps, sizeof(upStr));

        /* down speed, down symbol, up speed, up symbol */
        buf += gtr_sprintf(
            _("%1$s %2$s  %3$s %4$s"),
            dnStr,
            gtr_get_unicode_string(GTR_UNICODE_DOWN),
            upStr,
            gtr_get_unicode_string(GTR_UNICODE_UP));
    }
    else if (haveUp)
    {
        char upStr[32];
        tr_formatter_speed_KBps(upStr, uploadSpeed_KBps, sizeof(upStr));

        /* up speed, up symbol */
        buf += gtr_sprintf(_("%1$s  %2$s"), upStr, gtr_get_unicode_string(GTR_UNICODE_UP));
    }
    else if (st->isStalled)
    {
        buf += _("Stalled");
    }

    return buf;
}

Glib::ustring getShortStatusString(tr_torrent const* tor, tr_stat const* st, double uploadSpeed_KBps, double downloadSpeed_KBps)
{
    Glib::ustring gstr;

    switch (st->activity)
    {
    case TR_STATUS_STOPPED:
        gstr += st->finished ? _("Finished") : _("Paused");
        break;

    case TR_STATUS_CHECK_WAIT:
        gstr += _("Queued for verification");
        break;

    case TR_STATUS_DOWNLOAD_WAIT:
        gstr += _("Queued for download");
        break;

    case TR_STATUS_SEED_WAIT:
        gstr += _("Queued for seeding");
        break;

    case TR_STATUS_CHECK:
        gstr += gtr_sprintf(_("Verifying local data (%.1f%% tested)"), tr_truncd(st->recheckProgress * 100.0, 1));
        break;

    case TR_STATUS_DOWNLOAD:
    case TR_STATUS_SEED:
        {
            /* download/upload speed, ratio */
            gstr += gtr_sprintf("%s  ", getShortTransferString(tor, st, uploadSpeed_KBps, downloadSpeed_KBps));
            gstr += gtr_sprintf(_("Ratio: %s"), tr_strlratio(st->ratio));
            break;
        }

    default:
        break;
    }

    return gstr;
}

Glib::ustring getStatusString(
    tr_torrent const* tor,
    tr_stat const* st,
    double const uploadSpeed_KBps,
    double const downloadSpeed_KBps)
{
    Glib::ustring gstr;

    if (st->error != 0)
    {
        char const* fmt[] = {
            nullptr,
            N_("Tracker gave a warning: \"%s\""),
            N_("Tracker gave an error: \"%s\""),
            N_("Error: %s"),
        };

        gstr += gtr_sprintf(_(fmt[st->error]), st->errorString);
    }
    else
    {
        switch (st->activity)
        {
        case TR_STATUS_STOPPED:
        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK:
        case TR_STATUS_DOWNLOAD_WAIT:
        case TR_STATUS_SEED_WAIT:
            {
                gstr += getShortStatusString(tor, st, uploadSpeed_KBps, downloadSpeed_KBps);
                break;
            }

        case TR_STATUS_DOWNLOAD:
            {
                if (!tr_torrentHasMetadata(tor))
                {
                    /* Downloading metadata from 2 peer (s)(50% done) */
                    gstr += gtr_sprintf(
                        _("Downloading metadata from %1$'d %2$s (%3$d%% done)"),
                        st->peersConnected,
                        ngettext("peer", "peers", st->peersConnected),
                        (int)(100.0 * st->metadataPercentComplete));
                }
                else if (st->peersSendingToUs != 0 && st->webseedsSendingToUs != 0)
                {
                    /* Downloading from 2 of 3 peer (s) and 2 webseed (s) */
                    gstr += gtr_sprintf(
                        _("Downloading from %1$'d of %2$'d %3$s and %4$'d %5$s"),
                        st->peersSendingToUs,
                        st->peersConnected,
                        ngettext("peer", "peers", st->peersConnected),
                        st->webseedsSendingToUs,
                        ngettext("web seed", "web seeds", st->webseedsSendingToUs));
                }
                else if (st->webseedsSendingToUs != 0)
                {
                    /* Downloading from 3 web seed (s) */
                    gstr += gtr_sprintf(
                        _("Downloading from %1$'d %2$s"),
                        st->webseedsSendingToUs,
                        ngettext("web seed", "web seeds", st->webseedsSendingToUs));
                }
                else
                {
                    /* Downloading from 2 of 3 peer (s) */
                    gstr += gtr_sprintf(
                        _("Downloading from %1$'d of %2$'d %3$s"),
                        st->peersSendingToUs,
                        st->peersConnected,
                        ngettext("peer", "peers", st->peersConnected));
                }

                break;
            }

        case TR_STATUS_SEED:
            gstr += gtr_sprintf(
                ngettext(
                    "Seeding to %1$'d of %2$'d connected peer",
                    "Seeding to %1$'d of %2$'d connected peers",
                    st->peersConnected),
                st->peersGettingFromUs,
                st->peersConnected);
            break;
        }
    }

    if (st->activity != TR_STATUS_CHECK_WAIT && st->activity != TR_STATUS_CHECK && st->activity != TR_STATUS_DOWNLOAD_WAIT &&
        st->activity != TR_STATUS_SEED_WAIT && st->activity != TR_STATUS_STOPPED)
    {
        auto const buf = getShortTransferString(tor, st, uploadSpeed_KBps, downloadSpeed_KBps);

        if (!buf.empty())
        {
            gstr += gtr_sprintf(" - %s", buf);
        }
    }

    return gstr;
}

} // namespace

/***
****
***/

class TorrentCellRenderer::Impl
{
public:
    Impl(TorrentCellRenderer& renderer);
    ~Impl();

    void get_size_compact(Gtk::Widget& widget, int& width, int& height) const;
    void get_size_full(Gtk::Widget& widget, int& width, int& height) const;

    void render_compact(
        Cairo::RefPtr<Cairo::Context> const& cr,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gtk::CellRendererState flags);
    void render_full(
        Cairo::RefPtr<Cairo::Context> const& cr,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gtk::CellRendererState flags);

public:
    Glib::Property<void*> torrent;
    Glib::Property<int> bar_height;

    /* Use this instead of tr_stat.pieceUploadSpeed so that the model can
       control when the speed displays get updated. This is done to keep
       the individual torrents' speeds and the status bar's overall speed
       in sync even if they refresh at slightly different times */
    Glib::Property<double> upload_speed_KBps;

    /* @see upload_speed_Bps */
    Glib::Property<double> download_speed_KBps;

    Glib::Property<bool> compact;

private:
    TorrentCellRenderer& renderer_;

    Gtk::CellRendererText* text_renderer_ = nullptr;
    Gtk::CellRendererProgress* progress_renderer_ = nullptr;
    Gtk::CellRendererPixbuf* icon_renderer_ = nullptr;
};

/***
****
***/

namespace
{

Glib::RefPtr<Gdk::Pixbuf> get_icon(tr_torrent const* tor, Gtk::IconSize icon_size, Gtk::Widget& for_widget)
{
    Glib::ustring mime_type;
    auto const n_files = tr_torrentFileCount(tor);

    if (n_files == 0)
    {
        mime_type = UNKNOWN_MIME_TYPE;
    }
    else if (n_files > 1)
    {
        mime_type = DIRECTORY_MIME_TYPE;
    }
    else
    {
        auto const* const name = tr_torrentFile(tor, 0).name;

        mime_type = strchr(name, '/') != nullptr ? DIRECTORY_MIME_TYPE : gtr_get_mime_type_from_filename(name);
    }

    return gtr_get_mime_type_icon(mime_type, icon_size, for_widget);
}

} // namespace

/***
****
***/

void TorrentCellRenderer::Impl::get_size_compact(Gtk::Widget& widget, int& width, int& height) const
{
    int xpad;
    int ypad;
    Gtk::Requisition min_size;
    Gtk::Requisition icon_size;
    Gtk::Requisition name_size;
    Gtk::Requisition stat_size;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);

    auto const icon = get_icon(tor, COMPACT_ICON_SIZE, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_stat = getShortStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    renderer_.get_padding(xpad, ypad);

    /* get the idealized cell dimensions */
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_size(widget, min_size, icon_size);
    text_renderer_->property_text() = name;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->get_preferred_size(widget, min_size, name_size);
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_scale() = SMALL_SCALE;
    text_renderer_->get_preferred_size(widget, min_size, stat_size);

    /**
    *** LAYOUT
    **/

#define BAR_WIDTH 50

    width = xpad * 2 + icon_size.width + GUI_PAD + BAR_WIDTH + GUI_PAD + stat_size.width;
    height = ypad * 2 + std::max(name_size.height, bar_height.get_value());
}

void TorrentCellRenderer::Impl::get_size_full(Gtk::Widget& widget, int& width, int& height) const
{
    int xpad;
    int ypad;
    Gtk::Requisition min_size;
    Gtk::Requisition icon_size;
    Gtk::Requisition name_size;
    Gtk::Requisition stat_size;
    Gtk::Requisition prog_size;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);
    auto const* const inf = tr_torrentInfo(tor);

    auto const icon = get_icon(tor, FULL_ICON_SIZE, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_stat = getStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    auto const gstr_prog = getProgressString(tor, inf, st);
    renderer_.get_padding(xpad, ypad);

    /* get the idealized cell dimensions */
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_size(widget, min_size, icon_size);
    text_renderer_->property_text() = name;
    text_renderer_->property_weight() = Pango::WEIGHT_BOLD;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->get_preferred_size(widget, min_size, name_size);
    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_weight() = Pango::WEIGHT_NORMAL;
    text_renderer_->property_scale() = SMALL_SCALE;
    text_renderer_->get_preferred_size(widget, min_size, prog_size);
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->get_preferred_size(widget, min_size, stat_size);

    /**
    *** LAYOUT
    **/

    width = xpad * 2 + icon_size.width + GUI_PAD + std::max(prog_size.width, stat_size.width);
    height = ypad * 2 + name_size.height + prog_size.height + GUI_PAD_SMALL + bar_height.get_value() + GUI_PAD_SMALL +
        stat_size.height;
}

void TorrentCellRenderer::get_preferred_width_vfunc(Gtk::Widget& widget, int& minimum_width, int& natural_width) const
{
    if (impl_->torrent.get_value() != nullptr)
    {
        int w;
        int h;

        if (impl_->compact.get_value())
        {
            impl_->get_size_compact(widget, w, h);
        }
        else
        {
            impl_->get_size_full(widget, w, h);
        }

        minimum_width = w;
        natural_width = w;
    }
}

void TorrentCellRenderer::get_preferred_height_vfunc(Gtk::Widget& widget, int& minimum_height, int& natural_height) const
{
    if (impl_->torrent.get_value() != nullptr)
    {
        int w;
        int h;

        if (impl_->compact.get_value())
        {
            impl_->get_size_compact(widget, w, h);
        }
        else
        {
            impl_->get_size_full(widget, w, h);
        }

        minimum_height = h;
        natural_height = h;
    }
}

namespace
{

Gdk::RGBA get_text_color(Gtk::Widget& w, tr_stat const* st)
{
    static auto const red = Gdk::RGBA("red");

    if (st->error != 0)
    {
        return red;
    }
    else if (st->activity == TR_STATUS_STOPPED)
    {
        return w.get_style_context()->get_color(Gtk::STATE_FLAG_INSENSITIVE);
    }
    else
    {
        return w.get_style_context()->get_color(Gtk::STATE_FLAG_NORMAL);
    }
}

double get_percent_done(tr_torrent const* tor, tr_stat const* st, bool* seed)
{
    double d;

    if (st->activity == TR_STATUS_SEED && tr_torrentGetSeedRatio(tor, &d))
    {
        *seed = true;
        d = MAX(0.0, st->seedRatioPercentDone);
    }
    else
    {
        *seed = false;
        d = MAX(0.0, st->percentDone);
    }

    return d;
}

} // namespace

void TorrentCellRenderer::Impl::render_compact(
    Cairo::RefPtr<Cairo::Context> const& cr,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gtk::CellRendererState flags)
{
    int xpad;
    int ypad;
    int min_width;
    int width;
    bool seed;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);
    bool const active = st->activity != TR_STATUS_STOPPED && st->activity != TR_STATUS_DOWNLOAD_WAIT &&
        st->activity != TR_STATUS_SEED_WAIT;
    auto const percentDone = get_percent_done(tor, st, &seed);
    bool const sensitive = active || st->error;

    auto const icon = get_icon(tor, COMPACT_ICON_SIZE, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_stat = getShortStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    renderer_.get_padding(xpad, ypad);
    auto const text_color = get_text_color(widget, st);

    auto fill_area = background_area;
    fill_area.set_x(fill_area.get_x() + xpad);
    fill_area.set_y(fill_area.get_y() + ypad);
    fill_area.set_width(fill_area.get_width() - xpad * 2);
    fill_area.set_height(fill_area.get_height() - ypad * 2);

    auto icon_area = fill_area;
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_width(widget, min_width, width);
    icon_area.set_width(width);

    auto prog_area = fill_area;
    prog_area.set_width(BAR_WIDTH);

    auto stat_area = fill_area;
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->property_scale() = SMALL_SCALE;
    text_renderer_->get_preferred_width(widget, min_width, width);
    stat_area.set_width(width);

    auto name_area = fill_area;
    name_area.set_width(
        fill_area.get_width() - icon_area.get_width() - stat_area.get_width() - prog_area.get_width() - GUI_PAD * 3);

    if ((renderer_.get_state(widget, flags) & Gtk::StateFlags::STATE_FLAG_DIR_RTL) == 0)
    {
        icon_area.set_x(fill_area.get_x());
        prog_area.set_x(fill_area.get_x() + fill_area.get_width() - prog_area.get_width());
        stat_area.set_x(prog_area.get_x() - stat_area.get_width() - GUI_PAD);
        name_area.set_x(icon_area.get_x() + icon_area.get_width() + GUI_PAD);
    }
    else
    {
        icon_area.set_x(fill_area.get_x() + fill_area.get_width() - icon_area.get_width());
        prog_area.set_x(fill_area.get_x());
        stat_area.set_x(prog_area.get_x() + prog_area.get_width() + GUI_PAD);
        name_area.set_x(stat_area.get_x() + stat_area.get_width() + GUI_PAD);
    }

    /**
    *** RENDER
    **/

    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->property_sensitive() = sensitive;
    icon_renderer_->render(cr, widget, icon_area, icon_area, flags);

    progress_renderer_->property_value() = (int)(percentDone * 100.0);
    progress_renderer_->property_text() = Glib::ustring();
    progress_renderer_->property_sensitive() = sensitive;
    progress_renderer_->render(cr, widget, prog_area, prog_area, flags);

    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_scale() = SMALL_SCALE;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_END;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->render(cr, widget, stat_area, stat_area, flags);

    text_renderer_->property_text() = name;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->render(cr, widget, name_area, name_area, flags);
}

void TorrentCellRenderer::Impl::render_full(
    Cairo::RefPtr<Cairo::Context> const& cr,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gtk::CellRendererState flags)
{
    int xpad;
    int ypad;
    Gtk::Requisition min_size;
    Gtk::Requisition size;
    bool seed;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);
    auto const* const inf = tr_torrentInfo(tor);
    bool const active = st->activity != TR_STATUS_STOPPED && st->activity != TR_STATUS_DOWNLOAD_WAIT &&
        st->activity != TR_STATUS_SEED_WAIT;
    auto const percentDone = get_percent_done(tor, st, &seed);
    bool const sensitive = active || st->error;

    auto const icon = get_icon(tor, FULL_ICON_SIZE, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_prog = getProgressString(tor, inf, st);
    auto const gstr_stat = getStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    renderer_.get_padding(xpad, ypad);
    auto const text_color = get_text_color(widget, st);

    /* get the idealized cell dimensions */
    Gdk::Rectangle icon_area;
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_size(widget, min_size, size);
    icon_area.set_width(size.width);
    icon_area.set_height(size.height);

    Gdk::Rectangle name_area;
    text_renderer_->property_text() = name;
    text_renderer_->property_weight() = Pango::WEIGHT_BOLD;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->get_preferred_size(widget, min_size, size);
    name_area.set_height(size.height);

    Gdk::Rectangle prog_area;
    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_weight() = Pango::WEIGHT_NORMAL;
    text_renderer_->property_scale() = SMALL_SCALE;
    text_renderer_->get_preferred_size(widget, min_size, size);
    prog_area.set_height(size.height);

    Gdk::Rectangle stat_area;
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->get_preferred_size(widget, min_size, size);
    stat_area.set_height(size.height);

    Gdk::Rectangle prct_area;

    /**
    *** LAYOUT
    **/

    auto fill_area = background_area;
    fill_area.set_x(fill_area.get_x() + xpad);
    fill_area.set_y(fill_area.get_y() + ypad);
    fill_area.set_width(fill_area.get_width() - xpad * 2);
    fill_area.set_height(fill_area.get_height() - ypad * 2);

    /* icon */
    icon_area.set_y(fill_area.get_y() + (fill_area.get_height() - icon_area.get_height()) / 2);

    /* name */
    name_area.set_y(fill_area.get_y());
    name_area.set_width(fill_area.get_width() - GUI_PAD - icon_area.get_width());

    if ((renderer_.get_state(widget, flags) & Gtk::StateFlags::STATE_FLAG_DIR_RTL) == 0)
    {
        icon_area.set_x(fill_area.get_x());
        name_area.set_x(fill_area.get_x() + fill_area.get_width() - name_area.get_width());
    }
    else
    {
        icon_area.set_x(fill_area.get_x() + fill_area.get_width() - icon_area.get_width());
        name_area.set_x(fill_area.get_x());
    }

    /* prog */
    prog_area.set_x(name_area.get_x());
    prog_area.set_y(name_area.get_y() + name_area.get_height());
    prog_area.set_width(name_area.get_width());

    /* progressbar */
    prct_area.set_x(prog_area.get_x());
    prct_area.set_y(prog_area.get_y() + prog_area.get_height() + GUI_PAD_SMALL);
    prct_area.set_width(prog_area.get_width());
    prct_area.set_height(bar_height.get_value());

    /* status */
    stat_area.set_x(prct_area.get_x());
    stat_area.set_y(prct_area.get_y() + prct_area.get_height() + GUI_PAD_SMALL);
    stat_area.set_width(prct_area.get_width());

    /**
    *** RENDER
    **/

    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->property_sensitive() = sensitive;
    icon_renderer_->render(cr, widget, icon_area, icon_area, flags);

    text_renderer_->property_text() = name;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_END;
    text_renderer_->property_weight() = Pango::WEIGHT_BOLD;
    text_renderer_->render(cr, widget, name_area, name_area, flags);

    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_scale() = SMALL_SCALE;
    text_renderer_->property_weight() = Pango::WEIGHT_NORMAL;
    text_renderer_->render(cr, widget, prog_area, prog_area, flags);

    progress_renderer_->property_value() = (int)(percentDone * 100.0);
    progress_renderer_->property_text() = Glib::ustring();
    progress_renderer_->property_sensitive() = sensitive;
    progress_renderer_->render(cr, widget, prct_area, prct_area, flags);

    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->render(cr, widget, stat_area, stat_area, flags);
}

void TorrentCellRenderer::render_vfunc(
    Cairo::RefPtr<Cairo::Context> const& cr,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gdk::Rectangle const& /*cell_area*/,
    Gtk::CellRendererState flags)
{
#ifdef TEST_RTL
    auto const real_dir = widget.get_direction();
    widget.set_direction(Gtk::TEXT_DIR_RTL);
#endif

    if (impl_->torrent.get_value() != nullptr)
    {
        if (impl_->compact.get_value())
        {
            impl_->render_compact(cr, widget, background_area, flags);
        }
        else
        {
            impl_->render_full(cr, widget, background_area, flags);
        }
    }

#ifdef TEST_RTL
    widget.set_direction(real_dir);
#endif
}

TorrentCellRenderer::Impl::~Impl()
{
    text_renderer_->unreference();
    progress_renderer_->unreference();
    icon_renderer_->unreference();
}

TorrentCellRenderer::TorrentCellRenderer()
    : Glib::ObjectBase(typeid(TorrentCellRenderer))
    , Gtk::CellRenderer()
    , impl_(std::make_unique<Impl>(*this))
{
}

TorrentCellRenderer::~TorrentCellRenderer() = default;

TorrentCellRenderer::Impl::Impl(TorrentCellRenderer& renderer)
    : torrent(renderer, "torrent", nullptr)
    , bar_height(renderer, "bar-height", DEFAULT_BAR_HEIGHT)
    , upload_speed_KBps(renderer, "piece-upload-speed", 0)
    , download_speed_KBps(renderer, "piece-download-speed", 0)
    , compact(renderer, "compact", false)
    , renderer_(renderer)
{
    text_renderer_ = Gtk::make_managed<Gtk::CellRendererText>();
    text_renderer_->property_xpad() = 0;
    text_renderer_->property_ypad() = 0;

    progress_renderer_ = Gtk::make_managed<Gtk::CellRendererProgress>();
    icon_renderer_ = Gtk::make_managed<Gtk::CellRendererPixbuf>();
}

Glib::PropertyProxy<void*> TorrentCellRenderer::property_torrent()
{
    return impl_->torrent.get_proxy();
}

Glib::PropertyProxy<double> TorrentCellRenderer::property_piece_upload_speed()
{
    return impl_->upload_speed_KBps.get_proxy();
}

Glib::PropertyProxy<double> TorrentCellRenderer::property_piece_download_speed()
{
    return impl_->download_speed_KBps.get_proxy();
}

Glib::PropertyProxy<int> TorrentCellRenderer::property_bar_height()
{
    return impl_->bar_height.get_proxy();
}

Glib::PropertyProxy<bool> TorrentCellRenderer::property_compact()
{
    return impl_->compact.get_proxy();
}
