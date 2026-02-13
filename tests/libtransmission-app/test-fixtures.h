// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <gtest/gtest.h>

#include "tr/app/app.h"

class TransmissionTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tr::app::init();
    }
};
