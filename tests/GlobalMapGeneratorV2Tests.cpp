#include "world/GlobalMap.h"
#include "core/PersistenceLimits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <tuple>

namespace
{
    struct CanonicalTopology
    {
        std::vector<std::tuple<ProvinceId, int, int, ProvinceKind>> provinces;
        std::vector<std::tuple<ProvinceConnectionId, ProvinceId, ProvinceId, int>> connections;

        bool operator==(const CanonicalTopology&) const = default;
    };

    CanonicalTopology CaptureTopology(const GlobalMap& map)
    {
        CanonicalTopology result;
        for (const ProvinceId id : map.GetProvinceIds())
        {
            const auto* province = map.FindProvince(id);
            result.provinces.emplace_back(id, province->GetLayoutPosition().x,
                                          province->GetLayoutPosition().y,
                                          province->GetKind());
        }
        for (const ProvinceConnectionId id : map.GetConnectionIds())
        {
            const auto* connection = map.FindConnection(id);
            result.connections.emplace_back(id, connection->GetFirstProvinceId(),
                                            connection->GetSecondProvinceId(),
                                            connection->GetLevel());
        }
        return result;
    }

    long long DistanceSquared(Vec2i first, Vec2i second)
    {
        const long long dx = static_cast<long long>(first.x) - second.x;
        const long long dy = static_cast<long long>(first.y) - second.y;
        return dx * dx + dy * dy;
    }
}

TEST(GlobalMapGeneratorV2Tests, ThousandSeedsAreBoundedConnectedAndCanonical)
{
    GlobalMapGenerationParameters parameters;
    parameters.provinceCount = 32;
    parameters.extraEdgeCount = 12;

    for (std::uint32_t seed = 0; seed < 1000; ++seed)
    {
        parameters.seed = seed;
        const auto first = GlobalMapGenerator::Generate(parameters, 2);
        const auto second = GlobalMapGenerator::Generate(parameters, 2);
        ASSERT_TRUE(first.success) << "seed=" << seed << ": " << first.failureReason;
        ASSERT_TRUE(second.success) << "seed=" << seed << ": " << second.failureReason;
        EXPECT_EQ(CaptureTopology(first.map), CaptureTopology(second.map));
        EXPECT_EQ(first.homeProvinceByPlayer, second.homeProvinceByPlayer);
        EXPECT_TRUE(first.map.IsConnected());
        EXPECT_LE(first.map.GetEdgeCount(), PersistenceLimits::MaxGlobalEdges);

        std::set<std::pair<int, int>> positions;
        int buildableCount = 0;
        int cityCount = 0;
        int banditCount = 0;
        int eventCount = 0;
        for (const ProvinceId id : first.map.GetProvinceIds())
        {
            const auto* province = first.map.FindProvince(id);
            ASSERT_NE(province, nullptr);
            const Vec2i position = province->GetLayoutPosition();
            EXPECT_LE(DistanceSquared(position, Vec2i{}),
                      static_cast<long long>(parameters.layoutRadius) * parameters.layoutRadius);
            EXPECT_TRUE(positions.emplace(position.x, position.y).second);
            if (province->GetKind() == ProvinceKind::Buildable)
            {
                ++buildableCount;
                const auto* buildable = first.map.FindBuildableProvince(id);
                ASSERT_NE(buildable, nullptr);
                EXPECT_FALSE(buildable->GetParameters().definitionId.empty());
                EXPECT_GT(buildable->GetParameters().sizeX, 0);
                EXPECT_GT(buildable->GetParameters().sizeY, 0);
            }
            else if (province->GetKind() == ProvinceKind::NeutralSettlement)
                ++cityCount;
            else if (province->GetKind() == ProvinceKind::BanditCamp)
                ++banditCount;
            else if (province->GetKind() == ProvinceKind::TreasureSite)
                ++eventCount;
        }
        EXPECT_GE(buildableCount, 2 + parameters.minimumNeutralBuildables);
        EXPECT_GE(cityCount, 1);
        EXPECT_GE(banditCount, 1);
        EXPECT_GE(eventCount, 1);

        for (const auto& [playerId, homeId] : first.homeProvinceByPlayer)
        {
            const auto* home = first.map.FindBuildableProvince(homeId);
            ASSERT_NE(home, nullptr);
            const long long interiorRadius = parameters.layoutRadius - parameters.startBoundaryClearance;
            EXPECT_LE(DistanceSquared(home->GetLayoutPosition(), Vec2i{}),
                      interiorRadius * interiorRadius);
            EXPECT_GE(first.map.GetNeighbors(homeId).size(), 2u);
            EXPECT_EQ(home->GetOwnerId(), playerId);
            EXPECT_EQ(home->GetKnowledge(playerId), ProvinceKnowledgeLevel::Owned);
        }

        std::set<std::pair<ProvinceId, ProvinceId>> edgePairs;
        for (const ProvinceConnectionId connectionId : first.map.GetConnectionIds())
        {
            const auto* connection = first.map.FindConnection(connectionId);
            ASSERT_NE(connection, nullptr);
            EXPECT_TRUE(edgePairs.emplace(connection->GetFirstProvinceId(),
                                          connection->GetSecondProvinceId()).second);
            EXPECT_LT(connection->GetFirstProvinceId(), connection->GetSecondProvinceId());
        }
    }
}

TEST(GlobalMapGeneratorV2Tests, WealthScaleDoesNotChangeLayoutTypesOrTopology)
{
    GlobalMapGenerationParameters firstParameters;
    firstParameters.seed = 0x20260904u;
    firstParameters.provinceCount = 24;
    firstParameters.extraEdgeCount = 10;
    GlobalMapGenerationParameters secondParameters = firstParameters;
    secondParameters.buildableWealthScale = 1.8;
    secondParameters.cityWealthScale = 2.0;
    secondParameters.banditStrengthScale = 0.5;

    const auto first = GlobalMapGenerator::Generate(firstParameters, 2);
    const auto second = GlobalMapGenerator::Generate(secondParameters, 2);
    ASSERT_TRUE(first.success) << first.failureReason;
    ASSERT_TRUE(second.success) << second.failureReason;
    EXPECT_EQ(CaptureTopology(first.map), CaptureTopology(second.map));
    EXPECT_EQ(first.homeProvinceByPlayer, second.homeProvinceByPlayer);

    bool selectedValueChanged = false;
    for (const ProvinceId id : first.map.GetProvinceIds())
    {
        const auto* firstProvince = first.map.FindBuildableProvince(id);
        const auto* secondProvince = second.map.FindBuildableProvince(id);
        if (firstProvince == nullptr || secondProvince == nullptr)
            continue;
        selectedValueChanged |= firstProvince->GetParameters().resourceWealth !=
                                secondProvince->GetParameters().resourceWealth;
    }
    EXPECT_TRUE(selectedValueChanged);
}

TEST(GlobalMapGeneratorV2Tests, PlacementFailureReturnsNoPartialMap)
{
    GlobalMapGenerationParameters parameters;
    parameters.seed = 99;
    parameters.provinceCount = 5;
    parameters.layoutRadius = 10;
    parameters.minimumLayoutSpacing = 20;
    parameters.maximumPlacementAttemptsPerProvince = 2;

    const auto result = GlobalMapGenerator::Generate(parameters, 1);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.failureReason.empty());
    EXPECT_EQ(result.map.GetProvinceCount(), 0u);
    EXPECT_EQ(result.map.GetConnectionCount(), 0u);
    EXPECT_TRUE(result.homeProvinceByPlayer.empty());
}
