// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <string>

size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream);

void maintain_mmdb_file(std::string mmdb_file);

std::string get_location_from_ip(std::string ip);
