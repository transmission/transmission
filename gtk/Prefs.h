// This file Copyright © 2005-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h> /* tr_variant, tr_session */
#include <libtransmission/quark.h>

#include <cstdint> // int64_t
#include <string>
#include <string_view>
#include <vector>

void gtr_pref_init(std::string_view config_dir);

int64_t gtr_pref_int_get(tr_quark key);
void gtr_pref_int_set(tr_quark key, int64_t value);

double gtr_pref_double_get(tr_quark key);
void gtr_pref_double_set(tr_quark key, double value);

bool gtr_pref_flag_get(tr_quark key);
void gtr_pref_flag_set(tr_quark key, bool value);

std::vector<std::string> gtr_pref_strv_get(tr_quark key);

std::string gtr_pref_string_get(tr_quark key);
void gtr_pref_string_set(tr_quark key, std::string_view value);

void gtr_pref_save(tr_session* /*session*/);
tr_variant* gtr_pref_get_all();
