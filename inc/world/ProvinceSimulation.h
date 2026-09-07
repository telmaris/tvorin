#ifndef WORLD_PROVINCE_SIMULATION_H
#define WORLD_PROVINCE_SIMULATION_H

#include "core/FogOfWar.h"
#include "economy/BuildingFactory.h"
#include "economy/ConstructionQueue.h"
#include "economy/PlayerDataTracker.h"
#include "economy/PlayerEconomy.h"
#include "economy/ProvincePopulation.h"
#include "simulation/MapGenerator.h"
#include "simulation/RoadNetwork.h"
#include "world/WorldIds.h"

#include <cstdint>
#include <memory>
#include <vector>

class Building;
class Player;

// All state tied to one buildable province lives here. Strategic resources,
// research, focus and the recruited roster remain on Player.
struct ProvinceEconomy
{
    ProvinceId provinceId{InvalidProvinceId};
    PlayerId ownerId{InvalidPlayerId};
    std::uint64_t simulationTick{0};

    TileMap ownedTilemap;
    TileMap* tilemap{&ownedTilemap};
    std::unique_ptr<RoadNetwork> roadNetwork;
    BFactory build;
    std::vector<Building*> storages;
    std::vector<Building*> villages;
    std::uint32_t registryGeneration{0};
    PlayerDataTracker dataTracker;
    ConstructionQueue construction;
    PlayerEconomyTelemetry economyTelemetry;
    FogOfWarState fogOfWar;
    ProvincePopulationState population;
    std::vector<std::string> traitIds;

    ProvinceEconomy() = default;
    ProvinceEconomy(ProvinceId id, PlayerId owner);
    ProvinceEconomy(const ProvinceEconomy&) = delete;
    ProvinceEconomy& operator=(const ProvinceEconomy&) = delete;

    void BindOwner(Player& player);
    void SetTraitIds(std::vector<std::string> value) { traitIds = std::move(value); }
    void BindTileMap(TileMap& map);
    void RegisterBuilding(Building* building);
    void UnregisterBuilding(Building* building);
    // The world owns the clock. Local economy mirrors its tick instead of
    // advancing an independent simulation timeline.
    void Update(Player& player, double dt, std::uint64_t authoritativeTick);
    void UpdateFogOfWar(Player& player);
};

class ProvinceSimulation
{
public:
    ProvinceSimulation(ProvinceId provinceId, PlayerId ownerId);

    ProvinceId GetProvinceId() const { return economy.provinceId; }
    PlayerId GetOwnerId() const { return economy.ownerId; }
    void SetOwnerId(PlayerId ownerId) { economy.ownerId = ownerId; }
    ProvinceEconomy& GetEconomy() { return economy; }
    const ProvinceEconomy& GetEconomy() const { return economy; }

    Player* GetOwner() const { return owner; }
    void BindPlayer(Player& player);

    TileMap& GetTileMap() { return *economy.tilemap; }
    const TileMap& GetTileMap() const { return *economy.tilemap; }

    // All local services point to this province's map. The pointer form is
    // retained only for controlled compatibility with callers that provide a
    // map object during loading.
    void BindExternalTileMap(TileMap& externalMap);
    bool OwnsTileMap() const { return economy.tilemap == &economy.ownedTilemap; }

    bool GenerateTerrain(MapParameters parameters);
    // Local command handlers. GameWorld remains the authority/router, while
    // this object owns map-local validation and component mutations.
    Building* PlaceBuilding(Player& player, int tileId, std::unique_ptr<Building> building);
    bool DestroyBuilding(Player& player, int tileId);
    bool ConnectReceiver(Player& player, int sourceTileId, int targetTileId, bool alternative);
    bool SetRecipe(Player& player, int tileId, int recipeIndex);
    bool SetProductionBlocked(Player& player, int tileId, bool blocked);
    bool SetRoadPriority(Player& player, int tileId, ResourceType resource);
    bool BeginUpgrade(Player& player, int tileId, double duration);
    bool StartTechnologyResearch(Player& player, int tileId, const std::string& technologyId);
    bool RecruitUnit(Player& player, int tileId, const std::string& unitDefId);
    void Update(Player& player, double dt, std::uint64_t authoritativeTick);
    void UpdateFogOfWar(Player& player) { economy.UpdateFogOfWar(player); }
    void RestoreSimulationTick(std::uint64_t tick) { economy.simulationTick = tick; }

private:
    Player* owner{nullptr};
    ProvinceEconomy economy;
};

#endif
