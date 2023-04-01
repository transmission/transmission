// This file Copyright © 2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "item-queue.h"

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <optional>
#include <vector>

#if 0
template<typename Type>
class tr_download_queue
{
public:
    void move_top(Type const* items, size_t item_count);
    void move_up(Type const* items, size_t item_count);
    void move_down(Type const* items, size_t item_count);
    void move_bottom(Type const* items, size_t item_count);

    void set_position(Type item, size_t pos);

    [[nodiscard]] size_t get_position(Type id) const;

    [[nodiscard]] std::optional<Type> pop() noexcept;

    [[nodiscard]] std::vector<Type> queue() const;
};
#endif
