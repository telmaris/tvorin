#ifndef BUILDING_UPGRADE_VIEW_H
#define BUILDING_UPGRADE_VIEW_H

#include "economy/Building.h"
#include "economy/BuildingConfig.h"

// Read-only model shared by all building upgrade panels. The presence of a
// maxLevel alone is intentionally not enough: a contiguous, valid next record
// must be present in the data file.
struct BuildingUpgradeView
{
    const UpgradeComponent* component{nullptr};
    const BuildingUpgradeLevelDefinition* target{nullptr};
    int targetLevel{0};

    bool HasTarget() const { return component != nullptr && target != nullptr; }
    bool CanStart() const
    {
        return HasTarget() && !component->isUpgrading &&
               targetLevel <= component->maxLevel;
    }
};

inline BuildingUpgradeView MakeBuildingUpgradeView(const Building& building)
{
    BuildingUpgradeView view;
    view.component = building.GetComponent<UpgradeComponent>();
    if (view.component == nullptr)
        return view;

    view.targetLevel = view.component->level + 1;
    const auto& definition = GetBuildingDefinition(building.buildingType);
    view.target = FindUpgradeLevelDefinition(definition, view.targetLevel);
    return view;
}

#endif
