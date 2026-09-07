#include "world/ProvinceSimulation.h"

#include "economy/Building.h"
#include "economy/BuildingSalvage.h"
#include "economy/Player.h"

#include <algorithm>

ProvinceEconomy::ProvinceEconomy(ProvinceId id, PlayerId owner)
    : provinceId(id), ownerId(owner),
      roadNetwork(std::make_unique<RoadNetwork>(ownedTilemap)),
      build(nullptr, ownedTilemap, static_cast<int>(owner), id)
{
    ownedTilemap.provinceEconomy = this;
}

void ProvinceEconomy::BindOwner(Player& player)
{
    ownerId = player.id;
    build.player = &player;
    build.playerId = player.id;
    for (Building* building : dataTracker.buildings)
    {
        if (building == nullptr)
            continue;
        building->owner = &player;
        building->ownerId = player.id;
        building->provinceId = provinceId;
        building->provinceEconomy = this;
    }
}

void ProvinceEconomy::BindTileMap(TileMap& map)
{
    tilemap = &map;
    map.provinceEconomy = this;
    if (roadNetwork == nullptr)
        roadNetwork = std::make_unique<RoadNetwork>(map);
    else
        roadNetwork->RebindWorld(map);
    build.RebindTileMap(map);
}

void ProvinceEconomy::RegisterBuilding(Building* building)
{
    if (building == nullptr)
        return;
    building->provinceEconomy = this;
    building->ownerId = building->owner != nullptr ? building->owner->id : ownerId;
    building->provinceId = provinceId;
    dataTracker.RegisterBuilding(building);
    if (building->HasComponent<StorageComponent>())
        storages.push_back(building);
    if (building->HasComponent<PopulationComponent>())
        villages.push_back(building);
    ++registryGeneration;
}

void ProvinceEconomy::UnregisterBuilding(Building* building)
{
    if (building == nullptr)
        return;
    dataTracker.UnregisterBuilding(building);
    auto remove = [building](std::vector<Building*>& registry)
    {
        auto it = std::find(registry.begin(), registry.end(), building);
        if (it != registry.end())
            registry.erase(it);
    };
    remove(storages);
    remove(villages);
    ++registryGeneration;
    building->provinceEconomy = nullptr;
}

void ProvinceEconomy::Update(Player& player, double dt, std::uint64_t authoritativeTick)
{
    if (dt < 0.0 || tilemap == nullptr)
        return;

    construction.Refresh(player, *this);
    if (roadNetwork != nullptr)
        roadNetwork->Update(dt);

    std::vector<Building*> ordered(dataTracker.buildings.begin(), dataTracker.buildings.end());
    std::sort(ordered.begin(), ordered.end(), [](const Building* a, const Building* b)
    {
        if (a == nullptr || b == nullptr)
            return a != nullptr;
        return a->id < b->id;
    });

    const bool isCampaignPopulation = player.homeProvinceId != InvalidProvinceId;
    if (isCampaignPopulation)
    {
        if (!population.initialized)
        {
            // Migrate the legacy global pool exactly once for the home
            // province. Later-conquered provinces start with their own empty
            // local pool and can grow it through their villages.
            if (player.homeProvinceId == provinceId)
            {
                population.availableManpower = player.strategicResources.Get(
                    StrategicResourceType::Manpower);
                player.strategicResources.Set(StrategicResourceType::Manpower, 0.0);
            }
            population.initialized = true;
        }

        std::vector<Building*> villagesForAllocation;
        std::vector<double> capacities;
        for (Building* building : ordered)
        {
            if (building == nullptr || building->owner != &player ||
                building->IsUnderConstruction())
                continue;
            auto* village = building->GetComponent<PopulationComponent>();
            if (village == nullptr)
                continue;
            villagesForAllocation.push_back(building);
            capacities.push_back(static_cast<double>(player.ResolveStat(
                village->populationCap, building)));
        }

        const ProvincePopulationView view = BuildProvincePopulationView(*this, player);
        const auto residents = AllocateProvinceVillageResidents(
            view.currentPopulation, capacities);
        for (size_t i = 0; i < villagesForAllocation.size(); i++)
        {
            auto* village = villagesForAllocation[i]->GetComponent<PopulationComponent>();
            village->assignedResidents = residents[i];
            village->hasAssignedResidents = true;
        }
    }

    for (Building* building : ordered)
    {
        if (building == nullptr || building->owner != &player)
            continue;
        const bool wasUnderConstruction = building->IsUnderConstruction();
        building->Update(dt);
        if (wasUnderConstruction && !building->IsUnderConstruction())
        {
            tilemap->buildingsDirty = true;
            if (roadNetwork != nullptr)
                for (int tileId : tilemap->GetBuildingTileIds(building))
                    roadNetwork->UpdateNavMap(tileId, building);
            tilemap->AutoConnectBuilding(building);
        }
    }
    economyTelemetry.Update(player, dt);
    simulationTick = authoritativeTick;
}

void ProvinceEconomy::UpdateFogOfWar(Player& player)
{
    if (tilemap == nullptr)
        return;
    const Vec2i mapSize{tilemap->params.sizeX, tilemap->params.sizeY};
    if (mapSize.x <= 0 || mapSize.y <= 0)
        return;
    if (!fogOfWar.IsInitializedFor(mapSize))
        fogOfWar.Initialize(mapSize);
    else
        fogOfWar.BeginVisibilityUpdate();

    for (const Building* building : dataTracker.buildings)
    {
        if (building == nullptr || building->owner != &player)
            continue;
        const Vec2i anchor = tilemap->GetCoordsFromId(building->positionId);
        const Vec2i footprint = building->GetFootprint();
        const Vec2f center{
            static_cast<float>(anchor.x * TILE_SIZE) + footprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(anchor.y * TILE_SIZE) + footprint.y * TILE_SIZE * 0.5f};
        fogOfWar.RevealWorldCircle(center,
                                   FogOfWar::BuildingRevealRadiusWorld(building->buildingType, footprint));
    }
}

ProvinceSimulation::ProvinceSimulation(ProvinceId provinceId, PlayerId ownerId)
    : economy(provinceId, ownerId)
{
}

void ProvinceSimulation::BindExternalTileMap(TileMap& externalMap)
{
    economy.BindTileMap(externalMap);
}

void ProvinceSimulation::BindPlayer(Player& player)
{
    owner = &player;
    economy.BindOwner(player);
}

bool ProvinceSimulation::GenerateTerrain(MapParameters parameters)
{
    if (parameters.sizeX <= 0 || parameters.sizeY <= 0)
        return false;
    economy.ownedTilemap.tilemap.clear();
    economy.ownedTilemap.generator.GenerateTileMap(economy.ownedTilemap, parameters);
    economy.BindTileMap(economy.ownedTilemap);
    economy.simulationTick = 0;
    return !economy.ownedTilemap.tilemap.empty();
}

namespace
{
    bool BelongsTo(const ProvinceEconomy& economy, const Player& player, const Building* building)
    {
        return building != nullptr && building->owner == &player &&
               building->ownerId == player.id && building->provinceEconomy == &economy;
    }
}

Building* ProvinceSimulation::PlaceBuilding(Player& player, int tileId,
                                             std::unique_ptr<Building> building)
{
    if (economy.tilemap == nullptr || economy.ownerId != player.id || building == nullptr ||
        tileId < 0 || tileId >= static_cast<int>(economy.tilemap->tilemap.size()))
        return nullptr;
    economy.tilemap->BuildOnTile(tileId, &player, std::move(building));
    return economy.tilemap->GetBuilding(tileId);
}

bool ProvinceSimulation::DestroyBuilding(Player& player, int tileId)
{
    if (economy.tilemap == nullptr)
        return false;
    Building* building = economy.tilemap->GetBuilding(tileId);
    if (!BelongsTo(economy, player, building))
        return false;
    return ExecuteDemolition(*economy.tilemap, player, *building);
}

bool ProvinceSimulation::ConnectReceiver(Player& player, int sourceTileId, int targetTileId,
                                         bool alternative)
{
    if (economy.tilemap == nullptr)
        return false;
    Building* source = economy.tilemap->GetBuilding(sourceTileId);
    Building* target = economy.tilemap->GetBuilding(targetTileId);
    if (!BelongsTo(economy, player, source) || !BelongsTo(economy, player, target) ||
        source == target || source->IsUnderConstruction() || target->IsUnderConstruction())
        return false;
    economy.tilemap->ConnectReceiver(source, target, alternative);
    return true;
}

bool ProvinceSimulation::SetRecipe(Player& player, int tileId, int recipeIndex)
{
    if (economy.tilemap == nullptr)
        return false;
    Building* building = economy.tilemap->GetBuilding(tileId);
    if (!BelongsTo(economy, player, building) || building->IsUnderConstruction())
        return false;
    auto* recipes = building->GetComponent<RecipeComponent>();
    auto* production = building->GetComponent<ProductionComponent>();
    auto* logistics = building->GetComponent<LogisticsComponent>();
    auto* workers = building->GetComponent<WorkerComponent>();
    return recipes != nullptr && production != nullptr && logistics != nullptr && workers != nullptr &&
           recipes->SetActiveRecipe(recipeIndex, *building, *production, *logistics, *workers);
}

bool ProvinceSimulation::SetProductionBlocked(Player& player, int tileId, bool blocked)
{
    if (economy.tilemap == nullptr)
        return false;
    Building* building = economy.tilemap->GetBuilding(tileId);
    if (!BelongsTo(economy, player, building) || building->IsUnderConstruction() ||
        !building->CanBlockProduction())
        return false;
    building->SetProductionBlocked(blocked);
    return true;
}

bool ProvinceSimulation::SetRoadPriority(Player& player, int tileId, ResourceType resource)
{
    if (economy.tilemap == nullptr)
        return false;
    Building* building = economy.tilemap->GetBuilding(tileId);
    auto* road = building != nullptr ? building->GetComponent<RoadComponent>() : nullptr;
    if (!BelongsTo(economy, player, building) || building->IsUnderConstruction() ||
        !IsRoadLike(building->buildingType) || road == nullptr ||
        !RoadComponent::IsValidPriorityResource(resource))
        return false;
    road->SetPriorityResource(resource);
    if (economy.roadNetwork != nullptr)
        economy.roadNetwork->InvalidateRoutingCosts();
    return true;
}

bool ProvinceSimulation::BeginUpgrade(Player& player, int tileId, double duration)
{
    if (economy.tilemap == nullptr || duration < 0.0)
        return false;
    Building* building = economy.tilemap->GetBuilding(tileId);
    auto* upgrade = building != nullptr ? building->GetComponent<UpgradeComponent>() : nullptr;
    if (!BelongsTo(economy, player, building) || building->IsUnderConstruction() ||
        upgrade == nullptr || upgrade->isUpgrading || upgrade->level >= upgrade->maxLevel)
        return false;
    upgrade->isUpgrading = true;
    upgrade->upgradeRemaining = duration;
    return true;
}

bool ProvinceSimulation::StartTechnologyResearch(Player& player, int tileId,
                                                  const std::string& technologyId)
{
    if (economy.tilemap == nullptr || technologyId.empty())
        return false;
    Building* building = economy.tilemap->GetBuilding(tileId);
    if (!BelongsTo(economy, player, building) || building->IsUnderConstruction() ||
        building->buildingType != BuildingType::University ||
        building->GetComponent<ResearchComponent>() == nullptr)
        return false;
    return player.StartTechnologyResearch(technologyId, building, economy);
}

bool ProvinceSimulation::RecruitUnit(Player& player, int tileId, const std::string& unitDefId)
{
    if (economy.tilemap == nullptr)
        return false;
    Building* building = economy.tilemap->GetBuilding(tileId);
    if (!BelongsTo(economy, player, building) || building->IsUnderConstruction())
        return false;
    auto* recruitment = building->GetComponent<RecruitmentComponent>();
    return recruitment != nullptr && !unitDefId.empty() &&
           recruitment->QueueRecruitment(*building, unitDefId);
}

void ProvinceSimulation::Update(Player& player, double dt, std::uint64_t authoritativeTick)
{
    economy.Update(player, dt, authoritativeTick);
}
