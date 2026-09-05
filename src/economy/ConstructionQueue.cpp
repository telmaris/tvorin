#include "economy/ConstructionQueue.h"

#include "economy/Building.h"
#include "economy/Player.h"
#include "world/ProvinceSimulation.h"

#include <algorithm>

int ConstructionQueue::EffectiveBuilders(const Player& player) const
{
    // Never drop below a single builder, otherwise construction would deadlock.
    return player.ModifyBalanceInt(BalanceStat::BuilderAmount, builders.GetBase(),
                                   BuildingType::Building, ResourceType::Null, 1);
}

void ConstructionQueue::Refresh(Player& player)
{
    ProvinceEconomy* economy = player.GetProvinceEconomy();
    if (economy != nullptr)
        Refresh(player, *economy);
    else
    {
        order.clear();
        activeCount = 0;
    }
}

void ConstructionQueue::Refresh(Player& player, ProvinceEconomy& economy)
{
    // Upgrading buildings share this same builder-limited queue (user
    // request, 2026-07-20: "ulepszenie drogi trafi do kolejki budowy") — but
    // an upgrading building is NOT "under construction" (IsUnderConstruction()
    // gates other things like SetReceiver and must stay false so it keeps
    // transporting/operating normally), so it's gated by its own
    // UpgradeComponent::upgradeActive flag instead of constructionActive.
    std::vector<Building*> pending;
    for (Building* building : economy.dataTracker.buildings)
    {
        if (building == nullptr)
            continue;
        if (building->IsUnderConstruction())
            pending.push_back(building);
        else if (auto* upgrade = building->GetComponent<UpgradeComponent>(); upgrade != nullptr && upgrade->isUpgrading)
            pending.push_back(building);
    }

    // dataTracker.buildings iterates by pointer address (non-deterministic), so
    // sort by building id to get a stable, cross-machine-identical queue order.
    std::sort(pending.begin(), pending.end(), [](const Building* a, const Building* b)
    {
        return a->id < b->id;
    });

    int builderCount = EffectiveBuilders(player);
    order.clear();
    order.reserve(pending.size());
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        bool active = static_cast<int>(i) < builderCount;
        Building* building = pending[i];
        if (building->IsUnderConstruction())
            building->constructionActive = active;
        else if (auto* upgrade = building->GetComponent<UpgradeComponent>())
            upgrade->upgradeActive = active;
        order.push_back(building->id);
    }
    activeCount = std::min<int>(builderCount, static_cast<int>(pending.size()));
}

int ConstructionQueue::QueuePosition(int buildingId) const
{
    auto it = std::find(order.begin(), order.end(), buildingId);
    if (it == order.end())
        return 0;
    return static_cast<int>(std::distance(order.begin(), it)) + 1;
}

bool ConstructionQueue::IsActive(int buildingId) const
{
    int position = QueuePosition(buildingId);
    return position > 0 && position <= activeCount;
}
