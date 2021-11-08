/*
 * This file Copyright (C) 2008-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <memory>
#include <string>

#include <glibmm.h>
#include <gtkmm.h>

class Session;

class FreeSpaceLabel : public Gtk::Label
{
public:
    FreeSpaceLabel(Glib::RefPtr<Session> const& core, std::string const& dir = {});
    ~FreeSpaceLabel() override;

    void set_dir(std::string const& dir);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
