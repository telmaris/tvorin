#include "ui/BuildingPresentation.h"

#include "economy/BuildingConfig.h"
#include "economy/BuildingComponents.h"
#include "economy/Player.h"
#include "economy/ProvincePopulation.h"
#include "data/Resource.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace
{
    std::string Percent(double ratio)
    {
        const int percent = static_cast<int>(std::round(std::clamp(ratio, 0.0, 1.0) * 100.0));
        return std::to_string(percent) + "%";
    }

    std::string JoinResourceNames(const std::vector<ResourceType>& resources)
    {
        std::string result;
        for (size_t index = 0; index < resources.size(); ++index)
        {
            if (index > 0)
                result += ", ";
            result += ResourceDisplayName(resources[index]);
        }
        return result;
    }
}

VillagePresentationView BuildVillagePresentationView(const Building& building)
{
    VillagePresentationView view;
    const auto* population = building.GetComponent<PopulationComponent>();
    if (population == nullptr)
        return view;

    view.populationCap = building.owner != nullptr
        ? building.owner->ResolveStat(population->populationCap, &building)
        : population->populationCap.GetBase();
    const double manpowerRate = building.owner != nullptr
        ? building.owner->ResolveStat(population->manpowerRate, &building)
        : population->manpowerRate.GetBase();
    view.manpowerGainPerMinute = manpowerRate * population->GetManpowerProductivity() * 60.0;
    view.inhabitants = population->hasAssignedResidents
        ? population->assignedResidents
        : building.owner != nullptr
            ? building.owner->GetTotalPopulation()
            : 0.0;
    view.supplies = population->GetSupplyConsumptionViews(building);
    return view;
}

BuildingPresentationStatus BuildBuildingPresentationStatus(const Building& building)
{
    return BuildBuildingPresentationStatus(
        building, false, BuildingPresentationAudience::Owner);
}

BuildingPresentationStatus BuildBuildingPresentationStatus(const Building& building,
                                                             bool roadDisconnected)
{
    return BuildBuildingPresentationStatus(
        building, roadDisconnected, BuildingPresentationAudience::Owner);
}

BuildingPresentationStatus BuildBuildingPresentationStatus(
    const Building& building,
    bool roadDisconnected,
    BuildingPresentationAudience audience)
{
    BuildingPresentationStatus result;
    const bool ownerDetails = audience == BuildingPresentationAudience::Owner;

    if (building.IsUnderConstruction())
    {
        result.general = ownerDetails
            ? "Under construction - " + Percent(building.GetConstructionProgress())
            : "Under construction";
        return result;
    }

    if (const auto* upgrade = building.GetComponent<UpgradeComponent>();
        upgrade != nullptr && upgrade->isUpgrading)
    {
        const auto& definition = GetBuildingDefinition(building.buildingType);
        const auto* target = FindUpgradeLevelDefinition(definition, upgrade->level + 1);
        const double total = target != nullptr ? target->buildTime : 0.0;
        const double progress = total > 0.0
            ? 1.0 - upgrade->upgradeRemaining / total
            : 0.0;
        result.general = ownerDetails
            ? "Upgrading to level " + std::to_string(upgrade->level + 1) +
                  " - " + Percent(progress)
            : "Upgrading";
        return result;
    }

    // Visible opponents expose only a coarse operational state. Recipe,
    // buffers, worker assignment and logistics are private economic data.
    if (!ownerDetails)
    {
        result.general = "Operational";
        return result;
    }

    if (roadDisconnected)
    {
        result.general = "Disconnected from road network";
        return result;
    }

    const auto* production = building.GetComponent<ProductionComponent>();
    if (production == nullptr || production->products.empty() || production->cycleTime.GetBase() <= 0.0)
    {
        result.general = building.GetLifetime() > 0.0 ? "Operational" : "Idle";
        return result;
    }

    const auto* workers = building.GetComponent<WorkerComponent>();
    if (workers != nullptr && building.GetWorkerCapacity() > 0 && workers->assigned <= 0)
    {
        result.general = "Stopped - no workers";
        return result;
    }

    std::vector<ResourceType> missingInputs;
    for (const auto& [type, amount] : production->ingredients)
    {
        const auto bufferIt = production->inputBuffers.find(type);
        const int stored = bufferIt != production->inputBuffers.end()
            ? static_cast<int>(bufferIt->second.buffer.size()) : 0;
        if (stored < amount)
            missingInputs.push_back(type);
    }
    if (!missingInputs.empty())
    {
        result.general = "Waiting for: " + JoinResourceNames(missingInputs);
        return result;
    }

    for (const auto& [type, amount] : production->products)
    {
        const auto bufferIt = production->outputBuffers.find(type);
        if (bufferIt == production->outputBuffers.end())
            continue;
        if (bufferIt->second.bufferSize <= 0 ||
            static_cast<int>(bufferIt->second.buffer.size()) + std::max(0, amount) > bufferIt->second.bufferSize)
        {
            result.general = "Output full";
            return result;
        }
    }

    std::vector<ResourceType> productTypes;
    productTypes.reserve(production->products.size());
    for (const auto& [type, amount] : production->products)
    {
        (void)amount;
        productTypes.push_back(type);
    }

    // Recipe names such as "Default" are implementation/configuration labels,
    // not useful production status. Name the physical output instead; the
    // detailed panel already has the exact cycle progress bar.
    result.production = "Producing " + JoinResourceNames(productTypes);
    result.general = result.production;
    return result;
}

void DrawBuildingFootprintStatusOverlay(Vec2f position,
                                        Vec2i footprint,
                                        float renderHeight,
                                        float tileSize,
                                        BuildingFootprintOverlay overlay)
{
    const float width = footprint.x * tileSize;
    const float height = footprint.y * tileSize;
    const float top = renderHeight - position.y - height;
    if (overlay == BuildingFootprintOverlay::Disconnected)
    {
        DrawRectangle(static_cast<int>(position.x), static_cast<int>(top),
                      static_cast<int>(width), static_cast<int>(height),
                      Color{224, 78, 72, 38});
        DrawRectangleLinesEx({position.x - 1.0f, top - 1.0f, width + 2.0f, height + 2.0f},
                             2.0f, Color{240, 102, 94, 170});
        return;
    }

    const float pulse = 0.45f + 0.55f *
        std::abs(std::sin(static_cast<float>(GetTime()) * 7.0f));
    DrawRectangle(static_cast<int>(position.x), static_cast<int>(top),
                  static_cast<int>(width), static_cast<int>(height),
                  Color{76, 205, 210, static_cast<unsigned char>(18.0f + pulse * 22.0f)});
    DrawRectangleLinesEx({position.x - 2.0f, top - 2.0f, width + 4.0f, height + 4.0f},
                         2.5f,
                         Color{102, 232, 222, static_cast<unsigned char>(135.0f + pulse * 100.0f)});
}
