/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "qt/Prefs.h"

#include "tests/helpers/sandbox.h"

#include "gtest/gtest.h"

#include <set>

namespace transmission
{

namespace tests
{

class PrefsTest : public helpers::SandboxedTest
{
protected:
    void SetUp() override
    {
        SandboxedTest::SetUp();
        prefs_ = std::make_unique<Prefs>(sandboxDir().c_str());
    }

    void TearDown() override
    {
        prefs_.reset();
        SandboxedTest::TearDown();
    }

    std::unique_ptr<Prefs> prefs_;
};

TEST_F(PrefsTest, canSetString)
{
    auto constexpr Key = Prefs::DOWNLOAD_DIR;
    auto const expected = QStringLiteral("/home/ilyak/Загрузки");
    prefs_->set(Key, expected);

    auto const actual = prefs_->getString(Key);
    EXPECT_EQ(expected, actual);
}

TEST_F(PrefsTest, emitsChanged)
{
    auto constexpr Key = Prefs::DOWNLOAD_DIR;
    using collection = std::set<int>;
    auto changed_properties = collection{};
    auto const expected_pre = collection{}; 
    auto const expected_post = collection{ Key }; 

    auto on_received = [&](int id){ changed_properties.insert(id); };
    QObject::connect(prefs_.get(), &Prefs::changed, on_received);

    EXPECT_EQ(expected_pre, changed_properties);
    prefs_->set(Key, QStringLiteral("/some/path"));
    EXPECT_EQ(expected_post, changed_properties);
}

} // namespace tests

} // namespace transmission
