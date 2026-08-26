#include "simulation/RoadPriorityArbitration.h"

#include <gtest/gtest.h>

TEST(RoadPriorityArbitrationTests, MatchingProductPrecedesOrdinaryProducts)
{
    std::vector<RoadPriorityCandidate> candidates{
        {ResourceType::STONE, 4},
        {ResourceType::WOOD, 1},
        {ResourceType::WOOD, 3},
        {ResourceType::IRON, 2}};

    SortRoadPriorityCandidates(ResourceType::WOOD, candidates);

    ASSERT_EQ(candidates.size(), 4u);
    EXPECT_EQ(candidates[0].shipmentId, 1u);
    EXPECT_EQ(candidates[1].shipmentId, 3u);
    EXPECT_EQ(candidates[2].shipmentId, 2u);
    EXPECT_EQ(candidates[3].shipmentId, 4u);
}

TEST(RoadPriorityArbitrationTests, NullPriorityPreservesShipmentIdOrder)
{
    std::vector<RoadPriorityCandidate> candidates{
        {ResourceType::STONE, 8}, {ResourceType::WOOD, 2}, {ResourceType::IRON, 5}};

    SortRoadPriorityCandidates(ResourceType::Null, candidates);

    EXPECT_EQ(candidates[0].shipmentId, 2u);
    EXPECT_EQ(candidates[1].shipmentId, 5u);
    EXPECT_EQ(candidates[2].shipmentId, 8u);
}
