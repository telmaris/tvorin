#include "core/CheckedStateReader.h"
#include "core/GameWorld.h"
#include "core/PersistenceLimits.h"

#include <gtest/gtest.h>

#include <limits>
#include <filesystem>
#include <string>

TEST(PersistenceValidationTests, CheckedReaderRejectsInvalidCountsAndNonFiniteNumbers)
{
    CheckedStateReader negative("-1", "count");
    EXPECT_FALSE(negative.ReadCount(10).HasValue());

    CheckedStateReader tooLarge("11", "count");
    EXPECT_FALSE(tooLarge.ReadCount(10).HasValue());

    CheckedStateReader nonFinite("nan", "double");
    EXPECT_FALSE(nonFinite.ReadFiniteDouble(-1.0, 1.0).HasValue());
}

TEST(PersistenceValidationTests, MapAreaIsCheckedBeforeAllocation)
{
    std::size_t area = 0;
    EXPECT_TRUE(PersistenceLimits::CheckedArea(401, 401, area));
    EXPECT_EQ(area, 401u * 401u);
    EXPECT_TRUE(PersistenceLimits::CheckedArea(501, 501, area));
    EXPECT_EQ(area, 501u * 501u);
    EXPECT_FALSE(PersistenceLimits::CheckedArea(502, 1, area));
    EXPECT_FALSE(PersistenceLimits::CheckedArea(1001, 1001, area));
    EXPECT_FALSE(PersistenceLimits::CheckedArea(std::numeric_limits<int>::max(), 2, area));
}

TEST(PersistenceValidationTests, InvalidStateDoesNotMutateExistingWorld)
{
    MapParameters params;
    params.sizePreset = MapSizePreset::S;
    params.seed = 20260820;

    GameWorld world;
    ASSERT_TRUE(world.InitWorld("persistence-validation", nullptr, params));
    const std::uint64_t before = world.BuildChecksum();
    std::string payload = world.SerializeSimulationState();

    const std::string marker = "PARAMS ";
    const std::size_t markerPos = payload.find(marker);
    ASSERT_NE(markerPos, std::string::npos);
    const std::size_t dimensionsEnd = payload.find(' ', markerPos + marker.size());
    ASSERT_NE(dimensionsEnd, std::string::npos);
    const std::size_t secondDimensionEnd = payload.find(' ', dimensionsEnd + 1);
    ASSERT_NE(secondDimensionEnd, std::string::npos);
    payload.replace(markerPos + marker.size(),
                    secondDimensionEnd - (markerPos + marker.size()),
                    "1002 1002");

    EXPECT_FALSE(world.RestoreSimulationState(payload));
    EXPECT_EQ(world.BuildChecksum(), before);
}

TEST(PersistenceValidationTests, PreReworkSaveVersionsAreRejected)
{
    const std::filesystem::path fixtureRoot =
        std::filesystem::current_path() / "tests" / "fixtures";
    for (int version = 30; version <= 34; ++version)
    {
        SCOPED_TRACE(version);
        GameWorld world;
        EXPECT_FALSE(world.LoadFromFile(
            (fixtureRoot / ("save_v" + std::to_string(version) + ".rts")).string(),
            nullptr));
    }
}

TEST(PersistenceValidationTests, TrailingStateTokensAreRejectedWithoutMutation)
{
    GameWorld world;
    world.InitWorld("trailing-token-guard", nullptr, MapParameters{});
    const std::uint64_t before = world.BuildChecksum();
    std::string payload = world.SerializeSimulationState();
    ASSERT_FALSE(payload.empty());
    payload += "\nUNEXPECTED_TRAILING_TOKEN\n";

    EXPECT_FALSE(world.RestoreSimulationState(payload));
    EXPECT_EQ(world.BuildChecksum(), before);
}
