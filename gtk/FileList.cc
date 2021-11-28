/*
 * This file Copyright (C) 2009-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <limits.h> /* INT_MAX */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "FileList.h"
#include "HigWorkarea.h"
#include "IconCache.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#define TR_COLUMN_ID_KEY "tr-model-column-id-key"

namespace
{

enum
{
    /* these two fields could be any number at all so long as they're not
     * TR_PRI_LOW, TR_PRI_NORMAL, TR_PRI_HIGH, true, or false */
    NOT_SET = 1000,
    MIXED = 1001
};

class FileModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FileModelColumns()
    {
        add(icon);
        add(label);
        add(label_esc);
        add(prog);
        add(index);
        add(size);
        add(size_str);
        add(have);
        add(priority);
        add(enabled);
    }

    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> icon;
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<Glib::ustring> label_esc;
    Gtk::TreeModelColumn<int> prog;
    Gtk::TreeModelColumn<unsigned int> index;
    Gtk::TreeModelColumn<uint64_t> size;
    Gtk::TreeModelColumn<Glib::ustring> size_str;
    Gtk::TreeModelColumn<uint64_t> have;
    Gtk::TreeModelColumn<int> priority;
    Gtk::TreeModelColumn<int> enabled;
};

FileModelColumns const file_cols;

} // namespace

class FileList::Impl
{
public:
    Impl(FileList& widget, Glib::RefPtr<Session> const& core, int torrent_id);
    ~Impl();

    void set_torrent(int torrent_id);

private:
    void clearData();
    void refresh();

    bool getAndSelectEventPath(GdkEventButton const* event, Gtk::TreeViewColumn*& col, Gtk::TreeModel::Path& path);

    std::vector<tr_file_index_t> getActiveFilesForPath(Gtk::TreeModel::Path const& path) const;
    std::vector<tr_file_index_t> getSelectedFilesAndDescendants() const;
    std::vector<tr_file_index_t> getSubtree(Gtk::TreeModel::Path const& path) const;

    bool onViewButtonPressed(GdkEventButton const* event);
    bool onViewPathToggled(Gtk::TreeViewColumn* col, Gtk::TreeModel::Path const& path);
    void onRowActivated(Gtk::TreeModel::Path const& path, Gtk::TreeViewColumn* col);
    void cell_edited_callback(Glib::ustring const& path_string, Glib::ustring const& newname);
    bool on_rename_done_idle(Glib::ustring const& path_string, Glib::ustring const& newname, int error);

private:
    FileList& widget_;

    Glib::RefPtr<Session> const core_;
    // GtkWidget* top_ = nullptr; // == widget_
    Gtk::TreeView* view_ = nullptr;
    Glib::RefPtr<Gtk::TreeStore> store_;
    int torrent_id_ = 0;
    sigc::connection timeout_tag_;
};

void FileList::Impl::clearData()
{
    torrent_id_ = -1;

    if (timeout_tag_.connected())
    {
        timeout_tag_.disconnect();
    }
}

FileList::Impl::~Impl()
{
    clearData();
}

/***
****
***/

namespace
{

struct RefreshData
{
    int sort_column_id;
    bool resort_needed;
    tr_torrent* tor;
};

bool refreshFilesForeach(
    Glib::RefPtr<Gtk::TreeStore> const& store,
    Gtk::TreeModel::iterator const& iter,
    RefreshData& refresh_data)
{
    bool const is_file = iter->children().empty();

    auto const old_enabled = iter->get_value(file_cols.enabled);
    auto const old_priority = iter->get_value(file_cols.priority);
    auto const index = iter->get_value(file_cols.index);
    auto const old_have = iter->get_value(file_cols.have);
    auto const size = iter->get_value(file_cols.size);
    auto const old_prog = iter->get_value(file_cols.prog);

    if (is_file)
    {
        auto const* tor = refresh_data.tor;
        auto const file = tr_torrentFile(tor, index);
        int const enabled = file.wanted;
        int const priority = file.priority;
        auto const progress = file.progress;
        uint64_t const have = file.have;
        int const prog = std::clamp(int(100 * progress), 0, 100);

        if (priority != old_priority || enabled != old_enabled || have != old_have || prog != old_prog)
        {
            /* Changing a value in the sort column can trigger a resort
             * which breaks this foreach () call. (See #3529)
             * As a workaround: if that's about to happen, temporarily disable
             * sorting until we finish walking the tree. */
            if (!refresh_data.resort_needed &&
                (((refresh_data.sort_column_id == file_cols.priority.index()) && (priority != old_priority)) ||
                 ((refresh_data.sort_column_id == file_cols.enabled.index()) && (enabled != old_enabled))))
            {
                refresh_data.resort_needed = true;

                store->set_sort_column(GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, Gtk::SORT_ASCENDING);
            }

            (*iter)[file_cols.priority] = priority;
            (*iter)[file_cols.enabled] = enabled;
            (*iter)[file_cols.have] = have;
            (*iter)[file_cols.prog] = prog;
        }
    }
    else
    {
        uint64_t sub_size = 0;
        uint64_t have = 0;
        int prog;
        int enabled = NOT_SET;
        int priority = NOT_SET;

        /* since gtk_tree_model_foreach() is depth-first, we can
         * get the `sub' info by walking the immediate children */

        for (auto const& child : iter->children())
        {
            int64_t const child_size = child[file_cols.size];
            int64_t const child_have = child[file_cols.have];
            int const child_priority = child[file_cols.priority];
            int const child_enabled = child[file_cols.enabled];

            if ((child_enabled != false) && (child_enabled != NOT_SET))
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

        prog = sub_size != 0 ? (int)(100.0 * have / sub_size) : 1;

        if (size != sub_size || have != old_have || priority != old_priority || enabled != old_enabled || prog != old_prog)
        {
            (*iter)[file_cols.size] = sub_size;
            (*iter)[file_cols.size_str] = tr_strlsize(sub_size);
            (*iter)[file_cols.have] = have;
            (*iter)[file_cols.priority] = priority;
            (*iter)[file_cols.enabled] = enabled;
            (*iter)[file_cols.prog] = prog;
        }
    }

    return false; /* keep walking */
}

void gtr_tree_model_foreach_postorder_subtree(
    Gtk::TreeModel::iterator const& parent,
    Gtk::TreeModel::SlotForeachIter const& func)
{
    for (auto const& child : parent->children())
    {
        gtr_tree_model_foreach_postorder_subtree(child, func);
    }

    if (parent)
    {
        func(parent);
    }
}

void gtr_tree_model_foreach_postorder(Glib::RefPtr<Gtk::TreeModel> const& model, Gtk::TreeModel::SlotForeachIter const& func)
{
    for (auto const& iter : model->children())
    {
        gtr_tree_model_foreach_postorder_subtree(iter, func);
    }
}

} // namespace

void FileList::Impl::refresh()
{
    tr_torrent* tor = core_->find_torrent(torrent_id_);

    if (tor == nullptr)
    {
        widget_.clear();
    }
    else
    {
        Gtk::SortType order;
        int sort_column_id;
        store_->get_sort_column_id(sort_column_id, order);

        RefreshData refresh_data{ sort_column_id, false, tor };
        gtr_tree_model_foreach_postorder(
            store_,
            [this, &refresh_data](Gtk::TreeModel::iterator const& iter)
            { return refreshFilesForeach(store_, iter, refresh_data); });

        if (refresh_data.resort_needed)
        {
            store_->set_sort_column(sort_column_id, order);
        }
    }
}

/***
****
***/

namespace
{

bool getSelectedFilesForeach(
    Gtk::TreeModel::iterator const& iter,
    Glib::RefPtr<Gtk::TreeSelection> const& sel,
    std::vector<tr_file_index_t>& indexBuf)
{
    bool const is_file = iter->children().empty();

    if (is_file)
    {
        /* active means: if it's selected or any ancestor is selected */
        bool is_active = sel->is_selected(iter);

        if (!is_active)
        {
            for (auto walk = iter->parent(); !is_active && walk; walk = walk->parent())
            {
                is_active = sel->is_selected(walk);
            }
        }

        if (is_active)
        {
            indexBuf.push_back(iter->get_value(file_cols.index));
        }
    }

    return false; /* keep walking */
}

} // namespace

std::vector<tr_file_index_t> FileList::Impl::getSelectedFilesAndDescendants() const
{
    auto const sel = view_->get_selection();
    std::vector<tr_file_index_t> indexBuf;
    store_->foreach_iter([&sel, &indexBuf](Gtk::TreeModel::iterator const& iter)
                         { return getSelectedFilesForeach(iter, sel, indexBuf); });
    return indexBuf;
}

namespace
{

bool getSubtreeForeach(
    Gtk::TreeModel::Path const& path,
    Gtk::TreeModel::iterator const& iter,
    Gtk::TreeModel::Path const& subtree_path,
    std::vector<tr_file_index_t>& indexBuf)
{
    bool const is_file = iter->children().empty();

    if (is_file)
    {
        if (path == subtree_path || path.is_descendant(subtree_path))
        {
            indexBuf.push_back(iter->get_value(file_cols.index));
        }
    }

    return false; /* keep walking */
}

} // namespace

std::vector<tr_file_index_t> FileList::Impl::getSubtree(Gtk::TreeModel::Path const& subtree_path) const
{
    std::vector<tr_file_index_t> indexBuf;
    store_->foreach ([&subtree_path, &indexBuf](Gtk::TreeModel::Path const& path, Gtk::TreeModel::iterator const& iter)
                     { return getSubtreeForeach(path, iter, subtree_path, indexBuf); });
    return indexBuf;
}

/* if `path' is a selected row, all selected rows are returned.
 * otherwise, only the row indicated by `path' is returned.
 * this is for toggling all the selected rows' states in a batch.
 *
 * indexBuf should be large enough to hold tr_inf.fileCount files.
 */
std::vector<tr_file_index_t> FileList::Impl::getActiveFilesForPath(Gtk::TreeModel::Path const& path) const
{
    if (view_->get_selection()->is_selected(path))
    {
        /* clicked in a selected row... use the current selection */
        return getSelectedFilesAndDescendants();
    }
    else
    {
        /* clicked OUTSIDE of the selected row... just use the clicked row */
        return getSubtree(path);
    }
}

/***
****
***/

void FileList::clear()
{
    impl_->set_torrent(-1);
}

namespace
{

struct build_data
{
    Gtk::Widget* w;
    tr_torrent* tor;
    Gtk::TreeStore::iterator iter;
    Glib::RefPtr<Gtk::TreeStore> store;
};

struct row_struct
{
    uint64_t length = 0;
    Glib::ustring name;
    int index = 0;
};

using FileRowNode = Glib::NodeTree<row_struct>;

void buildTree(FileRowNode& node, build_data& build)
{
    auto& child_data = node.data();
    bool const isLeaf = node.child_count() == 0;

    auto const mime_type = isLeaf ? gtr_get_mime_type_from_filename(child_data.name) : DIRECTORY_MIME_TYPE;
    auto const icon = gtr_get_mime_type_icon(mime_type, Gtk::ICON_SIZE_MENU, *build.w);
    auto const file = tr_torrentFile(build.tor, child_data.index);
    int const priority = isLeaf ? file.priority : 0;
    bool const enabled = isLeaf ? file.wanted : true;
    auto name_esc = Glib::Markup::escape_text(child_data.name);

    auto const child_iter = build.store->append(build.iter->children());
    (*child_iter)[file_cols.index] = child_data.index;
    (*child_iter)[file_cols.label] = child_data.name;
    (*child_iter)[file_cols.label_esc] = name_esc;
    (*child_iter)[file_cols.size] = child_data.length;
    (*child_iter)[file_cols.size_str] = tr_strlsize(child_data.length);
    (*child_iter)[file_cols.icon] = icon;
    (*child_iter)[file_cols.priority] = priority;
    (*child_iter)[file_cols.enabled] = enabled;

    if (!isLeaf)
    {
        build_data b = build;
        b.iter = child_iter;
        node.foreach ([&b](auto& child_node) { buildTree(child_node, b); }, FileRowNode::TRAVERSE_ALL);
    }
}

FileRowNode* find_child(FileRowNode* parent, Glib::ustring const& name)
{
    for (auto* child = parent->first_child(); child != nullptr; child = child->next_sibling())
    {
        auto const& child_data = child->data();

        if (child_data.name == name)
        {
            return child;
        }
    }

    return nullptr;
}

} // namespace

void FileList::set_torrent(int torrent_id)
{
    impl_->set_torrent(torrent_id);
}

void FileList::Impl::set_torrent(int torrentId)
{
    /* unset the old fields */
    clearData();

    /* instantiate the model */
    store_ = Gtk::TreeStore::create(file_cols);
    torrent_id_ = torrentId;

    /* populate the model */
    auto* const tor = torrent_id_ > 0 ? core_->find_torrent(torrent_id_) : nullptr;
    if (tor != nullptr)
    {
        // build a GNode tree of the files
        FileRowNode root;
        auto& root_data = root.data();
        root_data.name = tr_torrentName(tor);
        root_data.index = -1;
        root_data.length = 0;

        for (tr_file_index_t i = 0, n_files = tr_torrentFileCount(tor); i < n_files; ++i)
        {
            auto* parent = &root;
            auto const file = tr_torrentFile(tor, i);

            for (char const *last = file.name, *next = strchr(last, '/'); last != nullptr;
                 last = next != nullptr ? next + 1 : nullptr, next = last != nullptr ? strchr(last, '/') : nullptr)
            {
                bool const isLeaf = next == nullptr;
                auto name = Glib::ustring(isLeaf ? last : std::string(last, next - last));
                auto* node = find_child(parent, name);

                if (node == nullptr)
                {
                    node = new FileRowNode();
                    auto& row = node->data();
                    row.name = std::move(name);
                    row.index = isLeaf ? (int)i : -1;
                    row.length = isLeaf ? file.length : 0;
                    parent->append(*node);
                }

                parent = node;
            }
        }

        // now, add them to the model
        struct build_data build;
        build.w = &widget_;
        build.tor = tor;
        build.store = store_;
        root.foreach ([&build](auto& child_node) { buildTree(child_node, build); }, FileRowNode::TRAVERSE_ALL);
    }

    if (torrent_id_ > 0)
    {
        refresh();
        timeout_tag_ = Glib::signal_timeout().connect_seconds(
            [this]() { return refresh(), true; },
            SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
    }

    view_->set_model(store_);

    /* set default sort by label */
    store_->set_sort_column(file_cols.label, Gtk::SORT_ASCENDING);

    view_->expand_all();
}

/***
****
***/

namespace
{

void renderDownload(Gtk::CellRenderer* renderer, Gtk::TreeModel::iterator const& iter)
{
    auto const enabled = iter->get_value(file_cols.enabled);
    static_cast<Gtk::CellRendererToggle*>(renderer)->property_inconsistent() = enabled == MIXED;
    static_cast<Gtk::CellRendererToggle*>(renderer)->property_active() = enabled == true;
}

void renderPriority(Gtk::CellRenderer* renderer, Gtk::TreeModel::iterator const& iter)
{
    Glib::ustring text;
    auto const priority = iter->get_value(file_cols.priority);

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

    static_cast<Gtk::CellRendererText*>(renderer)->property_text() = text;
}

/* build a filename from tr_torrentGetCurrentDir() + the model's FC_LABELs */
std::string buildFilename(tr_torrent const* tor, Gtk::TreeModel::iterator const& iter)
{
    std::list<std::string> tokens;
    for (auto child = iter; child; child = child->parent())
    {
        tokens.push_front(child->get_value(file_cols.label));
    }

    tokens.push_front(tr_torrentGetCurrentDir(tor));
    return Glib::build_filename(tokens);
}

} // namespace

void FileList::Impl::onRowActivated(Gtk::TreeModel::Path const& path, Gtk::TreeViewColumn* /*col*/)
{
    bool handled = false;
    auto const* tor = core_->find_torrent(torrent_id_);

    if (tor != nullptr)
    {
        if (auto const iter = store_->get_iter(path); iter)
        {
            auto filename = buildFilename(tor, iter);
            auto const prog = iter->get_value(file_cols.prog);

            /* if the file's not done, walk up the directory tree until we find
             * an ancestor that exists, and open that instead */
            if (!filename.empty() && (prog < 100 || !Glib::file_test(filename, Glib::FILE_TEST_EXISTS)))
            {
                do
                {
                    filename = Glib::path_get_dirname(filename);
                } while (!filename.empty() && !Glib::file_test(filename, Glib::FILE_TEST_EXISTS));
            }

            if (handled = !filename.empty(); handled)
            {
                gtr_open_file(filename);
            }
        }
    }

    // return handled;
}

bool FileList::Impl::onViewPathToggled(Gtk::TreeViewColumn* col, Gtk::TreeModel::Path const& path)
{
    if (col == nullptr || path.empty())
    {
        return false;
    }

    bool handled = false;

    auto const cid = GPOINTER_TO_INT(col->get_data(TR_COLUMN_ID_KEY));
    auto* tor = core_->find_torrent(torrent_id_);

    if (tor != nullptr && (cid == file_cols.priority.index() || cid == file_cols.enabled.index()))
    {
        auto const indexBuf = getActiveFilesForPath(path);

        auto const iter = store_->get_iter(path);

        if (cid == file_cols.priority.index())
        {
            auto priority = iter->get_value(file_cols.priority);

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

            tr_torrentSetFilePriorities(tor, indexBuf.data(), indexBuf.size(), priority);
        }
        else
        {
            auto enabled = iter->get_value(file_cols.enabled);
            enabled = !enabled;

            tr_torrentSetFileDLs(tor, indexBuf.data(), indexBuf.size(), enabled);
        }

        refresh();
        handled = true;
    }

    return handled;
}

/**
 * @note 'col' and 'path' are assumed not to be nullptr.
 */
bool FileList::Impl::getAndSelectEventPath(GdkEventButton const* event, Gtk::TreeViewColumn*& col, Gtk::TreeModel::Path& path)
{
    int cell_x;
    int cell_y;

    if (view_->get_path_at_pos(event->x, event->y, path, col, cell_x, cell_y))
    {
        auto const sel = view_->get_selection();

        if (!sel->is_selected(path))
        {
            sel->unselect_all();
            sel->select(path);
        }

        return true;
    }

    return false;
}

bool FileList::Impl::onViewButtonPressed(GdkEventButton const* event)
{
    Gtk::TreeViewColumn* col;
    Gtk::TreeModel::Path path;
    bool handled = false;

    if (event->type == GDK_BUTTON_PRESS && event->button == 1 && (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) == 0 &&
        getAndSelectEventPath(event, col, path))
    {
        handled = onViewPathToggled(col, path);
    }

    return handled;
}

struct rename_data
{
    Glib::ustring newname;
    Glib::ustring path_string;
    void* impl;
};

bool FileList::Impl::on_rename_done_idle(Glib::ustring const& path_string, Glib::ustring const& newname, int error)
{
    if (error == 0)
    {
        if (auto const iter = store_->get_iter(path_string); iter)
        {
            bool const isLeaf = iter->children().empty();
            auto const mime_type = isLeaf ? gtr_get_mime_type_from_filename(newname) : DIRECTORY_MIME_TYPE;
            auto const icon = gtr_get_mime_type_icon(mime_type, Gtk::ICON_SIZE_MENU, *view_);

            (*iter)[file_cols.label] = newname;
            (*iter)[file_cols.icon] = icon;

            if (!iter->parent())
            {
                core_->torrent_changed(torrent_id_);
            }
        }
    }
    else
    {
        Gtk::MessageDialog w(
            *static_cast<Gtk::Window*>(widget_.get_toplevel()),
            gtr_sprintf(_("Unable to rename file as \"%s\": %s"), newname, tr_strerror(error)),
            false,
            Gtk::MESSAGE_ERROR,
            Gtk::BUTTONS_CLOSE,
            true);
        w.set_secondary_text(_("Please correct the errors and try again."));
        w.run();
    }

    return false;
}

void FileList::Impl::cell_edited_callback(Glib::ustring const& path_string, Glib::ustring const& newname)
{
    tr_torrent* const tor = core_->find_torrent(torrent_id_);

    if (tor == nullptr)
    {
        return;
    }

    auto iter = store_->get_iter(path_string);
    if (!iter)
    {
        return;
    }

    /* build oldpath */
    Glib::ustring oldpath;

    for (;;)
    {
        oldpath.insert(0, iter->get_value(file_cols.label));

        iter = iter->parent();
        if (!iter)
        {
            break;
        }

        oldpath.insert(0, 1, G_DIR_SEPARATOR);
    }

    /* do the renaming */
    auto* rename_data = new struct rename_data();
    rename_data->newname = newname;
    rename_data->impl = this;
    rename_data->path_string = path_string;
    tr_torrentRenamePath(
        tor,
        oldpath.c_str(),
        newname.c_str(),
        static_cast<tr_torrent_rename_done_func>(
            [](tr_torrent* /*tor*/, char const* /*oldpath*/, char const* /*newname*/, int error, void* data)
            {
                Glib::signal_idle().connect(
                    [rdata = std::shared_ptr<struct rename_data>(static_cast<struct rename_data*>(data)), error]() {
                        return static_cast<Impl*>(rdata->impl)->on_rename_done_idle(rdata->path_string, rdata->newname, error);
                    });
            }),
        rename_data);
}

FileList::FileList(Glib::RefPtr<Session> const& core, int torrent_id)
    : Gtk::ScrolledWindow()
    , impl_(std::make_unique<Impl>(*this, core, torrent_id))
{
}

FileList::Impl::Impl(FileList& widget, Glib::RefPtr<Session> const& core, int torrent_id)
    : widget_(widget)
    , core_(core)
{
    /* create the view */
    view_ = Gtk::make_managed<Gtk::TreeView>();
    view_->set_border_width(GUI_PAD_BIG);
    view_->signal_button_press_event().connect(sigc::mem_fun(*this, &Impl::onViewButtonPressed), false);
    view_->signal_row_activated().connect(sigc::mem_fun(*this, &Impl::onRowActivated));
    view_->signal_button_release_event().connect([this](GdkEventButton* event)
                                                 { return on_tree_view_button_released(view_, event); });

    auto pango_font_description = view_->create_pango_context()->get_font_description();
    pango_font_description.set_size(pango_font_description.get_size() * 0.8);

    /* set up view */
    auto const sel = view_->get_selection();
    sel->set_mode(Gtk::SELECTION_MULTIPLE);
    view_->expand_all();
    view_->set_search_column(file_cols.label);

    {
        /* add file column */
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>();
        col->set_expand(true);
        col->set_title(_("Name"));
        col->set_resizable(true);
        auto* icon_rend = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        col->pack_start(*icon_rend, false);
        col->add_attribute(icon_rend->property_pixbuf(), file_cols.icon);
        /* add text renderer */
        auto* text_rend = Gtk::make_managed<Gtk::CellRendererText>();
        text_rend->property_editable() = true;
        text_rend->property_ellipsize() = Pango::ELLIPSIZE_END;
        text_rend->property_font_desc() = pango_font_description;
        text_rend->signal_edited().connect(sigc::mem_fun(*this, &Impl::cell_edited_callback));
        col->pack_start(*text_rend, true);
        col->add_attribute(text_rend->property_text(), file_cols.label);
        col->set_sort_column(file_cols.label);
        view_->append_column(*col);
    }

    {
        /* add "size" column */
        auto* rend = Gtk::make_managed<Gtk::CellRendererText>();
        rend->property_alignment() = Pango::ALIGN_RIGHT;
        rend->property_font_desc() = pango_font_description;
        rend->property_xpad() = GUI_PAD;
        rend->property_xalign() = 1.0F;
        rend->property_yalign() = 0.5F;
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(_("Size"), *rend);
        col->set_sizing(Gtk::TREE_VIEW_COLUMN_GROW_ONLY);
        col->set_sort_column(file_cols.size);
        col->add_attribute(rend->property_text(), file_cols.size_str);
        view_->append_column(*col);
    }

    {
        /* add "progress" column */
        auto const* title = _("Have");
        int width;
        int height;
        view_->create_pango_layout(title)->get_pixel_size(width, height);
        width += 30; /* room for the sort indicator */
        auto* rend = Gtk::make_managed<Gtk::CellRendererProgress>();
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(title, *rend);
        col->add_attribute(rend->property_text(), file_cols.prog);
        col->set_fixed_width(width);
        col->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        col->set_sort_column(file_cols.prog);
        view_->append_column(*col);
    }

    {
        /* add "enabled" column */
        auto const* title = _("Download");
        int width;
        int height;
        view_->create_pango_layout(title)->get_pixel_size(width, height);
        width += 30; /* room for the sort indicator */
        auto* rend = Gtk::make_managed<Gtk::CellRendererToggle>();
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(title, *rend);
        col->set_data(TR_COLUMN_ID_KEY, GINT_TO_POINTER(file_cols.enabled.index()));
        col->set_fixed_width(width);
        col->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        col->set_cell_data_func(*rend, sigc::ptr_fun(&renderDownload));
        col->set_sort_column(file_cols.enabled);
        view_->append_column(*col);
    }

    {
        /* add priority column */
        auto const* title = _("Priority");
        int width;
        int height;
        view_->create_pango_layout(title)->get_pixel_size(width, height);
        width += 30; /* room for the sort indicator */
        auto* rend = Gtk::make_managed<Gtk::CellRendererText>();
        rend->property_xalign() = 0.5F;
        rend->property_yalign() = 0.5F;
        auto* col = Gtk::make_managed<Gtk::TreeViewColumn>(title, *rend);
        col->set_data(TR_COLUMN_ID_KEY, GINT_TO_POINTER(file_cols.priority.index()));
        col->set_fixed_width(width);
        col->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        col->set_sort_column(file_cols.priority);
        col->set_cell_data_func(*rend, sigc::ptr_fun(&renderPriority));
        view_->append_column(*col);
    }

    /* add tooltip to tree */
    view_->set_tooltip_column(file_cols.label_esc.index());

    /* create the scrolled window and stick the view in it */
    widget_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    widget_.set_shadow_type(Gtk::SHADOW_IN);
    widget_.add(*view_);
    widget_.set_size_request(-1, 200);

    set_torrent(torrent_id);
}

FileList::~FileList() = default;
