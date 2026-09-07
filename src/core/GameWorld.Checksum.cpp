#include "core/GameWorld.h"
#include "core/SimulationState.h"

#include <algorithm>
#include <type_traits>
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

    void HashString(CanonicalStateWriter& state, const std::string& value);

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

    void HashBattleOutcome(CanonicalStateWriter& state, const BattleOutcome& outcome)
    {
        HashInt(state, outcome.valid ? 1 : 0);
        HashInt(state, static_cast<int>(outcome.winner));
        HashDouble(state, outcome.attackerStrength);
        HashDouble(state, outcome.defenderStrength);
        HashInt(state, outcome.attackerCasualtyBudget);
        HashInt(state, outcome.defenderCasualtyBudget);
        HashDouble(state, outcome.lootValue);
        HashInt(state, outcome.crushingVictory ? 1 : 0);
        HashValue(state, outcome.durationTicks);
        HashValue(state, outcome.attackerLostUnitIds.size());
        for (const int id : outcome.attackerLostUnitIds) HashInt(state, id);
        HashValue(state, outcome.defenderLostUnitIds.size());
        for (const int id : outcome.defenderLostUnitIds) HashInt(state, id);
    }

    void HashAppliedWorldEventEffect(CanonicalStateWriter& state,
                                     const AppliedWorldEventEffect& effect)
    {
        HashInt(state, static_cast<int>(effect.kind));
        HashInt(state, static_cast<int>(effect.resourceType));
        HashInt(state, effect.amount);
        HashInt(state, static_cast<int>(effect.stat));
        HashDouble(state, effect.additive);
        HashDouble(state, effect.multiplier);
        HashValue(state, effect.durationTicks);
    }

    void HashBattleSide(CanonicalStateWriter& state, const BattleSideSnapshot& side)
    {
        HashInt(state, side.ownerId);
        HashDouble(state, side.defensiveBonus);
        HashValue(state, side.units.size());
        for (const auto& unit : side.units)
        {
            HashInt(state, unit.instanceId);
            HashString(state, unit.unitDefId);
            HashDouble(state, unit.effectiveFieldAttack);
        }
    }

    // Character-by-character rather than std::hash<std::string>, which is not
    // guaranteed stable across STL versions/architectures — this checksum is
    // compared between host and client processes.
    void HashString(CanonicalStateWriter& state, const std::string& value)
    {
        state.String(value);
    }

    void HashProvinceTileMap(CanonicalStateWriter& state, ProvinceId provinceId,
                             const TileMap& map)
    {
        HashValue(state, provinceId);
        HashInt(state, map.params.sizeX);
        HashInt(state, map.params.sizeY);
        HashValue(state, map.params.seed);
        HashInt(state, static_cast<int>(map.params.sizePreset));
        HashDouble(state, map.params.resourceDensity);
        HashDouble(state, map.params.resourceFieldSize);
        HashInt(state, map.params.resourceRichness);
        HashInt(state, map.params.aiOpponentCount);
        HashInt(state, map.params.aiDifficulty);
        HashInt(state, map.params.debugMode ? 1 : 0);
        HashValue(state, static_cast<std::uint64_t>(map.tilemap.size()));
        for (const auto& tile : map.tilemap)
        {
            HashInt(state, tile.id);
            HashInt(state, static_cast<int>(tile.tileType));
            HashInt(state, static_cast<int>(tile.biome));
            HashInt(state, tile.terrainTextureId);
            HashInt(state, tile.resourceOverlayTextureId);
            HashInt(state, tile.resourceRichness);
            const auto* building = tile.GetBuilding();
            HashInt(state, building != nullptr ? building->id : 0);
            HashValue(state, building != nullptr ? building->provinceId : InvalidProvinceId);
        }
    }
}

std::uint64_t GameWorld::BuildChecksum() const
{
    CanonicalStateWriter state;
    // Keep the existing traversal readable while routing every primitive
    // through the canonical AUD-03 writer.
    auto& hash = state;
    HashValue(state, simulationTick);

    HashValue(state, static_cast<std::uint64_t>(globalMap.GetProvinceCount()));
    HashValue(state, globalMap.GetGenerationSeed());
    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr || province->GetSimulation() == nullptr)
            continue;
        HashProvinceTileMap(state, provinceId, province->GetSimulation()->GetTileMap());
    }

    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindProvince(provinceId);
        if (province == nullptr)
            continue;
        const auto* buildable = globalMap.FindBuildableProvince(provinceId);
        HashValue(state, provinceId);
        HashInt(state, static_cast<int>(province->GetKind()));
        HashInt(state, buildable != nullptr ? buildable->GetOwnerId() : InvalidPlayerId);
        HashInt(state, province->GetLayoutPosition().x);
        HashInt(state, province->GetLayoutPosition().y);
        if (const auto* buildable = dynamic_cast<const BuildableProvince*>(province); buildable != nullptr)
        {
            const auto& parameters = buildable->GetParameters();
            HashString(state, parameters.definitionId);
            HashInt(state, parameters.sizeX);
            HashInt(state, parameters.sizeY);
            HashValue(state, parameters.localSeed);
            HashDouble(state, parameters.resourceWealth);
            HashDouble(state, parameters.resourceDensity);
            HashDouble(state, parameters.resourceFieldSize);
            HashDouble(state, parameters.resourceRichness);
            HashDouble(state, parameters.waterAmount);
            HashDouble(state, parameters.mountainAmount);
            HashDouble(state, parameters.ruggedness);
            HashInt(state, parameters.wealthTier);
            HashValue(state, parameters.traitIds.size());
            for (const auto& traitId : parameters.traitIds)
                HashString(state, traitId);
            HashValue(state, parameters.naturalResourceTypes.size());
            for (const ResourceType resource : parameters.naturalResourceTypes)
                HashInt(state, static_cast<int>(resource));
            HashValue(state, parameters.resourceDeposits.size());
            for (const auto& deposit : parameters.resourceDeposits)
            {
                HashInt(state, static_cast<int>(deposit.resource));
                HashDouble(state, deposit.richness);
            }
        }
        else if (const auto* city = dynamic_cast<const NeutralCityProvince*>(province); city != nullptr)
        {
            HashString(state, city->GetDefinitionId());
            HashInt(state, city->GetWealthTier());
            const auto& cityState = city->GetState();
            HashDouble(state, cityState.barterPenaltyMultiplier);
            HashValue(state, cityState.revision);
            HashValue(state, cityState.stock.size());
            for (const auto& [type, amount] : cityState.stock)
            {
                HashInt(state, static_cast<int>(type));
                HashInt(state, amount);
            }
            HashValue(state, cityState.buyPrices.size());
            for (const auto& [type, price] : cityState.buyPrices)
            {
                HashInt(state, static_cast<int>(type));
                HashDouble(state, price);
            }
            HashValue(state, cityState.sellPrices.size());
            for (const auto& [type, price] : cityState.sellPrices)
            {
                HashInt(state, static_cast<int>(type));
                HashDouble(state, price);
            }
            HashValue(state, cityState.tradeScore.size());
            for (const auto& [ownerId, score] : cityState.tradeScore)
            {
                HashInt(state, ownerId);
                HashInt(state, score);
            }
        }
        else if (const auto* bandit = dynamic_cast<const BanditProvince*>(province); bandit != nullptr)
        {
            HashString(state, bandit->GetDefinitionId());
            HashInt(state, bandit->GetStrength());
            HashInt(state, bandit->GetRaidPressure());
            HashString(state, bandit->GetLootTableId());
            const auto& future = bandit->GetFutureBuildableParameters();
            HashString(state, future.definitionId);
            HashInt(state, future.sizeX);
            HashInt(state, future.sizeY);
            HashValue(state, future.localSeed);
            HashDouble(state, future.resourceWealth);
            HashDouble(state, future.resourceDensity);
            HashDouble(state, future.resourceFieldSize);
            HashDouble(state, future.resourceRichness);
            HashDouble(state, future.waterAmount);
            HashDouble(state, future.mountainAmount);
            HashDouble(state, future.ruggedness);
            HashInt(state, future.wealthTier);
            HashValue(state, future.traitIds.size());
            for (const auto& traitId : future.traitIds)
                HashString(state, traitId);
            HashValue(state, future.naturalResourceTypes.size());
            for (const ResourceType resource : future.naturalResourceTypes)
                HashInt(state, static_cast<int>(resource));
            HashValue(state, future.resourceDeposits.size());
            for (const auto& deposit : future.resourceDeposits)
            {
                HashInt(state, static_cast<int>(deposit.resource));
                HashDouble(state, deposit.richness);
            }
        }
        else if (const auto* event = dynamic_cast<const EventProvince*>(province); event != nullptr)
        {
            HashString(state, event->GetEventPoolId());
            HashValue(state, event->GetDiscoveryAttemptsByPlayer().size());
            for (const auto& [ownerId, attempts] : event->GetDiscoveryAttemptsByPlayer())
            {
                HashInt(state, ownerId);
                HashValue(state, attempts);
                HashInt(state, event->HasResolvedFor(ownerId) ? 1 : 0);
            }
            HashValue(state, event->GetResolvedByPlayer().size());
            for (const auto& [ownerId, resolved] : event->GetResolvedByPlayer())
            {
                HashInt(state, ownerId);
                HashInt(state, resolved ? 1 : 0);
            }
        }
        for (const auto& [playerId, player] : playerHandler.players)
        {
            HashInt(state, playerId);
            HashInt(state, static_cast<int>(province->GetKnowledge(playerId)));
        }
        for (ProvinceId neighbor : globalMap.GetNeighbors(provinceId))
            HashValue(state, neighbor);
        if (const auto* simulation = buildable != nullptr ? buildable->GetSimulation() : nullptr;
            simulation != nullptr)
            HashValue(state, simulation->GetEconomy().simulationTick);
    }
    HashValue(state, static_cast<std::uint64_t>(globalMap.GetConnectionCount()));
    for (ProvinceConnectionId connectionId : globalMap.GetConnectionIds())
    {
        const auto* connection = globalMap.FindConnection(connectionId);
        const auto* landRoute = dynamic_cast<const LandRouteConnection*>(connection);
        HashValue(state, connectionId);
        HashValue(state, connection != nullptr ? connection->GetFirstProvinceId()
                                                : InvalidProvinceId);
        HashValue(state, connection != nullptr ? connection->GetSecondProvinceId()
                                                : InvalidProvinceId);
        if (landRoute == nullptr)
        {
            HashString(state, {});
            HashInt(state, -1);
            HashInt(state, -1);
            HashValue(state, 0);
            continue;
        }
        HashString(state, landRoute->GetDefinitionId());
        HashInt(state, landRoute->GetLengthUnits());
        HashInt(state, landRoute->GetLevel());
        HashInt(state, landRoute->GetUpgradeTargetLevel());
        HashValue(state, landRoute->GetUpgradeRemainingTicks());
    }
    for (const auto& [playerId, player] : playerHandler.players)
    {
        HashInt(state, playerId);
        HashValue(state, player != nullptr ? player->homeProvinceId : InvalidProvinceId);
    }
    HashValue(state, pendingColonizations.size());
    for (const auto& [targetProvinceId, operation] : pendingColonizations)
    {
        HashValue(state, targetProvinceId);
        HashInt(state, operation.playerId);
        HashValue(state, operation.sourceProvinceId);
        HashValue(state, operation.targetProvinceId);
        HashValue(state, operation.journeyId);
        HashInt(state, static_cast<int>(operation.phase));
        HashValue(state, operation.phaseCompletionTick);
        HashValue(state, operation.settlementDurationTicks);
        HashValue(state, operation.cost.size());
        for (const auto& cost : operation.cost)
        {
            HashInt(state, static_cast<int>(cost.type));
            HashInt(state, cost.amount);
        }
    }
    HashValue(state, armyJourneySystem.GetNextJourneyId());
    HashValue(state, armyJourneySystem.GetJourneys().size());
    for (const auto& [journeyId, journey] : armyJourneySystem.GetJourneys())
    {
        HashValue(state, journeyId);
        HashInt(state, journey.ownerId);
        HashValue(state, journey.sourceProvinceId);
        HashValue(state, journey.targetProvinceId);
        HashInt(state, static_cast<int>(journey.status));
        HashInt(state, static_cast<int>(journey.kind));
        HashValue(state, journey.currentLeg);
        HashValue(state, journey.startTick);
        HashValue(state, journey.legCompletionTick);
        HashValue(state, journey.deterministicAttemptCounter);
        HashValue(state, journey.rules.baseLegDurationTicks);
        HashDouble(state, journey.rules.routeTravelSpeedMultiplier);
        HashInt(state, journey.rules.speedProfile.baseUnitsPerMinute);
        HashInt(state, journey.rules.speedProfile.moverSpeedBasisPoints);
        HashInt(state, journey.rules.speedProfile.playerRouteSpeedBasisPoints);
        HashInt(state, journey.rules.speedProfile.operationSpeedBasisPoints);
        HashInt(state, journey.rules.speedProfile.extraLegDistanceBasisPoints);
        HashValue(state, journey.legPlan.size());
        for (const auto& leg : journey.legPlan)
        {
            HashValue(state, leg.connectionId);
            HashInt(state, leg.lengthUnits);
            HashInt(state, leg.routeTimeBasisPoints);
            HashInt(state, leg.routeLevelAtStart);
            HashInt(state, leg.incidentReductionBasisPoints);
            HashValue(state, leg.durationTicks);
        }
        HashValue(state, journey.loadout.resources.size());
        for (const auto& resource : journey.loadout.resources)
        {
            HashInt(state, static_cast<int>(resource.type));
            HashInt(state, resource.amount);
        }
        HashValue(state, journey.loadout.minimumResources.size());
        for (const auto& resource : journey.loadout.minimumResources)
        {
            HashInt(state, static_cast<int>(resource.type));
            HashInt(state, resource.amount);
        }
        HashInt(state, journey.loadout.supplyRatioBasisPoints);
        HashValue(state, journey.loadout.modifiers.size());
        for (const auto& modifier : journey.loadout.modifiers)
        {
            HashInt(state, static_cast<int>(modifier.stat));
            HashInt(state, modifier.multiplierBasisPoints);
            HashString(state, modifier.source);
        }
        std::visit([&](const auto& payload)
        {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, ScoutParty> || std::is_same_v<Payload, ArmyParty>)
            {
                HashInt(state, std::is_same_v<Payload, ScoutParty> ? 0 : 2);
                HashValue(state, payload.unitInstanceIds.size());
                for (const int id : payload.unitInstanceIds) HashInt(state, id);
            }
            else if constexpr (std::is_same_v<Payload, TradeCargo>)
            {
                HashInt(state, 1);
                HashInt(state, static_cast<int>(payload.offerType));
                HashInt(state, static_cast<int>(payload.requestType));
                HashInt(state, payload.amount);
            }
            else if constexpr (std::is_same_v<Payload, ResourceConvoy>)
            {
                HashInt(state, 4);
                HashValue(state, payload.cargo.size());
                for (const auto& cargo : payload.cargo)
                {
                    HashInt(state, static_cast<int>(cargo.type));
                    HashInt(state, cargo.amount);
                }
            }
            else if constexpr (std::is_same_v<Payload, ArmyTransferParty>)
            {
                HashInt(state, 5);
                HashInt(state, payload.destinationBarracksBuildingId);
                HashValue(state, payload.unitInstanceIds.size());
                for (const int id : payload.unitInstanceIds)
                    HashInt(state, id);
            }
            else
            {
                HashInt(state, 3);
                HashInt(state, payload.householdCount);
            }
        }, journey.payload);
    }

    HashValue(state, nextTradeOrderId);
    HashValue(state, activeTradeOrders.size());
    for (const auto& [orderId, order] : activeTradeOrders)
    {
        HashValue(state, orderId);
        HashInt(state, order.playerId);
        HashValue(state, order.originProvinceId);
        HashValue(state, order.cityProvinceId);
        HashInt(state, static_cast<int>(order.request.offerType));
        HashInt(state, static_cast<int>(order.request.requestType));
        HashInt(state, order.request.requestedAmount);
        HashInt(state, order.request.offeredAmount);
        HashInt(state, static_cast<int>(order.request.mode));
        HashInt(state, order.offeredAmount);
        HashInt(state, order.cargoAmount);
        HashValue(state, order.cityRevisionAtStart);
        HashValue(state, order.journeyId);
        HashInt(state, static_cast<int>(order.status));
    }

    HashValue(state, battleSystem.GetNextBattleId());
    HashValue(state, battleSystem.GetBattles().size());
    for (const auto& [battleId, battle] : battleSystem.GetBattles())
    {
        HashValue(state, battleId);
        HashInt(state, battle.attackerId);
        HashInt(state, battle.defenderId);
        HashValue(state, battle.sourceProvinceId);
        HashValue(state, battle.targetProvinceId);
        HashValue(state, battle.journeyId);
        HashInt(state, static_cast<int>(battle.status));
        HashValue(state, battle.startTick);
        HashValue(state, battle.endTick);
        HashInt(state, battle.isRaid ? 1 : 0);
        HashInt(state, battle.raidStrength);
        HashDouble(state, battle.rules.baseLossFraction);
        HashDouble(state, battle.rules.casualtyCapFraction);
        HashDouble(state, battle.rules.drawBand);
        HashDouble(state, battle.rules.crushingRatio);
        HashDouble(state, battle.rules.lootFraction);
        HashValue(state, battle.rules.durationTicks);
        HashValue(state, battle.attackerUnitIds.size());
        for (const int id : battle.attackerUnitIds) HashInt(state, id);
        HashValue(state, battle.defenderUnitIds.size());
        for (const int id : battle.defenderUnitIds) HashInt(state, id);
        HashBattleSide(state, battle.attackerSnapshot);
        HashBattleSide(state, battle.defenderSnapshot);
        HashInt(state, battle.outcome.has_value() ? 1 : 0);
        if (battle.outcome.has_value()) HashBattleOutcome(state, *battle.outcome);
    }
    HashValue(state, battleSystem.GetReports().size());
    for (const auto& report : battleSystem.GetReports())
    {
        HashValue(state, report.battleId);
        HashInt(state, report.attackerId);
        HashInt(state, report.defenderId);
        HashValue(state, report.sourceProvinceId);
        HashValue(state, report.targetProvinceId);
        HashBattleOutcome(state, report.outcome);
        HashInt(state, report.banditTransformed ? 1 : 0);
        HashInt(state, report.cityDamaged ? 1 : 0);
        HashInt(state, report.raid ? 1 : 0);
        HashValue(state, report.destroyedBuildingIds.size());
        for (const int id : report.destroyedBuildingIds) HashInt(state, id);
        HashValue(state, report.lostResources.size());
        for (const auto& [type, amount] : report.lostResources)
        {
            HashInt(state, static_cast<int>(type));
            HashInt(state, amount);
        }
    }

    HashValue(state, eventSystem.GetNextInstanceId());
    const auto& schedulerStates = eventSystem.GetPeriodicScheduler().GetStates();
    HashValue(state, schedulerStates.size());
    for (const auto& [playerId, schedulerState] : schedulerStates)
    {
        HashInt(state, playerId);
        HashValue(state, schedulerState.nextCheckTick);
        HashValue(state, schedulerState.nextAllowedEventTick);
        HashValue(state, schedulerState.attemptCounter);
        HashValue(state, schedulerState.definitionCooldownUntil.size());
        for (const auto& [definitionId, cooldownUntil] : schedulerState.definitionCooldownUntil)
        {
            HashString(state, definitionId);
            HashValue(state, cooldownUntil);
        }
    }
    HashValue(state, eventSystem.GetInstances().size());
    for (const auto& [instanceId, instance] : eventSystem.GetInstances())
    {
        HashValue(state, instanceId);
        HashInt(state, instance.ownerId);
        HashValue(state, instance.provinceId);
        HashValue(state, instance.secondaryProvinceId);
        HashString(state, instance.definitionId);
        HashInt(state, static_cast<int>(instance.trigger));
        HashValue(state, instance.startTick);
        HashValue(state, instance.endTick);
        HashValue(state, instance.outcomeRoll);
        HashValue(state, instance.deterministicAttemptCounter);
        HashValue(state, instance.journeyId);
        HashInt(state, instance.expired ? 1 : 0);
        HashInt(state, instance.raidStarted ? 1 : 0);
        HashInt(state, instance.journeyEffectApplied ? 1 : 0);
        HashValue(state, instance.appliedEffects.size());
        for (const auto& effect : instance.appliedEffects)
            HashAppliedWorldEventEffect(state, effect);
    }
    HashValue(state, eventSystem.GetFeed().GetHistory().size());
    for (const auto& notification : eventSystem.GetFeed().GetHistory())
    {
        HashValue(state, notification.instanceId);
        HashInt(state, notification.ownerId);
        HashValue(state, notification.provinceId);
        HashValue(state, notification.secondaryProvinceId);
        HashString(state, notification.definitionId);
        HashString(state, notification.title);
        HashString(state, notification.description);
        HashInt(state, static_cast<int>(notification.trigger));
        HashValue(state, notification.startTick);
        HashValue(state, notification.endTick);
        HashValue(state, notification.outcomeRoll);
        HashInt(state, notification.expired ? 1 : 0);
        HashValue(state, notification.appliedEffects.size());
        for (const auto& effect : notification.appliedEffects)
            HashAppliedWorldEventEffect(state, effect);
    }

    for (const auto& [playerId, player] : playerHandler.players)
    {
        HashInt(hash, playerId);
        if (player == nullptr)
        {
            HashValue(hash, 0);
            continue;
        }

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

        std::vector<std::pair<ProvinceId, const ProvinceEconomy*>> provinceEconomies;
        for (const ProvinceId provinceId : globalMap.GetProvinceIds())
        {
            const auto* province = globalMap.FindBuildableProvince(provinceId);
            if (province == nullptr || province->GetOwnerId() != playerId ||
                province->GetSimulation() == nullptr)
                continue;
            const ProvinceEconomy* economy = player->GetProvinceEconomy(provinceId);
            if (economy == nullptr)
                continue;
            provinceEconomies.emplace_back(provinceId, economy);
        }
        if (provinceEconomies.empty())
        {
            HashValue(hash, 0);
            continue;
        }
        HashValue(hash, static_cast<std::uint64_t>(provinceEconomies.size()));
        for (const auto& [provinceId, economy] : provinceEconomies)
        {
            HashValue(hash, provinceId);
            HashInt(hash, economy->population.initialized ? 1 : 0);
            HashDouble(hash, economy->population.availableManpower);
            HashInt(hash, economy->dataTracker.CountBuildings(BuildingType::Headquarters));
            HashValue(hash, static_cast<std::uint64_t>(economy->dataTracker.buildings.size()));

        // dataTracker.buildings is a std::set<Building*> ordered by raw pointer
        // value, which differs between independently-allocated host/client
        // processes. Sort by the stable, assigned building id before hashing so
        // the checksum doesn't depend on heap layout.
        std::vector<const Building*> orderedBuildings(economy->dataTracker.buildings.begin(),
                                                       economy->dataTracker.buildings.end());
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
            HashInt(hash, building->ownerId != InvalidPlayerId
                ? building->ownerId : (building->owner != nullptr ? building->owner->id : -1));
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
                HashDouble(hash, population->assignedResidents);
                HashInt(hash, population->hasAssignedResidents ? 1 : 0);
                HashValue(hash, population->supplyDebt.size());
                for (const auto& [resource, debt] : population->supplyDebt)
                {
                    HashInt(hash, static_cast<int>(resource));
                    HashDouble(hash, debt);
                }
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
            if (const auto* coverage = building->GetComponent<DefenseCoverageComponent>(); coverage != nullptr)
            {
                HashDouble(hash, coverage->radius.GetBase());
                HashDouble(hash, coverage->baseProtection.GetBase());
                HashString(hash, coverage->requiredState);
            }
            if (const auto* garrison = building->GetComponent<GarrisonComponent>(); garrison != nullptr)
                HashInt(hash, garrison->capacity.GetBase());
            if (const auto* upkeep = building->GetComponent<GarrisonUpkeepComponent>(); upkeep != nullptr)
            {
                HashDouble(hash, upkeep->timer);
                HashValue(hash, static_cast<std::uint64_t>(upkeep->debtMicros));
                HashDouble(hash, upkeep->intervalSeconds);
                HashDouble(hash, upkeep->packageSize);
                HashInt(hash, upkeep->requestedAmount);
                HashInt(hash, static_cast<int>(upkeep->supplyStatus));
            }
            if (const auto* safety = building->GetComponent<SafetyComponent>(); safety != nullptr)
            {
                HashDouble(hash, safety->intrinsicResilience);
                HashInt(hash, safety->raidDestructible ? 1 : 0);
                HashInt(hash, safety->raidStockLossTarget ? 1 : 0);
            }

        }

            std::vector<ResourceShipment> shipments;
            if (economy->roadNetwork != nullptr)
                economy->roadNetwork->AppendShipmentRecords(shipments);
            HashValue(hash, economy->roadNetwork != nullptr
                ? economy->roadNetwork->GetNextShipmentId() : 0);
            HashValue(hash, shipments.size());
            for (const auto& shipment : shipments)
            {
                HashValue(hash, shipment.id);
                HashInt(hash, static_cast<int>(shipment.type));
                HashInt(hash, shipment.quantity);
                HashInt(hash, shipment.sourceBuildingId);
                HashInt(hash, shipment.targetBuildingId);
                HashInt(hash, shipment.currentPathStep);
                HashDouble(hash, shipment.elapsedTime);
                HashDouble(hash, shipment.transportTime);
                HashInt(hash, static_cast<int>(shipment.state));
                HashValue(hash, shipment.pathTileIds.size());
                for (int tileId : shipment.pathTileIds)
                    HashInt(hash, tileId);
            }

            for (const auto& [commandType, count] : economy->dataTracker.processedCommands)
            {
                HashInt(hash, static_cast<int>(commandType));
                HashInt(hash, count);
            }
        }

        // Recruited roster. std::map<int, BattleUnit>
        // keyed by instanceId is already deterministically ordered.
        HashValue(hash, static_cast<std::uint64_t>(player->roster.units.size()));
        for (const auto& [instanceId, unit] : player->roster.units)
        {
            HashInt(hash, unit.instanceId);
            HashInt(hash, unit.ownerPlayerId);
            HashString(hash, unit.unitDefId);
            HashValue(hash, unit.taskGroupId);
            HashValue(hash, static_cast<int>(unit.assignment.kind));
            HashValue(hash, unit.assignment.provinceId);
            HashValue(hash, unit.assignment.buildingId);
            HashValue(hash, unit.assignment.worldJourneyId);
            HashValue(hash, unit.assignment.battleId);
        }
        HashValue(hash, player->taskGroups.GetNextTaskGroupId());
        HashValue(hash, player->taskGroups.GetGroups().size());
        for (const auto& [groupId, group] : player->taskGroups.GetGroups())
        {
            HashValue(hash, groupId);
            HashValue(hash, group.stationProvinceId);
            HashValue(hash, group.homeBarracksBuildingId);
        }
    }

    return state.Finish();
}
