// This file Copyright (C) 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <string>

#include <glibmm.h>
#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

class Session;

class FreeSpaceLabel : public Gtk::Label
{
public:
    FreeSpaceLabel(Glib::RefPtr<Session> const& core, std::string const& dir = {});
    ~FreeSpaceLabel() override;

    TR_DISABLE_COPY_MOVE(FreeSpaceLabel)

    void set_dir(std::string const& dir);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
