// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include "libtransmission/global-ip-cache.h"
#include "libtransmission/net.h"
#include "libtransmission/utils.h"
#include "libtransmission/web.h"

#include "test-fixtures.h"

using GlobalIPTest = ::testing::Test;

class globalIPTestMediator final : public tr_web::Mediator
{
    [[nodiscard]] time_t now() const override
    {
        return tr_time();
    }
};

TEST_F(GlobalIPTest, bindAddr)
{
    auto mediator_ = globalIPTestMediator{};
    auto web_ = tr_web::create(mediator_);

    auto constexpr ipv4_str = std::string_view{ "8.8.8.8" };
    auto const ipv4_addr = std::optional{ tr_address::from_string(ipv4_str) };
}