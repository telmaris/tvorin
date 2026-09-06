#include "warfare/UnitDefinition.h"
#include "warfare/BattleUnit.h"
#include "economy/Player.h"
#include "economy/BuildingConfig.h"
#include "simulation/MapGenerator.h"
#include "core/GameWorld.h"

#include <gtest/gtest.h>

#include <fstream>

#include <filesystem>

namespace
{
    std::filesystem::path WriteUnitFixture(const std::string& text)
    {
        const auto path = std::filesystem::temp_directory_path() / "rts_unit_fixture.rtsdata";
        std::ofstream file(path);
        file << text;
        return path;
    }

    void FillOwnedGrass(TileMap& map, Player* owner, int width = 8, int height = 8)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
        {
            Tile tile{i};
            tile.owner = owner;
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }

    // Places a fully-constructed, fully-stocked Barracks ready to recruit
    // "militia" (cost: 3 FOOD_PROVISIONS + 5 manpower per assets/data/units.rtsdata).
    Barracks* PlaceReadyBarracks(TileMap& map, Player& player, Vec2i anchor = {1, 1})
    {
        auto* barracks = dynamic_cast<Barracks*>(
            map.PlaceLoadedBuilding(map.GetIdFromCoords(anchor), &player, std::make_unique<Barracks>(1)));
        if (barracks == nullptr)
            return nullptr;
        barracks->constructionRemaining = 0.0;
        barracks->storage.buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 40};
        barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(40);
        player.strategicResources.Set(StrategicResourceType::Manpower, 100);
        return barracks;
    }
}

TEST(BattleUnitTests, CatalogLoadsValidDefinitionsAndRejectsInvalidOnes)
{
    const auto path = WriteUnitFixture(R"DATA(
unit militia
    name "Militia"
    max_hp 20
    field_attack 3
    siege_power 1
    recruit_building Barracks
    requires_tech militia_drill
    recruit_time 8
    manpower_cost 5
    cost FOOD_PROVISIONS 5
end
unit broken
    name "Broken"
    max_hp 0
end
)DATA");

    const auto catalog = LoadUnitDefinitionsFromFile(path.string());
    EXPECT_EQ(catalog.size(), 1u);
    ASSERT_TRUE(catalog.contains("militia"));
    EXPECT_EQ(catalog.at("militia").displayName, "Militia");
    EXPECT_DOUBLE_EQ(catalog.at("militia").maxHp, 20.0);
    EXPECT_EQ(catalog.at("militia").requiredTechnology, "militia_drill");
    ASSERT_EQ(catalog.at("militia").cost.size(), 1u);
    EXPECT_EQ(catalog.at("militia").cost.front().type, ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(catalog.at("militia").cost.front().amount, 5);
    EXPECT_FALSE(catalog.contains("broken"));

    std::filesystem::remove(path);
}

TEST(BattleUnitTests, TechnologyGatedUnitCannotBeQueuedBeforeResearch)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    Barracks* barracks = PlaceReadyBarracks(map, player);
    ASSERT_NE(barracks, nullptr);

    EXPECT_EQ(barracks->recruitment.DiagnoseRecruitmentBlock(*barracks, "catapult"),
              "Requires tech: catapult_construction");
    EXPECT_FALSE(barracks->recruitment.QueueRecruitment(*barracks, "catapult"));

    player.technologies.RestoreTechnology("catapult_construction");
    EXPECT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "catapult"));
}

TEST(BattleUnitTests, RecruitmentEndToEndConsumesResourcesAndManpowerThenAddsToRoster)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    Barracks* barracks = PlaceReadyBarracks(map, player);
    ASSERT_NE(barracks, nullptr);

    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    EXPECT_EQ(barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].buffer.size(), 37u);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 95.0);
    EXPECT_TRUE(player.roster.units.empty());

    barracks->recruitment.Update(*barracks, 4.0);
    EXPECT_TRUE(player.roster.units.empty()) << "recruitTime is 8s, unit shouldn't be ready yet";

    barracks->recruitment.Update(*barracks, 4.0);
    ASSERT_EQ(player.roster.units.size(), 1u);
    const BattleUnit& unit = player.roster.units.begin()->second;
    EXPECT_EQ(unit.unitDefId, "militia");
    EXPECT_EQ(unit.ownerPlayerId, 0);
}

TEST(BattleUnitTests, RecruitTimeAndManpowerCostAreModifiableButFloored)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    Barracks* barracks = PlaceReadyBarracks(map, player);
    ASSERT_NE(barracks, nullptr);

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::UnitRecruitTime, 0.0, 0.5, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:fast_recruit"});
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::UnitRecruitManpowerCost, 0.0, 0.5, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:cheap_recruit"});

    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    const UnitDefinition* militia = FindUnitDefinition("militia");
    ASSERT_NE(militia, nullptr);
    ASSERT_FALSE(barracks->recruitment.queue.empty());
    EXPECT_DOUBLE_EQ(barracks->recruitment.queue.front().total, militia->recruitTime * 0.5);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower),
                     100.0 - militia->manpowerCost * 0.5);

    barracks->recruitment.queue.clear();
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::UnitRecruitTime, 0.0, 0.0, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:zero_recruit_time"});
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    EXPECT_GE(barracks->recruitment.queue.front().total, 1.0)
        << "recruitTime must be floored so a strong multiplier can't make recruitment instant";
}

// Updated 2026-07-15 (docs/work_plan_2026-07-13.md, A5 correction): missing
// resources no longer block QueueRecruitment outright — the order joins the
// queue tagged "waiting" and the shortfall is requested on demand (see
// RecruitmentComponent::QueueRecruitment). Manpower is the only hard gate,
// since it's a global pool with nothing to physically wait on.
TEST(BattleUnitTests, RecruitmentQueuesAsWaitingWithoutResourcesButFailsWithoutManpowerOrUnit)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    Barracks* barracks = PlaceReadyBarracks(map, player);
    ASSERT_NE(barracks, nullptr);

    barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].Clear();
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    ASSERT_FALSE(barracks->recruitment.queue.empty());
    EXPECT_FALSE(barracks->recruitment.queue.back().resourcesReady)
        << "no resources locally yet, so the order should start out waiting";
    barracks->recruitment.queue.clear();

    barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(40);
    player.strategicResources.Set(StrategicResourceType::Manpower, 0);
    EXPECT_FALSE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));

    EXPECT_FALSE(barracks->recruitment.QueueRecruitment(*barracks, "no_such_unit"));
}

TEST(BattleUnitTests, TechTreeModifierChangesEffectiveStatsIncludingAlreadyRecruitedUnit)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    Barracks* barracks = PlaceReadyBarracks(map, player);
    ASSERT_NE(barracks, nullptr);

    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    barracks->recruitment.Update(*barracks, 8.0);
    ASSERT_EQ(player.roster.units.size(), 1u);
    BattleUnit& unit = player.roster.units.begin()->second;

    double baseHp = unit.GetEffectiveMaxHp(player);
    EXPECT_DOUBLE_EQ(baseHp, 26.0);

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::UnitHp, 0.0, 1.5, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:hp_tech", std::nullopt, std::string("militia")});

    EXPECT_DOUBLE_EQ(unit.GetEffectiveMaxHp(player), 39.0);

    // A different unit definition must be unaffected by the filtered modifier.
    BattleUnit swordsman(999, player.id, "swordsman");
    const UnitDefinition* swordsmanDefinition = FindUnitDefinition("swordsman");
    ASSERT_NE(swordsmanDefinition, nullptr);
    EXPECT_DOUBLE_EQ(swordsman.GetEffectiveMaxHp(player), swordsmanDefinition->maxHp);
}

TEST(BattleUnitTests, InstanceIdsAreDeterministicAndUniquePerPlayer)
{
    TileMap map;
    Player playerA{0, map};
    Player playerB{1, map};
    FillOwnedGrass(map, nullptr, 12, 8);
    Barracks* barracksA = PlaceReadyBarracks(map, playerA, {1, 1});
    Barracks* barracksB = PlaceReadyBarracks(map, playerB, {6, 1});
    ASSERT_NE(barracksA, nullptr);
    ASSERT_NE(barracksB, nullptr);

    ASSERT_TRUE(barracksA->recruitment.QueueRecruitment(*barracksA, "militia"));
    barracksA->recruitment.Update(*barracksA, 8.0);
    ASSERT_TRUE(barracksA->recruitment.QueueRecruitment(*barracksA, "militia"));
    barracksA->recruitment.Update(*barracksA, 8.0);
    ASSERT_TRUE(barracksB->recruitment.QueueRecruitment(*barracksB, "militia"));
    barracksB->recruitment.Update(*barracksB, 8.0);

    ASSERT_EQ(playerA.roster.units.size(), 2u);
    ASSERT_EQ(playerB.roster.units.size(), 1u);

    std::vector<int> idsA;
    for (const auto& [id, unit] : playerA.roster.units)
        idsA.push_back(id);
    EXPECT_EQ(idsA[0], 0 * 100000 + 1);
    EXPECT_EQ(idsA[1], 0 * 100000 + 2);

    int idB = playerB.roster.units.begin()->first;
    EXPECT_EQ(idB, 1 * 100000 + 1);
}

// TODO #1 (2026-07-16) regression: while an order waited, the old code
// re-requested its FULL remaining cost every tick without netting out
// deliveries already on the road (RequestResource dispatches transport
// immediately, unlike MaintainRequests), so a Barracks kept ordering "na
// zapas" — far more than the queue needed. After the fix a Barracks must
// pull exactly one queued unit's cost at a time and end with an empty buffer.
TEST(BattleUnitTests, RecruitmentRequestsExactCostWithoutOverRequesting)
{
    TileMap map;
    // Map must be sized BEFORE the Player exists — Player's constructor
    // builds its RoadNetwork against the map as it is right then (same
    // ordering RoadNetworkTests relies on).
    FillOwnedGrass(map, nullptr, 10, 6);
    Player player{0, map};

    // Same proven delivery layout as RoadNetworkTests'
    // StorageComponentPushDeliversResourceOverRealPlacementPath.
    auto* warehouse = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{0, 1}, false));
    auto* barracks = dynamic_cast<Barracks*>(player.Build<Barracks>(Vec2i{5, 1}, false));
    auto* roadA = player.Build<Road>(Vec2i{3, 2}, false);
    auto* roadB = player.Build<Road>(Vec2i{4, 2}, false);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(barracks, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    warehouse->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(50);
    barracks->SetSupplier(ResourceType::FOOD_PROVISIONS, warehouse);
    player.strategicResources.Set(StrategicResourceType::Manpower, 100);

    // 3 militia x 3 FOOD_PROVISIONS, nothing buffered locally yet.
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));

    for (int tick = 0; tick < 400 && player.roster.units.size() < 3u; tick++)
    {
        warehouse->Update(0.25);
        roadA->Update(0.25);
        roadB->Update(0.25);
        barracks->Update(0.25);
    }

    ASSERT_EQ(player.roster.units.size(), 3u) << "all three queued militia should finish";
    EXPECT_EQ(warehouse->storage.buffers[ResourceType::FOOD_PROVISIONS].buffer.size(), 41u)
        << "exactly 3 x 3 FOOD_PROVISIONS may leave the warehouse — anything more is over-requesting";
    EXPECT_TRUE(barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].buffer.empty())
        << "the barracks must not stockpile beyond what the queue consumed";
}

// TODO #1 (2026-07-16): entries behind the front must flip resourcesReady
// (clearing the GUI's "Waiting for resources" label) the moment their cost is
// physically in the buffer — not only once they reach the front of the queue.
TEST(BattleUnitTests, QueuedEntriesFlipResourcesReadyAsDeliveriesArrive)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    Barracks* barracks = PlaceReadyBarracks(map, player);
    ASSERT_NE(barracks, nullptr);

    barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].Clear();
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    ASSERT_EQ(barracks->recruitment.queue.size(), 2u);
    EXPECT_FALSE(barracks->recruitment.queue[0].resourcesReady);
    EXPECT_FALSE(barracks->recruitment.queue[1].resourcesReady);

    // First delivery (3 FOOD) lands: only the front entry's cost is covered.
    barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(3);
    barracks->recruitment.Update(*barracks, 0.01);
    EXPECT_TRUE(barracks->recruitment.queue[0].resourcesReady);
    EXPECT_FALSE(barracks->recruitment.queue[1].resourcesReady);

    // Second delivery lands while the front entry is still training — the
    // second entry must flip immediately instead of waiting for the front.
    barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(3);
    barracks->recruitment.Update(*barracks, 0.01);
    EXPECT_TRUE(barracks->recruitment.queue[1].resourcesReady);
    EXPECT_GT(barracks->recruitment.queue[0].remaining, 0.0)
        << "front entry should still be training when the second one flips";
    EXPECT_TRUE(barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].buffer.empty())
        << "each flip consumes exactly that entry's cost";
}

// TODO #1 (2026-07-16): strict FIFO — a later entry must not grab buffered
// resources while an earlier entry still waits for its own cost, even when
// the later entry's cost is fully available.
TEST(BattleUnitTests, WaitingEntriesConsumeInFifoOrder)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    Barracks* barracks = PlaceReadyBarracks(map, player);
    ASSERT_NE(barracks, nullptr);

    const UnitDefinition* swordsman = FindUnitDefinition("swordsman");
    ASSERT_NE(swordsman, nullptr);
    const auto swordsmanCost = [&](ResourceType type)
    {
        for (const auto& cost : swordsman->cost)
            if (cost.type == type)
                return cost.amount;
        return 0;
    };
    const int swordCost = swordsmanCost(ResourceType::IRON_SWORD);
    const int swordsmanFoodCost = swordsmanCost(ResourceType::FOOD_PROVISIONS);

    // Swordsman queued first with no sword available;
    // militia (3 FOOD) queued second with its FOOD fully in the buffer.
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "swordsman"));
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    ASSERT_EQ(barracks->recruitment.queue.size(), 2u);
    EXPECT_FALSE(barracks->recruitment.queue[0].resourcesReady);
    EXPECT_FALSE(barracks->recruitment.queue[1].resourcesReady)
        << "militia must wait behind the blocked swordsman instead of consuming FOOD out of order";

    barracks->recruitment.Update(*barracks, 0.01);
    EXPECT_FALSE(barracks->recruitment.queue[1].resourcesReady);
    EXPECT_EQ(barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].buffer.size(), 40u)
        << "nothing may be consumed while the front entry is blocked";

    // The missing sword arrives: the whole queue unblocks in FIFO order in
    // one readiness pass (the current swordsman cost plus militia's FOOD).
    barracks->storage.buffers[ResourceType::IRON_SWORD] =
        ResourceBuffer{ResourceType::IRON_SWORD, swordCost};
    barracks->storage.buffers[ResourceType::IRON_SWORD].SetStoredAmount(swordCost);
    barracks->recruitment.Update(*barracks, 0.01);
    EXPECT_TRUE(barracks->recruitment.queue[0].resourcesReady);
    EXPECT_TRUE(barracks->recruitment.queue[1].resourcesReady);
    EXPECT_TRUE(barracks->storage.buffers[ResourceType::IRON_SWORD].buffer.empty());
    EXPECT_EQ(barracks->storage.buffers[ResourceType::FOOD_PROVISIONS].buffer.size(),
              static_cast<size_t>(40 - swordsmanFoodCost - 3));
}

TEST(BattleUnitTests, SaveAndLoadPreservesRosterAndInstanceCounter)
{
    GameWorld world;
    MapParameters params;
    params.sizePreset = MapSizePreset::S;
    params.aiOpponentCount = 0;
    params.seed = 999;
    world.InitWorld("test", nullptr, params);

    Player* human = world.GetPlayerHandler().players.at(0).get();
    TileMap& map = world.GetTileMap();
    Vec2i anchor{2, 2};
    const auto& def = GetBuildingDefinition(BuildingType::Barracks);
    for (int y = 0; y < def.footprint.y + 1; y++)
        for (int x = 0; x < def.footprint.x + 1; x++)
            map[{anchor.x + x, anchor.y + y}].tileType = TileType::GRASS;

    Barracks* barracks = PlaceReadyBarracks(map, *human, anchor);
    ASSERT_NE(barracks, nullptr);
    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "militia"));
    barracks->recruitment.Update(*barracks, 8.0);
    ASSERT_EQ(human->roster.units.size(), 1u);
    int originalInstanceId = human->roster.units.begin()->first;

    const auto path = std::filesystem::temp_directory_path() / "rts_roster_test.save";
    std::error_code cleanupError;
    std::filesystem::remove(path, cleanupError);
    ASSERT_FALSE(cleanupError) << "Failed to remove stale test save: "
                               << cleanupError.message();
    ASSERT_TRUE(world.SaveToFile(path.string()));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path.string(), nullptr));
    Player* loadedHuman = loaded.GetPlayerHandler().players.at(0).get();
    ASSERT_EQ(loadedHuman->roster.units.size(), 1u);
    const BattleUnit& loadedUnit = loadedHuman->roster.units.begin()->second;
    EXPECT_EQ(loadedUnit.instanceId, originalInstanceId);
    EXPECT_EQ(loadedUnit.unitDefId, "militia");
    EXPECT_EQ(loadedHuman->nextUnitInstanceId, human->nextUnitInstanceId);

    std::filesystem::remove(path, cleanupError);
    EXPECT_FALSE(cleanupError) << "Failed to remove test save: "
                               << cleanupError.message();
}
