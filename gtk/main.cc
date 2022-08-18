// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <cstdio>
#include <string>

#include <glibmm.h>
#include <glibmm/i18n.h>
#include <gtkmm.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "Application.h"
#include "Notify.h"
#include "Prefs.h"
#include "Utils.h"

namespace
{

auto const* const AppConfigDirName = "transmission";
auto const* const AppTranslationDomainName = "transmission-gtk";
auto const* const AppName = "transmission-gtk";

Glib::OptionEntry create_option_entry(Glib::ustring const& long_name, gchar short_name, Glib::ustring const& description)
{
    Glib::OptionEntry entry;
    entry.set_long_name(long_name);
    entry.set_short_name(short_name);
    entry.set_description(description);
    return entry;
}

} // namespace

int main(int argc, char** argv)
{
    /* init i18n */
    setlocale(LC_ALL, "");
    bindtextdomain(AppTranslationDomainName, TRANSMISSIONLOCALEDIR);
    bind_textdomain_codeset(AppTranslationDomainName, "UTF-8");
    textdomain(AppTranslationDomainName);

    /* init glib/gtk */
    Glib::init();
    Glib::set_application_name(_("Transmission"));

    /* default settings */
    std::string config_dir;
    bool show_version = false;
    bool start_paused = false;
    bool is_iconified = false;

    /* parse the command line */
    auto const config_dir_option = create_option_entry("config-dir", 'g', _("Where to look for configuration files"));
    auto const paused_option = create_option_entry("paused", 'p', _("Start with all torrents paused"));
    auto const minimized_option = create_option_entry("minimized", 'm', _("Start minimized in notification area"));
    auto const version_option = create_option_entry("version", 'v', _("Show version number and exit"));

    Glib::OptionGroup main_group({}, {});
    main_group.add_entry_filename(config_dir_option, config_dir);
    main_group.add_entry(paused_option, start_paused);
    main_group.add_entry(minimized_option, is_iconified);
    main_group.add_entry(version_option, show_version);

    Glib::OptionContext option_context(_("[torrent files or urls]"));
    option_context.set_main_group(main_group);
    Gtk::Main::add_gtk_option_group(option_context);
    option_context.set_translation_domain(GETTEXT_PACKAGE);

    try
    {
        option_context.parse(argc, argv);
    }
    catch (Glib::OptionError const& e)
    {
        g_print(_("%s\nRun '%s --help' to see a full list of available command line options.\n"), e.what().c_str(), argv[0]);
        return 1;
    }

    /* handle the trivial "version" option */
    if (show_version)
    {
        fprintf(stderr, "%s %s\n", AppName, LONG_VERSION_STRING);
        return 0;
    }

    /* init the unit formatters */
    tr_formatter_mem_init(mem_K, _(mem_K_str), _(mem_M_str), _(mem_G_str), _(mem_T_str));
    tr_formatter_size_init(disk_K, _(disk_K_str), _(disk_M_str), _(disk_G_str), _(disk_T_str));
    tr_formatter_speed_init(speed_K, _(speed_K_str), _(speed_M_str), _(speed_G_str), _(speed_T_str));

    /* set up the config dir */
    if (std::empty(config_dir))
    {
        config_dir = tr_getDefaultConfigDir(AppConfigDirName);
    }

    gtr_pref_init(config_dir);
    g_mkdir_with_parents(config_dir.c_str(), 0755);

    /* init notifications */
    gtr_notify_init();

    /* init the application for the specified config dir */
    return Application(config_dir, start_paused, is_iconified).run(argc, argv);
}
