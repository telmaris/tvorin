#include "core/GameSnapshot.h"
#include "core/GameWorld.h"

#include <gtest/gtest.h>

namespace
{
    void ExpectColorEquals(Color actual, Color expected)
    {
        EXPECT_EQ(actual.r, expected.r);
        EXPECT_EQ(actual.g, expected.g);
        EXPECT_EQ(actual.b, expected.b);
        EXPECT_EQ(actual.a, expected.a);
    }
}

TEST(GameSnapshotTests, RoundTripPreservesPlayerPaletteAndBuildingOwner)
{
    GameSnapshot original;
    original.simulationTick = 1234;
    original.localPlayerId = 7;
    original.mapSize = {2, 1};
    original.globalMapView.fogOfWarEnabled = false;
    original.players = {
        {7, Color{31, 163, 255, 255}},
        {11, Color{220, 73, 91, 255}},
    };

    GameSnapshotTile ownedBuilding;
    ownedBuilding.terrainTextureId = 12;
    ownedBuilding.hasBuilding = true;
    ownedBuilding.buildingType = BuildingType::Barracks;
    ownedBuilding.buildingFootprint = {2, 3};
    ownedBuilding.buildingUpgradeLevel = 3;
    ownedBuilding.buildingOwnerId = 11;
    ownedBuilding.isBuildingOperational = true;
    ownedBuilding.isBuildingUpgrading = true;
    ownedBuilding.roadDisconnected = true;
    ownedBuilding.roadUtilization = 0.8f;
    ownedBuilding.roadSaturated = true;
    original.tiles = {ownedBuilding, GameSnapshotTile{}};

    GameSnapshot restored;
    ASSERT_TRUE(GameSnapshot::TryDeserialize(original.Serialize(), restored));

    EXPECT_EQ(restored.simulationTick, original.simulationTick);
    EXPECT_EQ(restored.localPlayerId, original.localPlayerId);
    EXPECT_EQ(restored.mapSize.x, original.mapSize.x);
    EXPECT_EQ(restored.mapSize.y, original.mapSize.y);
    EXPECT_EQ(restored.globalMapView.fogOfWarEnabled,
              original.globalMapView.fogOfWarEnabled);
    EXPECT_EQ(restored.players, original.players);
    EXPECT_EQ(restored.tiles, original.tiles);
}

TEST(GameSnapshotTests, DeltaUpdatesBuildingOwnerWithoutReplacingPalette)
{
    GameSnapshot snapshot;
    snapshot.simulationTick = 10;
    snapshot.mapSize = {1, 1};
    snapshot.players = {
        {1, RED},
        {2, BLUE},
    };
    snapshot.tiles.resize(1);

    GameSnapshotDelta delta;
    delta.simulationTick = 20;
    delta.mapSize = snapshot.mapSize;
    delta.changes = {{0, GameSnapshotTile{}}};
    delta.changes.front().tile.hasBuilding = true;
    delta.changes.front().tile.buildingType = BuildingType::Barracks;
    delta.changes.front().tile.buildingUpgradeLevel = 2;
    delta.changes.front().tile.buildingOwnerId = 2;
    delta.changes.front().tile.isBuildingOperational = true;
    delta.changes.front().tile.isBuildingUpgrading = true;
    delta.changes.front().tile.roadDisconnected = true;
    delta.changes.front().tile.roadUtilization = 0.6f;
    delta.changes.front().tile.roadSaturated = true;

    GameSnapshotDelta restored;
    ASSERT_TRUE(GameSnapshotDelta::TryDeserialize(delta.Serialize(), restored));
    ASSERT_TRUE(restored.ApplyTo(snapshot));

    EXPECT_EQ(snapshot.simulationTick, 20);
    EXPECT_EQ(snapshot.players[0], (GameSnapshotPlayer{1, RED}));
    EXPECT_EQ(snapshot.players[1], (GameSnapshotPlayer{2, BLUE}));
    EXPECT_EQ(snapshot.tiles[0].buildingOwnerId, 2);
    EXPECT_EQ(snapshot.tiles[0].buildingUpgradeLevel, 2);
    EXPECT_TRUE(snapshot.tiles[0].isBuildingOperational);
    EXPECT_TRUE(snapshot.tiles[0].isBuildingUpgrading);
    EXPECT_TRUE(snapshot.tiles[0].roadDisconnected);
    EXPECT_FLOAT_EQ(snapshot.tiles[0].roadUtilization, 0.6f);
    EXPECT_TRUE(snapshot.tiles[0].roadSaturated);
}

TEST(GameSnapshotTests, RejectsAnOlderSnapshotVersion)
{
    GameSnapshot snapshot;
    snapshot.mapSize = {1, 1};
    snapshot.tiles.resize(1);

    std::string payload = snapshot.Serialize();
    payload.replace(0, std::to_string(SerializationVersion::GameSnapshotVersion).size(), "7");

    GameSnapshot parsed;
    EXPECT_FALSE(GameSnapshot::TryDeserialize(payload, parsed));
}

TEST(GameSnapshotTests, RejectsOversizedMapBeforeAllocatingTiles)
{
    GameSnapshot parsed;
    const std::string payload = std::to_string(SerializationVersion::GameSnapshotVersion) +
        " 0 0 2147483647 2147483647 0";
    EXPECT_FALSE(GameSnapshot::TryDeserialize(payload, parsed));
}

TEST(GameSnapshotTests, RejectsOversizedDeltaBeforeAllocatingChanges)
{
    GameSnapshotDelta parsed;
    EXPECT_FALSE(GameSnapshotDelta::TryDeserialize("0 2147483647 2147483647 1", parsed));
}

TEST(GameSnapshotTests, ResolvesOwnerColorsAndFallsBackForNeutralBuildings)
{
    GameSnapshot snapshot;
    snapshot.players = {
        {3, Color{80, 120, 210, 255}},
    };

    ExpectColorEquals(ResolveSnapshotPlayerColor(snapshot, 3), Color{80, 120, 210, 255});
    ExpectColorEquals(ResolveSnapshotPlayerColor(snapshot, -1), WHITE);
    ExpectColorEquals(ResolveSnapshotPlayerColor(snapshot, 999, MAGENTA), MAGENTA);
}

TEST(GameSnapshotTests, RoundTripPreservesAppliedWorldEventEffects)
{
    GameSnapshot original;
    original.mapSize = {1, 1};
    original.tiles.resize(1);
    WorldEventNotificationView notification;
    notification.instanceId = 42;
    notification.ownerId = 0;
    notification.definitionId = "frontier_harvest";
    notification.title = "Harvest";
    notification.description = "A test event";
    notification.appliedEffects = {
        {AppliedWorldEventEffectKind::ResourceDelta, ResourceType::WHEAT, 2},
        {AppliedWorldEventEffectKind::TimedModifier, ResourceType::Null, 0,
         BalanceStat::ProductionOutputAmount, 0.0, 1.1, 6000},
        {AppliedWorldEventEffectKind::BuildingLoss, ResourceType::Null, 1},
        {AppliedWorldEventEffectKind::RaidStarted, ResourceType::Null, 20},
    };
    original.eventNotifications.push_back(notification);

    GameSnapshot restored;
    ASSERT_TRUE(GameSnapshot::TryDeserialize(original.Serialize(), restored));
    ASSERT_EQ(restored.eventNotifications.size(), 1u);
    ASSERT_EQ(restored.eventNotifications.front().appliedEffects.size(), 4u);
    EXPECT_EQ(restored.eventNotifications.front().appliedEffects[0].resourceType,
              ResourceType::WHEAT);
    EXPECT_EQ(restored.eventNotifications.front().appliedEffects[0].amount, 2);
    EXPECT_EQ(restored.eventNotifications.front().appliedEffects[1].stat,
              BalanceStat::ProductionOutputAmount);
    EXPECT_DOUBLE_EQ(restored.eventNotifications.front().appliedEffects[1].multiplier, 1.1);
    EXPECT_EQ(restored.eventNotifications.front().appliedEffects[1].durationTicks, 6000u);
    EXPECT_EQ(restored.eventNotifications.front().appliedEffects[2].kind,
              AppliedWorldEventEffectKind::BuildingLoss);
    EXPECT_EQ(restored.eventNotifications.front().appliedEffects[3].kind,
              AppliedWorldEventEffectKind::RaidStarted);
    EXPECT_EQ(restored.eventNotifications.front().appliedEffects[3].amount, 20);
}

TEST(GameSnapshotTests, PublicEventFeedDoesNotRevealAnUndiscoveredProvince)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 20260904u;

    GameWorld world;
    ASSERT_TRUE(world.InitWorld("event-visibility", nullptr, parameters));

    const ProvinceId homeId = world.GetLocalProvinceId();
    ProvinceId hiddenProvinceId = InvalidProvinceId;
    for (const ProvinceId provinceId : world.GetGlobalMap().GetProvinceIds())
    {
        if (provinceId != homeId &&
            world.GetGlobalMap().FindProvince(provinceId)->GetKnowledge(0) ==
                ProvinceKnowledgeLevel::Hidden)
        {
            hiddenProvinceId = provinceId;
            break;
        }
    }
    ASSERT_NE(hiddenProvinceId, InvalidProvinceId);

    WorldEventNotificationView notification;
    notification.instanceId = 1;
    notification.provinceId = hiddenProvinceId;
    notification.definitionId = "public_event";
    world.GetEventSystem().GetFeed().Push(notification);

    const GameSnapshot hiddenSnapshot = world.BuildSnapshot();
    EXPECT_TRUE(std::none_of(hiddenSnapshot.eventNotifications.begin(),
                             hiddenSnapshot.eventNotifications.end(),
                             [hiddenProvinceId](const auto& event)
                             {
                                 return event.provinceId == hiddenProvinceId;
                             }));

    ASSERT_TRUE(world.GetGlobalMap().SetKnowledge(
        0, hiddenProvinceId, ProvinceKnowledgeLevel::Scouted));
    const GameSnapshot visibleSnapshot = world.BuildSnapshot();
    EXPECT_TRUE(std::any_of(visibleSnapshot.eventNotifications.begin(),
                            visibleSnapshot.eventNotifications.end(),
                            [hiddenProvinceId](const auto& event)
                            {
                                return event.provinceId == hiddenProvinceId;
                            }));
}
