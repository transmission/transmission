/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libtransmission/transmission.h>
#include "torrent-inspector.h"
#include "img_icon_full.h"

#define UNUSED G_GNUC_UNUSED

extern void doAction (const char * action_name, gpointer user_data );

static GtkActionGroup * myGroup = 0;

static void action_cb ( GtkAction * a, gpointer user_data )
{
  doAction ( gtk_action_get_name(a), user_data );
}

#if !GTK_CHECK_VERSION(2,8,0)
#define GTK_STOCK_INFO GTK_STOCK_PROPERTIES
#endif

#if !GTK_CHECK_VERSION(2,10,0)
#define GTK_STOCK_SELECT_ALL NULL
#endif

static GtkRadioActionEntry priority_toggle_entries[] =
{
  { "priority-high", NULL, N_("_High"), NULL, NULL, TR_PRI_HIGH },
  { "priority-normal", NULL, N_("_Normal"), NULL, NULL, TR_PRI_NORMAL },
  { "priority-low", NULL, N_("_Low"), NULL, NULL, TR_PRI_LOW }
};

extern void set_selected_file_priority ( tr_priority_t ); 

static void
priority_changed_cb (GtkAction *action UNUSED, GtkRadioAction *current)
{
  const int priority = gtk_radio_action_get_current_value (current);
  set_selected_file_priority ( priority );
}

static GtkToggleActionEntry show_toggle_entries[] = 
{
  { "toggle-main-window", NULL, 
    N_("Show _Main Window"), NULL, NULL, G_CALLBACK(action_cb), TRUE }, 
  { "toggle-debug-window", NULL,
    N_("Show _Debug Window"), NULL, NULL, G_CALLBACK(action_cb), FALSE }
};

static GtkActionEntry entries[] =
{
  { "file-menu", NULL, N_("_File"), NULL, NULL, NULL },
  { "edit-menu", NULL, N_("_Edit"), NULL, NULL, NULL },
  { "help-menu", NULL, N_("_Help"), NULL, NULL, NULL },
  { "priority-menu", NULL, N_("_Priority"), NULL, NULL, NULL },
  { "add-torrent", GTK_STOCK_OPEN, NULL, NULL,  NULL, G_CALLBACK(action_cb) },
  { "start-torrent", GTK_STOCK_MEDIA_PLAY,
    N_("_Start"), "<control>S", NULL, G_CALLBACK(action_cb) },
  { "recheck-torrent", GTK_STOCK_REFRESH,
    N_("Re_check"), NULL, NULL, G_CALLBACK(action_cb) },
  { "stop-torrent", GTK_STOCK_MEDIA_PAUSE,
    N_("_Pause"), "<control>P", NULL, G_CALLBACK(action_cb) },
  { "remove-torrent", GTK_STOCK_REMOVE,
    N_("_Remove"), "<control>R", NULL, G_CALLBACK(action_cb) },
  { "create-torrent", GTK_STOCK_NEW,
    N_("_Create New Torrent"), NULL, NULL, G_CALLBACK(action_cb) },
  { "close", GTK_STOCK_CLOSE,
    N_("_Close"), "<control>C", NULL, G_CALLBACK(action_cb) },
  { "quit", GTK_STOCK_QUIT,
    N_("_Quit"), "<control>Q", NULL, G_CALLBACK(action_cb) },
  { "select-all", GTK_STOCK_SELECT_ALL,
    N_("Select _All"), "<control>A", NULL, G_CALLBACK(action_cb) },
  { "unselect-all", NULL,
    N_("_Deselect All"), "<control>U", NULL, G_CALLBACK(action_cb) },
  { "edit-preferences", GTK_STOCK_PREFERENCES,
    N_("Edit _Preferences"), NULL, NULL, G_CALLBACK(action_cb) },
  { "show-torrent-inspector", GTK_STOCK_INFO,
    N_("_Torrent Info"), NULL, NULL, G_CALLBACK(action_cb) },
  { "show-about-dialog", GTK_STOCK_ABOUT,
    N_("_About Transmission"), NULL, NULL, G_CALLBACK(action_cb) },
  { "update-tracker", GTK_STOCK_REFRESH,
    N_("Update Tracker"), NULL, NULL, G_CALLBACK(action_cb) }
};

static void
ensure_tooltip (GtkActionEntry * e)
{
    if( !e->tooltip && e->label )
    {
        const char * src;
        char *tgt;
        e->tooltip = g_malloc( strlen( e->label ) + 1 );
        for( src=e->label, tgt=(char*)e->tooltip; *src; ++src )
            if( *src != '_' )
                *tgt++ = *src;
        *tgt++ = '\0';
    }
}

typedef struct
{
   const guint8* raw;
   const char * name;
}
BuiltinIconInfo;

/* only one icon now... but room to grow ;) */
const BuiltinIconInfo my_builtin_icons [] =
{
    { tr_icon_full, "transmission-logo" }
};

static void
register_my_icons ( void )
{
   int i;
   const int n = G_N_ELEMENTS( my_builtin_icons );
   GtkIconFactory * factory = gtk_icon_factory_new ();
   gtk_icon_factory_add_default( factory );

   for( i=0; i<n; ++i )
   {
       GdkPixbuf * p;
       int width;
       GtkIconSet * icon_set;

       p = gdk_pixbuf_new_from_inline( -1, my_builtin_icons[i].raw, FALSE, 0 );
       width = gdk_pixbuf_get_width( p );
       gtk_icon_theme_add_builtin_icon (my_builtin_icons[i].name, width, p );

       icon_set = gtk_icon_set_new_from_pixbuf( p );
       gtk_icon_factory_add( factory, my_builtin_icons[i].name, icon_set );

       g_object_unref( p );
       gdk_pixbuf_unref( p );
       gtk_icon_set_unref (icon_set);
    }

    g_object_unref (G_OBJECT (factory));
}

static GtkUIManager * myUIManager = NULL;

void
actions_init( GtkUIManager * ui_manager, gpointer callback_user_data )
{
  int i;
  const int n_entries = G_N_ELEMENTS( entries );
  GtkActionGroup * action_group;

  myUIManager = ui_manager;

  register_my_icons ();

  for( i=0; i<n_entries; ++i )
    ensure_tooltip (&entries[i]);

  action_group = myGroup = gtk_action_group_new( "Actions" );
  gtk_action_group_set_translation_domain( action_group, NULL );

  gtk_action_group_add_radio_actions( action_group,
                                      priority_toggle_entries,
                                      G_N_ELEMENTS(priority_toggle_entries),
                                      TR_PRI_NORMAL,
                                      G_CALLBACK(priority_changed_cb), NULL);

  gtk_action_group_add_toggle_actions ( action_group, 
					show_toggle_entries, 
					G_N_ELEMENTS(show_toggle_entries), 
					callback_user_data );

  gtk_action_group_add_actions( action_group,
                                entries, n_entries,
                                callback_user_data );

  gtk_ui_manager_insert_action_group( ui_manager, action_group, 0 );
  g_object_unref (G_OBJECT(action_group));
}

/****
*****
****/

static GHashTable * key_to_action = NULL;

static void
ensure_action_map_loaded (GtkUIManager * uim)
{
    GList * l;

    if ( key_to_action != NULL )
        return;

    key_to_action =
        g_hash_table_new_full( g_str_hash, g_str_equal, g_free, NULL);

    for( l=gtk_ui_manager_get_action_groups(uim); l!=NULL; l=l->next )
    {
        GtkActionGroup * action_group = GTK_ACTION_GROUP( l->data );
        GList *ait, *actions = gtk_action_group_list_actions( action_group );
        for( ait=actions; ait!=NULL; ait=ait->next )
        {
            GtkAction * action = GTK_ACTION( ait->data );
            const char * name = gtk_action_get_name( action );
            g_hash_table_insert( key_to_action, g_strdup(name), action );
        }
        g_list_free( actions );
    }
}

static GtkAction*
get_action( const char* name )
{
    ensure_action_map_loaded( myUIManager );
    return ( GtkAction* ) g_hash_table_lookup( key_to_action, name );
}

void
action_activate ( const char * name )
{
    GtkAction * action = get_action( name );
    g_assert( action != NULL );
    gtk_action_activate( action );
}

void
action_sensitize( const char * name, gboolean b )
{
    GtkAction * action = get_action( name );
    g_assert( action != NULL );
    g_object_set( action, "sensitive", b, NULL );
}

void
action_toggle( const char * name, gboolean b )
{
    GtkAction * action = get_action( name );
    gtk_toggle_action_set_active( GTK_TOGGLE_ACTION(action), b );
}

GtkWidget*
action_get_widget( const char * path )
{
    return gtk_ui_manager_get_widget( myUIManager, path );
}
