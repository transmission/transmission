/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "tr_icon.h"
#include "util.h"

enum
{
    PROP_ICON = 1,
    PROP_DOCKED,
    PROP_CLICK,
};

static void
tr_icon_init( GTypeInstance * instance, gpointer g_class );
static void
tr_icon_set_property( GObject * object, guint property_id,
                      const GValue * value, GParamSpec * pspec );
static void
tr_icon_get_property( GObject * object, guint property_id,
                      GValue * value, GParamSpec * pspec);
static void
tr_icon_class_init( gpointer g_class, gpointer g_class_data );
static void
tr_icon_dispose( GObject * obj );
static void
tr_icon_finalize( GObject * obj );
#ifdef TR_ICON_SUPPORTED
static void
clicked( GObject * obj, gpointer data );
#endif

GType
tr_icon_get_type( void )
{
    static GType type = 0;

    if( 0 == type )
    {
        static const GTypeInfo info =
        {
            sizeof( TrIconClass ),
            NULL,                       /* base_init */
            NULL,                       /* base_finalize */
            tr_icon_class_init,         /* class_init */
            NULL,                       /* class_finalize */
            NULL,                       /* class_data */
            sizeof( TrIcon ),
            0,                          /* n_preallocs */
            tr_icon_init,               /* instance_init */
            NULL,
        };
#ifdef TR_ICON_SUPPORTED
        type = GTK_TYPE_STATUS_ICON;
#else
        type = G_TYPE_OBJECT;
#endif
        type = g_type_register_static( type, "TrIcon", &info, 0 );
    }

    return type;
}

static void
tr_icon_class_init( gpointer g_class, gpointer g_class_data SHUTUP )
{
    GObjectClass * gobject_class;
    TrIconClass  * tricon_class;
    GParamSpec   * pspec;

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->set_property = tr_icon_set_property;
    gobject_class->get_property = tr_icon_get_property;
    gobject_class->dispose      = tr_icon_dispose;
    gobject_class->finalize     = tr_icon_finalize;

    pspec = g_param_spec_boolean( "icon", _("Icon"),
                                 _("Icon has been set from default window icon."),
                                  TRUE, G_PARAM_CONSTRUCT|G_PARAM_READWRITE );
    g_object_class_install_property( gobject_class, PROP_ICON, pspec );

    pspec = g_param_spec_boolean( "docked", _("Docked"),
                                 _("Icon is docked in a system tray."),
                                  FALSE, G_PARAM_READABLE );
    g_object_class_install_property( gobject_class, PROP_DOCKED, pspec );

    pspec = g_param_spec_int( "activate-action", _("Activate action"),
                              _("The action id to signal when icon is activated."),
                              G_MININT, G_MAXINT, -1, G_PARAM_READWRITE );
    g_object_class_install_property( gobject_class, PROP_CLICK, pspec );

    tricon_class = TR_ICON_CLASS( g_class );
    tricon_class->actionsig =
        g_signal_new( "action", G_TYPE_FROM_CLASS( g_class ),
                       G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                       g_cclosure_marshal_VOID__INT,
                       G_TYPE_NONE, 1, G_TYPE_INT );
}

static void
tr_icon_init( GTypeInstance * instance, gpointer g_class SHUTUP )
{
    TrIcon * self = ( TrIcon * )instance;

    self->clickact  = -1;
    self->disposed  = FALSE;

#ifdef TR_ICON_SUPPORTED
    g_signal_connect( self, "activate", G_CALLBACK( clicked ), NULL );
#endif
}

static void
tr_icon_set_property( GObject * object, guint property_id,
                      const GValue * value, GParamSpec * pspec)
{
    TrIcon * self = ( TrIcon * )object;

    if( self->disposed )
    {
        return;
    }

    switch( property_id )
    {
        case PROP_ICON:
#ifdef TR_ICON_SUPPORTED
            if( g_value_get_boolean( value ) )
            {
                GList  * icons = gtk_window_get_default_icon_list();
                if( NULL != icons && NULL != icons->data )
                {
                    gtk_status_icon_set_from_pixbuf( GTK_STATUS_ICON( self ),
                                                     icons->data );
                }
                g_list_free( icons );
            }
            else
            {
                gtk_status_icon_set_from_pixbuf( GTK_STATUS_ICON( self ),
                                                 NULL );
            }
#endif
            break;
        case PROP_CLICK:
            self->clickact = g_value_get_int( value );
            break;
        case PROP_DOCKED:
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
tr_icon_get_property( GObject * object, guint property_id,
                      GValue * value, GParamSpec * pspec )
{
    TrIcon * self = ( TrIcon * )object;
#ifdef TR_ICON_SUPPORTED
    GtkStatusIcon * icon;
#endif

    if( self->disposed )
    {
        return;
    }

    switch( property_id )
    {
        case PROP_ICON:
#ifdef TR_ICON_SUPPORTED
            icon = GTK_STATUS_ICON( self );
            if( GTK_IMAGE_PIXBUF == gtk_status_icon_get_storage_type( icon ) &&
                NULL != gtk_status_icon_get_pixbuf( icon ) )
            {
                g_value_set_boolean( value, TRUE );
            }
            else
#endif
            {
                g_value_set_boolean( value, FALSE );
            }
            break;
        case PROP_DOCKED:
#ifdef TR_ICON_SUPPORTED
            if( gtk_status_icon_is_embedded( GTK_STATUS_ICON( self ) ) )
            {
                g_value_set_boolean( value, TRUE );
            }
            else
#endif
            {
                g_value_set_boolean( value, FALSE );
            }
            break;
        case PROP_CLICK:
            g_value_set_int( value, self->clickact );
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
tr_icon_dispose( GObject * obj )
{
    TrIcon       * self = ( TrIcon * )obj;
    GObjectClass * parent;

    if( self->disposed )
    {
        return;
    }
    self->disposed = TRUE;

    /* Chain up to the parent class */
    parent = g_type_class_peek( g_type_parent( TR_ICON_TYPE ) );
    parent->dispose( obj );
}

static void
tr_icon_finalize( GObject * obj )
{
    GObjectClass * parent;

    /* Chain up to the parent class */
    parent = g_type_class_peek( g_type_parent( TR_ICON_TYPE ) );
    parent->finalize( obj );
}

TrIcon *
tr_icon_new( void )
{
    return g_object_new( TR_ICON_TYPE, NULL );
}

gboolean
tr_icon_docked( TrIcon * self )
{
    gboolean ret;

    g_object_get( self, "docked", &ret, NULL );

    return ret;
}

#ifdef TR_ICON_SUPPORTED

static void
clicked( GObject * obj, gpointer data SHUTUP )
{
    TrIcon      * self;
    TrIconClass * class;

    TR_IS_ICON( obj );
    self = TR_ICON( obj );

    if( self->disposed || 0 > self->clickact )
    {
        return;
    }

    class = g_type_class_peek( TR_ICON_TYPE );
    g_signal_emit( self, class->actionsig, 0, self->clickact );
}

#endif /* TR_ICON_SUPPORTED */
