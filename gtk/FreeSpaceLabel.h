// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/tr-macros.h>

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>
#include <gtkmm/label.h>

#include <memory>
#include <string_view>

class Session;

class FreeSpaceLabel : public Gtk::Label
{
public:
    explicit FreeSpaceLabel(Glib::RefPtr<Session> const& core, std::string_view dir = {});
    FreeSpaceLabel(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        std::string_view dir = {});
    ~FreeSpaceLabel() override;

    TR_DISABLE_COPY_MOVE(FreeSpaceLabel)

    void set_dir(std::string_view dir);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
