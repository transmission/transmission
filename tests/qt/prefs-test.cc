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
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        SandboxedTest::SetUp();
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        prefs_ = std::make_unique<Prefs>(sandboxDir().c_str());
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    }

    void TearDown() override
    {
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        prefs_.reset();
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        SandboxedTest::TearDown();
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    }

    std::unique_ptr<Prefs> prefs_;
};

TEST_F(PrefsTest, canSetString)
{
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto constexpr Key = Prefs::DOWNLOAD_DIR;
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto const expected = QStringLiteral("/home/ilyak/Загрузки");
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    prefs_->set(Key, expected);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;

    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto const actual = prefs_->getString(Key);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_EQ(expected, actual);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
}

TEST_F(PrefsTest, emitsChanged)
{
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto constexpr Key = Prefs::DOWNLOAD_DIR;
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    using collection = std::set<int>;
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto changed_properties = collection{};
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto const expected_pre = collection{};
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto const expected_post = collection{ Key };
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;

    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    auto on_received = [&](int id) { changed_properties.insert(id); };
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    QObject::connect(prefs_.get(), &Prefs::changed, on_received);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;

    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_EQ(expected_pre, changed_properties);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    prefs_->set(Key, QStringLiteral("/some/path"));
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    EXPECT_EQ(expected_post, changed_properties);
    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
}

} // namespace tests

} // namespace transmission
