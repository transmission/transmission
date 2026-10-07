// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <string>

std::string get_cache_dir();
std::string get_mmdb_file_path();

bool test_and_open_mmdb();
void close_mmdb();

bool decompress_gz_file(std::string filename);

void maintain_mmdb_file_async();
void maintain_mmdb_file();

std::string get_location_from_ip(std::string const& ip);
