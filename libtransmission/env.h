// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <string_view>

/** @brief Check if environment variable exists. */
[[nodiscard]] bool tr_env_key_exists(char const* key) noexcept;

/** @brief Get environment variable value as string. */
[[nodiscard]] std::string tr_env_get_string(std::string_view key, std::string_view default_value = {});
