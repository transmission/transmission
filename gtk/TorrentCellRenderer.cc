// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "TorrentCellRenderer.h"

#include "HigWorkarea.h" // GUI_PAD, GUI_PAD_SMALL
#include "Percents.h"
#include "Torrent.h"

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_truncd() */

#include <cairomm/context.h>
#include <cairomm/refptr.h>
#include <cairomm/surface.h>
#include <gdkmm/rectangle.h>
#include <gdkmm/rgba.h>
#include <giomm/icon.h>
#include <glibmm.h>
#include <glibmm/i18n.h>
#include <glibmm/property.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/cellrendererprogress.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/requisition.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/snapshot.h>
#endif

#include <fmt/core.h>

#include <algorithm> // std::max()
#include <cstring> // strchr()
#include <memory>
#include <optional>
#include <string>
#include <string_view>

/* #define TEST_RTL */

/***
****
***/

namespace
{

auto const DefaultBarHeight = 12;
auto const CompactBarWidth = 50;
auto const SmallScale = 0.9;
auto const CompactIconSize = IF_GTKMM4(Gtk::IconSize::NORMAL, Gtk::ICON_SIZE_MENU);
auto const FullIconSize = IF_GTKMM4(Gtk::IconSize::LARGE, Gtk::ICON_SIZE_DND);

auto get_height(Gtk::Requisition const& req)
{
    return req.IF_GTKMM4(get_height(), height);
}

auto get_width(Gtk::Requisition const& req)
{
    return req.IF_GTKMM4(get_width(), width);
}

} // namespace

/***
****
***/

class TorrentCellRenderer::Impl
{
    using SnapshotPtr = TorrentCellRenderer::SnapshotPtr;
    using IconSize = IF_GTKMM4(Gtk::IconSize, Gtk::BuiltinIconSize);

public:
    explicit Impl(TorrentCellRenderer& renderer);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

    Gtk::Requisition get_size_compact(Gtk::Widget& widget) const;
    Gtk::Requisition get_size_full(Gtk::Widget& widget) const;

    void render_compact(
        SnapshotPtr const& snapshot,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gtk::CellRendererState flags);
    void render_full(
        SnapshotPtr const& snapshot,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gtk::CellRendererState flags);

    auto& property_torrent()
    {
        return property_torrent_;
    }

    auto& property_bar_height()
    {
        return property_bar_height_;
    }

    auto& property_compact()
    {
        return property_compact_;
    }

private:
    void render_progress_bar(
        SnapshotPtr const& snapshot,
        Gtk::Widget& widget,
        Gdk::Rectangle const& area,
        Gtk::CellRendererState flags,
        Gdk::RGBA const& color);

    static void set_icon(Gtk::CellRendererPixbuf& renderer, Glib::RefPtr<Gio::Icon> const& icon, IconSize icon_size);
    static void adjust_progress_bar_hue(
        Cairo::RefPtr<Cairo::Context> const& context,
        Gdk::RGBA const& color,
        Gdk::Rectangle const& area);

private:
    TorrentCellRenderer& renderer_;

    Glib::Property<Torrent*> property_torrent_;
    Glib::Property<int> property_bar_height_;
    Glib::Property<bool> property_compact_;

    Gtk::CellRendererText* text_renderer_ = nullptr;
    Gtk::CellRendererProgress* progress_renderer_ = nullptr;
    Gtk::CellRendererPixbuf* icon_renderer_ = nullptr;
};

/***
****
***/

void TorrentCellRenderer::Impl::set_icon(
    Gtk::CellRendererPixbuf& renderer,
    Glib::RefPtr<Gio::Icon> const& icon,
    IconSize icon_size)
{
    renderer.property_gicon() = icon;
#if GTKMM_CHECK_VERSION(4, 0, 0)
    renderer.property_icon_size() = icon_size;
#else
    renderer.property_stock_size() = icon_size;
#endif
}

Gtk::Requisition TorrentCellRenderer::Impl::get_size_compact(Gtk::Widget& widget) const
{
    int xpad = 0;
    int ypad = 0;
    Gtk::Requisition min_size;
    Gtk::Requisition icon_size;
    Gtk::Requisition name_size;
    Gtk::Requisition stat_size;

    auto const& torrent = *property_torrent_.get_value();

    auto const icon = torrent.get_icon();
    auto const name = torrent.get_name();
    auto const gstr_stat = torrent.get_short_status_text();
    renderer_.get_padding(xpad, ypad);

    /* get the idealized cell dimensions */
    set_icon(*icon_renderer_, icon, CompactIconSize);
    icon_renderer_->get_preferred_size(widget, min_size, icon_size);
    text_renderer_->property_text() = name;
    text_renderer_->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(NONE);
    text_renderer_->property_scale() = 1.0;
    text_renderer_->get_preferred_size(widget, min_size, name_size);
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_size(widget, min_size, stat_size);

    /**
    *** LAYOUT
    **/

    return { xpad * 2 + get_width(icon_size) + GUI_PAD + CompactBarWidth + GUI_PAD + get_width(stat_size),
             ypad * 2 + std::max(get_height(name_size), property_bar_height_.get_value()) };
}

Gtk::Requisition TorrentCellRenderer::Impl::get_size_full(Gtk::Widget& widget) const
{
    int xpad = 0;
    int ypad = 0;
    Gtk::Requisition min_size;
    Gtk::Requisition icon_size;
    Gtk::Requisition name_size;
    Gtk::Requisition stat_size;
    Gtk::Requisition prog_size;

    auto const& torrent = *property_torrent_.get_value();

    auto const icon = torrent.get_icon();
    auto const name = torrent.get_name();
    auto const gstr_stat = torrent.get_long_status_text();
    auto const gstr_prog = torrent.get_long_progress_text();
    renderer_.get_padding(xpad, ypad);

    /* get the idealized cell dimensions */
    set_icon(*icon_renderer_, icon, FullIconSize);
    icon_renderer_->get_preferred_size(widget, min_size, icon_size);
    text_renderer_->property_text() = name;
    text_renderer_->property_weight() = TR_PANGO_WEIGHT(BOLD);
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(NONE);
    text_renderer_->get_preferred_size(widget, min_size, name_size);
    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_weight() = TR_PANGO_WEIGHT(NORMAL);
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_size(widget, min_size, prog_size);
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->get_preferred_size(widget, min_size, stat_size);

    /**
    *** LAYOUT
    **/

    return { xpad * 2 + get_width(icon_size) + GUI_PAD + std::max(get_width(prog_size), get_width(stat_size)),
             ypad * 2 + get_height(name_size) + get_height(prog_size) + GUI_PAD_SMALL + property_bar_height_.get_value() +
                 GUI_PAD_SMALL + get_height(stat_size) };
}

void TorrentCellRenderer::get_preferred_width_vfunc(Gtk::Widget& widget, int& minimum_width, int& natural_width) const
{
    if (impl_->property_torrent().get_value() != nullptr)
    {
        auto const size = impl_->property_compact().get_value() ? impl_->get_size_compact(widget) :
                                                                  impl_->get_size_full(widget);

        minimum_width = get_width(size);
        natural_width = minimum_width;
    }
}

void TorrentCellRenderer::get_preferred_height_vfunc(Gtk::Widget& widget, int& minimum_height, int& natural_height) const
{
    if (impl_->property_torrent().get_value() != nullptr)
    {
        auto const size = impl_->property_compact().get_value() ? impl_->get_size_compact(widget) :
                                                                  impl_->get_size_full(widget);

        minimum_height = get_height(size);
        natural_height = minimum_height;
    }
}

namespace
{

Gdk::RGBA const& get_progress_bar_color(Torrent const& torrent)
{
    static auto const steelblue_color = Gdk::RGBA("steelblue");
    static auto const forestgreen_color = Gdk::RGBA("forestgreen");
    static auto const silver_color = Gdk::RGBA("silver");

    switch (torrent.get_activity())
    {
    case TR_STATUS_DOWNLOAD:
        return steelblue_color;
    case TR_STATUS_SEED:
        return forestgreen_color;
    default:
        return silver_color;
    }
}

Cairo::RefPtr<Cairo::Surface> get_mask_surface(Cairo::RefPtr<Cairo::Surface> const& surface, Gdk::Rectangle const& area)
{
    auto const mask_surface = Cairo::Surface::create(surface, Cairo::CONTENT_ALPHA, area.get_width(), area.get_height());
    auto const mask_context = Cairo::Context::create(mask_surface);

    mask_context->set_source_rgb(0, 0, 0);
    mask_context->rectangle(area.get_x(), area.get_y(), area.get_width(), area.get_height());
    mask_context->fill();

    mask_context->set_operator(TR_CAIRO_CONTEXT_OPERATOR(CLEAR));
    mask_context->mask(surface, area.get_x(), area.get_y());
    mask_context->fill();

    return mask_surface;
}

template<typename... Ts>
void render_impl(Gtk::CellRenderer& renderer, Ts&&... args)
{
    renderer.IF_GTKMM4(snapshot, render)(std::forward<Ts>(args)...);
}

} // namespace

void TorrentCellRenderer::Impl::adjust_progress_bar_hue(
    Cairo::RefPtr<Cairo::Context> const& context,
    Gdk::RGBA const& color,
    Gdk::Rectangle const& area)
{
    using TrCairoContextOperator = IF_GTKMM4(Cairo::Context::Operator, Cairo::Operator);

    auto const mask_surface = get_mask_surface(context->get_target(), area);

    // Adjust surface color
    context->set_source_rgb(color.get_red(), color.get_green(), color.get_blue());
    context->set_operator(static_cast<TrCairoContextOperator>(CAIRO_OPERATOR_HSL_COLOR));
    context->rectangle(area.get_x(), area.get_y(), area.get_width(), area.get_height());
    context->fill();

    // Clear out masked (fully transparent) areas
    context->set_operator(TR_CAIRO_CONTEXT_OPERATOR(CLEAR));
    context->mask(mask_surface, area.get_x(), area.get_y());
    context->fill();
}

void TorrentCellRenderer::Impl::render_progress_bar(
    SnapshotPtr const& snapshot,
    Gtk::Widget& widget,
    Gdk::Rectangle const& area,
    Gtk::CellRendererState flags,
    Gdk::RGBA const& color)
{
    auto const context = IF_GTKMM4(snapshot->append_cairo(area), snapshot);

    auto const temp_area = Gdk::Rectangle(0, 0, area.get_width(), area.get_height());
    auto const temp_surface = Cairo::Surface::create(
        context->get_target(),
        Cairo::CONTENT_COLOR_ALPHA,
        area.get_width(),
        area.get_height());
    auto const temp_context = Cairo::Context::create(temp_surface);

    {
#if GTKMM_CHECK_VERSION(4, 0, 0)
        auto const temp_snapshot = Gtk::Snapshot::create();
#endif

        render_impl(*progress_renderer_, IF_GTKMM4(temp_snapshot, temp_context), widget, temp_area, temp_area, flags);

#if GTKMM_CHECK_VERSION(4, 0, 0)
        temp_snapshot->reference();
        auto const render_node = std::unique_ptr<GskRenderNode, void (*)(GskRenderNode*)>(
            gtk_snapshot_free_to_node(Glib::unwrap(temp_snapshot)),
            [](GskRenderNode* p) { gsk_render_node_unref(p); });
        gsk_render_node_draw(render_node.get(), temp_context->cobj());
#endif
    }

    adjust_progress_bar_hue(temp_context, color, temp_area);

    context->set_source(temp_context->get_target(), area.get_x(), area.get_y());
    context->rectangle(area.get_x(), area.get_y(), area.get_width(), area.get_height());
    context->fill();
}

void TorrentCellRenderer::Impl::render_compact(
    SnapshotPtr const& snapshot,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gtk::CellRendererState flags)
{
    int xpad = 0;
    int ypad = 0;
    int min_width = 0;
    int width = 0;

    auto const& torrent = *property_torrent_.get_value();
    auto const percent_done = torrent.get_percent_done().to_int();
    bool const sensitive = torrent.get_sensitive();

    if (torrent.get_error_code() != 0 && (flags & TR_GTK_CELL_RENDERER_STATE(SELECTED)) == Gtk::CellRendererState{})
    {
        text_renderer_->property_foreground() = "red";
    }
    else
    {
        text_renderer_->property_foreground_set() = false;
    }

    auto const icon = torrent.get_icon();
    auto const name = torrent.get_name();
    auto const& progress_color = get_progress_bar_color(torrent);
    auto const gstr_stat = torrent.get_short_status_text();
    renderer_.get_padding(xpad, ypad);

    auto fill_area = background_area;
    fill_area.set_x(fill_area.get_x() + xpad);
    fill_area.set_y(fill_area.get_y() + ypad);
    fill_area.set_width(fill_area.get_width() - xpad * 2);
    fill_area.set_height(fill_area.get_height() - ypad * 2);

    auto icon_area = fill_area;
    set_icon(*icon_renderer_, icon, CompactIconSize);
    icon_renderer_->get_preferred_width(widget, min_width, width);
    icon_area.set_width(width);

    auto prog_area = fill_area;
    prog_area.set_width(CompactBarWidth);

    auto stat_area = fill_area;
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(NONE);
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_width(widget, min_width, width);
    stat_area.set_width(width);

    auto name_area = fill_area;
    name_area.set_width(
        fill_area.get_width() - icon_area.get_width() - stat_area.get_width() - prog_area.get_width() - GUI_PAD * 3);

    if ((renderer_.get_state(widget, flags) & TR_GTK_STATE_FLAGS(DIR_RTL)) == Gtk::StateFlags{})
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

    set_icon(*icon_renderer_, icon, CompactIconSize);
    icon_renderer_->property_sensitive() = sensitive;
    render_impl(*icon_renderer_, snapshot, widget, icon_area, icon_area, flags);

    progress_renderer_->property_value() = percent_done;
    progress_renderer_->property_text() = fmt::format(FMT_STRING("{:d}%"), percent_done);
    progress_renderer_->property_sensitive() = sensitive;
    render_progress_bar(snapshot, widget, prog_area, flags, progress_color);

    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
    text_renderer_->property_sensitive() = sensitive;
    render_impl(*text_renderer_, snapshot, widget, stat_area, stat_area, flags);

    text_renderer_->property_text() = name;
    text_renderer_->property_scale() = 1.0;
    render_impl(*text_renderer_, snapshot, widget, name_area, name_area, flags);
}

void TorrentCellRenderer::Impl::render_full(
    SnapshotPtr const& snapshot,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gtk::CellRendererState flags)
{
    int xpad = 0;
    int ypad = 0;
    Gtk::Requisition min_size;
    Gtk::Requisition size;

    auto const& torrent = *property_torrent_.get_value();
    auto const percent_done = torrent.get_percent_done().to_int();
    bool const sensitive = torrent.get_sensitive();

    if (torrent.get_error_code() != 0 && (flags & TR_GTK_CELL_RENDERER_STATE(SELECTED)) == Gtk::CellRendererState{})
    {
        text_renderer_->property_foreground() = "red";
    }
    else
    {
        text_renderer_->property_foreground_set() = false;
    }

    auto const icon = torrent.get_icon();
    auto const name = torrent.get_name();
    auto const& progress_color = get_progress_bar_color(torrent);
    auto const gstr_prog = torrent.get_long_progress_text();
    auto const gstr_stat = torrent.get_long_status_text();
    renderer_.get_padding(xpad, ypad);

    /* get the idealized cell dimensions */
    Gdk::Rectangle icon_area;
    set_icon(*icon_renderer_, icon, FullIconSize);
    icon_renderer_->get_preferred_size(widget, min_size, size);
    icon_area.set_width(get_width(size));
    icon_area.set_height(get_height(size));

    Gdk::Rectangle name_area;
    text_renderer_->property_text() = name;
    text_renderer_->property_weight() = TR_PANGO_WEIGHT(BOLD);
    text_renderer_->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(NONE);
    text_renderer_->property_scale() = 1.0;
    text_renderer_->get_preferred_size(widget, min_size, size);
    name_area.set_height(get_height(size));

    Gdk::Rectangle prog_area;
    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_weight() = TR_PANGO_WEIGHT(NORMAL);
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_size(widget, min_size, size);
    prog_area.set_height(get_height(size));

    Gdk::Rectangle stat_area;
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->get_preferred_size(widget, min_size, size);
    stat_area.set_height(get_height(size));

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

    if ((renderer_.get_state(widget, flags) & TR_GTK_STATE_FLAGS(DIR_RTL)) == Gtk::StateFlags{})
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
    prct_area.set_height(property_bar_height_.get_value());

    /* status */
    stat_area.set_x(prct_area.get_x());
    stat_area.set_y(prct_area.get_y() + prct_area.get_height() + GUI_PAD_SMALL);
    stat_area.set_width(prct_area.get_width());

    /**
    *** RENDER
    **/

    set_icon(*icon_renderer_, icon, FullIconSize);
    icon_renderer_->property_sensitive() = sensitive;
    render_impl(*icon_renderer_, snapshot, widget, icon_area, icon_area, flags);

    text_renderer_->property_text() = name;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
    text_renderer_->property_weight() = TR_PANGO_WEIGHT(BOLD);
    text_renderer_->property_sensitive() = sensitive;
    render_impl(*text_renderer_, snapshot, widget, name_area, name_area, flags);

    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->property_weight() = TR_PANGO_WEIGHT(NORMAL);
    render_impl(*text_renderer_, snapshot, widget, prog_area, prog_area, flags);

    progress_renderer_->property_value() = percent_done;
    progress_renderer_->property_text() = Glib::ustring();
    progress_renderer_->property_sensitive() = sensitive;
    render_progress_bar(snapshot, widget, prct_area, flags, progress_color);

    text_renderer_->property_text() = gstr_stat;
    render_impl(*text_renderer_, snapshot, widget, stat_area, stat_area, flags);
}

void TorrentCellRenderer::IF_GTKMM4(snapshot_vfunc, render_vfunc)(
    SnapshotPtr const& snapshot,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gdk::Rectangle const& /*cell_area*/,
    Gtk::CellRendererState flags)
{
#ifdef TEST_RTL
    auto const real_dir = widget.get_direction();
    widget.set_direction(Gtk::TEXT_DIR_RTL);
#endif

    if (impl_->property_torrent().get_value() != nullptr)
    {
        if (impl_->property_compact().get_value())
        {
            impl_->render_compact(snapshot, widget, background_area, flags);
        }
        else
        {
            impl_->render_full(snapshot, widget, background_area, flags);
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
    , impl_(std::make_unique<Impl>(*this))
{
}

TorrentCellRenderer::~TorrentCellRenderer() = default;

TorrentCellRenderer::Impl::Impl(TorrentCellRenderer& renderer)
    : renderer_(renderer)
    , property_torrent_(renderer, "torrent", nullptr)
    , property_bar_height_(renderer, "bar-height", DefaultBarHeight)
    , property_compact_(renderer, "compact", false)
    , text_renderer_(Gtk::make_managed<Gtk::CellRendererText>())
    , progress_renderer_(Gtk::make_managed<Gtk::CellRendererProgress>())
    , icon_renderer_(Gtk::make_managed<Gtk::CellRendererPixbuf>())
{
    renderer_.property_xpad() = GUI_PAD_SMALL;
    renderer_.property_ypad() = GUI_PAD_SMALL;

    text_renderer_->property_xpad() = 0;
    text_renderer_->property_ypad() = 0;
}

Glib::PropertyProxy<Torrent*> TorrentCellRenderer::property_torrent()
{
    return impl_->property_torrent().get_proxy();
}

Glib::PropertyProxy<int> TorrentCellRenderer::property_bar_height()
{
    return impl_->property_bar_height().get_proxy();
}

Glib::PropertyProxy<bool> TorrentCellRenderer::property_compact()
{
    return impl_->property_compact().get_proxy();
}
