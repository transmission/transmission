// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifdef ENABLE_GETTEXT
#include <libintl.h>
#define _ gettext
#define tr_ngettext ngettext
#else
#define _(a) (a)
#define tr_ngettext(singular, plural, count) ((count) == 1 ? (singular) : (plural))
#endif
