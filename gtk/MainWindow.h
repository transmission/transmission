/******************************************************************************
 * Copyright (c) Transmission authors and contributors
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

#pragma once

#include <memory>

#include <glibmm.h>
#include <gtkmm.h>

class Session;

class MainWindow : public Gtk::ApplicationWindow
{
public:
    ~MainWindow() override;

    static std::unique_ptr<MainWindow> create(
        Gtk::Application& app,
        Glib::RefPtr<Gio::ActionGroup> const& actions,
        Glib::RefPtr<Session> const& core);

    Glib::RefPtr<Gtk::TreeSelection> get_selection() const;

    void set_busy(bool isBusy);
    void refresh();

protected:
    MainWindow(Gtk::Application& app, Glib::RefPtr<Gio::ActionGroup> const& actions, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
