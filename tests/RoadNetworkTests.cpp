#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "simulation/RoadNetwork.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    // Creates a rectangular player-owned grass map for pathing tests.
    void FillOwnedMap(TileMap& map, Player* owner, int width = 10, int height = 6)
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

    // Places a loaded building and registers every footprint tile in the road network.
    template <typename T>
    T* PlaceAndRegister(TileMap& map, RoadNetwork& network, Player* owner, Vec2i anchor, int id)
    {
        int tileId = map.GetIdFromCoords(anchor);
        auto* placed = dynamic_cast<T*>(map.PlaceLoadedBuilding(tileId, owner, std::make_unique<T>(id)));
        if (placed == nullptr)
            return nullptr;

        for (int occupiedTileId : map.GetBuildingTileIds(placed))
            network.UpdateNavMap(occupiedTileId, placed);
        return placed;
    }

    // Bare grass map with every Tile::owner left at its default (nullptr) —
    // matches what a freshly generated map actually looks like today (the
    // territory system that used to populate Tile::owner was removed in the
    // Tower Defense pivot, ETAP 1). Deliberately the opposite of
    // FillOwnedMap/PlaceAndRegister above, which exist only as test-only
    // shortcuts and must not be relied on to prove production code works.
    void FillUnownedMap(TileMap& map, int width = 10, int height = 6)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
            map.tilemap.emplace_back(i);
    }
}

TEST(RoadNetworkTests, CalculatesPathAcrossRoadTilesBetweenBuildingFootprints)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);

    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    std::vector<int> path = network.CalculatePath(source, destination);

    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), map.GetIdFromCoords({2, 2}));
    EXPECT_EQ(path.back(), map.GetIdFromCoords({5, 2}));
    EXPECT_NE(std::find(path.begin(), path.end(), map.GetIdFromCoords({3, 2})), path.end());
    EXPECT_NE(std::find(path.begin(), path.end(), map.GetIdFromCoords({4, 2})), path.end());
}

TEST(RoadNetworkTests, ReturnsEmptyPathWhenRoadConnectionIsBroken)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);

    PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);

    EXPECT_TRUE(network.CalculatePath(source, destination).empty());
}

TEST(RoadNetworkTests, BeginTransportQueuesResourceOnSourceWhenPathAndCapacityExist)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);
    // Dispatch delay is a balance default, not part of this path-queueing
    // contract. Pin it so a gameplay-balance change cannot alter this test.
    source->dispatchDelay.SetBase(0.1);

    source->storage.buffers.clear();
    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));

    ASSERT_EQ(source->transportables.size(), 1u);
    EXPECT_EQ(source->transportables.front(), &wood);
    EXPECT_EQ(wood.sourceBuilding, source);
    EXPECT_EQ(wood.targetBuilding, destination);
    EXPECT_FALSE(wood.transportPath.empty());
    EXPECT_NE(wood.shipmentId, 0u);
    EXPECT_EQ(network.GetLiveShipmentCount(), 1u);
    EXPECT_EQ(network.GetShipmentRecordCount(), 1u);
    const ResourceShipment* record = network.FindShipmentRecord(wood.shipmentId);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->type, ResourceType::WOOD);
    EXPECT_EQ(record->quantity, 1);
    EXPECT_EQ(record->sourceBuildingId, source->id);
    EXPECT_EQ(record->targetBuildingId, destination->id);
    EXPECT_EQ(record->pathTileIds, wood.transportPath);
    EXPECT_DOUBLE_EQ(wood.transportTime, 0.1);

    source->UpdateTransportables(0.09);
    EXPECT_EQ(wood.currentPathStep, 0);
    record = network.FindShipmentRecord(wood.shipmentId);
    ASSERT_NE(record, nullptr);
    EXPECT_DOUBLE_EQ(record->elapsedTime, 0.09);
    ASSERT_EQ(source->transportables.size(), 1u);
    EXPECT_EQ(source->transportables.front(), &wood);

    wood.ReleaseShipment();
    EXPECT_EQ(network.GetLiveShipmentCount(), 0u);
    EXPECT_EQ(network.GetShipmentRecordCount(), 0u);
    EXPECT_EQ(wood.shipmentId, 0u);
    source->transportables.clear();
}

TEST(RoadNetworkTests, DispatchDelayIsBalanceModifiableForBuildingAndResource)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4), nullptr);
    // This test verifies the resource-scoped modifier, starting from a known
    // base rather than the data-driven gameplay default.
    source->dispatchDelay.SetBase(0.1);

    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::TransportDispatchDelay,
        0.0,
        2.0,
        BalanceModifierScope::Global(),
        BuildingType::StorageBuilding,
        ResourceType::WOOD,
        "test:dispatch_delay"});

    EXPECT_DOUBLE_EQ(source->GetModifiedDispatchDelay(ResourceType::WOOD), 0.2);
    EXPECT_DOUBLE_EQ(source->GetModifiedDispatchDelay(ResourceType::STONE), 0.1);

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));
    EXPECT_DOUBLE_EQ(wood.transportTime, 0.2);

    source->UpdateTransportables(0.19);
    EXPECT_EQ(wood.currentPathStep, 0);
    EXPECT_EQ(source->transportables.size(), 1u);

    source->UpdateTransportables(0.02);
    EXPECT_EQ(wood.currentPathStep, 1);
    EXPECT_TRUE(source->transportables.empty());
    ASSERT_EQ(roadA->transportables.size(), 1u);
    EXPECT_EQ(roadA->transportables.front(), &wood);
    EXPECT_EQ(network.GetLiveShipmentCount(), 1u);

    wood.ReleaseShipment();
    roadA->transportables.clear();
}

TEST(RoadNetworkTests, DispatchDelaySerializesResourcesCreatedInTheSameTick)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4), nullptr);
    source->dispatchDelay.SetBase(0.1);

    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 3};

    Resource first{ResourceType::WOOD};
    Resource second{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &first));
    ASSERT_TRUE(network.BeginTransport(source, destination, &second));

    source->UpdateTransportables(0.11);
    EXPECT_EQ(first.currentPathStep, 1);
    EXPECT_EQ(second.currentPathStep, 0);
    EXPECT_DOUBLE_EQ(second.elapsedTime, 0.0);
    ASSERT_EQ(roadA->transportables.size(), 1u);
    ASSERT_EQ(source->transportables.size(), 1u);

    source->UpdateTransportables(0.09);
    EXPECT_EQ(second.currentPathStep, 0);
    source->UpdateTransportables(0.02);
    EXPECT_EQ(second.currentPathStep, 1);
    ASSERT_EQ(roadA->transportables.size(), 2u);

    first.ReleaseShipment();
    second.ReleaseShipment();
    roadA->transportables.clear();
}

TEST(RoadNetworkTests, PrioritizedRoadAdmissionWinsAcrossConvergingSources)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player, 12, 8);
    RoadNetwork network{map};

    auto* ordinarySource = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* prioritySource = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 5}, 2);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {8, 3}, 3);
    ASSERT_NE(ordinarySource, nullptr);
    ASSERT_NE(prioritySource, nullptr);
    ASSERT_NE(destination, nullptr);

    int roadId = 100;
    for (int x = 2; x <= 7; ++x)
    {
        ASSERT_NE(PlaceAndRegister<Road>(map, network, &player, {x, 2}, roadId++), nullptr);
        ASSERT_NE(PlaceAndRegister<Road>(map, network, &player, {x, 6}, roadId++), nullptr);
    }
    for (int y = 3; y <= 5; ++y)
        ASSERT_NE(PlaceAndRegister<Road>(map, network, &player, {7, y}, roadId++), nullptr);

    const int sharedRoadId = map.GetIdFromCoords({7, 3});
    auto* sharedRoad = map.GetBuilding(sharedRoadId);
    ASSERT_NE(sharedRoad, nullptr);
    sharedRoad->GetComponent<RoadComponent>()->maxCapacity.SetBase(1);
    sharedRoad->GetComponent<RoadComponent>()->SetPriorityResource(ResourceType::WOOD);

    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::STONE] = ResourceBuffer{ResourceType::STONE, 2};
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};

    Resource ordinary{ResourceType::STONE};
    Resource priority{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(ordinarySource, destination, &ordinary));
    ASSERT_TRUE(network.BeginTransport(prioritySource, destination, &priority));

    // Pin both shipments at the same ready-to-enter boundary. The ordinary
    // source is updated first to prove building order cannot beat priority.
    for (Resource* resource : {&ordinary, &priority})
    {
        resource->transportPath = {resource->sourceBuilding->positionId,
                                   sharedRoadId, destination->positionId};
        resource->currentPathStep = 0;
        resource->elapsedTime = 0.0;
        resource->transportTime = 0.0;
    }

    network.Update(0.0);
    ordinarySource->UpdateTransportables(0.0);
    EXPECT_EQ(ordinarySource->transportables.size(), 1u);
    EXPECT_TRUE(prioritySource->transportables.size() == 1u);

    prioritySource->UpdateTransportables(0.0);
    EXPECT_TRUE(ordinarySource->transportables.size() == 1u);
    EXPECT_TRUE(prioritySource->transportables.empty());
    ASSERT_EQ(sharedRoad->transportables.size(), 1u);
    EXPECT_EQ(sharedRoad->transportables.front(), &priority);

    // Drain the granted shipment, then let the ordinary shipment use the
    // same newly-free slot. This also guards against duplication or loss under
    // strict priority while the tile is congested.
    network.Update(0.0);
    sharedRoad->Update(1.0);
    ASSERT_EQ(destination->storage.buffers[ResourceType::WOOD].buffer.size(), 1u);
    ordinarySource->UpdateTransportables(0.0);
    ASSERT_EQ(sharedRoad->transportables.size(), 1u);
    EXPECT_EQ(sharedRoad->transportables.front(), &ordinary);
    network.Update(0.0);
    sharedRoad->Update(1.0);
    EXPECT_EQ(destination->storage.buffers[ResourceType::STONE].buffer.size(), 1u);

    ordinary.ReleaseShipment();
    priority.ReleaseShipment();
    ordinarySource->transportables.clear();
    prioritySource->transportables.clear();
    sharedRoad->transportables.clear();
}

TEST(RoadNetworkTests, ProjectsInFlightResourceForRenderingWithoutPointers)
{
    TileMap map;
    Player player{7, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4), nullptr);

    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    source->dispatchDelay.SetBase(2.0);

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));
    ASSERT_GT(wood.transportTime, 0.0);

    wood.elapsedTime = wood.transportTime * 0.5;
    std::vector<ShipmentRenderState> views;
    network.AppendShipmentRenderStates(views);

    ASSERT_EQ(views.size(), 1u);
    EXPECT_EQ(views.front().ownerPlayerId, 7);
    EXPECT_EQ(views.front().shipmentId, wood.shipmentId);
    EXPECT_EQ(views.front().resourceType, ResourceType::WOOD);
    EXPECT_EQ(views.front().previousTileId, -1);
    EXPECT_EQ(views.front().fromTileId, wood.transportPath[0]);
    EXPECT_EQ(views.front().toTileId, wood.transportPath[1]);
    EXPECT_FLOAT_EQ(views.front().progress, 0.5f);
    EXPECT_FALSE(views.front().waitingForCapacity);

    source->UpdateTransportables(wood.transportTime * 0.5 + 0.01);
    EXPECT_EQ(network.GetLiveShipmentCount(), 1u)
        << "a road-tile hand-off must not end the world-owned shipment";
    ASSERT_EQ(wood.currentPathStep, 1);
    views.clear();
    network.AppendShipmentRenderStates(views);
    ASSERT_EQ(views.size(), 1u);
    EXPECT_EQ(views.front().previousTileId, wood.transportPath[0]);
    EXPECT_EQ(views.front().fromTileId, wood.transportPath[1]);
    EXPECT_EQ(views.front().toTileId, wood.transportPath[2]);

    wood.elapsedTime = wood.transportTime;
    views.clear();
    network.AppendShipmentRenderStates(views);
    ASSERT_EQ(views.size(), 1u);
    EXPECT_FLOAT_EQ(views.front().progress, 1.0f);
    EXPECT_TRUE(views.front().waitingForCapacity);

    wood.ReleaseShipment();
    views.clear();
    network.AppendShipmentRenderStates(views);
    EXPECT_TRUE(views.empty());
    Building* carrier = map.GetBuilding(wood.transportPath[wood.currentPathStep]);
    ASSERT_NE(carrier, nullptr);
    carrier->transportables.clear();
}

TEST(RoadNetworkTests, RoadCapacityLimitsEntryAndQueuesOverflowAtSource)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    roadA->road.maxCapacity.SetBase(1);
    roadB->road.maxCapacity.SetBase(1);
    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};

    Resource woodA{ResourceType::WOOD};
    Resource woodB{ResourceType::WOOD};
    Resource woodC{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &woodA));
    ASSERT_TRUE(network.BeginTransport(source, destination, &woodB));
    ASSERT_TRUE(network.BeginTransport(source, destination, &woodC));

    source->UpdateTransportables(1.1);

    EXPECT_EQ(roadA->transportables.size(), 1u);
    EXPECT_EQ(source->transportables.size(), 2u);
    EXPECT_EQ(network.GetLiveShipmentCount(), 3u);
}

TEST(RoadNetworkTests, OpposingFullRoadSegmentsSwapToBreakDeadlock)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* leftStorage = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* rightStorage = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(leftStorage, nullptr);
    ASSERT_NE(rightStorage, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    roadA->road.maxCapacity.SetBase(1);
    roadB->road.maxCapacity.SetBase(1);
    leftStorage->storage.buffers.clear();
    rightStorage->storage.buffers.clear();
    leftStorage->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};
    rightStorage->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};

    Resource eastbound{ResourceType::WOOD};
    Resource westbound{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(leftStorage, rightStorage, &eastbound));
    ASSERT_TRUE(network.BeginTransport(rightStorage, leftStorage, &westbound));

    leftStorage->UpdateTransportables(1.1);
    rightStorage->UpdateTransportables(1.1);
    ASSERT_EQ(roadA->transportables.size(), 1u);
    ASSERT_EQ(roadB->transportables.size(), 1u);
    ASSERT_EQ(roadA->transportables.front(), &eastbound);
    ASSERT_EQ(roadB->transportables.front(), &westbound);

    eastbound.elapsedTime = eastbound.transportTime;
    westbound.elapsedTime = westbound.transportTime;
    roadA->UpdateTransportables(0.1);

    EXPECT_EQ(roadA->transportables.size(), 1u);
    EXPECT_EQ(roadB->transportables.size(), 1u);
    EXPECT_EQ(roadA->transportables.front(), &westbound);
    EXPECT_EQ(roadB->transportables.front(), &eastbound);
}

TEST(RoadNetworkTests, HeadquartersAcceptsPaperResource)
{
    Headquarters destination{2};
    ASSERT_TRUE(destination.CanAcceptResource(ResourceType::PAPER));
    ASSERT_TRUE(destination.CanReceiveResource(ResourceType::PAPER));

    Resource paper{ResourceType::PAPER};
    destination.AddResource(&paper);

    auto paperIt = destination.storage.buffers.find(ResourceType::PAPER);
    ASSERT_NE(paperIt, destination.storage.buffers.end());
    EXPECT_EQ(paperIt->second.buffer.size(), 1u);
}

TEST(RoadNetworkTests, BeginTransportRejectsFullDestination)
{
    TileMap map;
    Player player{0, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);

    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};
    destination->storage.buffers[ResourceType::WOOD].SetStoredAmount(1);

    Resource wood{ResourceType::WOOD};
    EXPECT_FALSE(network.BeginTransport(source, destination, &wood));
    EXPECT_TRUE(source->transportables.empty());
    destination->storage.buffers[ResourceType::WOOD].Clear();
}

// Replaces the old "path leaves owner territory" scenario: individual tile
// ownership (Tile::owner) was removed with the territory system in the Tower
// Defense pivot (ETAP 1) and Transportable::Update no longer reads it — the
// analogous real-world event is a road segment on the path changing hands
// (e.g. a future building-capture mechanic), which flips the Road building's
// own `owner` field.
TEST(RoadNetworkTests, TransportableCancelsWhenPathRoadChangesOwner)
{
    TileMap map;
    Player player{0, map};
    Player enemy{1, map};
    FillOwnedMap(map, &player);
    RoadNetwork network{map};

    auto* source = PlaceAndRegister<StorageBuilding>(map, network, &player, {0, 1}, 1);
    auto* destination = PlaceAndRegister<StorageBuilding>(map, network, &player, {5, 1}, 2);
    auto* roadA = PlaceAndRegister<Road>(map, network, &player, {3, 2}, 3);
    auto* roadB = PlaceAndRegister<Road>(map, network, &player, {4, 2}, 4);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    source->storage.buffers.clear();
    source->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    destination->storage.buffers.clear();
    destination->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};

    Resource wood{ResourceType::WOOD};
    ASSERT_TRUE(network.BeginTransport(source, destination, &wood));
    ASSERT_FALSE(wood.transportPath.empty());

    int roadATileId = map.GetIdFromCoords({3, 2});
    auto roadStepIt = std::find(wood.transportPath.begin(), wood.transportPath.end(), roadATileId);
    ASSERT_NE(roadStepIt, wood.transportPath.end());
    wood.currentPathStep = static_cast<int>(std::distance(wood.transportPath.begin(), roadStepIt));

    roadA->owner = &enemy;
    EXPECT_EQ(wood.Update(0.1), TransportUpdateResult::Finished);
    EXPECT_EQ(source->storage.buffers[ResourceType::WOOD].buffer.size(), 1u);
    // Plain vector clear, NOT ResourceBuffer::Clear(): the cancellation
    // returned the stack-local wood into this buffer. ResourceBuffer now
    // ignores external stack-backed values during owned-resource cleanup.
    source->storage.buffers[ResourceType::WOOD].buffer.clear();
    roadA->owner = &player;
}

// Regression test for the P0 audit finding (docs/post_pivot_audit_2026-07-12.md,
// T1): every other test in this file builds its world through FillOwnedMap,
// which manually stamps Tile::owner on every tile — a shortcut nothing in
// production does since the territory system was removed (ETAP 1). That
// shortcut is exactly why these tests kept passing while real games had
// completely dead logistics. This test instead uses the actual production
// placement path (Player::Build<T>, the same call GameCommand execution and
// AI both use) on a bare map where Tile::owner is never touched.
TEST(RoadNetworkTests, ProductionPlacementFindsPathWithoutTileOwnership)
{
    TileMap map;
    FillUnownedMap(map);
    Player player{0, map};

    auto* source = player.Build<StorageBuilding>(Vec2i{0, 1}, false);
    auto* destination = player.Build<StorageBuilding>(Vec2i{5, 1}, false);
    auto* roadA = player.Build<Road>(Vec2i{3, 2}, false);
    auto* roadB = player.Build<Road>(Vec2i{4, 2}, false);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    std::vector<int> path = player.roadNetwork->CalculatePath(source, destination);
    ASSERT_FALSE(path.empty());

    Resource wood{ResourceType::WOOD};
    EXPECT_TRUE(player.roadNetwork->BeginTransport(source, destination, &wood));
}

// End-to-end companion to the test above: drives the full transport pipeline
// (Building::HandleTransport -> Player::BeginTransport ->
// RoadNetwork::CalculatePath/Transportable::Update) over a real placement and
// asserts the resource actually arrives — catching both the CalculatePath bug
// and the Transportable::Update self-cancel bug that a path-only assertion
// would miss.
TEST(RoadNetworkTests, StorageTransportDeliversResourceOverRealPlacementPath)
{
    TileMap map;
    FillUnownedMap(map);
    Player player{0, map};

    auto* source = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{0, 1}, false));
    auto* destination = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{5, 1}, false));
    auto* roadA = dynamic_cast<Road*>(player.Build<Road>(Vec2i{3, 2}, false));
    auto* roadB = dynamic_cast<Road*>(player.Build<Road>(Vec2i{4, 2}, false));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    source->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    ASSERT_EQ(source->storage.buffers[ResourceType::WOOD].buffer.size(), 1u);

    // Warehouses are passive (StorageComponent has no Update): a transfer only
    // happens when someone asks for it, so request it explicitly. Delivery
    // then happens inside Transportable::Update -> Building::ReceptTransport
    // -> AddResource as the roads advance the resource, so ticking roadA/roadB
    // is enough to observe it land in destination's buffer.
    ASSERT_EQ(source->HandleTransport(ResourceType::WOOD, 1, destination), 1);
    for (int tick = 0; tick < 10 && destination->storage.buffers[ResourceType::WOOD].buffer.empty(); tick++)
    {
        source->Update(1.0);
        roadA->Update(1.0);
        roadB->Update(1.0);
    }

    EXPECT_EQ(destination->storage.buffers[ResourceType::WOOD].buffer.size(), 1u);
    EXPECT_TRUE(source->storage.buffers[ResourceType::WOOD].buffer.empty());
}
