/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <limits.h> /* INT_MAX */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "file-list.h"
#include "hig.h"
#include "icons.h"
#include "tr-prefs.h"
#include "util.h"

#define TR_DOWNLOAD_KEY "tr-download-key"
#define TR_COLUMN_ID_KEY "tr-model-column-id-key"
#define TR_PRIORITY_KEY "tr-priority-key"

enum
{
    /* these two fields could be any number at all so long as they're not
     * TR_PRI_LOW, TR_PRI_NORMAL, TR_PRI_HIGH, TRUE, or FALSE */
    NOT_SET = 1000,
    MIXED = 1001
};

enum
{
    FC_ICON,
    FC_LABEL,
    FC_LABEL_ESC,
    FC_PROG,
    FC_INDEX,
    FC_SIZE,
    FC_SIZE_STR,
    FC_HAVE,
    FC_PRIORITY,
    FC_ENABLED,
    N_FILE_COLS
};

typedef struct
{
    TrCore* core;
    GtkWidget* top;
    GtkWidget* view;
    GtkTreeModel* model; /* same object as store, but recast */
    GtkTreeStore* store; /* same object as model, but recast */
    int torrentId;
    guint timeout_tag;
}
FileData;

static void clearData(FileData* data)
{
    data->torrentId = -1;

    if (data->timeout_tag != 0)
    {
        g_source_remove(data->timeout_tag);
        data->timeout_tag = 0;
    }
}

static void freeData(gpointer data)
{
    clearData(data);
    g_free(data);
}

/***
****
***/

struct RefreshData
{
    int sort_column_id;
    gboolean resort_needed;

    tr_file_stat* refresh_file_stat;
    tr_torrent* tor;

    FileData* file_data;
};

static gboolean refreshFilesForeach(GtkTreeModel* model, GtkTreePath* path UNUSED, GtkTreeIter* iter, gpointer gdata)
{
    struct RefreshData* refresh_data = gdata;
    FileData* data = refresh_data->file_data;
    unsigned int index;
    uint64_t size;
    uint64_t old_have;
    int old_prog;
    int old_priority;
    int old_enabled;
    gboolean const is_file = !gtk_tree_model_iter_has_child(model, iter);

    gtk_tree_model_get(model, iter,
        FC_ENABLED, &old_enabled,
        FC_PRIORITY, &old_priority,
        FC_INDEX, &index,
        FC_HAVE, &old_have,
        FC_SIZE, &size,
        FC_PROG, &old_prog,
        -1);

    if (is_file)
    {
        tr_torrent* tor = refresh_data->tor;
        tr_info const* inf = tr_torrentInfo(tor);
        int const enabled = inf->files[index].dnd ? 0 : 1;
        int const priority = inf->files[index].priority;
        uint64_t const have = refresh_data->refresh_file_stat[index].bytesCompleted;
        int const prog = size != 0 ? (int)(100.0 * have / size) : 1;

        if (priority != old_priority || enabled != old_enabled || have != old_have || prog != old_prog)
        {
            /* Changing a value in the sort column can trigger a resort
             * which breaks this foreach () call. (See #3529)
             * As a workaround: if that's about to happen, temporarily disable
             * sorting until we finish walking the tree. */
            if (!refresh_data->resort_needed)
            {
                if ((refresh_data->resort_needed = (refresh_data->sort_column_id == FC_PRIORITY && priority != old_priority) ||
                    (refresh_data->sort_column_id == FC_ENABLED && enabled != old_enabled)))
                {
                    refresh_data->resort_needed = TRUE;
                    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(data->model),
                        GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
                }
            }

            gtk_tree_store_set(data->store, iter,
                FC_PRIORITY, priority,
                FC_ENABLED, enabled,
                FC_HAVE, have,
                FC_PROG, prog,
                -1);
        }
    }
    else
    {
        GtkTreeIter child;
        uint64_t sub_size = 0;
        uint64_t have = 0;
        int prog;
        int enabled = NOT_SET;
        int priority = NOT_SET;

        /* since gtk_tree_model_foreach() is depth-first, we can
         * get the `sub' info by walking the immediate children */

        if (gtk_tree_model_iter_children(model, &child, iter))
        {
            do
            {
                int child_enabled;
                int child_priority;
                int64_t child_have;
                int64_t child_size;

                gtk_tree_model_get(model, &child,
                    FC_SIZE, &child_size,
                    FC_HAVE, &child_have,
                    FC_PRIORITY, &child_priority,
                    FC_ENABLED, &child_enabled,
                    -1);

                if ((child_enabled != FALSE) && (child_enabled != NOT_SET))
                {
                    sub_size += child_size;
                    have += child_have;
                }

                if (enabled == NOT_SET)
                {
                    enabled = child_enabled;
                }
                else if (enabled != child_enabled)
                {
                    enabled = MIXED;
                }

                if (priority == NOT_SET)
                {
                    priority = child_priority;
                }
                else if (priority != child_priority)
                {
                    priority = MIXED;
                }
            }
            while (gtk_tree_model_iter_next(model, &child));
        }

        prog = sub_size != 0 ? (int)(100.0 * have / sub_size) : 1;

        if (size != sub_size || have != old_have || priority != old_priority || enabled != old_enabled || prog != old_prog)
        {
            char size_str[64];
            tr_strlsize(size_str, sub_size, sizeof(size_str));
            gtk_tree_store_set(data->store, iter,
                FC_SIZE, sub_size,
                FC_SIZE_STR, size_str,
                FC_HAVE, have,
                FC_PRIORITY, priority,
                FC_ENABLED, enabled,
                FC_PROG, prog,
                -1);
        }
    }

    return FALSE; /* keep walking */
}

static void gtr_tree_model_foreach_postorder_subtree(GtkTreeModel* model, GtkTreeIter* parent, GtkTreeModelForeachFunc func,
    gpointer data)
{
    GtkTreeIter child;

    if (gtk_tree_model_iter_children(model, &child, parent))
    {
        do
        {
            gtr_tree_model_foreach_postorder_subtree(model, &child, func, data);
        }
        while (gtk_tree_model_iter_next(model, &child));
    }

    if (parent != NULL)
    {
        func(model, NULL, parent, data);
    }
}

static void gtr_tree_model_foreach_postorder(GtkTreeModel* model, GtkTreeModelForeachFunc func, gpointer data)
{
    GtkTreeIter iter;

    if (gtk_tree_model_iter_nth_child(model, &iter, NULL, 0))
    {
        do
        {
            gtr_tree_model_foreach_postorder_subtree(model, &iter, func, data);
        }
        while (gtk_tree_model_iter_next(model, &iter));
    }
}

static void refresh(FileData* data)
{
    tr_torrent* tor = gtr_core_find_torrent(data->core, data->torrentId);

    if (tor == NULL)
    {
        gtr_file_list_clear(data->top);
    }
    else
    {
        GtkSortType order;
        int sort_column_id;
        tr_file_index_t fileCount;
        struct RefreshData refresh_data;
        GtkTreeSortable* sortable = GTK_TREE_SORTABLE(data->model);
        gtk_tree_sortable_get_sort_column_id(sortable, &sort_column_id, &order);

        refresh_data.sort_column_id = sort_column_id;
        refresh_data.resort_needed = FALSE;
        refresh_data.refresh_file_stat = tr_torrentFiles(tor, &fileCount);
        refresh_data.tor = tor;
        refresh_data.file_data = data;

        gtr_tree_model_foreach_postorder(data->model, refreshFilesForeach, &refresh_data);

        if (refresh_data.resort_needed)
        {
            gtk_tree_sortable_set_sort_column_id(sortable, sort_column_id, order);
        }

        tr_torrentFilesFree(refresh_data.refresh_file_stat, fileCount);
    }
}

static gboolean refreshModel(gpointer file_data)
{
    refresh(file_data);

    return G_SOURCE_CONTINUE;
}

/***
****
***/

struct ActiveData
{
    GtkTreeSelection* sel;
    tr_file_index_t* indexBuf;
    size_t indexCount;
};

static gboolean getSelectedFilesForeach(GtkTreeModel* model, GtkTreePath* path UNUSED, GtkTreeIter* iter, gpointer gdata)
{
    gboolean const is_file = !gtk_tree_model_iter_has_child(model, iter);

    if (is_file)
    {
        struct ActiveData* data = gdata;

        /* active means: if it's selected or any ancestor is selected */
        gboolean is_active = gtk_tree_selection_iter_is_selected(data->sel, iter);

        if (!is_active)
        {
            GtkTreeIter walk = *iter;
            GtkTreeIter parent;

            while (!is_active && gtk_tree_model_iter_parent(model, &parent, &walk))
            {
                is_active = gtk_tree_selection_iter_is_selected(data->sel, &parent);
                walk = parent;
            }
        }

        if (is_active)
        {
            unsigned int i;
            gtk_tree_model_get(model, iter, FC_INDEX, &i, -1);
            data->indexBuf[data->indexCount++] = i;
        }
    }

    return FALSE; /* keep walking */
}

static size_t getSelectedFilesAndDescendants(GtkTreeView* view, tr_file_index_t* indexBuf)
{
    struct ActiveData data;

    data.sel = gtk_tree_view_get_selection(view);
    data.indexBuf = indexBuf;
    data.indexCount = 0;
    gtk_tree_model_foreach(gtk_tree_view_get_model(view), getSelectedFilesForeach, &data);
    return data.indexCount;
}

struct SubtreeForeachData
{
    GtkTreePath* path;
    tr_file_index_t* indexBuf;
    size_t indexCount;
};

static gboolean getSubtreeForeach(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter, gpointer gdata)
{
    gboolean const is_file = !gtk_tree_model_iter_has_child(model, iter);

    if (is_file)
    {
        struct SubtreeForeachData* data = gdata;

        if (gtk_tree_path_compare(path, data->path) == 0 || gtk_tree_path_is_descendant(path, data->path))
        {
            unsigned int i;
            gtk_tree_model_get(model, iter, FC_INDEX, &i, -1);
            data->indexBuf[data->indexCount++] = i;
        }
    }

    return FALSE; /* keep walking */
}

static size_t getSubtree(GtkTreeView* view, GtkTreePath* path, tr_file_index_t* indexBuf)
{
    struct SubtreeForeachData tmp;
    tmp.indexBuf = indexBuf;
    tmp.indexCount = 0;
    tmp.path = path;
    gtk_tree_model_foreach(gtk_tree_view_get_model(view), getSubtreeForeach, &tmp);
    return tmp.indexCount;
}

/* if `path' is a selected row, all selected rows are returned.
 * otherwise, only the row indicated by `path' is returned.
 * this is for toggling all the selected rows' states in a batch.
 *
 * indexBuf should be large enough to hold tr_inf.fileCount files.
 */
static size_t getActiveFilesForPath(GtkTreeView* view, GtkTreePath* path, tr_file_index_t* indexBuf)
{
    size_t indexCount;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(view);

    if (gtk_tree_selection_path_is_selected(sel, path))
    {
        /* clicked in a selected row... use the current selection */
        indexCount = getSelectedFilesAndDescendants(view, indexBuf);
    }
    else
    {
        /* clicked OUTSIDE of the selected row... just use the clicked row */
        indexCount = getSubtree(view, path, indexBuf);
    }

    return indexCount;
}

/***
****
***/

void gtr_file_list_clear(GtkWidget* w)
{
    gtr_file_list_set_torrent(w, -1);
}

struct build_data
{
    GtkWidget* w;
    tr_torrent* tor;
    GtkTreeIter* iter;
    GtkTreeStore* store;
};

struct row_struct
{
    uint64_t length;
    char* name;
    int index;
};

static void buildTree(GNode* node, gpointer gdata)
{
    char size_str[64];
    GtkTreeIter child_iter;
    struct build_data* build = gdata;
    struct row_struct* child_data = node->data;
    gboolean const isLeaf = node->children == NULL;

    char const* mime_type = isLeaf ? gtr_get_mime_type_from_filename(child_data->name) : DIRECTORY_MIME_TYPE;
    GdkPixbuf* icon = gtr_get_mime_type_icon(mime_type, GTK_ICON_SIZE_MENU, build->w);
    tr_info const* inf = tr_torrentInfo(build->tor);
    int const priority = isLeaf ? inf->files[child_data->index].priority : 0;
    gboolean const enabled = isLeaf ? !inf->files[child_data->index].dnd : TRUE;
    char* name_esc = g_markup_escape_text(child_data->name, -1);

    tr_strlsize(size_str, child_data->length, sizeof(size_str));

    gtk_tree_store_insert_with_values(build->store, &child_iter, build->iter, INT_MAX,
        FC_INDEX, child_data->index,
        FC_LABEL, child_data->name,
        FC_LABEL_ESC, name_esc,
        FC_SIZE, child_data->length,
        FC_SIZE_STR, size_str,
        FC_ICON, icon,
        FC_PRIORITY, priority,
        FC_ENABLED, enabled,
        -1);

    if (!isLeaf)
    {
        struct build_data b = *build;
        b.iter = &child_iter;
        g_node_children_foreach(node, G_TRAVERSE_ALL, buildTree, &b);
    }

    g_free(name_esc);
    g_object_unref(icon);

    /* we're done with this node */
    g_free(child_data->name);
    g_free(child_data);
}

static GNode* find_child(GNode* parent, char const* name)
{
    GNode* child = parent->children;

    while (child != NULL)
    {
        struct row_struct const* child_data = child->data;

        if (*child_data->name == *name && g_strcmp0(child_data->name, name) == 0)
        {
            break;
        }

        child = child->next;
    }

    return child;
}

void gtr_file_list_set_torrent(GtkWidget* w, int torrentId)
{
    GtkTreeStore* store;
    FileData* data = g_object_get_data(G_OBJECT(w), "file-data");

    /* unset the old fields */
    clearData(data);

    /* instantiate the model */
    store = gtk_tree_store_new(N_FILE_COLS,
        GDK_TYPE_PIXBUF, /* icon */
        G_TYPE_STRING, /* label */
        G_TYPE_STRING, /* label esc */
        G_TYPE_INT, /* prog [0..100] */
        G_TYPE_UINT, /* index */
        G_TYPE_UINT64, /* size */
        G_TYPE_STRING, /* size str */
        G_TYPE_UINT64, /* have */
        G_TYPE_INT, /* priority */
        G_TYPE_INT); /* dl enabled */

    data->store = store;
    data->model = GTK_TREE_MODEL(store);
    data->torrentId = torrentId;

    /* populate the model */
    if (torrentId > 0)
    {
        tr_torrent* tor = gtr_core_find_torrent(data->core, torrentId);

        if (tor != NULL)
        {
            tr_info const* inf = tr_torrentInfo(tor);
            struct row_struct* root_data;
            GNode* root;
            struct build_data build;

            /* build a GNode tree of the files */
            root_data = g_new0(struct row_struct, 1);
            root_data->name = g_strdup(tr_torrentName(tor));
            root_data->index = -1;
            root_data->length = 0;
            root = g_node_new(root_data);

            for (tr_file_index_t i = 0; i < inf->fileCount; ++i)
            {
                GNode* parent = root;
                tr_file const* file = &inf->files[i];
                char** tokens = g_strsplit(file->name, G_DIR_SEPARATOR_S, 0);

                for (int j = 0; tokens[j] != NULL; ++j)
                {
                    gboolean const isLeaf = tokens[j + 1] == NULL;
                    char const* name = tokens[j];
                    GNode* node = find_child(parent, name);

                    if (node == NULL)
                    {
                        struct row_struct* row = g_new(struct row_struct, 1);
                        row->name = g_strdup(name);
                        row->index = isLeaf ? (int)i : -1;
                        row->length = isLeaf ? file->length : 0;
                        node = g_node_new(row);
                        g_node_append(parent, node);
                    }

                    parent = node;
                }

                g_strfreev(tokens);
            }

            /* now, add them to the model */
            build.w = w;
            build.tor = tor;
            build.store = data->store;
            build.iter = NULL;
            g_node_children_foreach(root, G_TRAVERSE_ALL, buildTree, &build);

            /* cleanup */
            g_node_destroy(root);
            g_free(root_data->name);
            g_free(root_data);
        }

        refresh(data);
        data->timeout_tag = gdk_threads_add_timeout_seconds(SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS, refreshModel, data);
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(data->view), data->model);

    /* set default sort by label */
    gtk_tree_sortable_set_sort_column_id(data->model, FC_LABEL, GTK_SORT_ASCENDING);

    gtk_tree_view_expand_all(GTK_TREE_VIEW(data->view));
    g_object_unref(data->model);
}

/***
****
***/

static void renderDownload(GtkTreeViewColumn* column UNUSED, GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* iter,
    gpointer data UNUSED)
{
    gboolean enabled;
    gtk_tree_model_get(model, iter, FC_ENABLED, &enabled, -1);
    g_object_set(renderer, "inconsistent", enabled == MIXED, "active", enabled == TRUE, NULL);
}

static void renderPriority(GtkTreeViewColumn* column UNUSED, GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* iter,
    gpointer data UNUSED)
{
    int priority;
    char const* text;
    gtk_tree_model_get(model, iter, FC_PRIORITY, &priority, -1);

    switch (priority)
    {
    case TR_PRI_HIGH:
        text = _("High");
        break;

    case TR_PRI_NORMAL:
        text = _("Normal");
        break;

    case TR_PRI_LOW:
        text = _("Low");
        break;

    default:
        text = _("Mixed");
        break;
    }

    g_object_set(renderer, "text", text, NULL);
}

/* build a filename from tr_torrentGetCurrentDir() + the model's FC_LABELs */
static char* buildFilename(tr_torrent* tor, GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter)
{
    char* ret;
    GtkTreeIter child;
    GtkTreeIter parent = *iter;
    int n = gtk_tree_path_get_depth(path);
    char** tokens = g_new0(char*, n + 2);
    tokens[0] = g_strdup(tr_torrentGetCurrentDir(tor));

    do
    {
        child = parent;
        gtk_tree_model_get(model, &child, FC_LABEL, &tokens[n--], -1);
    }
    while (gtk_tree_model_iter_parent(model, &parent, &child));

    ret = g_build_filenamev(tokens);
    g_strfreev(tokens);
    return ret;
}

static gboolean onRowActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* col UNUSED, gpointer gdata)
{
    gboolean handled = FALSE;
    FileData* data = gdata;
    tr_torrent* tor = gtr_core_find_torrent(data->core, data->torrentId);

    if (tor != NULL)
    {
        GtkTreeIter iter;
        GtkTreeModel* model = gtk_tree_view_get_model(view);

        if (gtk_tree_model_get_iter(model, &iter, path))
        {
            int prog;
            char* filename = buildFilename(tor, model, path, &iter);
            gtk_tree_model_get(model, &iter, FC_PROG, &prog, -1);

            /* if the file's not done, walk up the directory tree until we find
             * an ancestor that exists, and open that instead */
            if (filename != NULL && (prog < 100 || !g_file_test(filename, G_FILE_TEST_EXISTS)))
            {
                do
                {
                    char* tmp = g_path_get_dirname(filename);
                    g_free(filename);
                    filename = tmp;
                }
                while (!tr_str_is_empty(filename) && !g_file_test(filename, G_FILE_TEST_EXISTS));
            }

            if ((handled = !tr_str_is_empty(filename)))
            {
                gtr_open_file(filename);
            }
        }
    }

    return handled;
}

static gboolean onViewPathToggled(GtkTreeView* view, GtkTreeViewColumn* col, GtkTreePath* path, FileData* data)
{
    int cid;
    tr_torrent* tor;
    gboolean handled = FALSE;

    if (col == NULL || path == NULL)
    {
        return FALSE;
    }

    cid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(col), TR_COLUMN_ID_KEY));
    tor = gtr_core_find_torrent(data->core, data->torrentId);

    if (tor != NULL && (cid == FC_PRIORITY || cid == FC_ENABLED))
    {
        GtkTreeIter iter;
        tr_file_index_t* const indexBuf = g_new0(tr_file_index_t, tr_torrentInfo(tor)->fileCount);
        size_t const indexCount = getActiveFilesForPath(view, path, indexBuf);
        GtkTreeModel* model = data->model;

        gtk_tree_model_get_iter(model, &iter, path);

        if (cid == FC_PRIORITY)
        {
            int priority;
            gtk_tree_model_get(model, &iter, FC_PRIORITY, &priority, -1);

            switch (priority)
            {
            case TR_PRI_NORMAL:
                priority = TR_PRI_HIGH;
                break;

            case TR_PRI_HIGH:
                priority = TR_PRI_LOW;
                break;

            default:
                priority = TR_PRI_NORMAL;
                break;
            }

            tr_torrentSetFilePriorities(tor, indexBuf, indexCount, priority);
        }
        else
        {
            int enabled;
            gtk_tree_model_get(model, &iter, FC_ENABLED, &enabled, -1);
            enabled = !enabled;

            tr_torrentSetFileDLs(tor, indexBuf, indexCount, enabled);
        }

        refresh(data);
        g_free(indexBuf);
        handled = TRUE;
    }

    return handled;
}

/**
 * @note 'col' and 'path' are assumed not to be NULL.
 */
static gboolean getAndSelectEventPath(GtkTreeView* treeview, GdkEventButton* event, GtkTreeViewColumn** col, GtkTreePath** path)
{
    GtkTreeSelection* sel;

    if (gtk_tree_view_get_path_at_pos(treeview, event->x, event->y, path, col, NULL, NULL))
    {
        sel = gtk_tree_view_get_selection(treeview);

        if (!gtk_tree_selection_path_is_selected(sel, *path))
        {
            gtk_tree_selection_unselect_all(sel);
            gtk_tree_selection_select_path(sel, *path);
        }

        return TRUE;
    }

    return FALSE;
}

static gboolean onViewButtonPressed(GtkWidget* w, GdkEventButton* event, gpointer gdata)
{
    GtkTreeViewColumn* col;
    GtkTreePath* path = NULL;
    gboolean handled = FALSE;
    GtkTreeView* treeview = GTK_TREE_VIEW(w);
    FileData* data = gdata;

    if (event->type == GDK_BUTTON_PRESS && event->button == 1 && (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) == 0 &&
        getAndSelectEventPath(treeview, event, &col, &path))
    {
        handled = onViewPathToggled(treeview, col, path, data);

        if (path != NULL)
        {
            gtk_tree_path_free(path);
        }
    }

    return handled;
}

struct rename_data
{
    int error;
    char* newname;
    char* path_string;
    FileData* file_data;
};

static int on_rename_done_idle(struct rename_data* data)
{
    if (data->error == 0)
    {
        GtkTreeIter iter;

        if (gtk_tree_model_get_iter_from_string(data->file_data->model, &iter, data->path_string))
        {
            gboolean const isLeaf = !gtk_tree_model_iter_has_child(data->file_data->model, &iter);
            char const* mime_type = isLeaf ? gtr_get_mime_type_from_filename(data->newname) : DIRECTORY_MIME_TYPE;
            GdkPixbuf* icon = gtr_get_mime_type_icon(mime_type, GTK_ICON_SIZE_MENU, data->file_data->view);

            gtk_tree_store_set(data->file_data->store, &iter, FC_LABEL, data->newname, FC_ICON, icon, -1);

            GtkTreeIter parent;

            if (!gtk_tree_model_iter_parent(data->file_data->model, &parent, &iter))
            {
                gtr_core_torrent_changed(data->file_data->core, data->file_data->torrentId);
            }
        }
    }
    else
    {
        GtkWidget* w = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(data->file_data->top)), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, _("Unable to rename file as \"%s\": %s"), data->newname,
            tr_strerror(data->error));
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(w), "%s", _("Please correct the errors and try again."));
        gtk_dialog_run(GTK_DIALOG(w));
        gtk_widget_destroy(w);
    }

    /* cleanup */
    g_free(data->path_string);
    g_free(data->newname);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void on_rename_done(tr_torrent* tor G_GNUC_UNUSED, char const* oldpath G_GNUC_UNUSED, char const* newname G_GNUC_UNUSED,
    int error, struct rename_data* rename_data)
{
    rename_data->error = error;
    gdk_threads_add_idle((GSourceFunc)on_rename_done_idle, rename_data);
}

static void cell_edited_callback(GtkCellRendererText* cell G_GNUC_UNUSED, gchar* path_string, gchar* newname, FileData* data)
{
    tr_torrent* tor;
    GString* oldpath;
    GtkTreeIter iter;
    struct rename_data* rename_data;

    tor = gtr_core_find_torrent(data->core, data->torrentId);

    if (tor == NULL)
    {
        return;
    }

    if (!gtk_tree_model_get_iter_from_string(data->model, &iter, path_string))
    {
        return;
    }

    /* build oldpath */
    oldpath = g_string_new(NULL);

    for (;;)
    {
        char* token = NULL;
        GtkTreeIter child;
        gtk_tree_model_get(data->model, &iter, FC_LABEL, &token, -1);
        g_string_prepend(oldpath, token);
        g_free(token);

        child = iter;

        if (!gtk_tree_model_iter_parent(data->model, &iter, &child))
        {
            break;
        }

        g_string_prepend_c(oldpath, G_DIR_SEPARATOR);
    }

    /* do the renaming */
    rename_data = g_new0(struct rename_data, 1);
    rename_data->newname = g_strdup(newname);
    rename_data->file_data = data;
    rename_data->path_string = g_strdup(path_string);
    tr_torrentRenamePath(tor, oldpath->str, newname, (tr_torrent_rename_done_func)on_rename_done, rename_data);

    /* cleanup */
    g_string_free(oldpath, TRUE);
}

GtkWidget* gtr_file_list_new(TrCore* core, int torrentId)
{
    int size;
    int width;
    GtkWidget* ret;
    GtkWidget* view;
    GtkWidget* scroll;
    GtkCellRenderer* rend;
    GtkTreeSelection* sel;
    GtkTreeViewColumn* col;
    GtkTreeView* tree_view;
    char const* title;
    PangoLayout* pango_layout;
    PangoContext* pango_context;
    PangoFontDescription* pango_font_description;
    FileData* data = g_new0(FileData, 1);

    data->core = core;

    /* create the view */
    view = gtk_tree_view_new();
    tree_view = GTK_TREE_VIEW(view);
    gtk_container_set_border_width(GTK_CONTAINER(view), GUI_PAD_BIG);
    g_signal_connect(view, "button-press-event", G_CALLBACK(onViewButtonPressed), data);
    g_signal_connect(view, "row_activated", G_CALLBACK(onRowActivated), data);
    g_signal_connect(view, "button-release-event", G_CALLBACK(on_tree_view_button_released), NULL);

    pango_context = gtk_widget_create_pango_context(view);
    pango_font_description = pango_font_description_copy(pango_context_get_font_description(pango_context));
    size = pango_font_description_get_size(pango_font_description);
    pango_font_description_set_size(pango_font_description, size * 0.8);
    g_object_unref(G_OBJECT(pango_context));

    /* set up view */
    sel = gtk_tree_view_get_selection(tree_view);
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_view_expand_all(tree_view);
    gtk_tree_view_set_search_column(tree_view, FC_LABEL);

    /* add file column */
    col = GTK_TREE_VIEW_COLUMN(g_object_new(GTK_TYPE_TREE_VIEW_COLUMN, "expand", TRUE, "title", _("Name"), NULL));
    gtk_tree_view_column_set_resizable(col, TRUE);
    rend = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, rend, FALSE);
    gtk_tree_view_column_add_attribute(col, rend, "pixbuf", FC_ICON);
    /* add text renderer */
    rend = gtk_cell_renderer_text_new();
    g_object_set(rend, "editable", TRUE, NULL);
    g_object_set(rend, "ellipsize", PANGO_ELLIPSIZE_END, "font-desc", pango_font_description, NULL);
    g_signal_connect(rend, "edited", (GCallback)cell_edited_callback, data);
    gtk_tree_view_column_pack_start(col, rend, TRUE);
    gtk_tree_view_column_set_attributes(col, rend, "text", FC_LABEL, NULL);
    gtk_tree_view_column_set_sort_column_id(col, FC_LABEL);
    gtk_tree_view_append_column(tree_view, col);

    /* add "size" column */
    title = _("Size");
    rend = gtk_cell_renderer_text_new();
    g_object_set(rend, "alignment", PANGO_ALIGN_RIGHT, "font-desc", pango_font_description, "xpad", GUI_PAD, "xalign", 1.0F,
        "yalign", 0.5F, NULL);
    col = gtk_tree_view_column_new_with_attributes(title, rend, NULL);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
    gtk_tree_view_column_set_sort_column_id(col, FC_SIZE);
    gtk_tree_view_column_set_attributes(col, rend, "text", FC_SIZE_STR, NULL);
    gtk_tree_view_append_column(tree_view, col);

    /* add "progress" column */
    title = _("Have");
    pango_layout = gtk_widget_create_pango_layout(view, title);
    pango_layout_get_pixel_size(pango_layout, &width, NULL);
    width += 30; /* room for the sort indicator */
    g_object_unref(G_OBJECT(pango_layout));
    rend = gtk_cell_renderer_progress_new();
    col = gtk_tree_view_column_new_with_attributes(title, rend, "value", FC_PROG, NULL);
    gtk_tree_view_column_set_fixed_width(col, width);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(col, FC_PROG);
    gtk_tree_view_append_column(tree_view, col);

    /* add "enabled" column */
    title = _("Download");
    pango_layout = gtk_widget_create_pango_layout(view, title);
    pango_layout_get_pixel_size(pango_layout, &width, NULL);
    width += 30; /* room for the sort indicator */
    g_object_unref(G_OBJECT(pango_layout));
    rend = gtk_cell_renderer_toggle_new();
    col = gtk_tree_view_column_new_with_attributes(title, rend, NULL);
    g_object_set_data(G_OBJECT(col), TR_COLUMN_ID_KEY, GINT_TO_POINTER(FC_ENABLED));
    gtk_tree_view_column_set_fixed_width(col, width);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_cell_data_func(col, rend, renderDownload, NULL, NULL);
    gtk_tree_view_column_set_sort_column_id(col, FC_ENABLED);
    gtk_tree_view_append_column(tree_view, col);

    /* add priority column */
    title = _("Priority");
    pango_layout = gtk_widget_create_pango_layout(view, title);
    pango_layout_get_pixel_size(pango_layout, &width, NULL);
    width += 30; /* room for the sort indicator */
    g_object_unref(G_OBJECT(pango_layout));
    rend = gtk_cell_renderer_text_new();
    g_object_set(rend, "xalign", (gfloat)0.5, "yalign", (gfloat)0.5, NULL);
    col = gtk_tree_view_column_new_with_attributes(title, rend, NULL);
    g_object_set_data(G_OBJECT(col), TR_COLUMN_ID_KEY, GINT_TO_POINTER(FC_PRIORITY));
    gtk_tree_view_column_set_fixed_width(col, width);
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(col, FC_PRIORITY);
    gtk_tree_view_column_set_cell_data_func(col, rend, renderPriority, NULL, NULL);
    gtk_tree_view_append_column(tree_view, col);

    /* add tooltip to tree */
    gtk_tree_view_set_tooltip_column(tree_view, FC_LABEL_ESC);

    /* create the scrolled window and stick the view in it */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_widget_set_size_request(scroll, -1, 200);

    ret = scroll;
    data->view = view;
    data->top = scroll;
    g_object_set_data_full(G_OBJECT(ret), "file-data", data, freeData);
    gtr_file_list_set_torrent(ret, torrentId);

    pango_font_description_free(pango_font_description);
    return ret;
}
