#include "core/GameWorld.h"
#include "core/SimulationState.h"

#include <algorithm>
#include <vector>

namespace
{
    void HashValue(CanonicalStateWriter& state, std::uint64_t value)
    {
        state.U64(value);
    }

    void HashInt(CanonicalStateWriter& state, int value)
    {
        state.I32(value);
    }

    void HashDouble(CanonicalStateWriter& state, double value)
    {
        state.FixedDouble3(value);
    }

    void HashResourceBuffers(CanonicalStateWriter& state, const std::map<ResourceType, ResourceBuffer>& buffers)
    {
        HashValue(state, static_cast<std::uint64_t>(buffers.size()));
        for (const auto& [type, buffer] : buffers)
        {
            HashInt(state, static_cast<int>(type));
            HashInt(state, buffer.bufferSize);
            HashInt(state, static_cast<int>(buffer.buffer.size()));
        }
    }

    // Character-by-character rather than std::hash<std::string>, which is not
    // guaranteed stable across STL versions/architectures — this checksum is
    // compared between host and client processes.
    void HashString(CanonicalStateWriter& state, const std::string& value)
    {
        state.String(value);
    }
}

std::uint64_t GameWorld::BuildChecksum() const
{
    CanonicalStateWriter state;
    // Keep the existing traversal readable while routing every primitive
    // through the canonical AUD-03 writer.
    auto& hash = state;
    HashValue(state, simulationTick);
    HashInt(state, tilemap.params.sizeX);
    HashInt(state, tilemap.params.sizeY);
    HashValue(state, tilemap.params.seed);

    // Military road ring (TD etap-2): immutable after generation, but included
    // so any host/client generation divergence surfaces as a checksum
    // mismatch immediately rather than only once units start marching.
    const auto& militaryRoutes = militaryRoads.GetRoutes();
    HashValue(hash, static_cast<std::uint64_t>(militaryRoutes.size()));
    for (const auto& route : militaryRoutes)
    {
        HashInt(hash, route.playerA);
        HashInt(hash, route.playerB);
        HashValue(hash, static_cast<std::uint64_t>(route.tiles.size()));
        for (int tileId : route.tiles)
            HashInt(hash, tileId);
    }

    for (const auto& [playerId, player] : playerHandler.players)
    {
        HashInt(hash, playerId);
        if (player == nullptr)
        {
            HashValue(hash, 0);
            continue;
        }

        // TD(etap-6): elimination flag — written only inside the sim tick,
        // must diverge into a checksum mismatch immediately if host/client
        // ever disagree on who's defeated.
        HashInt(hash, player->defeated ? 1 : 0);
        HashInt(hash, player->nextUnitInstanceId);
        for (const auto& [resourceType, amount] : player->strategicResources.values)
        {
            HashInt(hash, static_cast<int>(resourceType));
            HashDouble(hash, amount);
        }
        for (const auto& technologyId : player->technologies.GetUnlocked())
            HashString(hash, technologyId);
        for (const auto& focusId : player->focuses.GetUnlocked())
            HashString(hash, focusId);
        HashString(hash, player->focuses.GetActiveFocusId());
        HashDouble(hash, player->focuses.GetActiveFocusRemaining());

        HashInt(hash, player->dataTracker.CountBuildings(BuildingType::Headquarters));
        HashValue(hash, static_cast<std::uint64_t>(player->dataTracker.buildings.size()));

        // dataTracker.buildings is a std::set<Building*> ordered by raw pointer
        // value, which differs between independently-allocated host/client
        // processes. Sort by the stable, assigned building id before hashing so
        // the checksum doesn't depend on heap layout.
        std::vector<const Building*> orderedBuildings(player->dataTracker.buildings.begin(),
                                                        player->dataTracker.buildings.end());
        std::sort(orderedBuildings.begin(), orderedBuildings.end(), [](const Building* a, const Building* b)
        {
            return a->id < b->id;
        });

        for (const auto* building : orderedBuildings)
        {
            if (building == nullptr)
                continue;

            HashInt(hash, building->id);
            HashInt(hash, building->positionId);
            HashInt(hash, static_cast<int>(building->buildingType));
            HashInt(hash, building->owner != nullptr ? building->owner->id : -1);
            HashInt(hash, static_cast<int>(building->constructionRemaining * 1000.0));
            HashInt(hash, building->GetTotalProduced());
            HashInt(hash, building->IsProductionBlocked() ? 1 : 0);
            HashInt(hash, static_cast<int>(building->buildCostRecordState));
            HashInt(hash, building->buildCostWasPaid ? 1 : 0);
            HashValue(hash, static_cast<std::uint64_t>(building->paidBuildCosts.size()));
            for (const auto& cost : building->paidBuildCosts)
            {
                HashInt(hash, static_cast<int>(cost.type));
                HashInt(hash, cost.amount);
            }

            if (const auto* production = building->GetComponent<ProductionComponent>(); production != nullptr)
            {
                HashDouble(hash, production->elapsed);
                HashInt(hash, production->started ? 1 : 0);
                HashInt(hash, production->totalProduced);
                HashResourceBuffers(hash, production->inputBuffers);
                HashResourceBuffers(hash, production->outputBuffers);
            }
            if (const auto* workers = building->GetComponent<WorkerComponent>(); workers != nullptr)
                HashInt(hash, workers->assigned);
            if (const auto* research = building->GetComponent<ResearchComponent>(); research != nullptr)
            {
                HashString(hash, research->technologyId);
                HashDouble(hash, research->remaining);
                HashDouble(hash, research->total);
            }
            if (const auto* storage = building->GetComponent<StorageComponent>(); storage != nullptr)
                HashResourceBuffers(hash, storage->buffers);
            if (const auto* local = building->GetComponent<LocalResourceBufferComponent>(); local != nullptr)
                HashResourceBuffers(hash, local->buffers);
            if (const auto* population = building->GetComponent<PopulationComponent>(); population != nullptr)
            {
                HashDouble(hash, population->upkeepTimer);
                HashDouble(hash, population->householdUpkeepTimer);
                HashDouble(hash, population->urbanUpkeepTimer);
                HashInt(hash, population->hasFood ? 1 : 0);
                HashDouble(hash, population->foodSupplyLevel);
                HashInt(hash, static_cast<int>(population->foodBuffer.buffer.size()));
                HashInt(hash, population->settlementLevel);
                HashDouble(hash, population->householdSupplyLevel);
                HashInt(hash, static_cast<int>(population->householdGoodsBuffer.buffer.size()));
                HashDouble(hash, population->urbanSupplyLevel);
                HashInt(hash, static_cast<int>(population->urbanGoodsBuffer.buffer.size()));
            }
            if (const auto* recruitment = building->GetComponent<RecruitmentComponent>(); recruitment != nullptr)
            {
                HashValue(hash, static_cast<std::uint64_t>(recruitment->queue.size()));
                for (const auto& entry : recruitment->queue)
                {
                    HashString(hash, entry.unitDefId);
                    HashDouble(hash, entry.remaining);
                    HashInt(hash, entry.resourcesReady ? 1 : 0);
                }
            }
            if (const auto* upgrade = building->GetComponent<UpgradeComponent>(); upgrade != nullptr)
            {
                HashInt(hash, upgrade->level);
                HashInt(hash, upgrade->isUpgrading ? 1 : 0);
                HashDouble(hash, upgrade->upgradeRemaining);
            }
            if (const auto* road = building->GetComponent<RoadComponent>(); road != nullptr)
                HashInt(hash, static_cast<int>(road->priorityResource));

            // TD(etap-6): HQ HP/thorns cadence — mutated every tick under siege.
            if (const auto* hq = building->GetComponent<HqComponent>(); hq != nullptr)
            {
                HashInt(hash, static_cast<int>(hq->currentHp * 1000.0));
                HashInt(hash, static_cast<int>(hq->thornsTimer * 1000.0));
            }

            // TD(etap-7): tower attack cooldown + ammo on hand — both mutated
            // every tick a tower is active.
            if (const auto* tower = building->GetComponent<TowerCombatComponent>(); tower != nullptr)
            {
                HashInt(hash, static_cast<int>(tower->attackTimer * 1000.0));
                HashInt(hash, static_cast<int>(tower->targetMode));
                if (const auto* storage = building->GetComponent<LocalResourceBufferComponent>(); storage != nullptr)
                {
                    auto ammoIt = storage->buffers.find(tower->ammoResource);
                    HashInt(hash, ammoIt != storage->buffers.end()
                                      ? static_cast<int>(ammoIt->second.buffer.size())
                                      : 0);
                }
            }
        }

        // TD(etap-6.3): productivity ramps on buildings captured from an
        // eliminated player.
        for (const auto& ramp : player->conqueredEconomy.GetRamps())
        {
            HashInt(hash, ramp.buildingId);
            HashInt(hash, static_cast<int>(ramp.elapsed * 1000.0));
        }

        for (const auto& [commandType, count] : player->dataTracker.processedCommands)
        {
            HashInt(hash, static_cast<int>(commandType));
            HashInt(hash, count);
        }

        // TD(etap-3): recruited-but-not-deployed roster. std::map<int, BattleUnit>
        // keyed by instanceId is already deterministically ordered.
        HashValue(hash, static_cast<std::uint64_t>(player->roster.units.size()));
        for (const auto& [instanceId, unit] : player->roster.units)
        {
            HashInt(hash, unit.instanceId);
            HashInt(hash, unit.ownerPlayerId);
            HashString(hash, unit.unitDefId);
            HashInt(hash, static_cast<int>(unit.currentHp * 1000.0));
            HashInt(hash, static_cast<int>(unit.state));
        }
    }

    // TD(etap-4): deployed (marching/fighting/arrived) units and their spawn
    // queues. std::map ordering (instanceId; (fromPlayerId,toPlayerId) pair)
    // is already deterministic; std::deque preserves FIFO order.
    HashValue(hash, static_cast<std::uint64_t>(deployedUnits.size()));
    for (const auto& [instanceId, unit] : deployedUnits)
    {
        HashInt(hash, unit.instanceId);
        HashInt(hash, unit.ownerPlayerId);
        HashString(hash, unit.unitDefId);
        HashInt(hash, static_cast<int>(unit.currentHp * 1000.0));
        HashInt(hash, static_cast<int>(unit.state));
        HashInt(hash, unit.routeFromPlayerId);
        HashInt(hash, unit.routeToPlayerId);
        HashInt(hash, unit.tileIndex);
        HashInt(hash, static_cast<int>(unit.tileProgress * 1000.0));
        // TD(etap-5): mutated every tick while FightingUnit.
        HashInt(hash, static_cast<int>(unit.attackTimer * 1000.0));
    }

    HashValue(hash, static_cast<std::uint64_t>(spawnQueues.size()));
    for (const auto& [routeKey, queue] : spawnQueues)
    {
        HashInt(hash, routeKey.first);
        HashInt(hash, routeKey.second);
        HashValue(hash, static_cast<std::uint64_t>(queue.size()));
        for (int unitInstanceId : queue)
            HashInt(hash, unitInstanceId);
    }

    // TD(etap-7.2): in-flight tower projectiles. Persisted in SimulationState
    // and included here so host/client never silently disagree about their
    // existence/position mid-flight. std::map<int, ...> keyed by an
    // allocation-order id is already deterministically ordered.
    HashValue(hash, static_cast<std::uint64_t>(projectiles.size()));
    for (const auto& [id, projectile] : projectiles)
    {
        HashInt(hash, id);
        HashInt(hash, projectile.sourcePlayerId);
        HashInt(hash, projectile.targetUnitInstanceId);
        HashInt(hash, static_cast<int>(projectile.position.x * 1000.0f));
        HashInt(hash, static_cast<int>(projectile.position.y * 1000.0f));
        HashInt(hash, projectile.ticksRemaining);
    }

    return state.Finish();
}
