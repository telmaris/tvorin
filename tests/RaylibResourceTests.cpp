#include "ui/RaylibResource.h"
#include "ui/UiAssetContext.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace
{
    struct FakeResource
    {
        int id{0};
    };

    struct FakeResourceTraits
    {
        static inline int releaseCount{0};
        static inline int lastReleasedId{0};

        static bool IsValid(const FakeResource& value) noexcept
        {
            return value.id != 0;
        }

        static void Release(FakeResource value) noexcept
        {
            ++releaseCount;
            lastReleasedId = value.id;
        }
    };

    using FakeHandle = tvorin::ui::RaylibResource<FakeResource, FakeResourceTraits>;

    class RaylibResourceTests : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            FakeResourceTraits::releaseCount = 0;
            FakeResourceTraits::lastReleasedId = 0;
        }
    };
}

static_assert(!std::is_copy_constructible_v<tvorin::ui::TextureHandle>);
static_assert(!std::is_copy_assignable_v<tvorin::ui::TextureHandle>);
static_assert(std::is_nothrow_move_constructible_v<tvorin::ui::TextureHandle>);
static_assert(std::is_nothrow_move_assignable_v<tvorin::ui::TextureHandle>);

TEST_F(RaylibResourceTests, MoveTransfersOwnershipAndResetReleasesExactlyOnce)
{
    FakeHandle first(FakeResource{17});
    FakeHandle second(std::move(first));

    EXPECT_FALSE(first.IsValid());
    EXPECT_TRUE(second.IsValid());
    second.Reset();
    second.Reset();

    EXPECT_EQ(FakeResourceTraits::releaseCount, 1);
    EXPECT_EQ(FakeResourceTraits::lastReleasedId, 17);
}

TEST_F(RaylibResourceTests, FailedOrEmptyResourceDoesNotCallTheDeleter)
{
    FakeHandle empty(FakeResource{});
    empty.Reset();

    EXPECT_EQ(FakeResourceTraits::releaseCount, 0);
}

TEST_F(RaylibResourceTests, ClosedContextPreventsLateRaylibRelease)
{
    tvorin::ui::UiAssetContext context;
    FakeHandle handle(FakeResource{23}, context.Token());

    context.Close();
    handle.Reset();

    EXPECT_EQ(FakeResourceTraits::releaseCount, 0);
}

TEST(UiAssetContextTests, MissingTextureIsNotCached)
{
    tvorin::ui::UiAssetContext context;
    EXPECT_EQ(context.LoadTexture("assets/__missing_texture_for_test__.png"), nullptr);
    EXPECT_TRUE(context.IsOpen());
}
