#include "core/GameWorldInternal.h"
#include "core/Log.h"
#include "core/RoadTopology.h"
#include "economy/StockpileIndex.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace GameWorldInternal;

// Creates and registers the requested runtime object.
void Tile::CreateBuilding(std::unique_ptr<Building> &&bld,
                          std::optional<TileType> matchedTerrain)
{
    building = std::move(bld);
    building->placement = this;
    building->InitBuilding(matchedTerrain.value_or(tileType));
}

// Clears the building anchored on this tile.
void Tile::DestroyBuilding()
{
    building = nullptr;
    buildingRef = nullptr;
}

// Returns the building occupying this tile, following footprint references.
Building* Tile::GetBuilding()
{
    return building != nullptr ? building.get() : buildingRef;
}

// Returns the building occupying this tile, following footprint references.
const Building* Tile::GetBuilding() const
{
    return building != nullptr ? building.get() : buildingRef;
}

Tile &TileMap::GetTile(int id)
{
    return tilemap[id];
}

// Updates the requested state value.
void TileMap::SetTile(int id, Tile &&tile)
{
    tilemap[id] = std::move(tile);
}

// Builds the requested map object or helper path.
void TileMap::BuildOnTile(int id, Player *player, std::unique_ptr<Building> &&building)
{
    if (id < 0 || id >= tilemap.size())
        return;

    Vec2i anchor = GetCoordsFromId(id);
    Vec2i footprint = building->GetFootprint();
    if (!CanPlaceBuilding(building->buildingType, anchor, footprint, player))
        return;
    const TerrainPlacementEvaluation terrain = EvaluateTerrainPlacement(
        building->buildingType, anchor, footprint);
    if (!terrain.valid)
        return;

    {
        Log::Msg(building->tag, building->id, " Created");
        building->owner = player;
        building->ownerId = player != nullptr ? player->id : InvalidPlayerId;
        building->provinceId = provinceEconomy != nullptr
            ? provinceEconomy->provinceId : InvalidProvinceId;
        building->provinceEconomy = provinceEconomy;
        building->positionId = id;

        Tile &anchorTile = tilemap[id];
        std::optional<TileType> matchedTerrain;
        const auto& definition = GetBuildingDefinition(building->buildingType);
        if (!definition.terrainProductions.empty() ||
            building->buildingType == BuildingType::Woodcutter)
            matchedTerrain = terrain.matchedTerrainType;
        anchorTile.CreateBuilding(std::move(building), matchedTerrain);
        Building* placed = anchorTile.building.get();
        if (provinceEconomy != nullptr)
            provinceEconomy->RegisterBuilding(placed);
        else if (player != nullptr)
            player->RegisterBuilding(placed);

        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                int tileId = GetIdFromCoords(pos);
                if (tileId != id)
                    tilemap[tileId].buildingRef = placed;
            }
        }

        if (IsRoadLike(placed->buildingType))
            RefreshRoadTilesAround(anchor);

        buildingsDirty = true;
    }
}

// Initializes TileMap::DestroyBuildingAt.
void TileMap::DestroyBuildingAt(int id)
{
    if (id < 0 || id >= tilemap.size())
        return;

    Building* building = tilemap[id].building.get();
    if (building == nullptr)
        return;

    Player* owner = building->owner;
    ProvinceEconomy* economy = building->provinceEconomy != nullptr
        ? building->provinceEconomy : provinceEconomy;
    Vec2i anchor = GetCoordsFromId(building->positionId);
    Vec2i footprint = building->GetFootprint();
    std::vector<int> occupiedTileIds = GetBuildingTileIds(building);

    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!IsInside(pos))
                continue;

            int tileId = GetIdFromCoords(pos);
            if (tileId != id && tilemap[tileId].buildingRef == building)
                tilemap[tileId].buildingRef = nullptr;
        }
    }

    bool wasRoad = building->HasComponent<RoadComponent>();

    // Detach every live logistics link before the object is released.  A
    // destroyed HQ is replaced with a storage building during conquest, but
    // the transferred producers may still point at the old HQ as their
    // receiver/supplier.  Those raw pointers otherwise become a release-only
    // use-after-free on the next production tick.
    for (auto& tile : tilemap)
    {
        Building* other = tile.building.get();
        if (other == nullptr || other == building)
            continue;

        if (auto* logistics = other->GetComponent<LogisticsComponent>())
        {
            for (auto it = logistics->suppliers.begin(); it != logistics->suppliers.end();)
            {
                auto& suppliers = it->second;
                suppliers.erase(std::remove(suppliers.begin(), suppliers.end(), building), suppliers.end());
                if (suppliers.empty())
                {
                    logistics->pendingRequests.erase(it->first);
                    it = logistics->suppliers.erase(it);
                }
                else
                    ++it;
            }

            for (auto it = logistics->receivers.begin(); it != logistics->receivers.end();)
                it = it->second == building ? logistics->receivers.erase(it) : std::next(it);
            for (auto it = logistics->altReceivers.begin(); it != logistics->altReceivers.end();)
                it = it->second == building ? logistics->altReceivers.erase(it) : std::next(it);
        }

        // A delivery whose destination disappears must be cancelled while
        // both endpoint pointers are still valid.  Deliveries originating at
        // the destroyed building may finish, but must no longer retain its
        // address for a possible return path.
        for (auto it = other->transportables.begin(); it != other->transportables.end();)
        {
            Transportable* transport = *it;
            // `transportables` is a legacy raw-pointer carrier list. A
            // completed delivery may have released its resource before an
            // obsolete carrier entry is removed, so prove registry ownership
            // without dereferencing the pointer first.
            if (transport == nullptr || owner == nullptr || economy == nullptr || economy->roadNetwork == nullptr ||
                !economy->roadNetwork->IsTrackingShipment(transport))
            {
                it = other->transportables.erase(it);
                continue;
            }
            if (transport->targetBuilding == building)
            {
                if (auto* resource = dynamic_cast<Resource*>(transport))
                {
                    building->CancelRequestedResource(resource->type);
                    if (transport->sourceBuilding != nullptr && transport->sourceBuilding != building)
                        transport->sourceBuilding->ReturnOutgoingResource(resource);
                }
                // The registry was verified above on the current owner's
                // network. Do not call Transportable::ReleaseShipment here:
                // its legacy back-pointer may reference a network that was
                // destroyed during an earlier ownership change.
                economy->roadNetwork->ReleaseShipment(transport);
                it = other->transportables.erase(it);
                continue;
            }
            if (transport->sourceBuilding == building)
                transport->sourceBuilding = nullptr;
            ++it;
        }
    }

    if (economy != nullptr)
        economy->UnregisterBuilding(building);
    else if (owner != nullptr)
        owner->UnregisterBuilding(building);
    if (auto* workers = building->GetComponent<WorkerComponent>())
    {
        if (owner != nullptr && workers->assigned > 0)
        {
            double workerPool = owner->strategicResources.Get(StrategicResourceType::Workers);
            owner->strategicResources.Set(StrategicResourceType::Workers, workerPool - workers->assigned);
            owner->strategicResources.Add(StrategicResourceType::Manpower, workers->assigned);
            workers->assigned = 0;
        }
    }
    tilemap[id].DestroyBuilding();

    if (economy != nullptr && economy->roadNetwork != nullptr)
    {
        for (int tileId : occupiedTileIds)
            economy->roadNetwork->UpdateNavMap(tileId, nullptr);
    }

    if (wasRoad)
        RefreshRoadTilesAround(anchor);

    buildingsDirty = true;
}

// Initializes TileMap::PlaceLoadedBuilding.
Building* TileMap::PlaceLoadedBuilding(int id, Player *player, std::unique_ptr<Building> &&building)
{
    if (id < 0 || id >= tilemap.size() || building == nullptr)
        return nullptr;

    Vec2i anchor = GetCoordsFromId(id);
    Vec2i footprint = building->GetFootprint();
    if (!IsInsideFootprint(anchor, footprint))
        return nullptr;

    building->owner = player;
    building->ownerId = player != nullptr ? player->id : InvalidPlayerId;
    building->provinceId = provinceEconomy != nullptr
        ? provinceEconomy->provinceId : InvalidProvinceId;
    building->provinceEconomy = provinceEconomy;
    building->positionId = id;

    Tile &anchorTile = tilemap[id];
    anchorTile.CreateBuilding(std::move(building));
    Building* placed = anchorTile.building.get();
    if (provinceEconomy != nullptr)
        provinceEconomy->RegisterBuilding(placed);
    else if (player != nullptr)
        player->RegisterBuilding(placed);

    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            int tileId = GetIdFromCoords(pos);
            if (tileId != id)
                tilemap[tileId].buildingRef = placed;
        }
    }

    buildingsDirty = true;
    return placed;
}

// Advances UpdateBuildings for one frame or simulation tick.
void TileMap::UpdateBuildings(double dt)
{
    for(auto& tile : tilemap)
    {
        if(tile.building == nullptr)
            continue;

        bool wasUnderConstruction = tile.building->IsUnderConstruction();
        tile.building->Update(dt);
        if (wasUnderConstruction && !tile.building->IsUnderConstruction() && tile.building->owner != nullptr)
        {
            // Construction just finished — redraw the cached building layer so the
            // in-progress shade is replaced by the finished sprite.
            buildingsDirty = true;
            ProvinceEconomy* economy = tile.building->provinceEconomy;
            if (economy != nullptr && economy->roadNetwork != nullptr)
            {
                for (int tileId : GetBuildingTileIds(tile.building.get()))
                    economy->roadNetwork->UpdateNavMap(tileId, tile.building.get());
            }

            AutoConnectBuilding(tile.building.get());
        }
    }
}

// Returns the building occupying a tile id, or nullptr.
Building* TileMap::GetBuilding(int id)
{
    if(id < 0 || id >= tilemap.size()) return nullptr;
    return tilemap[id].GetBuilding();
}

// Returns the building occupying map coordinates, or nullptr.
Building* TileMap::GetBuilding(Vec2i pos)
{
    if (!IsInside(pos))
        return nullptr;

    return GetBuilding(GetIdFromCoords(pos));
}

bool TileMap::ContainsBuilding(const Building* candidate) const
{
    if (candidate == nullptr)
        return false;

    for (const Tile& tile : tilemap)
        if (tile.building.get() == candidate || tile.buildingRef == candidate)
            return true;
    return false;
}

// Converts map coordinates to a linear tile id.
int TileMap::GetIdFromCoords(Vec2i coords) const
{
    return (coords.x + coords.y*params.sizeX);
}

// Converts a linear tile id to map coordinates.
Vec2i TileMap::GetCoordsFromId(int id) const
{
    return Vec2i{id % params.sizeX, id / params.sizeX};
}

// Returns whether this condition is currently true.
bool TileMap::IsInside(Vec2i coords) const
{
    return coords.x >= 0 && coords.x < params.sizeX &&
           coords.y >= 0 && coords.y < params.sizeY;
}

// Returns whether this condition is currently true.
bool TileMap::IsInsideFootprint(Vec2i anchor, Vec2i footprint) const
{
    return IsInside(anchor) && IsInside({anchor.x + footprint.x - 1, anchor.y + footprint.y - 1});
}

// Returns whether this condition is currently true.
bool TileMap::CanBuildFootprint(Vec2i anchor, Vec2i footprint, Player* player, BuildingType type) const
{
    if (!IsInsideFootprint(anchor, footprint))
        return false;

    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            const Tile& tile = tilemap[GetIdFromCoords({anchor.x + x, anchor.y + y})];
            if (tile.HasBuilding())
                return false;
        }
    }

    (void)player;
    (void)type;
    return true;
}

TerrainPlacementEvaluation TileMap::EvaluateTerrainPlacement(
    BuildingType type, Vec2i anchor, Vec2i footprint, int minimumTiles) const
{
    std::vector<TileType> allowedTypes;
    const auto& definition = GetBuildingDefinition(type);
    for (const auto& terrainProduction : definition.terrainProductions)
        allowedTypes.push_back(terrainProduction.tileType);
    if (allowedTypes.empty() && type == BuildingType::Woodcutter)
        allowedTypes.push_back(TileType::WOOD);

    if (allowedTypes.empty())
        return {};

    TerrainPlacementEvaluation result;
    result.valid = false;
    result.failure = TerrainPlacementFailure::InsufficientMatchingTerrain;
    if (!IsInsideFootprint(anchor, footprint))
    {
        result.failure = TerrainPlacementFailure::OutsideMap;
        return result;
    }

    std::vector<int> matchingCounts(allowedTypes.size(), 0);
    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            const Tile& tile = tilemap[GetIdFromCoords({anchor.x + x, anchor.y + y})];
            if (tile.resourceRichness <= 0)
                continue;
            auto it = std::find(allowedTypes.begin(), allowedTypes.end(), tile.tileType);
            if (it != allowedTypes.end())
                matchingCounts[static_cast<size_t>(std::distance(allowedTypes.begin(), it))]++;
        }
    }

    int bestIndex = -1;
    for (size_t i = 0; i < matchingCounts.size(); i++)
    {
        if (matchingCounts[i] < minimumTiles)
            continue;
        // The definition order is the stable tie-break for multi-terrain
        // producers such as Mine.
        if (bestIndex < 0 || matchingCounts[i] > matchingCounts[static_cast<size_t>(bestIndex)])
            bestIndex = static_cast<int>(i);
    }
    if (bestIndex < 0)
        return result;

    result.valid = true;
    result.matchedTerrainType = allowedTypes[static_cast<size_t>(bestIndex)];
    result.matchingTiles = matchingCounts[static_cast<size_t>(bestIndex)];
    result.failure = TerrainPlacementFailure::None;
    return result;
}

// Returns whether this condition is currently true.
bool TileMap::HasRequiredTerrainForBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, int minimumTiles) const
{
    return EvaluateTerrainPlacement(type, anchor, footprint, minimumTiles).valid;
}

// Returns whether this condition is currently true.
bool TileMap::CanPlaceBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, Player* player) const
{
    if (!CanBuildFootprint(anchor, footprint, player, type))
        return false;

    if (!EvaluateTerrainPlacement(type, anchor, footprint, 2).valid)
        return false;

    return true;
}

// Returns every tile id occupied by a building footprint.
std::vector<int> TileMap::GetBuildingTileIds(const Building* building) const
{
    std::vector<int> result;
    if (building == nullptr)
        return result;

    Vec2i anchor = GetCoordsFromId(building->positionId);
    Vec2i footprint = building->GetFootprint();
    for (int y = 0; y < footprint.y; y++)
    {
        for (int x = 0; x < footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (IsInside(pos))
                result.push_back(GetIdFromCoords(pos));
        }
    }
    return result;
}

// Returns walkable neighbor tile ids around a building footprint.
std::vector<int> TileMap::GetAdjacentTileIds(const Building* building) const
{
    if (building == nullptr)
        return {};

    return GetAdjacentTileIds(GetCoordsFromId(building->positionId), building->GetFootprint());
}

std::vector<int> TileMap::GetAdjacentTileIds(Vec2i anchor, Vec2i footprint) const
{
    std::vector<int> result;
    for (int y = -1; y <= footprint.y; y++)
    {
        for (int x = -1; x <= footprint.x; x++)
        {
            bool insideFootprint = x >= 0 && x < footprint.x && y >= 0 && y < footprint.y;
            bool diagonalOnly = (x == -1 || x == footprint.x) && (y == -1 || y == footprint.y);
            if (insideFootprint || diagonalOnly)
                continue;

            Vec2i pos{anchor.x + x, anchor.y + y};
            if (IsInside(pos))
                result.push_back(GetIdFromCoords(pos));
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

// Returns the default texture id for a terrain type.
int TileMap::GetTerrainTextureId(TileType type) const
{
    auto it = terrainVariants.find(type);
    if (it == terrainVariants.end() || it->second.empty())
        return 0;
    return it->second.front().textureId;
}

// Picks a map position or generated value.
int TileMap::PickTerrainTexture(TileType type, std::mt19937& rng) const
{
    auto it = terrainVariants.find(type);
    if (it == terrainVariants.end() || it->second.empty())
        return 0;

    int totalWeight = 0;
    for (const auto& variant : it->second)
        totalWeight += std::max(0, variant.weight);

    if (totalWeight <= 0)
        return it->second.front().textureId;

    std::uniform_int_distribution<int> dist(1, totalWeight);
    int roll = dist(rng);
    for (const auto& variant : it->second)
    {
        roll -= std::max(0, variant.weight);
        if (roll <= 0)
            return variant.textureId;
    }

    return it->second.back().textureId;
}

int TileMap::PickResourceOverlayTexture(TileType type, ResourceOverlayEdgeDirection edge, std::mt19937& rng) const
{
    auto it = resourceOverlayVariants.find(type);
    if (it == resourceOverlayVariants.end())
        return -1;

    const std::vector<WeightedTileVariant>* variants = &it->second.fillers;
    switch (edge)
    {
        case ResourceOverlayEdgeDirection::North: variants = &it->second.northEdges; break;
        case ResourceOverlayEdgeDirection::East:  variants = &it->second.eastEdges; break;
        case ResourceOverlayEdgeDirection::South: variants = &it->second.southEdges; break;
        case ResourceOverlayEdgeDirection::West:  variants = &it->second.westEdges; break;
        case ResourceOverlayEdgeDirection::None:  break;
    }
    if (variants->empty())
        return -1;

    int totalWeight = 0;
    for (const auto& variant : *variants)
        totalWeight += std::max(0, variant.weight);
    if (totalWeight <= 0)
        return variants->front().textureId;

    std::uniform_int_distribution<int> dist(1, totalWeight);
    int roll = dist(rng);
    for (const auto& variant : *variants)
    {
        roll -= std::max(0, variant.weight);
        if (roll <= 0)
            return variant.textureId;
    }
    return variants->back().textureId;
}

bool TileMap::HasResourceOverlay(TileType type) const
{
    auto it = resourceOverlayVariants.find(type);
    return it != resourceOverlayVariants.end() &&
           (!it->second.fillers.empty() || !it->second.northEdges.empty());
}

// Returns the canonical four-neighbour road mask used by both renderers.
int TileMap::GetRoadAutotileMask(Vec2i pos) const
{
    return RoadTopology::GetCardinalMask(pos.x, pos.y, params.sizeX, params.sizeY,
        [&](int checkX, int checkY)
        {
            const Building* building = tilemap[GetIdFromCoords({checkX, checkY})].GetBuilding();
            return building != nullptr && IsRoadLike(building->buildingType);
        });
}

// Returns the texture id matching a road autotile mask.
int TileMap::GetRoadTextureId(Vec2i pos) const
{
    constexpr int roadAtlasBaseId = 5;
    return roadAtlasBaseId + GetRoadAutotileMask(pos);
}

// Initializes TileMap::RefreshRoadTilesAround.
void TileMap::RefreshRoadTilesAround(Vec2i pos)
{
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            Vec2i check{pos.x + x, pos.y + y};
            if (!IsInside(check))
                continue;

            auto* building = GetBuilding(check);
            if (building != nullptr && IsRoadLike(building->buildingType))
                building->textureId = GetRoadTextureId(check);
        }
    }
    buildingsDirty = true;
}

// Finds the best matching runtime object.
Building* TileMap::FindNearestStorage(Building* source, Player* player)
{
    if (source == nullptr || player == nullptr)
        return nullptr;

    Vec2i origin = GetCoordsFromId(source->positionId);
    Building* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();

    // Use the province-local storage registry instead of scanning the map.
    // storages[] contains all StorageComponent buildings owned by player.
    ProvinceEconomy* economy = source->provinceEconomy != nullptr
        ? source->provinceEconomy : player->GetProvinceEconomy();
    if (economy == nullptr)
        return nullptr;
    for (Building* storage : economy->storages)
    {
        if (storage == nullptr || storage == source)
            continue;

        Vec2i pos = GetCoordsFromId(storage->positionId);
        int distance = std::abs(pos.x - origin.x) + std::abs(pos.y - origin.y);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = storage;
        }
    }

    return best;
}

Building* TileMap::FindDefaultStorage(Building* source, Player* player)
{
    if (source == nullptr || player == nullptr)
        return nullptr;

    Building* headquarters = nullptr;
    ProvinceEconomy* economy = source->provinceEconomy != nullptr
        ? source->provinceEconomy : player->GetProvinceEconomy();
    if (economy == nullptr)
        return nullptr;
    for (Building* warehouse : StockpileIndex::Warehouses(*economy))
    {
        if (warehouse == source)
            continue;
        if (warehouse->buildingType == BuildingType::Headquarters)
        {
            if (headquarters == nullptr || warehouse->id < headquarters->id)
                headquarters = warehouse;
        }
    }
    if (headquarters != nullptr)
        return headquarters;

    Vec2i origin = GetCoordsFromId(source->positionId);
    Building* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (Building* warehouse : StockpileIndex::Warehouses(*economy))
    {
        if (warehouse == source)
            continue;
        Vec2i pos = GetCoordsFromId(warehouse->positionId);
        int distance = std::abs(pos.x - origin.x) + std::abs(pos.y - origin.y);
        if (distance < bestDistance ||
            (distance == bestDistance && (best == nullptr || warehouse->id < best->id)))
        {
            bestDistance = distance;
            best = warehouse;
        }
    }
    return best;
}

// Initializes TileMap::ConnectReceiver.
void TileMap::ConnectReceiver(Building* source, Building* receiver, bool alternative)
{
    if (source == nullptr || receiver == nullptr || source == receiver)
        return;

    // Storage hubs expose outgoing buffers rather than producer outputs, so
    // there is no output view to iterate below. A SetReceiver command from a
    // hub is therefore interpreted as an explicit supplier assignment for
    // every input the target can accept. This keeps the command meaningful
    // for Barracks with local resource buffers and gives the AI job a
    // verifiable postcondition.
    if (source->IsStorageLike())
    {
        for (const auto& input : receiver->GetInputBufferViews())
            if (receiver->CanAcceptResource(input.type))
                receiver->SetSupplier(input.type, source);
        return;
    }

    for (const auto& output : source->GetOutputBufferViews())
    {
        if (!receiver->CanAcceptResource(output.type))
            continue;

        bool disconnect = false;
        for (const auto& view : source->GetReceiverViews())
        {
            if (view.type == output.type && view.building == receiver)
            {
                disconnect = true;
                break;
            }
        }

        if (disconnect)
        {
            source->RemoveReceiver(output.type, receiver);
            receiver->RemoveSupplier(output.type, source);
            if (receiver->CanAcceptResource(output.type) && !receiver->HasSupplier(output.type))
            {
                Building* storage = FindDefaultStorage(receiver, receiver->owner);
                if (storage != nullptr && storage != receiver)
                    receiver->SetSupplier(output.type, storage);
            }
        }
        else
        {
            if (alternative)
                source->SetAlternativeReceiver(output.type, receiver);
            else
                source->SetReceiver(output.type, receiver);
        }
    }
}

// Initializes TileMap::AutoConnectBuilding.
void TileMap::AutoConnectBuilding(Building* building)
{
    if (building == nullptr || building->owner == nullptr)
        return;

    const bool isStorageHub = building->buildingType == BuildingType::Headquarters ||
                              building->buildingType == BuildingType::StorageBuilding;
    if (isStorageHub)
    {
        // OPTIMIZATION: tracked buildings (ETAP 10 registry) instead of a full
        // tilemap scan. Sorted by
        // id (not the set's native pointer order) because this loop's
        // outcome — which building wins a receiver/supplier slot — is
        // simulation-visible and must be identical across processes/hosts;
        // GetTrackedBuildings() orders by Building* (heap address), which is
        // NOT deterministic across separately-constructed GameWorld
        // instances (found via a flaky lockstep-determinism test).
        const ProvinceEconomy* economy = building->provinceEconomy;
        if (economy == nullptr)
            return;
        std::vector<Building*> ordered(economy->dataTracker.buildings.begin(),
                                       economy->dataTracker.buildings.end());
        std::sort(ordered.begin(), ordered.end(), [](Building* a, Building* b) { return a->id < b->id; });
        for (Building* other : ordered)
        {
            if (other == nullptr || other == building)
                continue;

            for (const auto& output : other->GetOutputBufferViews())
            {
                // T3 fix (docs/post_pivot_audit_2026-07-12.md): only wire the
                // new warehouse/HQ as a receiver only for types it can
                // actually accept. Without this check, building one next
                // to a producer of an unrelated resource silently hijacked
                // that producer's receiver, blocking its real fallback
                // delivery to the nearest storage.
                if (!building->CanAcceptResource(output.type))
                    continue;

                if (!other->HasReceiver(output.type))
                {
                    other->SetReceiver(output.type, building);
                    continue;
                }

                // A newly completed warehouse must become a usable overflow
                // sink even when the producer already has the HQ as its
                // primary receiver. Without this alternative lane, adding
                // storage increases aggregate capacity but leaves every
                // producer pointed at the full original warehouse, so a full
                // output buffer can deadlock the entire material chain.
                if (building->buildingType == BuildingType::StorageBuilding)
                    other->SetAlternativeReceiver(output.type, building);
            }

            for (const auto& input : other->GetInputBufferViews())
            {
                if (!other->HasSupplier(input.type))
                    other->SetSupplier(input.type, building);
            }
        }
        return;
    }

    Building* storage = FindDefaultStorage(building, building->owner);
    if (storage == nullptr)
        return;

    for (const auto& output : building->GetOutputBufferViews())
    {
        if (!building->HasReceiver(output.type))
            building->SetReceiver(output.type, storage);
    }

    for (const auto& input : building->GetInputBufferViews())
    {
        if (building->HasSupplier(input.type))
            continue;

        // Construction must never change another building's output
        // destination. In particular, do not discover a nearby producer and
        // add this consumer as its alternative receiver: that silently made a
        // newly built Lumber Mill/Smith intercept resources which the player
        // had routed elsewhere. The warehouse link only supplies this new
        // building; a direct producer-consumer route is created exclusively
        // by the player's SetReceiver command.
        if (storage != building && building->CanAcceptResource(input.type))
            building->SetSupplier(input.type, storage);
    }
}

