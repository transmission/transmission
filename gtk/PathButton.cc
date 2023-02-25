// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "PathButton.h"

#include "Utils.h"

#include <giomm/file.h>
#include <glibmm/error.h>
#include <glibmm/i18n.h>
#include <glibmm/property.h>
#include <gtkmm/box.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include <vector>

class PathButton::Impl
{
public:
    explicit Impl(PathButton& widget);
    ~Impl() = default;

    TR_DISABLE_COPY_MOVE(Impl)

#if GTKMM_CHECK_VERSION(4, 0, 0)
    std::string const& get_filename() const;
    void set_filename(std::string const& value);

    void set_shortcut_folders(std::list<std::string> const& value);

    void add_filter(Glib::RefPtr<Gtk::FileFilter> const& value);

    Glib::Property<Gtk::FileChooser::Action>& property_action();
    Glib::Property<Glib::ustring>& property_title();

    sigc::signal<void()>& signal_selection_changed();
#endif

private:
#if GTKMM_CHECK_VERSION(4, 0, 0)
    void show_dialog();

    void update();
    void update_mode();
#endif

private:
#if GTKMM_CHECK_VERSION(4, 0, 0)
    PathButton& widget_;

    Glib::Property<Gtk::FileChooser::Action> action_;
    Glib::Property<Glib::ustring> title_;

    sigc::signal<void()> selection_changed_;

    Gtk::Image* const image_ = nullptr;
    Gtk::Label* const label_ = nullptr;
    Gtk::Image* const mode_ = nullptr;

    std::string current_file_;
    std::list<std::string> shortcut_folders_;
    std::vector<Glib::RefPtr<Gtk::FileFilter>> filters_;
#endif
};

PathButton::Impl::Impl([[maybe_unused]] PathButton& widget)
#if GTKMM_CHECK_VERSION(4, 0, 0)
    : widget_(widget)
    , action_(widget, "action", Gtk::FileChooser::Action::OPEN)
    , title_(widget, "title", {})
    , image_(Gtk::make_managed<Gtk::Image>())
    , label_(Gtk::make_managed<Gtk::Label>())
    , mode_(Gtk::make_managed<Gtk::Image>())
#endif
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    action_.get_proxy().signal_changed().connect([this]() { update_mode(); });

    label_->set_ellipsize(Pango::EllipsizeMode::END);
    label_->set_hexpand(true);
    label_->set_halign(Gtk::Align::START);

    auto* const layout = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
    layout->append(*image_);
    layout->append(*label_);
    layout->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    layout->append(*mode_);
    widget_.set_child(*layout);

    widget_.signal_clicked().connect(sigc::mem_fun(*this, &Impl::show_dialog));

    update();
    update_mode();
#endif
}

#if GTKMM_CHECK_VERSION(4, 0, 0)

std::string const& PathButton::Impl::get_filename() const
{
    return current_file_;
}

void PathButton::Impl::set_filename(std::string const& value)
{
    current_file_ = value;
    update();
    selection_changed_.emit();
}

void PathButton::Impl::set_shortcut_folders(std::list<std::string> const& value)
{
    shortcut_folders_ = value;
}

void PathButton::Impl::add_filter(Glib::RefPtr<Gtk::FileFilter> const& value)
{
    filters_.push_back(value);
}

Glib::Property<Gtk::FileChooser::Action>& PathButton::Impl::property_action()
{
    return action_;
}

Glib::Property<Glib::ustring>& PathButton::Impl::property_title()
{
    return title_;
}

sigc::signal<void()>& PathButton::Impl::signal_selection_changed()
{
    return selection_changed_;
}

void PathButton::Impl::show_dialog()
{
    auto const title = title_.get_value();

    auto dialog = std::make_shared<Gtk::FileChooserDialog>(!title.empty() ? title : _("Select a File"), action_.get_value());
    dialog->set_transient_for(gtr_widget_get_window(widget_));
    dialog->add_button(_("_Cancel"), Gtk::ResponseType::CANCEL);
    dialog->add_button(_("_Open"), Gtk::ResponseType::ACCEPT);
    dialog->set_modal(true);

    if (!current_file_.empty())
    {
        dialog->set_file(Gio::File::create_for_path(current_file_));
    }

    for (auto const& folder : shortcut_folders_)
    {
        dialog->remove_shortcut_folder(Gio::File::create_for_path(folder));
        dialog->add_shortcut_folder(Gio::File::create_for_path(folder));
    }

    for (auto const& filter : filters_)
    {
        dialog->add_filter(filter);
    }

    dialog->signal_response().connect(
        [this, dialog](int response) mutable
        {
            if (response == Gtk::ResponseType::ACCEPT)
            {
                set_filename(dialog->get_file()->get_path());
                selection_changed_.emit();
            }

            dialog.reset();
        });

    dialog->show();
}

void PathButton::Impl::update()
{
    if (!current_file_.empty())
    {
        auto const file = Gio::File::create_for_path(current_file_);

        try
        {
            image_->set(file->query_info()->get_icon());
        }
        catch (Glib::Error const&)
        {
            image_->set_from_icon_name("image-missing");
        }

        label_->set_text(file->get_basename());
    }
    else
    {
        image_->set_from_icon_name("image-missing");
        label_->set_text(_("(None)"));
    }

    widget_.set_tooltip_text(current_file_);
}

void PathButton::Impl::update_mode()
{
    mode_->set_from_icon_name(
        action_.get_value() == Gtk::FileChooser::Action::SELECT_FOLDER ? "folder-open-symbolic" : "document-open-symbolic");
}

#endif

PathButton::PathButton()
    : Glib::ObjectBase(typeid(PathButton))
    , impl_(std::make_unique<Impl>(*this))
{
}

PathButton::PathButton(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& /*builder*/)
    : Glib::ObjectBase(typeid(PathButton))
    , BaseWidgetType(cast_item)
    , impl_(std::make_unique<Impl>(*this))
{
}

PathButton::~PathButton() = default;

void PathButton::set_shortcut_folders(std::list<std::string> const& value)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    impl_->set_shortcut_folders(value);
#else
    for (auto const& folder : value)
    {
        remove_shortcut_folder(folder);
        add_shortcut_folder(folder);
    }
#endif
}

#if GTKMM_CHECK_VERSION(4, 0, 0)

std::string PathButton::get_filename() const
{
    return impl_->get_filename();
}

void PathButton::set_filename(std::string const& value)
{
    impl_->set_filename(value);
}

void PathButton::add_filter(Glib::RefPtr<Gtk::FileFilter> const& value)
{
    impl_->add_filter(value);
}

Glib::PropertyProxy<Gtk::FileChooser::Action> PathButton::property_action()
{
    return impl_->property_action().get_proxy();
}

Glib::PropertyProxy<Glib::ustring> PathButton::property_title()
{
    return impl_->property_title().get_proxy();
}

sigc::signal<void()>& PathButton::signal_selection_changed()
{
    return impl_->signal_selection_changed();
}

#endif
