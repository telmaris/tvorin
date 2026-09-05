#ifndef PLAYER_DATA_TRACKER_H
#define PLAYER_DATA_TRACKER_H

#include "economy/Building.h"
#include "core/GameCommand.h"

#include <map>
#include <set>

// Player-local data index fed by simulation events instead of tilemap scans.
struct PlayerDataTracker
{
    std::set<Building*> buildings;
    std::map<BuildingType, std::set<Building*>> buildingsByType;
    std::map<GameCommandType, int> processedCommands;

    // Adds a building to all player-local indexes.
    void RegisterBuilding(Building* building)
    {
        if (building == nullptr)
            return;

        buildings.insert(building);
        buildingsByType[building->buildingType].insert(building);
        IndexComponent<RoadComponent>(building);
        IndexComponent<ProductionComponent>(building);
        IndexComponent<LogisticsComponent>(building);
        IndexComponent<WorkerComponent>(building);
        IndexComponent<RecipeComponent>(building);
        IndexComponent<ResearchComponent>(building);
        IndexComponent<StorageComponent>(building);
        IndexComponent<PopulationComponent>(building);
        IndexComponent<RecruitmentComponent>(building);
    }

    // Removes a building from all player-local indexes.
    void UnregisterBuilding(Building* building)
    {
        if (building == nullptr)
            return;

        buildings.erase(building);
        auto byType = buildingsByType.find(building->buildingType);
        if (byType != buildingsByType.end())
        {
            byType->second.erase(building);
            if (byType->second.empty())
                buildingsByType.erase(byType);
        }
        UnindexComponent<RoadComponent>(building);
        UnindexComponent<ProductionComponent>(building);
        UnindexComponent<LogisticsComponent>(building);
        UnindexComponent<WorkerComponent>(building);
        UnindexComponent<RecipeComponent>(building);
        UnindexComponent<ResearchComponent>(building);
        UnindexComponent<StorageComponent>(building);
        UnindexComponent<PopulationComponent>(building);
        UnindexComponent<RecruitmentComponent>(building);
    }

    // Records a command accepted by the simulation for later player analytics.
    void TrackCommand(GameCommandType type)
    {
        processedCommands[type]++;
    }

    // Returns whether at least one building of this type exists.
    bool HasBuilding(BuildingType type, bool completedOnly = false) const
    {
        auto it = buildingsByType.find(type);
        if (it == buildingsByType.end())
            return false;
        if (!completedOnly)
            return !it->second.empty();

        for (const auto* building : it->second)
            if (building != nullptr && !building->IsUnderConstruction())
                return true;
        return false;
    }

    // Returns the number of tracked buildings of a type.
    int CountBuildings(BuildingType type, bool completedOnly = false) const
    {
        auto it = buildingsByType.find(type);
        if (it == buildingsByType.end())
            return 0;
        if (!completedOnly)
            return static_cast<int>(it->second.size());

        int count = 0;
        for (const auto* building : it->second)
            if (building != nullptr && !building->IsUnderConstruction())
                count++;
        return count;
    }

    template<typename T>
    const std::set<Building*>& BuildingsWithComponent() const
    {
        static const std::set<Building*> empty;
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability == BuildingCapability::Count)
            return empty;
        else
        {
            auto it = buildingsByComponent.find(capability);
            return it != buildingsByComponent.end() ? it->second : empty;
        }
    }

private:
    std::map<BuildingCapability, std::set<Building*>> buildingsByComponent;

    template<typename T>
    void IndexComponent(Building* building)
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
            if (building->HasComponent<T>())
                buildingsByComponent[capability].insert(building);
    }

    template<typename T>
    void UnindexComponent(Building* building)
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
        {
            auto it = buildingsByComponent.find(capability);
            if (it == buildingsByComponent.end())
                return;
            it->second.erase(building);
            if (it->second.empty())
                buildingsByComponent.erase(it);
        }
    }
};

#endif
