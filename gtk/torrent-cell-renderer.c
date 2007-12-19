/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id:$
 */

#include "assert.h"
#include <gtk/gtk.h>
#include <gtk/gtkcellrenderertext.h>
#include <glib/gi18n.h>
#include <libtransmission/transmission.h>
#include "torrent-cell-renderer.h"
#include "tr_torrent.h"
#include "util.h"

enum
{
    P_TORRENT = 1,
    P_BAR_HEIGHT,
    P_MINIMAL,
    P_SHOW_UNAVAILABLE,
    P_GRADIENT,
    P_COLOR_VERIFIED,
    P_COLOR_VERIFIED_2,
    P_COLOR_MISSING,
    P_COLOR_MISSING_2,
    P_COLOR_UNWANTED,
    P_COLOR_UNWANTED_2,
    P_COLOR_UNAVAILABLE,
    P_COLOR_UNAVAILABLE_2,
    P_COLOR_PAUSED,
    P_COLOR_PAUSED_2,
    P_COLOR_VERIFYING,
    P_COLOR_VERIFYING_2,
    P_COLOR_SEEDING,
    P_COLOR_SEEDING_2
};

#define DEFAULT_BAR_HEIGHT 12

#define DEFAULT_COLOR_VERIFIED       "#77aaff"
#define DEFAULT_COLOR_VERIFIED_2     "#002277"
#define DEFAULT_COLOR_SEEDING        "#77ff88"
#define DEFAULT_COLOR_SEEDING_2      "#007700"
#define DEFAULT_COLOR_MISSING        "#fcfcfc" 
#define DEFAULT_COLOR_MISSING_2      "#acacac" 
#define DEFAULT_COLOR_UNWANTED       "#e0e0e0" 
#define DEFAULT_COLOR_UNWANTED_2     "#808080" 
#define DEFAULT_COLOR_UNAVAILABLE    "#ff7788"
#define DEFAULT_COLOR_UNAVAILABLE_2  "#770000"
#define DEFAULT_COLOR_PAUSED         "#959595"
#define DEFAULT_COLOR_PAUSED_2       "#555555"
#define DEFAULT_COLOR_VERIFYING      "#ffff77"
#define DEFAULT_COLOR_VERIFYING_2    "#777700"

/***
****
***/

static char*
getProgressString( const tr_torrent * tor, const tr_stat * torStat )
{
    const int allDownloaded = torStat->leftUntilDone == 0;
    const uint64_t haveTotal = torStat->haveUnchecked + torStat->haveValid;
    const tr_info * info = tr_torrentInfo( tor );
    const int isSeed = torStat->haveValid >= info->totalSize;
    char buf1[128], buf2[128], buf3[128];
    char * str;

    if( !allDownloaded )
        str = g_strdup_printf(
                  _("%s of %s (%.2f%%)"),
                  tr_strlsize( buf1, haveTotal, sizeof(buf1) ),
                  tr_strlsize( buf2, torStat->desiredSize, sizeof(buf2) ),
                  torStat->percentDone * 100.0 );
    else if( !isSeed )
        str = g_strdup_printf(
                  _("%s of %s (%.2f%%), uploaded %s (Ratio: %.2f"),
                  tr_strlsize( buf1, haveTotal, sizeof(buf1) ),
                  tr_strlsize( buf2, info->totalSize, sizeof(buf2) ),
                  torStat->percentComplete * 100.0,
                  tr_strlsize( buf3, torStat->uploadedEver, sizeof(buf3) ),
                               torStat->ratio * 100.0 );
    else
        str = g_strdup_printf(
                  _("%s, uploaded %s (Ratio: %.2f)"),
                  tr_strlsize( buf1, info->totalSize, sizeof(buf1) ),
                  tr_strlsize( buf2, torStat->uploadedEver, sizeof(buf2) ),
                  torStat->ratio * 100.0 );

    return str;
}

static char*
getStatusString( const tr_torrent * tor UNUSED, const tr_stat * torStat )
{
    const int isActive = torStat->status != TR_STATUS_STOPPED;
    const int isChecking = torStat->status == TR_STATUS_CHECK
                        || torStat->status == TR_STATUS_CHECK_WAIT;

    GString * gstr = g_string_new( NULL );

    if( torStat->error )
    {
        g_string_assign( gstr, torStat->errorString );
    }
    else switch( torStat->status )
    {
        case TR_STATUS_STOPPED:
            g_string_assign( gstr, _("Paused" ) );
            break;

        case TR_STATUS_CHECK_WAIT:
            g_string_assign( gstr, _( "Waiting to Verify local data" ) );
            break;

        case TR_STATUS_CHECK:
            g_string_append_printf( gstr, _("Verifying local data (%.1f%% tested)"),
                                    torStat->recheckProgress * 100.0 );
            break;

        case TR_STATUS_DOWNLOAD:
            g_string_append_printf( gstr,
                                    ngettext( _("Downloading from %d of %d connected peer" ),
                                              _("Downloading from %d of %d connected peers" ),
                                              torStat->peersConnected ),
                                    torStat->peersSendingToUs,
                                    torStat->peersConnected );
            break;

        case TR_STATUS_DONE:
        case TR_STATUS_SEED:
            g_string_append_printf( gstr,
                                    ngettext( _( "Seeding to %d of %d connected peer" ),
                                              _( "Seeding to %d of %d connected peers" ),
                                              torStat->peersGettingFromUs ),
                                    torStat->peersGettingFromUs,
                                    torStat->peersConnected );
            break;
    }

    if( isActive && !isChecking )
    {
        char ulbuf[64], dlbuf[64];

        if (torStat->status == TR_STATUS_DOWNLOAD)
            g_string_append_printf( gstr, _(" - DL: %s, UL: %s" ),
                                    tr_strlspeed( dlbuf, torStat->rateDownload, sizeof(dlbuf) ),
                                    tr_strlspeed( ulbuf, torStat->rateUpload, sizeof(ulbuf) ) );
        else
            g_string_append_printf( gstr, _(" - UL: %s" ),
                                    tr_strlspeed( ulbuf, torStat->rateUpload, sizeof(ulbuf) ) );
    }

    return g_string_free( gstr, FALSE );
}

/***
****
***/

static GtkCellRendererClass * parent_class = NULL;

struct TorrentCellRendererPrivate
{
    tr_torrent * tor;
    GtkCellRenderer * text_renderer;
    int bar_height;
    gboolean minimal;
    gboolean show_unavailable;
    gboolean gradient;
    GdkColor color_paused[2];
    GdkColor color_verified[2];
    GdkColor color_missing[2];
    GdkColor color_unwanted[2];
    GdkColor color_unavailable[2];
    GdkColor color_verifying[2];
    GdkColor color_seeding[2];
};

static void
torrent_cell_renderer_get_size( GtkCellRenderer  * cell,
                                GtkWidget        * widget,
                                GdkRectangle     * cell_area,
                                gint             * x_offset,
                                gint             * y_offset,
                                gint             * width,
                                gint             * height)
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( cell );
    int xpad, ypad;
    g_object_get( self, "xpad", &xpad, "ypad", &ypad, NULL );

    if( self && self->priv->tor )
    {
        const tr_torrent * tor = self->priv->tor;
        const tr_info * info = tr_torrentInfo( tor );
        const char * name = info->name;
        const tr_stat * torStat = tr_torrentStat( (tr_torrent*)tor );
        char * progressString = getProgressString( tor, torStat );
        char * statusString = getStatusString( tor, torStat );
        char * str;
        int tmp_w, tmp_h;
        int w=0, h=0;

        /* above the progressbar */
        str = g_markup_printf_escaped( "<b>%s</b>\n<small>%s</small>",
                                       name, progressString );
        g_object_set( self->priv->text_renderer, "markup", str, NULL );
        gtk_cell_renderer_get_size( self->priv->text_renderer,
                                    widget, NULL, NULL, NULL, &tmp_w, &tmp_h );
        h += tmp_h;
        w = MAX( w, tmp_w );
        g_free( str );

        /* below the progressbar */
        str = g_markup_printf_escaped( "<small>%s</small>", statusString );
        g_object_set( self->priv->text_renderer, "markup", str, NULL );
        gtk_cell_renderer_get_size( self->priv->text_renderer,
                                    widget, NULL, NULL, NULL, &tmp_w, &tmp_h );
        h += tmp_h;
        w = MAX( w, tmp_w );
        g_free( str );

        /* make the progressbar the same height as the below */
        h += self->priv->bar_height;

        if( cell_area ) {
            if( x_offset ) *x_offset = 0;
            if( y_offset ) {
                *y_offset = 0.5 * (cell_area->height - (h + (2 * ypad)));
                *y_offset = MAX( *y_offset, 0 );
            }
        }

        *width = w + xpad*2;
        *height = h + ypad*2;

        /* cleanup */
        g_free( statusString );
        g_free( progressString );
    }
}

static void
fillRect( TorrentCellRenderer * self,
          GdkGC               * gc,
          GdkDrawable         * drawable,
          const GdkRectangle  * area_in,
          const GdkColor      * colors,
          size_t                n_colors )
{
    const int drawGradient = self->priv->gradient && ( n_colors > 1 );
    assert( n_colors==1 || n_colors==2 );

    if( !drawGradient )
    {
        gdk_gc_set_rgb_fg_color( gc, colors );
        gdk_draw_rectangle( drawable, gc, TRUE,
                            area_in->x, area_in->y,
                            area_in->width, area_in->height );
    }
    else
    {
        int i;
        const int steps = area_in->height;
        const int step_height = area_in->height / steps;
        const int r_inc = ((int)colors[1].red   - (int)colors[0].red) / steps;
        const int g_inc = ((int)colors[1].green - (int)colors[0].green) / steps;
        const int b_inc = ((int)colors[1].blue  - (int)colors[0].blue) / steps;

        GdkRectangle area = *area_in;
        GdkColor color = colors[0];
     
        area.height = step_height;
        for( i=0; i<steps; ++i ) {
            gdk_gc_set_rgb_fg_color( gc, &color );
            gdk_draw_rectangle( drawable, gc, TRUE,
                                area.x, area.y, area.width, area.height );
            area.y += step_height;
            color.red   += r_inc;
            color.green += g_inc;
            color.blue  += b_inc;
        }
    }
}

static void
drawRegularBar( TorrentCellRenderer * self,
                const tr_info       * info,
                const tr_stat       * torStat,
                GdkDrawable         * drawable,
                GtkWidget           * widget,
                const GdkRectangle  * area )
{
#if 1
    const double verified = torStat->haveValid / (double)info->totalSize;
    const double unverified = torStat->haveUnchecked / (double)info->totalSize;
    const double unavailable = ( torStat->desiredSize - torStat->desiredAvailable ) / (double)info->totalSize;
    const double unwanted = ( info->totalSize - torStat->desiredSize ) / (double)info->totalSize;
#else /* for testing */
    const double verified = 0.5;
    const double unverified = 0.1;
    const double unavailable = 0.1;
    const double unwanted = 0.1;
#endif
    const double missing = 1.0 - verified - unverified - unavailable - unwanted;
    const int verifiedWidth = (int)( verified * area->width );
    const int unverifiedWidth = (int)( unverified * area->width );
    const int unavailableWidth = (int)( unavailable * area->width );
    const int unwantedWidth = (int)( unwanted * area->width );
    const int missingWidth = (int)( missing * area->width );

    const gboolean isActive = torStat->status == TR_STATUS_DOWNLOAD
                           || torStat->status == TR_STATUS_DONE
                           || torStat->status == TR_STATUS_SEED;
    const gboolean isChecking = torStat->status == TR_STATUS_CHECK
                             || torStat->status == TR_STATUS_CHECK_WAIT;

    int x = area->x;
    int w = 0;
    GdkGC * gc = gdk_gc_new( drawable );
    GdkRectangle rect = *area;

    if(( w = verifiedWidth )) {
        const GdkColor * colors;
        if( !isActive )
            colors = self->priv->color_paused;
        else if( torStat->status == TR_STATUS_DOWNLOAD )
            colors = self->priv->color_verified;
        else
            colors = self->priv->color_seeding;
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, colors, 2 );
        x += w;
    }

    if(( w = unverifiedWidth )) {
        const GdkColor * colors = isActive ? self->priv->color_verifying
                                           : self->priv->color_paused;
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, colors, 2 );
        x += w;
    }

    if(( w = missingWidth )) {
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, self->priv->color_missing, 2 );
        x += w;
    }

    if(( w = unwantedWidth )) {
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, self->priv->color_unwanted, 2 );
        x += w;
    }

    if(( w = unavailableWidth )) {
        const GdkColor * colors = isActive && self->priv->show_unavailable
            ? self->priv->color_unavailable
            : self->priv->color_missing;
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, colors, 2 );
        x += w;
    }

    if( isChecking ) {
        const int checkedWidth = torStat->recheckProgress * area->width;
        int h2 = area->height / 2;
        rect = *area;
        rect.y += h2;
        rect.height -= h2;
        fillRect( self, gc, drawable, &rect, self->priv->color_missing, 2 );
        rect.width = checkedWidth;
        fillRect( self, gc, drawable, &rect, self->priv->color_verifying, 2 );
    }

    gtk_paint_shadow( gtk_widget_get_style( widget ),
                      drawable,
                      GTK_STATE_NORMAL,
                      GTK_SHADOW_IN,
                      NULL,
                      widget,
                      NULL,
                      area->x, area->y, area->width, area->height );

    gdk_gc_unref( gc );
}

static void
torrent_cell_renderer_render( GtkCellRenderer      * cell,
                              GdkDrawable          * window,
                              GtkWidget            * widget,
                              GdkRectangle         * background_area,
                              GdkRectangle         * cell_area,
                              GdkRectangle         * expose_area,
                              GtkCellRendererState   flags)
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( cell );
    if( self && self->priv->tor )
    {
        const tr_torrent * tor = self->priv->tor;
        const tr_info * info = tr_torrentInfo( tor );
        const char * name = info->name;
        const tr_stat * torStat = tr_torrentStat( (tr_torrent*)tor );
        char * progressString = getProgressString( tor, torStat );
        char * statusString = getStatusString( tor, torStat );
        char * str;
        GdkRectangle my_bg;
        GdkRectangle my_cell;
        GdkRectangle my_expose;
        int xpad, ypad;
        int w, h;
        g_object_get( self, "xpad", &xpad, "ypad", &ypad, NULL );

        my_bg = *background_area; 
        my_bg.x += xpad;
        my_bg.y += ypad;
        my_bg.width -= xpad*2;
        my_cell = *cell_area;
        my_cell.x += xpad;
        my_cell.y += ypad;
        my_cell.width -= xpad*2;
        my_expose = *expose_area;
        my_expose.x += xpad;
        my_expose.y += ypad;
        my_expose.width -= xpad*2;

        /* above the progressbar */
        str = g_markup_printf_escaped( "<b>%s</b>\n<small>%s</small>",
                                       name, progressString );
        g_object_set( self->priv->text_renderer, "markup", str, NULL );
        gtk_cell_renderer_get_size( self->priv->text_renderer,
                                    widget, NULL, NULL, NULL, &w, &h );
        my_bg.height = h;
        my_cell.height = h;
        my_expose.height = h;
        gtk_cell_renderer_render( self->priv->text_renderer,
                                  window, widget,
                                  &my_bg, &my_cell, &my_expose, flags );
        my_bg.y += h;
        my_cell.y += h;
        my_expose.y += h;

        /* the progressbar */
        my_cell.height = self->priv->bar_height;
        drawRegularBar( self, info, torStat, window, widget, &my_cell );
        my_bg.y     += my_cell.height;
        my_cell.y   += my_cell.height;
        my_expose.y += my_cell.height;

        /* below progressbar */
        str = g_markup_printf_escaped( "<small>%s</small>", statusString );
        g_object_set( self->priv->text_renderer, "markup", str, NULL );
        gtk_cell_renderer_get_size( self->priv->text_renderer,
                                    widget, NULL, NULL, NULL, &w, &h );
        my_bg.height      = h;
        my_cell.height    = h;
        my_expose.height  = h;
        gtk_cell_renderer_render( self->priv->text_renderer,
                                  window, widget,
                                  &my_bg, &my_cell, &my_expose, flags );

        g_free( statusString );
        g_free( progressString );
    }
}

static void
setColor( GdkColor * color, const GValue * value )
{
    gdk_color_parse( g_value_get_string( value ), color );
}

static void
torrent_cell_renderer_set_property( GObject      * object,
                                    guint          property_id,
                                    const GValue * value,
                                    GParamSpec   * pspec)
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( object );
    struct TorrentCellRendererPrivate * p = self->priv;

    switch( property_id )
    {
        case P_TORRENT:             p->tor = g_value_get_pointer( value ); break;
        case P_BAR_HEIGHT:          p->bar_height = g_value_get_int( value ); break;
        case P_MINIMAL:             p->minimal          = g_value_get_boolean( value ); break;
        case P_GRADIENT:            p->gradient         = g_value_get_boolean( value ); break;
        case P_SHOW_UNAVAILABLE:    p->show_unavailable = g_value_get_boolean( value ); break;
        case P_COLOR_MISSING:       setColor( &p->color_missing[0],     value ); break;
        case P_COLOR_MISSING_2:     setColor( &p->color_missing[1],     value ); break;
        case P_COLOR_UNWANTED:      setColor( &p->color_unwanted[0],    value ); break;
        case P_COLOR_UNWANTED_2:    setColor( &p->color_unwanted[1],    value ); break;
        case P_COLOR_PAUSED:        setColor( &p->color_paused[0],      value ); break;
        case P_COLOR_PAUSED_2:      setColor( &p->color_paused[1],      value ); break;
        case P_COLOR_VERIFIED:      setColor( &p->color_verified[0],    value ); break;
        case P_COLOR_VERIFIED_2:    setColor( &p->color_verified[1],    value ); break;
        case P_COLOR_UNAVAILABLE:   setColor( &p->color_unavailable[0], value ); break;
        case P_COLOR_UNAVAILABLE_2: setColor( &p->color_unavailable[1], value ); break;
        case P_COLOR_VERIFYING:     setColor( &p->color_verifying[0],   value ); break;
        case P_COLOR_VERIFYING_2:   setColor( &p->color_verifying[1],   value ); break;
        case P_COLOR_SEEDING:       setColor( &p->color_seeding[0],     value ); break;
        case P_COLOR_SEEDING_2:     setColor( &p->color_seeding[1],     value ); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec ); break;
    }
}

static void
setValueColor( GValue * value, const GdkColor * color )
{
    char buf[16];
    g_snprintf( buf, sizeof(buf), "#%2.2x%2.2x%2.2x",
                (color->red >> 8) & 0xff,
                (color->green >> 8) & 0xff,
                (color->blue >> 8) & 0xff );
    g_value_set_string( value, buf );
}


static void
torrent_cell_renderer_get_property( GObject      * object,
                                    guint          property_id,
                                    GValue       * value,
                                    GParamSpec   * pspec)
{
    const TorrentCellRenderer * self = TORRENT_CELL_RENDERER( object );
    struct TorrentCellRendererPrivate * p = self->priv;

    switch( property_id )
    {
        case P_TORRENT:             g_value_set_pointer( value, p->tor ); break;
        case P_BAR_HEIGHT:          g_value_set_int( value, p->bar_height ); break;
        case P_MINIMAL:             g_value_set_boolean( value, p->minimal ); break;
        case P_GRADIENT:            g_value_set_boolean( value, p->gradient ); break;
        case P_SHOW_UNAVAILABLE:    g_value_set_boolean( value, p->show_unavailable ); break;
        case P_COLOR_MISSING:       setValueColor( value, &p->color_missing[0] ); break;
        case P_COLOR_MISSING_2:     setValueColor( value, &p->color_missing[1] ); break;
        case P_COLOR_UNWANTED:      setValueColor( value, &p->color_unwanted[0] ); break;
        case P_COLOR_UNWANTED_2:    setValueColor( value, &p->color_unwanted[1] ); break;
        case P_COLOR_PAUSED:        setValueColor( value, &p->color_paused[0] ); break;
        case P_COLOR_PAUSED_2:      setValueColor( value, &p->color_paused[1] ); break;
        case P_COLOR_VERIFIED:      setValueColor( value, &p->color_verified[0] ); break;
        case P_COLOR_VERIFIED_2:    setValueColor( value, &p->color_verified[1] ); break;
        case P_COLOR_UNAVAILABLE:   setValueColor( value, &p->color_unavailable[0] ); break;
        case P_COLOR_UNAVAILABLE_2: setValueColor( value, &p->color_unavailable[1] ); break;
        case P_COLOR_VERIFYING:     setValueColor( value, &p->color_verifying[0] ); break;
        case P_COLOR_VERIFYING_2:   setValueColor( value, &p->color_verifying[1] ); break;
        case P_COLOR_SEEDING:       setValueColor( value, &p->color_seeding[0] ); break;
        case P_COLOR_SEEDING_2:     setValueColor( value, &p->color_seeding[1] ); break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
torrent_cell_renderer_class_init( TorrentCellRendererClass * klass )
{
    GObjectClass * gobject_class = G_OBJECT_CLASS( klass );
    GtkCellRendererClass * cell_renderer_class = GTK_CELL_RENDERER_CLASS( klass );

    g_type_class_add_private( klass, sizeof(struct TorrentCellRendererPrivate) );

    parent_class = (GtkCellRendererClass*) g_type_class_peek_parent( klass );

    cell_renderer_class->render = torrent_cell_renderer_render;
    cell_renderer_class->get_size = torrent_cell_renderer_get_size;
    gobject_class->set_property = torrent_cell_renderer_set_property;
    gobject_class->get_property = torrent_cell_renderer_get_property;

    g_object_class_install_property( gobject_class, P_TORRENT,
        g_param_spec_pointer( "torrent", NULL, "tr_torrent*",
                              G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_BAR_HEIGHT,
        g_param_spec_int( "bar-height", NULL, "Bar Height",
                          1, INT_MAX, DEFAULT_BAR_HEIGHT, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_MINIMAL,
        g_param_spec_boolean( "minimal", NULL, "Minimal Mode",
                              FALSE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_GRADIENT,
        g_param_spec_boolean( "gradient", NULL, "Render Progress as a Gradient",
                              TRUE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_SHOW_UNAVAILABLE,
        g_param_spec_boolean( "unavailable", NULL, "Show Unavailable",
                              FALSE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_MISSING,
        g_param_spec_string( "missing-color", NULL, "Color for Missing Data",
                             DEFAULT_COLOR_MISSING, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_MISSING,
        g_param_spec_string( "missing-color-2", NULL, "Gradient Color for Missing Data",
                             DEFAULT_COLOR_MISSING_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNWANTED,
        g_param_spec_string( "unwanted-color", NULL, "Color for Unwanted Data",
                             DEFAULT_COLOR_UNWANTED, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNWANTED_2,
        g_param_spec_string( "unwanted-color-2", NULL, "Gradient Color for Unwanted Data",
                             DEFAULT_COLOR_UNWANTED_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_PAUSED,
        g_param_spec_string( "paused-color", NULL, "Color for Paused Data",
                             DEFAULT_COLOR_PAUSED, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_PAUSED_2,
        g_param_spec_string( "paused-color-2", NULL, "Gradient Color for Paused Data",
                             DEFAULT_COLOR_PAUSED_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFIED,
        g_param_spec_string( "verified-color", NULL, "Color for Verified Data",
                             DEFAULT_COLOR_VERIFIED, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFIED_2,
        g_param_spec_string( "verified-color-2", NULL, "Gradient Color for Verified Data",
                             DEFAULT_COLOR_VERIFIED_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNAVAILABLE,
        g_param_spec_string( "unavailable-color", NULL, "Color for Unavailable Data",
                             DEFAULT_COLOR_UNAVAILABLE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNAVAILABLE_2,
        g_param_spec_string( "unavailable-color-2", NULL, "Gradient Color for Unavailable Data",
                             DEFAULT_COLOR_UNAVAILABLE_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFYING,
        g_param_spec_string( "verifying-color", NULL, "Color for Verifying Data",
                             DEFAULT_COLOR_VERIFYING, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFYING_2,
        g_param_spec_string( "verifying-color-2", NULL, "Gradient Color for Verifying Data",
                             DEFAULT_COLOR_VERIFYING_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_SEEDING,
        g_param_spec_string( "seeding-color", NULL, "Color for Seeding Data",
                             DEFAULT_COLOR_SEEDING, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_SEEDING_2,
        g_param_spec_string( "seeding-color-2", NULL, "Second Color for Seeding Data",
                             DEFAULT_COLOR_SEEDING_2, G_PARAM_READWRITE ) );
}

static void
torrent_cell_renderer_init( GTypeInstance * instance, gpointer g_class UNUSED )
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( instance );
    struct TorrentCellRendererPrivate * p;
    
    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE( self,
                                                  TORRENT_CELL_RENDERER_TYPE,
                                                  struct TorrentCellRendererPrivate );

    p->tor = NULL;
    p->text_renderer = gtk_cell_renderer_text_new( );
    p->gradient = TRUE;
    p->show_unavailable = TRUE;
    p->bar_height = DEFAULT_BAR_HEIGHT;
    g_object_set( p->text_renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL );

    gdk_color_parse( DEFAULT_COLOR_VERIFIED,      &p->color_verified[0] );
    gdk_color_parse( DEFAULT_COLOR_VERIFIED_2,    &p->color_verified[1] );
    gdk_color_parse( DEFAULT_COLOR_MISSING,       &p->color_missing[0] );
    gdk_color_parse( DEFAULT_COLOR_MISSING_2,     &p->color_missing[1] );
    gdk_color_parse( DEFAULT_COLOR_UNWANTED,      &p->color_unwanted[0] );
    gdk_color_parse( DEFAULT_COLOR_UNWANTED_2,    &p->color_unwanted[1] );
    gdk_color_parse( DEFAULT_COLOR_UNAVAILABLE,   &p->color_unavailable[0] );
    gdk_color_parse( DEFAULT_COLOR_UNAVAILABLE_2, &p->color_unavailable[1] );
    gdk_color_parse( DEFAULT_COLOR_VERIFYING,     &p->color_verifying[0] );
    gdk_color_parse( DEFAULT_COLOR_VERIFYING_2,   &p->color_verifying[1] );
    gdk_color_parse( DEFAULT_COLOR_SEEDING,       &p->color_seeding[0] );
    gdk_color_parse( DEFAULT_COLOR_SEEDING_2,     &p->color_seeding[1] );
    gdk_color_parse( DEFAULT_COLOR_PAUSED,        &p->color_paused[0] );
    gdk_color_parse( DEFAULT_COLOR_PAUSED_2,      &p->color_paused[1] );
}

GType
torrent_cell_renderer_get_type( void )
{
    static GType type = 0;

    if( !type )
    {
        static const GTypeInfo info =
        {
            sizeof( TorrentCellRendererClass ),
            NULL, /* base_init */
            NULL, /* base_finalize */
            (GClassInitFunc)torrent_cell_renderer_class_init,
            NULL, /* class_finalize */
            NULL, /* class_data */
            sizeof( TorrentCellRenderer ),
            0, /* n_preallocs */
            (GInstanceInitFunc)torrent_cell_renderer_init,
            NULL
        };

        type = g_type_register_static( GTK_TYPE_CELL_RENDERER,
                                       "TorrentCellRenderer",
                                       &info, (GTypeFlags)0 );
    }

    return type;
}

GtkCellRenderer *
torrent_cell_renderer_new( void )
{
    return (GtkCellRenderer *) g_object_new( TORRENT_CELL_RENDERER_TYPE, NULL );
}
