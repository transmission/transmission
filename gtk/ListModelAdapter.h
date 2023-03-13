// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <giomm/listmodel.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/value.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelcolumn.h>

#include <optional>
#include <unordered_map>
#include <vector>

class ListModelAdapter
    : public Gtk::TreeModel
    , public Glib::Object
{
    using IdGetter = std::function<int(Glib::RefPtr<Glib::ObjectBase const> const&)>;
    using ValueGetter = std::function<void(Glib::RefPtr<Glib::ObjectBase const> const&, int, Glib::ValueBase&)>;

    enum class PositionAdjustment
    {
        DECREMENT = -1,
        INCREMENT = 1,
    };

    struct ItemInfo
    {
        int id = 0;
        sigc::connection notify_tag;
    };

    using TrTreeModelFlags = IF_GTKMM4(Gtk::TreeModel::Flags, Gtk::TreeModelFlags);

public:
    template<typename T>
    static Glib::RefPtr<ListModelAdapter> create(Glib::RefPtr<Gio::ListModel> const& adaptee)
    {
        return Glib::make_refptr_for_instance(
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            new ListModelAdapter(adaptee, T::get_columns(), &T::get_item_id, &T::get_item_value));
    }

protected:
    // Gtk::TreeModel
    TrTreeModelFlags get_flags_vfunc() const override;
    int get_n_columns_vfunc() const override;
    GType get_column_type_vfunc(int index) const override;
    bool iter_next_vfunc(iterator const& iter, iterator& iter_next) const override;
    bool get_iter_vfunc(Path const& path, iterator& iter) const override;
    bool iter_children_vfunc(iterator const& parent, iterator& iter) const override;
    bool iter_parent_vfunc(iterator const& child, iterator& iter) const override;
    bool iter_nth_root_child_vfunc(int position, iterator& iter) const override;
    bool iter_has_child_vfunc(const_iterator const& iter) const override;
    int iter_n_root_children_vfunc() const override;
    TreeModel::Path get_path_vfunc(const_iterator const& iter) const override;
    void get_value_vfunc(const_iterator const& iter, int column, Glib::ValueBase& value) const override;

private:
    ListModelAdapter(
        Glib::RefPtr<Gio::ListModel> const& adaptee,
        Gtk::TreeModelColumnRecord const& columns,
        IdGetter id_getter,
        ValueGetter value_getter);

    std::optional<guint> find_item_position_by_id(int item_id) const;
    void adjust_item_positions(guint min_position, PositionAdjustment adjustment);

    void on_adaptee_items_changed(guint position, guint removed, guint added);
    void on_adaptee_item_changed(Glib::RefPtr<Glib::ObjectBase const> const& item);

private:
    Glib::RefPtr<Gio::ListModel> const adaptee_;
    Gtk::TreeModelColumnRecord const& columns_;
    IdGetter const id_getter_;
    ValueGetter const value_getter_;

    int const stamp_ = 1;
    std::vector<ItemInfo> items_;
    std::unordered_map<int, guint> mutable item_positions_;
};
