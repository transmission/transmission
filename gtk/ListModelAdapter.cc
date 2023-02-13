// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "ListModelAdapter.h"

#include "Utils.h"

namespace
{

template<typename T>
int iter_get_stamp(T const& iter)
{
    return iter.gobj()->stamp;
}

template<typename T>
void iter_set_stamp(T& iter, int value)
{
    iter.gobj()->stamp = value;
}

template<typename T>
int iter_get_item_id(T const& iter)
{
    return GPOINTER_TO_INT(iter.gobj()->user_data);
}

template<typename T>
void iter_set_item_id(T& iter, int value)
{
    iter.gobj()->user_data = GINT_TO_POINTER(value);
}

template<typename T>
void iter_clear(T& iter)
{
    iter_set_stamp(iter, 0);
    iter_set_item_id(iter, 0);
}

} // namespace

ListModelAdapter::ListModelAdapter(
    Glib::RefPtr<Gio::ListModel> const& adaptee,
    Gtk::TreeModelColumnRecord const& columns,
    IdGetter id_getter,
    ValueGetter value_getter)
    : Glib::ObjectBase(typeid(ListModelAdapter))
    , adaptee_(adaptee)
    , columns_(columns)
    , id_getter_(std::move(id_getter))
    , value_getter_(std::move(value_getter))
{
    adaptee_->signal_items_changed().connect(sigc::mem_fun(*this, &ListModelAdapter::on_adaptee_items_changed));

    on_adaptee_items_changed(0, 0, adaptee_->get_n_items());
}

ListModelAdapter::TrTreeModelFlags ListModelAdapter::get_flags_vfunc() const
{
    return TR_GTK_TREE_MODEL_FLAGS(ITERS_PERSIST) | TR_GTK_TREE_MODEL_FLAGS(LIST_ONLY);
}

int ListModelAdapter::get_n_columns_vfunc() const
{
    return columns_.size();
}

GType ListModelAdapter::get_column_type_vfunc(int index) const
{
    g_return_val_if_fail(index >= 0, G_TYPE_INVALID);
    g_return_val_if_fail(index < get_n_columns_vfunc(), G_TYPE_INVALID);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return columns_.types()[index];
}

bool ListModelAdapter::iter_next_vfunc(iterator const& iter, iterator& iter_next) const
{
    iter_clear(iter_next);

    if (iter)
    {
        g_return_val_if_fail(iter_get_stamp(iter) == stamp_, false);

        if (auto const position = find_item_position_by_id(iter_get_item_id(iter)); position.has_value())
        {
            if (auto const next_position = position.value() + 1; next_position < items_.size())
            {
                iter_set_stamp(iter_next, stamp_);
                iter_set_item_id(iter_next, items_.at(next_position).id);
                return true;
            }
        }
    }

    return false;
}

bool ListModelAdapter::get_iter_vfunc(Path const& path, iterator& iter) const
{
    iter_clear(iter);

    g_return_val_if_fail(path.size() == 1, false);

    return iter_nth_root_child_vfunc(path.front(), iter);
}

bool ListModelAdapter::iter_children_vfunc(iterator const& parent, iterator& iter) const
{
    iter_clear(iter);

    if (parent || items_.empty())
    {
        return false;
    }

    iter_set_stamp(iter, stamp_);
    iter_set_item_id(iter, items_.front().id);
    return true;
}

bool ListModelAdapter::iter_parent_vfunc(iterator const& /*child*/, iterator& iter) const
{
    iter_clear(iter);
    return false;
}

bool ListModelAdapter::iter_nth_root_child_vfunc(int position, iterator& iter) const
{
    iter_clear(iter);

    g_return_val_if_fail(position >= 0, false);

    if (position >= iter_n_root_children_vfunc())
    {
        return false;
    }

    iter_set_stamp(iter, stamp_);
    iter_set_item_id(iter, items_.at(position).id);
    return true;
}

bool ListModelAdapter::iter_has_child_vfunc(const_iterator const& /*iter*/) const
{
    return false;
}

int ListModelAdapter::iter_n_root_children_vfunc() const
{
    return items_.size();
}

Gtk::TreeModel::Path ListModelAdapter::get_path_vfunc(const_iterator const& iter) const
{
    auto path = Path();

    if (iter)
    {
        g_return_val_if_fail(iter_get_stamp(iter) == stamp_, path);

        if (auto const position = find_item_position_by_id(iter_get_item_id(iter)); position.has_value())
        {
            path.push_back(position.value());
        }
    }

    return path;
}

void ListModelAdapter::get_value_vfunc(const_iterator const& iter, int column, Glib::ValueBase& value) const
{
    g_return_if_fail(column >= 0);
    g_return_if_fail(column < get_n_columns_vfunc());

    value.init(get_column_type_vfunc(column));

    if (!iter)
    {
        return;
    }

    auto const position = find_item_position_by_id(iter_get_item_id(iter));
    if (!position.has_value())
    {
        return;
    }

    auto const item = adaptee_->get_object(position.value());
    if (item == nullptr)
    {
        return;
    }

    value_getter_(item, column, value);
}

std::optional<guint> ListModelAdapter::find_item_position_by_id(int item_id) const
{
    auto const item_position_it = item_positions_.find(item_id);
    return item_position_it != item_positions_.end() ? std::make_optional(item_position_it->second) : std::nullopt;
}

void ListModelAdapter::adjust_item_positions(guint min_position, PositionAdjustment adjustment)
{
    for (auto item_it = std::next(items_.begin(), min_position); item_it != items_.end(); ++item_it)
    {
        if (auto const item_position_it = item_positions_.find(item_it->id); item_position_it != item_positions_.end())
        {
            item_position_it->second += static_cast<int>(adjustment);
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ListModelAdapter::on_adaptee_items_changed(guint position, guint removed, guint added)
{
    g_assert(position + removed <= items_.size());
    g_assert(position + added <= adaptee_->get_n_items());

    for (auto i = 0U; i < removed; ++i)
    {
        auto const removed_position = position + removed - i - 1;
        auto info = items_.at(removed_position);

        items_.erase(std::next(items_.begin(), removed_position));
        info.notify_tag.disconnect();

        item_positions_.erase(info.id);
        adjust_item_positions(removed_position, PositionAdjustment::DECREMENT);

        auto path = Path();
        path.push_back(removed_position);

        row_deleted(path);
    }

    for (auto i = 0U; i < added; ++i)
    {
        auto const added_position = position + i;
        auto const item = adaptee_->get_object(added_position);
        auto const info = ItemInfo{
            .id = id_getter_(item),
            .notify_tag = gtr_object_signal_notify(*item.get())
                              .connect(sigc::mem_fun(*this, &ListModelAdapter::on_adaptee_item_changed)),
        };

        items_.insert(std::next(items_.begin(), added_position), info);

        adjust_item_positions(added_position, PositionAdjustment::INCREMENT);
        item_positions_.emplace(info.id, added_position);

        auto path = Path();
        path.push_back(added_position);

        auto iter = iterator(this);
        iter_set_stamp(iter, stamp_);
        iter_set_item_id(iter, info.id);

        row_inserted(path, iter);
    }
}

void ListModelAdapter::on_adaptee_item_changed(Glib::RefPtr<Glib::ObjectBase const> const& item)
{
    g_return_if_fail(item != nullptr);

    auto const item_id = id_getter_(item);

    if (auto const position = find_item_position_by_id(item_id); position.has_value())
    {
        auto path = Path();
        path.push_back(position.value());

        auto iter = iterator(this);
        iter_set_stamp(iter, stamp_);
        iter_set_item_id(iter, item_id);

        row_changed(path, iter);
    }
}
