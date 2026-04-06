// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QTemporaryDir>

#include "TrQtInit.h"

class BasicTest
{
public:
    BasicTest()
    {
        trqt::trqt_init();
    }

    BasicTest(BasicTest&&) = delete;
    BasicTest(BasicTest const&) = delete;
    BasicTest& operator=(BasicTest&&) = delete;
    BasicTest& operator=(BasicTest const&) = delete;
    virtual ~BasicTest() = default;
};

class SandboxedTest : public BasicTest
{
public:
    SandboxedTest() = default;
    SandboxedTest(SandboxedTest&&) = delete;
    SandboxedTest(SandboxedTest const&) = delete;
    SandboxedTest& operator=(SandboxedTest&&) = delete;
    SandboxedTest& operator=(SandboxedTest const&) = delete;
    ~SandboxedTest() override = default;

    [[nodiscard]] bool isValid() const
    {
        return sandbox_.isValid();
    }

    [[nodiscard]] QString sandboxDir() const
    {
        return sandbox_.path();
    }

private:
    QTemporaryDir sandbox_{};
};
