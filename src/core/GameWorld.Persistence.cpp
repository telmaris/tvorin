#include "core/GameWorldInternal.h"
#include "core/PersistenceLimits.h"
#include "platform/AtomicFile.h"
#include "warfare/UnitDefinition.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
#include <set>
#include <tuple>

using namespace GameWorldInternal;

namespace
{
    struct PendingProvinceShipments
    {
        ProvinceId provinceId{InvalidProvinceId};
        ShipmentId nextShipmentId{1};
        std::vector<ResourceShipment> shipments;
    };

    bool IsValidMapSizePresetValue(int value) noexcept
    {
        return value >= static_cast<int>(MapSizePreset::S) &&
               value <= static_cast<int>(MapSizePreset::XL);
    }

}

bool GameWorld::SaveToFile(const std::string& path) const
{
    namespace fs = std::filesystem;
    const fs::path destination(path);
    std::ostringstream serialized;
    if (!SaveToStream(serialized) || !serialized.good())
        return false;
    std::string payload = std::move(serialized).str();
    if (payload.empty() || payload.size() > PersistenceLimits::MaxSerializedStateBytes)
        return false;

    std::error_code ignored;
    if (fs::exists(destination, ignored))
    {
        for (std::size_t index = PersistenceLimits::SaveBackupCount; index > 1; --index)
        {
            const fs::path older = destination.string() + ".bak." + std::to_string(index);
            const fs::path newer = destination.string() + ".bak." + std::to_string(index - 1);
            ignored.clear();
            fs::remove(older, ignored);
            ignored.clear();
            fs::rename(newer, older, ignored);
        }
        ignored.clear();
        fs::copy_file(destination, destination.string() + ".bak.1",
                      fs::copy_options::overwrite_existing, ignored);
    }

    std::string error;
    return AtomicFileTransaction::Write(destination,
        [&](std::ostream& out)
        {
            out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            return out.good();
        }, &error);
}

std::string GameWorld::SerializeSimulationState() const
{
    std::ostringstream out;
    if (!SaveToStream(out))
        return {};
    std::string payload = std::move(out).str();
    if (payload.size() > PersistenceLimits::MaxSerializedStateBytes)
        return {};
    return payload;
}

bool GameWorld::SaveToStream(std::ostream& out) const
{

    // Save v51: WorldJourney is the only expedition runtime state.
    // Save v50: every in-flight local-road shipment, including its exact
    // current leg time, is part of authoritative persistence and resync.
    // Save v48: immutable journey route-speed rules are persisted.
    // Save v47: authoritative trade orders join city stock and trade journeys.
    // Save v46: immutable battle rules are stored with each battle instance.
    // Save v45: journeys, events, battles and defense upkeep correction state.
    // Save v43: full campaign-generation parameters and pending colonization.
    // Save v41: per-province command telemetry for multi-map checksum parity.
    // Save v40: full connection IDs, route levels and upgrade state.
    // Save v39: per-province maps and roster location/expedition assignments.
    // Save v38: campaign graph, province discovery and expedition state.
    // Save v37: peaceful roster-only state; combat and military-road runtime
    // data were removed from the format.
    // Save v35: exact construction payment records for deterministic salvage.
    // Save v34: private Barracks buffers separated from StorageComponent.
    // Save v33: independent household/urban Village upkeep timers.
    // Save v32: runtime identifiers and simulation tick for multiplayer restore.
    // Save v31: transparent per-tile resource-overlay cells.
    // Save v30: settlement tiers and their household/urban supply buffers.
    // Save v28: added the UPG block (UpgradeComponent — generic per-instance
    // building upgrade progression, introduced for Road).
    // Snapshot recovery must preserve values tightly enough to reproduce the
    // deterministic checksum; the default stream precision silently rounds
    // timers and resource rates after a few ticks.
    const auto primaryPlayerIt = playerHandler.players.find(0);
    const auto* primaryHome = primaryPlayerIt == playerHandler.players.end() ||
                              primaryPlayerIt->second == nullptr
        ? nullptr : globalMap.FindBuildableProvince(primaryPlayerIt->second->homeProvinceId);
    const TileMap* primaryMapPtr = primaryHome != nullptr && primaryHome->GetSimulation() != nullptr
        ? &primaryHome->GetSimulation()->GetTileMap() : nullptr;
    if (primaryMapPtr == nullptr)
        return false;
    const TileMap& primaryMap = *primaryMapPtr;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "RTS_SAVE " << SerializationVersion::GameWorldSaveVersion << '\n';
    out << "WORLD " << std::quoted(worldName) << '\n';
    out << "RUNTIME " << localPlayerId << ' ' << simulationTick << ' ' << nextCommandId << '\n';
    out << "PARAMS " << primaryMap.params.sizeX << ' ' << primaryMap.params.sizeY << ' '
        << primaryMap.params.seed << ' ' << static_cast<int>(primaryMap.params.sizePreset) << ' '
        << primaryMap.params.resourceDensity << ' ' << primaryMap.params.resourceFieldSize << ' '
        << primaryMap.params.resourceRichness << ' ' << primaryMap.params.aiOpponentCount << ' '
        << primaryMap.params.aiDifficulty << ' ' << primaryMap.params.debugMode << '\n';

    std::vector<std::tuple<PlayerId, ProvinceId, ProvinceKnowledgeLevel>> knowledgeEntries;
    for (const auto& [playerId, player] : playerHandler.players)
    {
        (void)player;
        for (ProvinceId provinceId : globalMap.GetProvinceIds())
        {
            const auto* province = globalMap.FindProvince(provinceId);
            if (province != nullptr && province->GetKnowledge(playerId) != ProvinceKnowledgeLevel::Hidden)
                knowledgeEntries.emplace_back(static_cast<PlayerId>(playerId), provinceId,
                                              province->GetKnowledge(playerId));
        }
    }
    out << "CAMPAIGN\nGLOBAL_MAP " << globalMap.GetProvinceCount() << ' '
        << globalMap.GetEdgeCount() << ' ' << globalMap.GetGenerationSeed() << '\n';
    const auto& campaignMap = campaignGenerationParameters.globalMap;
    out << "CAMPAIGN_PARAMETERS " << campaignMap.seed << ' '
        << campaignMap.provinceCount << ' ' << campaignMap.extraEdgeCount << ' '
        << campaignMap.layoutRadius << ' ' << campaignMap.minimumLayoutSpacing << ' '
        << campaignMap.maximumPlacementAttemptsPerProvince << ' '
        << campaignMap.minimumNeutralBuildables << ' ' << campaignMap.buildableWeight << ' '
        << campaignMap.neutralCityWeight << ' ' << campaignMap.banditCampWeight << ' '
        << campaignMap.eventSiteWeight << ' ' << campaignMap.startBoundaryClearance << ' '
        << campaignMap.buildableWealthScale << ' ' << campaignMap.cityWealthScale << ' '
        << campaignMap.banditStrengthScale << ' ' << (campaignMap.fogOfWarEnabled ? 1 : 0) << '\n';
    out << "PROVINCES " << globalMap.GetProvinceCount() << '\n';
    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindProvince(provinceId);
        if (province == nullptr)
            return false;
        const auto* buildable = globalMap.FindBuildableProvince(provinceId);
        out << "PROVINCE " << provinceId << ' ' << static_cast<int>(province->GetKind()) << ' '
            << (buildable != nullptr ? buildable->GetOwnerId() : InvalidPlayerId) << ' '
            << province->GetLayoutPosition().x << ' '
            << province->GetLayoutPosition().y << '\n';
        if (buildable != nullptr)
        {
            const auto& value = buildable->GetParameters();
            out << "BUILDABLE_DATA " << provinceId << ' ' << std::quoted(value.definitionId) << ' '
                << value.sizeX << ' ' << value.sizeY << ' ' << value.localSeed << ' '
                << value.resourceWealth << ' ' << value.resourceDensity << ' '
                << value.resourceFieldSize << ' ' << value.resourceRichness << ' '
                << value.waterAmount << ' ' << value.mountainAmount << ' ' << value.ruggedness << ' '
                << value.wealthTier << ' ' << value.traitIds.size();
            for (const auto& traitId : value.traitIds)
                out << ' ' << std::quoted(traitId);
            out << ' ' << value.naturalResourceTypes.size();
            for (const ResourceType resource : value.naturalResourceTypes)
                out << ' ' << static_cast<int>(resource);
            out << ' ' << value.resourceDeposits.size();
            for (const auto& deposit : value.resourceDeposits)
                out << ' ' << static_cast<int>(deposit.resource) << ' ' << deposit.richness;
            out << '\n';
        }
        else if (const auto* city = dynamic_cast<const NeutralCityProvince*>(province))
        {
            const auto& state = city->GetState();
            out << "CITY_DATA " << provinceId << ' ' << std::quoted(city->GetDefinitionId()) << ' '
                << city->GetWealthTier() << ' ' << state.barterPenaltyMultiplier << ' '
                << state.revision << ' ' << state.stock.size();
            for (const auto& [type, amount] : state.stock)
                out << ' ' << static_cast<int>(type) << ' ' << amount;
            out << ' ' << state.buyPrices.size();
            for (const auto& [type, price] : state.buyPrices)
                out << ' ' << static_cast<int>(type) << ' ' << price;
            out << ' ' << state.sellPrices.size();
            for (const auto& [type, price] : state.sellPrices)
                out << ' ' << static_cast<int>(type) << ' ' << price;
            out << ' ' << state.tradeScore.size();
            for (const auto& [playerId, score] : state.tradeScore)
                out << ' ' << playerId << ' ' << score;
            out << '\n';
        }
        else if (const auto* bandit = dynamic_cast<const BanditProvince*>(province))
        {
            const auto& future = bandit->GetFutureBuildableParameters();
            out << "BANDIT_DATA " << provinceId << ' ' << std::quoted(bandit->GetDefinitionId()) << ' '
                << bandit->GetStrength() << ' ' << bandit->GetRaidPressure() << ' '
                << std::quoted(bandit->GetLootTableId()) << ' '
                << std::quoted(future.definitionId) << ' ' << future.sizeX << ' ' << future.sizeY << ' '
                << future.localSeed << ' ' << future.resourceWealth << ' ' << future.resourceDensity << ' '
                << future.resourceFieldSize << ' ' << future.resourceRichness << ' '
                << future.waterAmount << ' ' << future.mountainAmount << ' ' << future.ruggedness << ' '
                << future.wealthTier << ' ' << future.traitIds.size();
            for (const auto& traitId : future.traitIds)
                out << ' ' << std::quoted(traitId);
            out << ' ' << future.naturalResourceTypes.size();
            for (const ResourceType resource : future.naturalResourceTypes)
                out << ' ' << static_cast<int>(resource);
            out << ' ' << future.resourceDeposits.size();
            for (const auto& deposit : future.resourceDeposits)
                out << ' ' << static_cast<int>(deposit.resource) << ' ' << deposit.richness;
            out << '\n';
        }
        else if (const auto* event = dynamic_cast<const EventProvince*>(province))
        {
            std::set<PlayerId> eventPlayers;
            for (const auto& [playerId, attempts] : event->GetDiscoveryAttemptsByPlayer())
                eventPlayers.insert(playerId);
            for (const auto& [playerId, resolved] : event->GetResolvedByPlayer())
                eventPlayers.insert(playerId);
            out << "EVENT_DATA " << provinceId << ' ' << std::quoted(event->GetEventPoolId()) << ' '
                << eventPlayers.size() << '\n';
            for (PlayerId playerId : eventPlayers)
                out << "EVENT_PLAYER " << playerId << ' ' << event->GetDiscoveryAttempts(playerId)
                    << ' ' << (event->HasResolvedFor(playerId) ? 1 : 0) << '\n';
        }
        const auto* simulation = buildable != nullptr ? buildable->GetSimulation() : nullptr;
        out << "PROVINCE_STATE " << provinceId << ' ' << (simulation != nullptr ? 1 : 0) << ' '
            << (simulation != nullptr ? simulation->GetEconomy().simulationTick : 0) << '\n';
        if (simulation != nullptr)
        {
            const auto& population = simulation->GetEconomy().population;
            out << "PROVINCE_POPULATION " << provinceId << ' '
                << (population.initialized ? 1 : 0) << ' '
                << population.availableManpower << '\n';
        }
        else
            out << "PROVINCE_POPULATION " << provinceId << " 0 0\n";
    }
    out << "ENDPROVINCES\nCONNECTIONS " << globalMap.GetConnectionCount() << '\n';
    for (ProvinceConnectionId connectionId : globalMap.GetConnectionIds())
    {
        const auto* connection = globalMap.FindConnection(connectionId);
        const auto* landRoute = dynamic_cast<const LandRouteConnection*>(connection);
        if (landRoute == nullptr)
            return false;
        out << "CONNECTION " << connectionId << ' '
            << landRoute->GetFirstProvinceId() << ' '
            << landRoute->GetSecondProvinceId() << ' '
            << std::quoted(landRoute->GetDefinitionId()) << ' '
            << landRoute->GetLengthUnits() << ' '
            << landRoute->GetLevel() << ' '
            << landRoute->GetUpgradeTargetLevel() << ' '
            << landRoute->GetUpgradeRemainingTicks() << '\n';
    }
    out << "KNOWLEDGE " << knowledgeEntries.size() << '\n';
    for (const auto& [playerId, provinceId, level] : knowledgeEntries)
        out << "KNOW " << playerId << ' ' << provinceId << ' ' << static_cast<int>(level) << '\n';
    out << "ENDCAMPAIGN\n";
    out << "COLONIZATION " << pendingColonizations.size() << '\n';
    for (const auto& [targetProvinceId, operation] : pendingColonizations)
    {
        (void)targetProvinceId;
        out << "COLONIZATION_OP " << operation.playerId << ' '
            << operation.sourceProvinceId << ' ' << operation.targetProvinceId << ' '
            << operation.journeyId << ' ' << static_cast<int>(operation.phase) << ' '
            << operation.phaseCompletionTick << ' ' << operation.settlementDurationTicks << ' '
            << operation.cost.size() << '\n';
        for (const auto& cost : operation.cost)
            out << "COLONIZATION_COST " << static_cast<int>(cost.type) << ' '
                << cost.amount << '\n';
    }
    if (render != nullptr)
    {
        out << "CAMERA " << render->camera.target.x << ' ' << render->camera.target.y << ' '
            << render->camera.zoom << ' ' << render->camera.rotation << '\n';
    }
    else
    {
        out << "CAMERA 0 0 1.25 0\n";
    }

    out << "PLAYERS " << playerHandler.players.size() << '\n';
    for (const auto& [id, player] : playerHandler.players)
    {
        const auto* home = player != nullptr
            ? globalMap.FindBuildableProvince(player->homeProvinceId) : nullptr;
        const ProvinceEconomy* economy = home != nullptr && home->GetSimulation() != nullptr
            ? &home->GetSimulation()->GetEconomy() : nullptr;
        if (player == nullptr || economy == nullptr)
            return false;
        out << "PLAYER " << id << ' ' << player->homeProvinceId << ' '
            << player->strategicResources.values.size() << ' '
            << player->technologies.GetUnlocked().size() << ' '
            << player->focuses.GetUnlocked().size() << ' '
            << static_cast<int>(player->controllerType) << ' ' << std::quoted(player->name) << ' '
            << static_cast<int>(player->color.r) << ' ' << static_cast<int>(player->color.g) << ' '
            << static_cast<int>(player->color.b) << ' ' << static_cast<int>(player->color.a) << '\n';
        for (const auto& [type, value] : player->strategicResources.values)
            out << "STRAT " << static_cast<int>(type) << ' ' << value << '\n';
        for (const auto& techId : player->technologies.GetUnlocked())
            out << "TECH " << std::quoted(techId) << '\n';
        for (const auto& focusId : player->focuses.GetUnlocked())
            out << "FOCUS " << std::quoted(focusId) << '\n';
        out << "ACTIVE_FOCUS " << std::quoted(player->focuses.GetActiveFocusId()) << ' '
            << player->focuses.GetActiveFocusRemaining() << '\n';

        // Recruited roster. Location and expedition assignment are stable IDs;
        // no local-map pointers are serialized.
        out << "ROSTER " << player->nextUnitInstanceId << ' ' << player->roster.units.size() << '\n';
        for (const auto& [instanceId, unit] : player->roster.units)
        {
            const UnitAssignment& assignment = unit.assignment;
            if (!assignment.IsStructurallyValid())
                return false;
            out << "UNIT " << unit.instanceId << ' ' << unit.ownerPlayerId << ' '
                << std::quoted(unit.unitDefId) << ' ' << static_cast<int>(assignment.kind) << ' '
                << assignment.provinceId << ' ' << assignment.buildingId << ' '
                << assignment.worldJourneyId << ' ' << assignment.battleId << ' '
                << unit.taskGroupId << '\n';
        }

        out << "TASK_GROUPS " << player->taskGroups.GetNextTaskGroupId() << ' '
            << player->taskGroups.GetGroups().size() << '\n';
        for (const auto& [groupId, group] : player->taskGroups.GetGroups())
            out << "TASK_GROUP " << groupId << ' ' << group.stationProvinceId << ' '
                << group.homeBarracksBuildingId << '\n';

        out << "COMMANDSTATS " << economy->dataTracker.processedCommands.size() << '\n';
        for (const auto& [commandType, count] : economy->dataTracker.processedCommands)
            out << "CMDSTAT " << static_cast<int>(commandType) << ' ' << count << '\n';
        std::vector<std::pair<ProvinceId, const ProvinceEconomy*>> provinceEconomies;
        for (const ProvinceId provinceId : globalMap.GetProvinceIds())
        {
            const auto* province = globalMap.FindBuildableProvince(provinceId);
            if (province == nullptr || province->GetOwnerId() != id ||
                province->GetSimulation() == nullptr)
                continue;
            const auto* provinceEconomy = player->GetProvinceEconomy(provinceId);
            if (provinceEconomy == nullptr)
                return false;
            provinceEconomies.emplace_back(provinceId, provinceEconomy);
        }
        if (provinceEconomies.size() > PersistenceLimits::MaxProvinceMaps)
            return false;
        out << "PROVINCE_COMMANDSTATS " << provinceEconomies.size() << '\n';
        for (const auto& [provinceId, provinceEconomy] : provinceEconomies)
        {
            out << "PROVINCE_STATS " << provinceId << ' '
                << provinceEconomy->dataTracker.processedCommands.size() << '\n';
            for (const auto& [commandType, count] : provinceEconomy->dataTracker.processedCommands)
                out << "PCMDSTAT " << static_cast<int>(commandType) << ' ' << count << '\n';
        }
        out << "ENDPLAYER\n";
    }

    const auto writeMapState = [&out](const TileMap& sourceMap) -> bool
    {
        const TileMap& tilemap = sourceMap;
        out << "TILES " << tilemap.tilemap.size() << '\n';
    for (const auto& tile : tilemap.tilemap)
    {
        out << "T " << tile.id << ' ' << static_cast<int>(tile.tileType) << ' '
            << tile.terrainTextureId << ' ' << tile.resourceOverlayTextureId << ' '
            << tile.resourceRichness << ' '
            << static_cast<int>(tile.biome) << '\n';
    }

    int buildingCount = 0;
    for (const auto& tile : tilemap.tilemap)
    {
        if (tile.building != nullptr)
            buildingCount++;
    }

    out << "BUILDINGS " << buildingCount << '\n';
    for (const auto& tile : tilemap.tilemap)
    {
        const auto* building = tile.building.get();
        if (building == nullptr)
            continue;

        int ownerId = building->ownerId != InvalidPlayerId
            ? building->ownerId
            : (building->owner != nullptr ? building->owner->id : -1);
        out << "B " << building->positionId << ' ' << static_cast<int>(building->buildingType) << ' '
            << building->id << ' ' << ownerId << ' ' << building->textureId << ' '
            << building->footprint.x << ' ' << building->footprint.y << ' '
            << building->productionBlocked << ' ' << building->lifetime << ' '
            << building->activeTime << ' ' << building->totalProduced << ' '
            << building->transportTime.GetBase() << ' '
            << static_cast<int>(building->buildCostRecordState) << ' '
            << building->paidBuildCosts.size() << '\n';
        for (const auto& cost : building->paidBuildCosts)
            out << "PAID " << static_cast<int>(cost.type) << ' ' << cost.amount << '\n';
        out << "CONSTRUCTION " << building->buildTime.GetBase() << ' ' << building->constructionRemaining << '\n';

        if (const auto* prod = building->GetComponent<ProductionComponent>())
        {
            const auto* workers = building->GetComponent<WorkerComponent>();
            const auto* recipes = building->GetComponent<RecipeComponent>();
            // Only University actually has a ResearchComponent alongside
            // ProductionComponent — every other production building legitimately
            // has none. Write an empty placeholder rather than requiring one
            // (a pre-existing bug: this whole PROD block used to bail out with
            // `return false` for every non-University production building,
            // silently breaking SaveToFile for any game with e.g. a Woodcutter).
            const auto* research = building->GetComponent<ResearchComponent>();
            const auto* logistics = building->GetComponent<LogisticsComponent>();
            if (workers == nullptr || recipes == nullptr || logistics == nullptr)
                return false;

            out << "PROD " << static_cast<int>(prod->terrainType) << ' '
                << prod->cycleTime.GetBase() << ' ' << prod->elapsed << ' '
                << prod->started << ' ' << prod->totalProduced << '\n';
            out << "WORKERS " << workers->capacity.GetBase() << ' ' << workers->assigned << '\n';
            out << "RECIPE " << recipes->activeRecipeIndex << '\n';
            out << "RESEARCH " << std::quoted(research != nullptr ? research->technologyId : std::string{}) << ' '
                << (research != nullptr ? research->remaining : 0.0) << ' '
                << (research != nullptr ? research->total : 0.0) << '\n';

            out << "INGREDIENTS " << prod->ingredients.size() << '\n';
            for (const auto& [type, amount] : prod->ingredients)
                out << "ING " << static_cast<int>(type) << ' ' << amount << '\n';

            out << "PRODUCTS " << prod->products.size() << '\n';
            for (const auto& [type, amount] : prod->products)
                out << "PRODUCT " << static_cast<int>(type) << ' ' << amount << '\n';

            out << "INPUTS " << prod->inputBuffers.size() << '\n';
            for (const auto& [type, buffer] : prod->inputBuffers)
                SaveResourceBuffer(out, "INPUT", buffer);

            out << "OUTPUTS " << prod->outputBuffers.size() << '\n';
            for (const auto& [type, buffer] : prod->outputBuffers)
                SaveResourceBuffer(out, "OUTPUT", buffer);

            int supplierCount = 0;
            for (const auto& [type, suppliers] : logistics->suppliers)
                for (const auto* supplier : suppliers)
                    if (supplier != nullptr) supplierCount++;

            out << "SUPPLIERS " << supplierCount << '\n';
            for (const auto& [type, suppliers] : logistics->suppliers)
                for (const auto* supplier : suppliers)
                    if (supplier != nullptr)
                        out << "SUP " << static_cast<int>(type) << ' ' << supplier->positionId << '\n';

            out << "RECEIVERS " << logistics->receivers.size() << '\n';
            for (const auto& [type, receiver] : logistics->receivers)
                out << "REC " << static_cast<int>(type) << ' '
                    << (receiver != nullptr ? receiver->positionId : -1) << '\n';
            out << "ALT_RECEIVERS " << logistics->altReceivers.size() << '\n';
            for (const auto& [type, receiver] : logistics->altReceivers)
                out << "ALTREC " << static_cast<int>(type) << ' '
                    << (receiver != nullptr ? receiver->positionId : -1) << '\n';

            out << "ENDPROD\n";
        }

        if (const auto* storage = building->GetComponent<StorageComponent>())
        {
            out << "STOR " << storage->buffers.size() << '\n';
            for (const auto& [type, buffer] : storage->buffers)
                SaveResourceBuffer(out, "BUF", buffer);
            out << "ENDSTOR\n";
        }
        if (const auto* local = building->GetComponent<LocalResourceBufferComponent>())
        {
            out << "LOCALBUF " << local->buffers.size() << '\n';
            for (const auto& [type, buffer] : local->buffers)
                SaveResourceBuffer(out, "BUF", buffer);
            out << "ENDLOCALBUF\n";
        }

        if (const auto* pop = building->GetComponent<PopulationComponent>())
        {
            out << "VIL " << pop->manpowerRate.GetBase() << ' ' << pop->upkeepTimer << ' '
                << pop->upkeepInterval << ' ' << pop->foodPackageUpkeep << ' '
                << pop->hasFood << ' ' << pop->populationCap.GetBase() << ' '
                << pop->foodSupplyLevel << ' ' << pop->foodBuffer.bufferSize << ' '
                << pop->foodBuffer.buffer.size() << ' ' << pop->settlementLevel << ' '
                << pop->householdSupplyLevel << ' ' << pop->householdGoodsBuffer.bufferSize << ' '
                << pop->householdGoodsBuffer.buffer.size() << ' ' << pop->urbanSupplyLevel << ' '
                << pop->urbanGoodsBuffer.bufferSize << ' ' << pop->urbanGoodsBuffer.buffer.size() << ' '
                << pop->householdUpkeepTimer << ' ' << pop->urbanUpkeepTimer << ' '
                << pop->assignedResidents << ' ' << (pop->hasAssignedResidents ? 1 : 0) << '\n';
            out << "VIL_DEBT " << pop->supplyDebt.size();
            for (const auto& [resource, debt] : pop->supplyDebt)
                out << ' ' << static_cast<int>(resource) << ' ' << debt;
            out << '\n';
        }

        if (const auto* recruitment = building->GetComponent<RecruitmentComponent>())
        {
            out << "RECRUIT " << recruitment->queue.size() << '\n';
            for (const auto& entry : recruitment->queue)
                out << "RQ " << std::quoted(entry.unitDefId) << ' ' << entry.total << ' ' << entry.remaining
                    << ' ' << (entry.resourcesReady ? 1 : 0) << '\n';
        }

        if (const auto* upgrade = building->GetComponent<UpgradeComponent>())
        {
            out << "UPG " << upgrade->level << ' ' << (upgrade->isUpgrading ? 1 : 0) << ' '
                << upgrade->upgradeRemaining << '\n';
        }

        if (const auto* road = building->GetComponent<RoadComponent>())
            out << "ROADPRIORITY " << static_cast<int>(road->priorityResource) << '\n';

        const auto* coverage = building->GetComponent<DefenseCoverageComponent>();
        const auto* garrison = building->GetComponent<GarrisonComponent>();
        const auto* upkeep = building->GetComponent<GarrisonUpkeepComponent>();
        if (coverage != nullptr || garrison != nullptr || upkeep != nullptr)
        {
            const auto* safety = building->GetComponent<SafetyComponent>();
            out << "DEFENSE "
                << (coverage != nullptr ? coverage->radius.GetBase() : 0.0) << ' '
                << (coverage != nullptr ? coverage->baseProtection.GetBase() : 0.0) << ' '
                << std::quoted(coverage != nullptr ? coverage->requiredState : std::string{}) << ' '
                << (garrison != nullptr ? garrison->capacity.GetBase() : 0) << ' '
                << (upkeep != nullptr ? upkeep->timer : 0.0) << ' '
                << (upkeep != nullptr ? upkeep->debtMicros : 0) << ' '
                << (upkeep != nullptr ? upkeep->intervalSeconds : 0.0) << ' '
                << (upkeep != nullptr ? upkeep->packageSize : 0.0) << ' '
                << (upkeep != nullptr ? upkeep->requestedAmount : 0) << ' '
                << (upkeep != nullptr ? static_cast<int>(upkeep->supplyStatus) : 0) << ' '
                << (safety != nullptr ? safety->intrinsicResilience : 0.0) << ' '
                << (safety != nullptr && safety->raidDestructible ? 1 : 0) << ' '
                << (safety != nullptr && safety->raidStockLossTarget ? 1 : 0) << '\n';
        }

        out << "ENDB\n";
    }

        return true;
    };

    if (!writeMapState(primaryMap))
        return false;

    std::vector<std::pair<ProvinceId, const TileMap*>> provinceMaps;
    for (const ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr || provinceId == primaryHome->GetId() ||
            province->GetOwnerId() == InvalidPlayerId || province->GetSimulation() == nullptr)
            continue;
        const auto* simulation = province->GetSimulation();
        if (!simulation->OwnsTileMap())
            return false;
        provinceMaps.emplace_back(provinceId, &simulation->GetTileMap());
    }
    if (provinceMaps.size() > PersistenceLimits::MaxProvinceMaps - 1)
        return false;
    out << "PROVINCE_MAPS " << provinceMaps.size() << '\n';
    for (const auto& [provinceId, map] : provinceMaps)
    {
        if (map == nullptr)
            return false;
        out << "PROVINCE_MAP " << provinceId << ' '
            << map->params.sizeX << ' ' << map->params.sizeY << ' '
            << map->params.seed << ' ' << static_cast<int>(map->params.sizePreset) << ' '
            << map->params.resourceDensity << ' ' << map->params.resourceFieldSize << ' '
            << map->params.resourceRichness << ' ' << map->params.aiOpponentCount << ' '
            << map->params.aiDifficulty << ' ' << map->params.debugMode << '\n';
        if (!writeMapState(*map))
            return false;
        out << "ENDPROVINCE_MAP\n";
    }

    std::vector<std::pair<ProvinceId, const RoadNetwork*>> shipmentNetworks;
    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindBuildableProvince(provinceId);
        const auto* simulation = province != nullptr ? province->GetSimulation() : nullptr;
        const auto* network = simulation != nullptr
            ? simulation->GetEconomy().roadNetwork.get() : nullptr;
        if (province != nullptr && province->GetOwnerId() != InvalidPlayerId && network != nullptr)
            shipmentNetworks.emplace_back(provinceId, network);
    }
    if (shipmentNetworks.size() > PersistenceLimits::MaxProvinceMaps)
        return false;
    out << "SHIPMENT_PROVINCES " << shipmentNetworks.size() << '\n';
    std::size_t totalShipmentCount = 0;
    for (const auto& [provinceId, network] : shipmentNetworks)
    {
        std::vector<ResourceShipment> shipments;
        network->AppendShipmentRecords(shipments);
        if (shipments.size() != network->GetLiveShipmentCount() ||
            shipments.size() > PersistenceLimits::MaxActiveShipments - totalShipmentCount)
            return false;
        totalShipmentCount += shipments.size();
        out << "SHIPMENT_PROVINCE " << provinceId << ' ' << network->GetNextShipmentId()
            << ' ' << shipments.size() << '\n';
        for (const auto& shipment : shipments)
        {
            out << "SHIPMENT " << shipment.id << ' ' << static_cast<int>(shipment.type) << ' '
                << shipment.quantity << ' ' << shipment.sourceBuildingId << ' '
                << shipment.targetBuildingId << ' ' << shipment.currentPathStep << ' '
                << shipment.elapsedTime << ' ' << shipment.transportTime << ' '
                << static_cast<int>(shipment.state) << ' ' << shipment.pathTileIds.size();
            for (int tileId : shipment.pathTileIds)
                out << ' ' << tileId;
            out << '\n';
        }
        out << "ENDSHIPMENT_PROVINCE\n";
    }
    out << "ENDSHIPMENTS\n";

    out << "JOURNEYS " << armyJourneySystem.GetNextJourneyId() << ' '
        << armyJourneySystem.GetJourneys().size() << '\n';
    for (const auto& [journeyId, journey] : armyJourneySystem.GetJourneys())
    {
        out << "JOURNEY " << journeyId << ' ' << journey.ownerId << ' '
            << journey.sourceProvinceId << ' ' << journey.targetProvinceId << ' '
            << static_cast<int>(journey.status) << ' ' << journey.currentLeg << ' '
            << journey.startTick << ' ' << journey.legCompletionTick << ' '
            << journey.deterministicAttemptCounter << ' ' << static_cast<int>(journey.kind) << ' '
            << journey.rules.baseLegDurationTicks << ' '
            << journey.rules.routeTravelSpeedMultiplier << ' '
            << journey.rules.speedProfile.baseUnitsPerMinute << ' '
            << journey.rules.speedProfile.moverSpeedBasisPoints << ' '
            << journey.rules.speedProfile.playerRouteSpeedBasisPoints << ' '
            << journey.rules.speedProfile.operationSpeedBasisPoints << ' '
            << journey.rules.speedProfile.extraLegDistanceBasisPoints << ' '
            << journey.legPlan.size();
        for (const auto& leg : journey.legPlan)
            out << ' ' << leg.connectionId << ' ' << leg.lengthUnits << ' '
                << leg.routeTimeBasisPoints << ' ' << leg.routeLevelAtStart << ' '
                << leg.incidentReductionBasisPoints << ' ' << leg.durationTicks;
        out << ' ' << journey.loadout.resources.size();
        for (const auto& resource : journey.loadout.resources)
            out << ' ' << static_cast<int>(resource.type) << ' ' << resource.amount;
        out << ' ' << journey.loadout.minimumResources.size();
        for (const auto& resource : journey.loadout.minimumResources)
            out << ' ' << static_cast<int>(resource.type) << ' ' << resource.amount;
        out << ' ' << journey.loadout.supplyRatioBasisPoints << ' '
            << journey.loadout.modifiers.size();
        for (const auto& modifier : journey.loadout.modifiers)
            out << ' ' << static_cast<int>(modifier.stat) << ' '
                << modifier.multiplierBasisPoints << ' ' << modifier.source;
        out << ' ';
        if (const auto* scout = std::get_if<ScoutParty>(&journey.payload))
        {
            out << 0 << ' ' << scout->unitInstanceIds.size();
            for (const int unitId : scout->unitInstanceIds)
                out << ' ' << unitId;
        }
        else if (const auto* trade = std::get_if<TradeCargo>(&journey.payload))
        {
            out << 1 << ' ' << static_cast<int>(trade->offerType) << ' '
                << static_cast<int>(trade->requestType) << ' ' << trade->amount;
        }
        else if (const auto* army = std::get_if<ArmyParty>(&journey.payload))
        {
            out << 2 << ' ' << army->unitInstanceIds.size();
            for (const int unitId : army->unitInstanceIds)
                out << ' ' << unitId;
        }
        else if (const auto* convoy = std::get_if<ResourceConvoy>(&journey.payload))
        {
            out << 4 << ' ' << convoy->cargo.size();
            for (const auto& cargo : convoy->cargo)
                out << ' ' << static_cast<int>(cargo.type) << ' ' << cargo.amount;
        }
        else if (const auto* transfer = std::get_if<ArmyTransferParty>(&journey.payload))
        {
            out << 5 << ' ' << transfer->destinationBarracksBuildingId << ' '
                << transfer->unitInstanceIds.size();
            for (const int unitId : transfer->unitInstanceIds)
                out << ' ' << unitId;
        }
        else
        {
            out << 3 << ' ' << std::get<Colonists>(journey.payload).householdCount;
        }
        out << '\n';
    }

    out << "TRADES " << nextTradeOrderId << ' ' << activeTradeOrders.size() << '\n';
    for (const auto& [orderId, order] : activeTradeOrders)
    {
        out << "TRADE " << orderId << ' ' << order.playerId << ' '
            << order.originProvinceId << ' ' << order.cityProvinceId << ' '
            << static_cast<int>(order.request.offerType) << ' '
            << static_cast<int>(order.request.requestType) << ' '
            << order.request.requestedAmount << ' ' << order.request.offeredAmount << ' '
            << order.offeredAmount << ' ' << order.cargoAmount << ' '
            << order.cityRevisionAtStart << ' ' << order.journeyId << ' '
            << static_cast<int>(order.request.mode) << ' '
            << static_cast<int>(order.status) << '\n';
    }
    out << "END_TRADES\n";

    out << "BATTLES " << battleSystem.GetNextBattleId() << ' '
        << battleSystem.GetBattles().size() << ' ' << battleSystem.GetReports().size() << '\n';
    const auto writeBattleUnitIds = [&out](const char* tag, const std::vector<int>& ids)
    {
        out << tag << ' ' << ids.size();
        for (const int id : ids)
            out << ' ' << id;
        out << '\n';
    };
    const auto writeBattleSide = [&out](const char* tag, const BattleSideSnapshot& side)
    {
        out << tag << ' ' << side.ownerId << ' ' << side.defensiveBonus << ' '
            << side.units.size() << '\n';
        for (const auto& unit : side.units)
            out << "SIDE_UNIT " << unit.instanceId << ' ' << std::quoted(unit.unitDefId) << ' '
                << unit.effectiveFieldAttack << '\n';
    };
    const auto writeOutcome = [&out](const char* tag, const std::optional<BattleOutcome>& outcome)
    {
        out << tag << ' ' << (outcome.has_value() ? 1 : 0);
        if (!outcome.has_value())
        {
            out << '\n';
            return;
        }
        const auto& value = *outcome;
        out << ' ' << (value.valid ? 1 : 0) << ' ' << static_cast<int>(value.winner) << ' '
            << value.attackerStrength << ' ' << value.defenderStrength << ' '
            << value.attackerCasualtyBudget << ' ' << value.defenderCasualtyBudget << ' '
            << value.lootValue << ' ' << (value.crushingVictory ? 1 : 0) << ' '
            << value.durationTicks << ' ' << value.attackerLostUnitIds.size();
        for (const int id : value.attackerLostUnitIds)
            out << ' ' << id;
        out << ' ' << value.defenderLostUnitIds.size();
        for (const int id : value.defenderLostUnitIds)
            out << ' ' << id;
        out << '\n';
    };
    for (const auto& [battleId, battle] : battleSystem.GetBattles())
    {
        out << "BATTLE " << battleId << ' ' << battle.attackerId << ' ' << battle.defenderId << ' '
            << battle.sourceProvinceId << ' ' << battle.targetProvinceId << ' ' << battle.journeyId << ' '
            << static_cast<int>(battle.status) << ' ' << battle.startTick << ' ' << battle.endTick << ' '
            << (battle.isRaid ? 1 : 0) << ' ' << battle.raidStrength << ' '
            << battle.rules.baseLossFraction << ' ' << battle.rules.casualtyCapFraction << ' '
            << battle.rules.drawBand << ' ' << battle.rules.crushingRatio << ' '
            << battle.rules.lootFraction << ' ' << battle.rules.durationTicks << '\n';
        writeBattleUnitIds("ATTACK_IDS", battle.attackerUnitIds);
        writeBattleUnitIds("DEFENDER_IDS", battle.defenderUnitIds);
        writeBattleSide("ATTACK_SIDE", battle.attackerSnapshot);
        writeBattleSide("DEFENDER_SIDE", battle.defenderSnapshot);
        writeOutcome("OUTCOME", battle.outcome);
        out << "END_BATTLE\n";
    }
    for (const auto& report : battleSystem.GetReports())
    {
        out << "REPORT " << report.battleId << ' ' << report.attackerId << ' ' << report.defenderId << ' '
            << report.sourceProvinceId << ' ' << report.targetProvinceId << ' '
            << (report.banditTransformed ? 1 : 0) << ' ' << (report.cityDamaged ? 1 : 0) << ' '
            << (report.raid ? 1 : 0) << ' ' << report.destroyedBuildingIds.size();
        for (const int id : report.destroyedBuildingIds)
            out << ' ' << id;
        out << ' ' << report.lostResources.size();
        for (const auto& [type, amount] : report.lostResources)
            out << ' ' << static_cast<int>(type) << ' ' << amount;
        const auto& outcome = report.outcome;
        out << " OUTCOME 1 " << (outcome.valid ? 1 : 0) << ' ' << static_cast<int>(outcome.winner) << ' '
            << outcome.attackerStrength << ' ' << outcome.defenderStrength << ' '
            << outcome.attackerCasualtyBudget << ' ' << outcome.defenderCasualtyBudget << ' '
            << outcome.lootValue << ' ' << (outcome.crushingVictory ? 1 : 0) << ' '
            << outcome.durationTicks << ' ' << outcome.attackerLostUnitIds.size();
        for (const int id : outcome.attackerLostUnitIds)
            out << ' ' << id;
        out << ' ' << outcome.defenderLostUnitIds.size();
        for (const int id : outcome.defenderLostUnitIds)
            out << ' ' << id;
        out << '\n';
    }
    out << "END_BATTLES\n";

    out << "EVENT_RUNTIME " << eventSystem.GetNextInstanceId() << ' '
        << eventSystem.GetPeriodicScheduler().GetStates().size() << '\n';
    for (const auto& [playerId, schedulerState] : eventSystem.GetPeriodicScheduler().GetStates())
    {
        out << "EVENT_SCHEDULER " << playerId << ' ' << schedulerState.nextCheckTick << ' '
            << schedulerState.nextAllowedEventTick << ' ' << schedulerState.attemptCounter << ' '
            << schedulerState.definitionCooldownUntil.size() << '\n';
        for (const auto& [definitionId, cooldownUntil] : schedulerState.definitionCooldownUntil)
            out << "SCHEDULER_COOLDOWN " << std::quoted(definitionId) << ' '
                << cooldownUntil << '\n';
    }
    out << "EVENT_INSTANCES " << eventSystem.GetInstances().size() << '\n';
    for (const auto& [instanceId, instance] : eventSystem.GetInstances())
    {
        out << "EVENT_INSTANCE " << instanceId << ' ' << instance.ownerId << ' '
            << instance.provinceId << ' ' << instance.secondaryProvinceId << ' '
            << std::quoted(instance.definitionId) << ' ' << static_cast<int>(instance.trigger) << ' '
            << instance.startTick << ' ' << instance.endTick << ' ' << instance.outcomeRoll << ' '
            << instance.deterministicAttemptCounter << ' ' << instance.journeyId << ' '
            << (instance.expired ? 1 : 0) << ' ' << (instance.raidStarted ? 1 : 0) << ' '
            << (instance.journeyEffectApplied ? 1 : 0) << ' '
            << instance.appliedEffects.size() << '\n';
        for (const auto& effect : instance.appliedEffects)
            out << "EVENT_APPLIED " << instanceId << ' ' << static_cast<int>(effect.kind) << ' '
                << static_cast<int>(effect.resourceType) << ' ' << effect.amount << ' '
                << static_cast<int>(effect.stat) << ' ' << effect.additive << ' '
                << effect.multiplier << ' ' << effect.durationTicks << '\n';
    }
    out << "EVENT_FEED " << eventSystem.GetFeed().GetHistory().size() << '\n';
    for (const auto& notification : eventSystem.GetFeed().GetHistory())
    {
        out << "EVENT_NOTIFICATION " << notification.instanceId << ' ' << notification.ownerId << ' '
            << notification.provinceId << ' ' << notification.secondaryProvinceId << ' '
            << std::quoted(notification.definitionId) << ' ' << std::quoted(notification.title) << ' '
            << std::quoted(notification.description) << ' ' << static_cast<int>(notification.trigger) << ' '
            << notification.startTick << ' ' << notification.endTick << ' ' << notification.outcomeRoll << ' '
            << (notification.expired ? 1 : 0) << ' ' << notification.appliedEffects.size() << '\n';
        for (const auto& effect : notification.appliedEffects)
            out << "EVENT_FEED_APPLIED " << notification.instanceId << ' '
                << static_cast<int>(effect.kind) << ' ' << static_cast<int>(effect.resourceType) << ' '
                << effect.amount << ' ' << static_cast<int>(effect.stat) << ' '
                << effect.additive << ' ' << effect.multiplier << ' ' << effect.durationTicks << '\n';
    }
    out << "END_EVENT_RUNTIME\n";

    return true;
}

bool GameWorld::LoadFromFile(const std::string& path, Renderer* renderer)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;

    std::string payload((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (payload.empty() || payload.size() > PersistenceLimits::MaxSerializedStateBytes)
        return false;

    try
    {
        GameWorld candidate;
        std::istringstream state(payload);
        if (!candidate.LoadFromStream(state, renderer, -1))
            return false;
        *this = std::move(candidate);
        RebindMovedState();
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool GameWorld::RestoreSimulationState(std::string_view payload, int localPlayerIdOverride)
{
    // Full state transfer has a hard cap in SnapshotTransfer. Keeping the
    // same bound at the persistence boundary prevents an alternate caller
    // from handing the parser an unbounded network allocation.
    if (payload.empty() || payload.size() > PersistenceLimits::MaxSerializedStateBytes)
        return false;

    try
    {
        GameWorld candidate;
        std::istringstream in{std::string(payload)};
        if (!candidate.LoadFromStream(in, render, localPlayerIdOverride))
            return false;
        *this = std::move(candidate);
        RebindMovedState();
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void GameWorld::RebindMovedState()
{
    for (auto& [playerId, player] : playerHandler.players)
    {
        if (player == nullptr)
            continue;
        bool boundHome = false;
        for (const ProvinceId provinceId : globalMap.GetProvinceIds())
        {
            auto* province = globalMap.FindBuildableProvince(provinceId);
            if (province == nullptr || province->GetOwnerId() != playerId ||
                province->GetSimulation() == nullptr)
                continue;
            player->BindProvince(provinceId, *province->GetSimulation());
            if (provinceId == player->homeProvinceId)
                boundHome = true;
        }
        if (boundHome)
        {
            player->SetActiveProvince(player->homeProvinceId);
            if (auto* home = globalMap.FindBuildableProvince(player->homeProvinceId);
                home != nullptr && home->GetSimulation() != nullptr &&
                home->GetSimulation()->OwnsTileMap())
                player->RebindTileMap(player->homeProvinceId, home->GetSimulation()->GetTileMap());
        }
    }
}

bool GameWorld::LoadFromStream(std::istream& in, Renderer* renderer,
                               int localPlayerIdOverride)
{

    std::string tag;
    int version = 0;
    in >> tag >> version;
    // The save format is intentionally a hard boundary. A pre-rework save
    // cannot be partially interpreted because it contains removed combat and
    // military-road sections.
    if (tag != "RTS_SAVE" || version != SerializationVersion::GameWorldSaveVersion)
        return false;

    render = renderer;

    in >> tag >> std::quoted(worldName);
    if (tag != "WORLD")
        return false;

    int serializedLocalPlayerId = localPlayerId;
    in >> tag >> serializedLocalPlayerId >> simulationTick >> nextCommandId;
    if (tag != "RUNTIME")
        return false;
    localPlayerId = localPlayerIdOverride >= 0 ? localPlayerIdOverride : serializedLocalPlayerId;

    MapParameters primaryMapParams;
    int preset = 0;
    in >> tag >> primaryMapParams.sizeX >> primaryMapParams.sizeY >> primaryMapParams.seed >> preset;
    if (tag != "PARAMS")
        return false;
    std::size_t expectedTileCount = 0;
    if (!PersistenceLimits::CheckedArea(primaryMapParams.sizeX, primaryMapParams.sizeY, expectedTileCount) ||
        !IsValidMapSizePresetValue(preset))
        return false;
    primaryMapParams.sizePreset = static_cast<MapSizePreset>(preset);
    if (version >= 3)
    {
        in >> primaryMapParams.resourceDensity >> primaryMapParams.resourceFieldSize
            >> primaryMapParams.resourceRichness >> primaryMapParams.aiOpponentCount
            >> primaryMapParams.aiDifficulty;
        if (version >= 4)
            in >> primaryMapParams.debugMode;
    else
        primaryMapParams.debugMode = false;
    }

    globalMap = GlobalMap{};
    pendingHomeProvinceByPlayer.clear();
    pendingColonizations.clear();
    activeTradeOrders.clear();
    nextTradeOrderId = 1;
    std::vector<std::tuple<PlayerId, ProvinceId, ProvinceKnowledgeLevel>> restoredKnowledge;
    std::vector<ColonizationOperation> restoredColonizations;
    std::vector<WorldJourney> restoredJourneys;
    WorldJourneyId restoredNextJourneyId = 1;
    std::vector<TradeOrder> restoredTradeOrders;
    std::uint64_t restoredNextTradeOrderId = 1;
    std::vector<BattleInstance> restoredBattles;
    std::vector<BattleReport> restoredBattleReports;
    BattleId restoredNextBattleId = 1;
    std::vector<WorldEventInstance> restoredEvents;
    std::vector<WorldEventNotificationView> restoredEventNotifications;
    std::vector<PendingProvinceShipments> restoredProvinceShipments;
    WorldEventInstanceId restoredNextEventId = 1;
    in >> tag;
    if (tag != "CAMPAIGN")
        return false;
    int provinceCount = 0;
    int edgeCount = 0;
    std::uint32_t generationSeed = 0;
    in >> tag >> provinceCount >> edgeCount >> generationSeed;
    if (tag != "GLOBAL_MAP" ||
        !PersistenceLimits::IsCountInRange(provinceCount, PersistenceLimits::MaxGlobalProvinces) ||
        !PersistenceLimits::IsCountInRange(edgeCount, PersistenceLimits::MaxGlobalEdges) ||
        provinceCount <= 0)
        return false;
    globalMap.SetGenerationSeed(generationSeed);
    GlobalMapGenerationParameters restoredCampaignMap;
    int restoredFogOfWar = 1;
    in >> tag >> restoredCampaignMap.seed >> restoredCampaignMap.provinceCount
       >> restoredCampaignMap.extraEdgeCount >> restoredCampaignMap.layoutRadius
       >> restoredCampaignMap.minimumLayoutSpacing
       >> restoredCampaignMap.maximumPlacementAttemptsPerProvince
       >> restoredCampaignMap.minimumNeutralBuildables >> restoredCampaignMap.buildableWeight
       >> restoredCampaignMap.neutralCityWeight >> restoredCampaignMap.banditCampWeight
       >> restoredCampaignMap.eventSiteWeight >> restoredCampaignMap.startBoundaryClearance
       >> restoredCampaignMap.buildableWealthScale >> restoredCampaignMap.cityWealthScale
       >> restoredCampaignMap.banditStrengthScale >> restoredFogOfWar;
    if (!in || tag != "CAMPAIGN_PARAMETERS" ||
        restoredCampaignMap.provinceCount != provinceCount ||
        restoredCampaignMap.provinceCount <= 0 ||
        restoredCampaignMap.extraEdgeCount < 0 || restoredCampaignMap.layoutRadius <= 0 ||
        restoredCampaignMap.minimumLayoutSpacing < 0 ||
        restoredCampaignMap.maximumPlacementAttemptsPerProvince <= 0 ||
        restoredCampaignMap.minimumNeutralBuildables < 0 ||
        restoredCampaignMap.buildableWeight < 0 || restoredCampaignMap.neutralCityWeight < 0 ||
        restoredCampaignMap.banditCampWeight < 0 || restoredCampaignMap.eventSiteWeight < 0 ||
        restoredCampaignMap.startBoundaryClearance < 0 ||
        !std::isfinite(restoredCampaignMap.buildableWealthScale) ||
         !std::isfinite(restoredCampaignMap.cityWealthScale) ||
         !std::isfinite(restoredCampaignMap.banditStrengthScale) ||
         (restoredFogOfWar != 0 && restoredFogOfWar != 1) ||
        restoredCampaignMap.buildableWealthScale < 0.0 ||
        restoredCampaignMap.cityWealthScale < 0.0 ||
        restoredCampaignMap.banditStrengthScale < 0.0)
        return false;
    restoredCampaignMap.fogOfWarEnabled = restoredFogOfWar != 0;
    globalMap.SetFogOfWarEnabled(restoredCampaignMap.fogOfWarEnabled);
    campaignGenerationParameters.localMap = primaryMapParams;
    campaignGenerationParameters.globalMap = restoredCampaignMap;
    in >> tag;
    int serializedProvinceCount = 0;
    if (tag != "PROVINCES" || !(in >> serializedProvinceCount) ||
        serializedProvinceCount != provinceCount)
        return false;
    std::map<ProvinceId, std::pair<bool, std::uint64_t>> provinceStates;
    for (int i = 0; i < provinceCount; ++i)
    {
        std::uint64_t provinceIdValue = 0;
        int kind = 0;
        int ownerId = InvalidPlayerId;
        Vec2i position{};
        in >> tag >> provinceIdValue >> kind >> ownerId >> position.x >> position.y;
        if (!in || tag != "PROVINCE" || provinceIdValue == InvalidProvinceId ||
            provinceIdValue > std::numeric_limits<ProvinceId>::max() ||
            kind < static_cast<int>(ProvinceKind::Buildable) ||
            kind > static_cast<int>(ProvinceKind::TreasureSite) ||
            ownerId < InvalidPlayerId)
            return false;
        const ProvinceId provinceId = static_cast<ProvinceId>(provinceIdValue);
        std::unique_ptr<IProvince> province;
        if (static_cast<ProvinceKind>(kind) == ProvinceKind::Buildable)
            province = std::make_unique<BuildableProvince>(provinceId, position, ownerId);
        else
        {
            if (ownerId != InvalidPlayerId)
                return false;
            switch (static_cast<ProvinceKind>(kind))
            {
                case ProvinceKind::NeutralSettlement:
                    province = std::make_unique<NeutralCityProvince>(provinceId, position);
                    break;
                case ProvinceKind::BanditCamp:
                    province = std::make_unique<BanditProvince>(provinceId, position);
                    break;
                case ProvinceKind::TreasureSite:
                    province = std::make_unique<EventProvince>(provinceId, position);
                    break;
                case ProvinceKind::Buildable:
                    return false;
            }
        }
        if (!globalMap.AddProvince(std::move(province)))
            return false;

        std::size_t provinceExpectedTileCount = 0;
        in >> tag;
        if (static_cast<ProvinceKind>(kind) == ProvinceKind::Buildable)
        {
            BuildableProvinceParameters parameters;
            int traitCount = 0;
            std::uint64_t dataProvinceId = 0;
            in >> dataProvinceId >> std::quoted(parameters.definitionId) >> parameters.sizeX
               >> parameters.sizeY >> parameters.localSeed >> parameters.resourceWealth
               >> parameters.resourceDensity >> parameters.resourceFieldSize
               >> parameters.resourceRichness >> parameters.waterAmount
               >> parameters.mountainAmount >> parameters.ruggedness >> parameters.wealthTier
               >> traitCount;
            if (!in || tag != "BUILDABLE_DATA" || dataProvinceId != provinceId ||
                !PersistenceLimits::IsCountInRange(traitCount, PersistenceLimits::MaxBufferEntries) ||
                !PersistenceLimits::CheckedArea(parameters.sizeX, parameters.sizeY,
                                                provinceExpectedTileCount) ||
                !std::isfinite(parameters.resourceWealth) ||
                !std::isfinite(parameters.resourceDensity) ||
                !std::isfinite(parameters.resourceFieldSize) ||
                !std::isfinite(parameters.resourceRichness) ||
                !std::isfinite(parameters.waterAmount) ||
                !std::isfinite(parameters.mountainAmount) ||
                !std::isfinite(parameters.ruggedness))
                return false;
            parameters.traitIds.reserve(static_cast<std::size_t>(traitCount));
            for (int traitIndex = 0; traitIndex < traitCount; ++traitIndex)
            {
                std::string traitId;
                in >> std::quoted(traitId);
                if (!in || traitId.empty())
                    return false;
                parameters.traitIds.push_back(std::move(traitId));
            }
            int naturalResourceCount = 0;
            in >> naturalResourceCount;
            if (!in || !PersistenceLimits::IsCountInRange(naturalResourceCount, 16))
                return false;
            int previousResource = -1;
            for (int resourceIndex = 0; resourceIndex < naturalResourceCount; ++resourceIndex)
            {
                int resourceValue = 0;
                in >> resourceValue;
                if (!in || resourceValue <= previousResource || resourceValue < 0 || resourceValue > 255 ||
                    resourceValue == static_cast<int>(ResourceType::Null))
                    return false;
                previousResource = resourceValue;
                const ResourceType resource = static_cast<ResourceType>(resourceValue);
                if (resource != ResourceType::WOOD && resource != ResourceType::STONE &&
                    resource != ResourceType::COAL && resource != ResourceType::IRON_ORE &&
                    resource != ResourceType::COPPER_ORE && resource != ResourceType::CLAY &&
                    resource != ResourceType::SAND)
                    return false;
                parameters.naturalResourceTypes.push_back(resource);
            }
            if (version >= 58)
            {
                int depositCount = 0;
                in >> depositCount;
                if (!in || !PersistenceLimits::IsCountInRange(depositCount, 16))
                    return false;
                for (int depositIndex = 0; depositIndex < depositCount; depositIndex++)
                {
                    int resourceValue = 0;
                    double richness = 0.0;
                    in >> resourceValue >> richness;
                    if (!in || resourceValue < 0 || resourceValue > 255 ||
                        resourceValue == static_cast<int>(ResourceType::Null) ||
                        !std::isfinite(richness) || richness < 0.0)
                        return false;
                    parameters.resourceDeposits.push_back({
                        static_cast<ResourceType>(resourceValue), richness});
                }
            }
            auto* restored = globalMap.FindBuildableProvince(provinceId);
            if (restored == nullptr)
                return false;
            restored->SetParameters(std::move(parameters));
        }
        else if (static_cast<ProvinceKind>(kind) == ProvinceKind::NeutralSettlement)
        {
            std::uint64_t dataProvinceId = 0;
            std::string definitionId;
            int wealthTier = 0;
            double barterPenalty = 0.0;
            std::uint64_t revision = 0;
            int stockCount = 0;
            int buyPriceCount = 0;
            int sellPriceCount = 0;
            int scoreCount = 0;
            in >> dataProvinceId >> std::quoted(definitionId) >> wealthTier >> barterPenalty >> revision
               >> stockCount;
            if (!in || tag != "CITY_DATA" || dataProvinceId != provinceId || definitionId.empty() ||
                !std::isfinite(barterPenalty) || barterPenalty < 1.0 ||
                !PersistenceLimits::IsCountInRange(stockCount, PersistenceLimits::MaxBufferEntries))
                return false;
            auto* city = dynamic_cast<NeutralCityProvince*>(globalMap.FindProvince(provinceId));
            if (city == nullptr)
                return false;
            city->SetDefinitionId(std::move(definitionId));
            city->SetWealthTier(wealthTier);
            city->SetBarterPenaltyMultiplier(barterPenalty);
            auto& state = city->GetStateForAuthority();
            state.stock.clear();
            state.buyPrices.clear();
            state.sellPrices.clear();
            state.tradeScore.clear();
            for (int index = 0; index < stockCount; ++index)
            {
                int resourceType = 0;
                int amount = 0;
                in >> resourceType >> amount;
                if (!in || resourceType < 0 || resourceType > 255 || amount < 0 ||
                    !state.stock.emplace(static_cast<ResourceType>(resourceType), amount).second)
                    return false;
            }
            in >> buyPriceCount;
            if (!PersistenceLimits::IsCountInRange(buyPriceCount, PersistenceLimits::MaxBufferEntries))
                return false;
            for (int index = 0; index < buyPriceCount; ++index)
            {
                int resourceType = 0;
                double price = 0.0;
                in >> resourceType >> price;
                if (!in || resourceType < 0 || resourceType > 255 || !std::isfinite(price) || price <= 0.0 ||
                    !state.buyPrices.emplace(static_cast<ResourceType>(resourceType), price).second)
                    return false;
            }
            in >> sellPriceCount;
            if (!PersistenceLimits::IsCountInRange(sellPriceCount, PersistenceLimits::MaxBufferEntries))
                return false;
            for (int index = 0; index < sellPriceCount; ++index)
            {
                int resourceType = 0;
                double price = 0.0;
                in >> resourceType >> price;
                if (!in || resourceType < 0 || resourceType > 255 || !std::isfinite(price) || price <= 0.0 ||
                    !state.sellPrices.emplace(static_cast<ResourceType>(resourceType), price).second)
                    return false;
            }
            in >> scoreCount;
            if (!PersistenceLimits::IsCountInRange(scoreCount, PersistenceLimits::MaxBufferEntries))
                return false;
            for (int index = 0; index < scoreCount; ++index)
            {
                int scorePlayerId = InvalidPlayerId;
                int score = 0;
                in >> scorePlayerId >> score;
                if (!in || scorePlayerId == InvalidPlayerId || score < 0 || score > 1000 ||
                    !state.tradeScore.emplace(scorePlayerId, score).second)
                    return false;
            }
            state.revision = revision == 0 ? 1 : revision;
            city->SetWealthTier(wealthTier);
            city->SetBarterPenaltyMultiplier(barterPenalty);
            state.revision = revision == 0 ? 1 : revision;
        }
        else if (static_cast<ProvinceKind>(kind) == ProvinceKind::BanditCamp)
        {
            std::uint64_t dataProvinceId = 0;
            std::string definitionId;
            std::string lootTableId;
            int strength = 0;
            int raidPressure = 0;
            BuildableProvinceParameters future;
            int traitCount = 0;
            in >> dataProvinceId >> std::quoted(definitionId) >> strength >> raidPressure
               >> std::quoted(lootTableId) >> std::quoted(future.definitionId) >> future.sizeX
               >> future.sizeY >> future.localSeed >> future.resourceWealth >> future.resourceDensity
               >> future.resourceFieldSize >> future.resourceRichness >> future.waterAmount
               >> future.mountainAmount >> future.ruggedness >> future.wealthTier >> traitCount;
            if (!in || tag != "BANDIT_DATA" || dataProvinceId != provinceId || definitionId.empty() ||
                strength < 0 || raidPressure < 0 || future.definitionId.empty() ||
                !PersistenceLimits::IsCountInRange(traitCount, PersistenceLimits::MaxBufferEntries) ||
                !PersistenceLimits::CheckedArea(future.sizeX, future.sizeY, provinceExpectedTileCount) ||
                !std::isfinite(future.resourceWealth) || !std::isfinite(future.resourceDensity) ||
                !std::isfinite(future.resourceFieldSize) || !std::isfinite(future.resourceRichness) ||
                !std::isfinite(future.waterAmount) || !std::isfinite(future.mountainAmount) ||
                !std::isfinite(future.ruggedness))
                return false;
            future.traitIds.reserve(static_cast<std::size_t>(traitCount));
            for (int traitIndex = 0; traitIndex < traitCount; ++traitIndex)
            {
                std::string traitId;
                in >> std::quoted(traitId);
                if (!in || traitId.empty())
                    return false;
                future.traitIds.push_back(std::move(traitId));
            }
            int naturalResourceCount = 0;
            in >> naturalResourceCount;
            if (!in || !PersistenceLimits::IsCountInRange(naturalResourceCount, 16))
                return false;
            int previousResource = -1;
            for (int resourceIndex = 0; resourceIndex < naturalResourceCount; ++resourceIndex)
            {
                int resourceValue = 0;
                in >> resourceValue;
                if (!in || resourceValue <= previousResource || resourceValue < 0 || resourceValue > 255 ||
                    resourceValue == static_cast<int>(ResourceType::Null))
                    return false;
                previousResource = resourceValue;
                const ResourceType resource = static_cast<ResourceType>(resourceValue);
                if (resource != ResourceType::WOOD && resource != ResourceType::STONE &&
                    resource != ResourceType::COAL && resource != ResourceType::IRON_ORE &&
                    resource != ResourceType::COPPER_ORE && resource != ResourceType::CLAY &&
                    resource != ResourceType::SAND)
                    return false;
                future.naturalResourceTypes.push_back(resource);
            }
            if (version >= 58)
            {
                int depositCount = 0;
                in >> depositCount;
                if (!in || !PersistenceLimits::IsCountInRange(depositCount, 16))
                    return false;
                for (int depositIndex = 0; depositIndex < depositCount; depositIndex++)
                {
                    int resourceValue = 0;
                    double richness = 0.0;
                    in >> resourceValue >> richness;
                    if (!in || resourceValue < 0 || resourceValue > 255 ||
                        resourceValue == static_cast<int>(ResourceType::Null) ||
                        !std::isfinite(richness) || richness < 0.0)
                        return false;
                    future.resourceDeposits.push_back({
                        static_cast<ResourceType>(resourceValue), richness});
                }
            }
            auto* bandit = dynamic_cast<BanditProvince*>(globalMap.FindProvince(provinceId));
            if (bandit == nullptr)
                return false;
            bandit->SetStrength(strength);
            bandit->SetRaidPressure(raidPressure);
            bandit->SetLootTableId(std::move(lootTableId));
            bandit->SetDefinitionId(std::move(definitionId));
            bandit->SetFutureBuildableParameters(std::move(future));
        }
        else
        {
            std::uint64_t dataProvinceId = 0;
            std::string poolId;
            int eventPlayerCount = 0;
            in >> dataProvinceId >> std::quoted(poolId) >> eventPlayerCount;
            if (!in || tag != "EVENT_DATA" || dataProvinceId != provinceId ||
                !PersistenceLimits::IsCountInRange(eventPlayerCount, PersistenceLimits::MaxBufferEntries))
                return false;
            auto* event = dynamic_cast<EventProvince*>(globalMap.FindProvince(provinceId));
            if (event == nullptr)
                return false;
            event->SetEventPoolId(std::move(poolId));
            for (int eventIndex = 0; eventIndex < eventPlayerCount; ++eventIndex)
            {
                int eventPlayerId = InvalidPlayerId;
                std::uint32_t attempts = 0;
                int resolved = 0;
                in >> tag >> eventPlayerId >> attempts >> resolved;
                if (!in || tag != "EVENT_PLAYER" || eventPlayerId == InvalidPlayerId ||
                    (resolved != 0 && resolved != 1) ||
                    !event->RestoreDiscoveryState(eventPlayerId, attempts, resolved != 0))
                    return false;
            }
        }

        int hasSimulation = 0;
        std::uint64_t simulationTick = 0;
        in >> tag >> provinceIdValue >> hasSimulation >> simulationTick;
        if (!in || tag != "PROVINCE_STATE" || provinceIdValue != provinceId ||
            (hasSimulation != 0 && hasSimulation != 1))
            return false;
        provinceStates[provinceId] = {hasSimulation != 0, simulationTick};
        if (hasSimulation)
        {
            auto* buildable = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(provinceId));
            if (buildable == nullptr)
                return false;
            buildable->CreateSimulation().RestoreSimulationTick(simulationTick);
        }
        if (version >= 58)
        {
            int initialized = 0;
            double availableManpower = 0.0;
            in >> tag >> provinceIdValue >> initialized >> availableManpower;
            if (!in || tag != "PROVINCE_POPULATION" || provinceIdValue != provinceId ||
                (initialized != 0 && initialized != 1) ||
                !std::isfinite(availableManpower) || availableManpower < 0.0)
                return false;
            if (hasSimulation)
            {
                auto* buildable = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(provinceId));
                if (buildable == nullptr || buildable->GetSimulation() == nullptr)
                    return false;
                auto& population = buildable->GetSimulation()->GetEconomy().population;
                population.initialized = initialized != 0;
                population.availableManpower = availableManpower;
            }
        }
    }
    in >> tag;
    if (tag != "ENDPROVINCES")
        return false;
    in >> tag >> serializedProvinceCount;
    if (tag != "CONNECTIONS" || serializedProvinceCount != edgeCount)
        return false;
    for (int i = 0; i < edgeCount; ++i)
    {
        std::uint64_t connectionIdValue = 0;
        std::uint64_t fromValue = 0;
        std::uint64_t toValue = 0;
        std::string definitionId;
        int level = 0;
        int lengthUnits = 0;
        int targetLevel = -1;
        std::uint64_t remainingTicks = 0;
        in >> tag >> connectionIdValue >> fromValue >> toValue >>
            std::quoted(definitionId) >> lengthUnits >> level >> targetLevel >> remainingTicks;
        if (!in || tag != "CONNECTION" || fromValue == InvalidProvinceId ||
            connectionIdValue == InvalidProvinceConnectionId ||
            connectionIdValue > std::numeric_limits<ProvinceConnectionId>::max() ||
            toValue == InvalidProvinceId || fromValue > std::numeric_limits<ProvinceId>::max() ||
            toValue > std::numeric_limits<ProvinceId>::max() ||
            fromValue >= toValue ||
            lengthUnits <= 0 ||
            !globalMap.AddConnection(static_cast<ProvinceId>(fromValue),
                                     static_cast<ProvinceId>(toValue),
                                     static_cast<ProvinceConnectionId>(connectionIdValue),
                                     definitionId))
            return false;
        auto* connection = dynamic_cast<LandRouteConnection*>(
            globalMap.FindConnection(static_cast<ProvinceConnectionId>(connectionIdValue)));
        if (connection == nullptr ||
            connection->GetLengthUnits() != lengthUnits ||
            !connection->SetRuntimeState(level, targetLevel, remainingTicks))
            return false;
    }
    int knowledgeCount = 0;
    std::set<std::pair<PlayerId, ProvinceId>> knowledgeKeys;
    in >> tag >> knowledgeCount;
    if (tag != "KNOWLEDGE" ||
        !PersistenceLimits::IsCountInRange(knowledgeCount, PersistenceLimits::MaxProvinceKnowledgeEntries))
        return false;
    for (int i = 0; i < knowledgeCount; ++i)
    {
        int playerId = InvalidPlayerId;
        std::uint64_t provinceIdValue = 0;
        int level = 0;
        in >> tag >> playerId >> provinceIdValue >> level;
        if (!in || tag != "KNOW" || playerId == InvalidPlayerId ||
            provinceIdValue == InvalidProvinceId || provinceIdValue > std::numeric_limits<ProvinceId>::max() ||
            level < static_cast<int>(ProvinceKnowledgeLevel::Hidden) ||
            level > static_cast<int>(ProvinceKnowledgeLevel::Owned) ||
            globalMap.FindProvince(static_cast<ProvinceId>(provinceIdValue)) == nullptr ||
            !knowledgeKeys.insert({playerId, static_cast<ProvinceId>(provinceIdValue)}).second)
            return false;
        restoredKnowledge.emplace_back(playerId, static_cast<ProvinceId>(provinceIdValue),
                                       static_cast<ProvinceKnowledgeLevel>(level));
    }
    in >> tag;
    if (tag != "ENDCAMPAIGN" || !globalMap.IsConnected())
        return false;
    int colonizationCount = 0;
    in >> tag >> colonizationCount;
    if (tag != "COLONIZATION" ||
        !PersistenceLimits::IsCountInRange(colonizationCount,
                                           PersistenceLimits::MaxColonizationOperations))
        return false;
    for (int index = 0; index < colonizationCount; ++index)
    {
        ColonizationOperation operation;
        int costCount = 0;
        int phase = 0;
        in >> tag >> operation.playerId >> operation.sourceProvinceId
           >> operation.targetProvinceId >> operation.journeyId >> phase
           >> operation.phaseCompletionTick >> operation.settlementDurationTicks >> costCount;
        if (!in || tag != "COLONIZATION_OP" || operation.playerId == InvalidPlayerId ||
            operation.sourceProvinceId == InvalidProvinceId ||
            operation.targetProvinceId == InvalidProvinceId ||
            operation.journeyId == InvalidWorldJourneyId ||
            phase < static_cast<int>(ColonizationPhase::Traveling) ||
            phase > static_cast<int>(ColonizationPhase::Failed) ||
            operation.settlementDurationTicks == 0 ||
            (static_cast<ColonizationPhase>(phase) == ColonizationPhase::Traveling &&
             operation.phaseCompletionTick != 0) ||
            (static_cast<ColonizationPhase>(phase) == ColonizationPhase::Establishing &&
             operation.phaseCompletionTick < simulationTick) ||
            !PersistenceLimits::IsCountInRange(costCount, PersistenceLimits::MaxBufferEntries))
            return false;
        operation.phase = static_cast<ColonizationPhase>(phase);
        std::set<ResourceType> costTypes;
        for (int costIndex = 0; costIndex < costCount; ++costIndex)
        {
            int resourceType = 0;
            int amount = 0;
            in >> tag >> resourceType >> amount;
            if (!in || tag != "COLONIZATION_COST" || resourceType < 0 || resourceType > 255 ||
                amount <= 0 || !costTypes.insert(static_cast<ResourceType>(resourceType)).second)
                return false;
            operation.cost.push_back({static_cast<ResourceType>(resourceType), amount});
        }
        if (operation.cost.empty())
            return false;
        restoredColonizations.push_back(std::move(operation));
    }
    for (const auto& [playerId, provinceId, level] : restoredKnowledge)
        if (!globalMap.SetKnowledge(playerId, provinceId, level))
            return false;
    if (version >= 2)
    {
        Camera2D camera{};
        in >> tag >> camera.target.x >> camera.target.y >> camera.zoom >> camera.rotation;
        if (tag != "CAMERA")
            return false;
        if (render != nullptr)
        {
            render->camera = camera;
            render->ClampCameraToMap({primaryMapParams.sizeX, primaryMapParams.sizeY});
        }
    }

    playerHandler.players.clear();
    controllers.clear();
    int playerCount = 0;
    in >> tag >> playerCount;
    if (tag != "PLAYERS" || !PersistenceLimits::IsCountInRange(playerCount, PersistenceLimits::MaxSupportedPlayers))
        return false;
    std::map<PlayerId, std::map<GameCommandType, int>> pendingCommandStats;
    std::map<PlayerId, std::map<ProvinceId, std::map<GameCommandType, int>>> pendingProvinceCommandStats;

    for (int i = 0; i < playerCount; i++)
    {
        int playerId = 0;
        std::uint64_t homeProvinceValue = 0;
        int strategicCount = 0;
        int technologyCount = 0;
        int focusCount = 0;
        in >> tag >> playerId >> homeProvinceValue >> strategicCount >> technologyCount >> focusCount;
        if (tag != "PLAYER")
            return false;
        if (playerHandler.players.contains(playerId) || playerId < 0 ||
            strategicCount < 0 || technologyCount < 0 || focusCount < 0)
            return false;
        if (playerId == InvalidPlayerId || homeProvinceValue == InvalidProvinceId ||
            homeProvinceValue > std::numeric_limits<ProvinceId>::max())
            return false;
        const ProvinceId homeProvinceId = static_cast<ProvinceId>(homeProvinceValue);
        auto* homeProvince = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(homeProvinceId));
        if (homeProvince == nullptr ||
            (homeProvince->GetOwnerId() != InvalidPlayerId && homeProvince->GetOwnerId() != playerId))
            return false;

        auto player = std::make_unique<Player>(playerId);
        int controllerType = 0;
        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 255;
        in >> controllerType >> std::quoted(player->name) >> red >> green >> blue >> alpha;
        if (controllerType < static_cast<int>(PlayerControllerType::LocalHuman) ||
            controllerType > static_cast<int>(PlayerControllerType::Remote) ||
            red < 0 || red > 255 || green < 0 || green > 255 ||
            blue < 0 || blue > 255 || alpha < 0 || alpha > 255)
            return false;
        player->controllerType = static_cast<PlayerControllerType>(controllerType);
        player->homeProvinceId = homeProvinceId;
        player->color = Color{static_cast<unsigned char>(red), static_cast<unsigned char>(green),
                              static_cast<unsigned char>(blue), static_cast<unsigned char>(alpha)};
        if (localPlayerIdOverride >= 0)
            player->controllerType = playerId == localPlayerId ? PlayerControllerType::LocalHuman : PlayerControllerType::Remote;
        for (int s = 0; s < strategicCount; s++)
        {
            int type = 0;
            double value = 0.0;
            in >> tag >> type >> value;
            if (tag != "STRAT")
                return false;
            player->strategicResources.values[static_cast<StrategicResourceType>(type)] = value;
        }

        for (int t = 0; t < technologyCount; t++)
        {
            std::string techId;
            in >> tag >> std::quoted(techId);
            if (tag != "TECH")
                return false;
            player->technologies.RestoreTechnology(techId);
        }
        for (int f = 0; f < focusCount; f++)
        {
            std::string focusId;
            in >> tag >> std::quoted(focusId);
            if (tag != "FOCUS")
                return false;
            player->focuses.RestoreFocus(focusId);
        }
        std::string activeFocusId;
        double activeFocusRemaining = 0.0;
        in >> tag >> std::quoted(activeFocusId) >> activeFocusRemaining;
        if (tag != "ACTIVE_FOCUS" || !player->focuses.RestoreActiveFocus(activeFocusId, activeFocusRemaining))
            return false;
        player->RefreshTechnologyModifiers();

        int rosterCount = 0;
        in >> tag >> player->nextUnitInstanceId >> rosterCount;
        if (tag != "ROSTER" || player->nextUnitInstanceId <= 0 ||
            !PersistenceLimits::IsCountInRange(rosterCount, PersistenceLimits::MaxUnits))
            return false;
        for (int u = 0; u < rosterCount; u++)
        {
            int instanceId = 0;
            int ownerPlayerId = 0;
            std::string unitDefId;
            int assignmentKind = 0;
            std::uint64_t assignmentProvinceValue = 0;
            int assignmentBuildingId = 0;
            std::uint64_t activeJourneyValue = 0;
            std::uint64_t activeBattleValue = 0;
            std::uint64_t taskGroupValue = 0;
            in >> tag >> instanceId >> ownerPlayerId >> std::quoted(unitDefId)
               >> assignmentKind >> assignmentProvinceValue >> assignmentBuildingId
               >> activeJourneyValue >> activeBattleValue >> taskGroupValue;
            if (tag != "UNIT" || instanceId <= 0 || ownerPlayerId != playerId ||
                unitDefId.empty() || player->roster.units.contains(instanceId) ||
                assignmentKind < static_cast<int>(UnitAssignmentKind::BarracksReserve) ||
                assignmentKind > static_cast<int>(UnitAssignmentKind::Unassigned) ||
                assignmentProvinceValue > std::numeric_limits<ProvinceId>::max() ||
                assignmentBuildingId < 0 || assignmentBuildingId >= 300'000 ||
                activeJourneyValue == InvalidWorldJourneyId &&
                    static_cast<UnitAssignmentKind>(assignmentKind) == UnitAssignmentKind::Journey ||
                activeBattleValue == InvalidBattleId &&
                    static_cast<UnitAssignmentKind>(assignmentKind) == UnitAssignmentKind::Battle)
                return false;

            BattleUnit unit(instanceId, ownerPlayerId, unitDefId);
            unit.assignment.kind = static_cast<UnitAssignmentKind>(assignmentKind);
            unit.assignment.provinceId = static_cast<ProvinceId>(assignmentProvinceValue);
            unit.assignment.buildingId = assignmentBuildingId;
            unit.assignment.worldJourneyId = static_cast<WorldJourneyId>(activeJourneyValue);
            unit.assignment.battleId = static_cast<BattleId>(activeBattleValue);
            unit.taskGroupId = static_cast<TaskGroupId>(taskGroupValue);
            if (!unit.assignment.IsStructurallyValid())
                return false;
            player->roster.AddUnit(std::move(unit));
        }
        if (player->roster.units.size() != static_cast<std::size_t>(rosterCount))
            return false;
        TaskGroupId nextTaskGroupId = InvalidTaskGroupId;
        int taskGroupCount = 0;
        in >> tag >> nextTaskGroupId >> taskGroupCount;
        if (tag != "TASK_GROUPS" || nextTaskGroupId == InvalidTaskGroupId ||
            !PersistenceLimits::IsCountInRange(taskGroupCount,
                                                TaskGroupRegistry::MaxTaskGroupsPerPlayer))
            return false;
        std::map<TaskGroupId, TaskGroup> restoredTaskGroups;
        for (int groupIndex = 0; groupIndex < taskGroupCount; ++groupIndex)
        {
            TaskGroup group;
            in >> tag >> group.id >> group.stationProvinceId >> group.homeBarracksBuildingId;
            if (!in || tag != "TASK_GROUP" || group.id == InvalidTaskGroupId ||
                !restoredTaskGroups.emplace(group.id, group).second)
                return false;
        }
        if (!player->taskGroups.Restore(nextTaskGroupId, std::move(restoredTaskGroups)))
            return false;
        for (const auto& [unitId, unit] : player->roster.units)
            if (unit.taskGroupId != InvalidTaskGroupId &&
                player->taskGroups.Find(unit.taskGroupId) == nullptr)
                return false;
        int commandStatCount = 0;
        in >> tag >> commandStatCount;
        if (tag != "COMMANDSTATS" || commandStatCount < 0)
            return false;
        std::map<GameCommandType, int> restoredCommandStats;
        for (int c = 0; c < commandStatCount; ++c)
        {
            int commandType = 0;
            int count = 0;
            in >> tag >> commandType >> count;
            if (tag != "CMDSTAT" || count < 0)
                return false;
            restoredCommandStats[static_cast<GameCommandType>(commandType)] = count;
        }

        int provinceCommandStatsCount = 0;
        in >> tag >> provinceCommandStatsCount;
        if (version < 41)
        {
            if (tag != "ENDPLAYER")
                return false;
        }
        else
        {
            if (tag != "PROVINCE_COMMANDSTATS" ||
                !PersistenceLimits::IsCountInRange(provinceCommandStatsCount,
                                                    PersistenceLimits::MaxProvinceMaps))
                return false;
            for (int p = 0; p < provinceCommandStatsCount; ++p)
            {
                std::uint64_t provinceIdValue = 0;
                int statsCount = 0;
                in >> tag >> provinceIdValue >> statsCount;
                if (!in || tag != "PROVINCE_STATS" || provinceIdValue == InvalidProvinceId ||
                    provinceIdValue > std::numeric_limits<ProvinceId>::max() ||
                    !PersistenceLimits::IsCountInRange(statsCount, PersistenceLimits::MaxBufferEntries))
                    return false;
                const ProvinceId provinceId = static_cast<ProvinceId>(provinceIdValue);
                const auto* province = globalMap.FindBuildableProvince(provinceId);
                if (province == nullptr || province->GetOwnerId() != playerId ||
                    !pendingProvinceCommandStats[playerId].emplace(provinceId,
                                                                     std::map<GameCommandType, int>{}).second)
                    return false;
                auto& stats = pendingProvinceCommandStats[playerId][provinceId];
                for (int s = 0; s < statsCount; ++s)
                {
                    int commandType = 0;
                    int count = 0;
                    in >> tag >> commandType >> count;
                    if (!in || tag != "PCMDSTAT" || count < 0)
                        return false;
                    stats[static_cast<GameCommandType>(commandType)] = count;
                }
            }
            in >> tag;
            if (tag != "ENDPLAYER")
                return false;
        }
        playerHandler.players[playerId] = std::move(player);
        AttachControllerForPlayer(playerHandler.players[playerId].get());
        pendingCommandStats[playerId] = std::move(restoredCommandStats);
    }

    std::set<ProvinceId> homeProvinceIds;
    std::map<PlayerId, std::size_t> ownedProvinceCounts;
    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindProvince(provinceId);
        const auto* buildable = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr || buildable == nullptr || buildable->GetOwnerId() == InvalidPlayerId)
            continue;
        if (!playerHandler.players.contains(buildable->GetOwnerId()) ||
            province->GetKind() != ProvinceKind::Buildable)
            return false;
        ++ownedProvinceCounts[buildable->GetOwnerId()];
    }
    for (const auto& [playerId, player] : playerHandler.players)
    {
        if (player == nullptr || player->homeProvinceId == InvalidProvinceId ||
            !homeProvinceIds.insert(player->homeProvinceId).second)
            return false;
        const auto* home = globalMap.FindBuildableProvince(player->homeProvinceId);
        if (home == nullptr || home->GetKind() != ProvinceKind::Buildable ||
            home->GetOwnerId() != playerId)
            return false;
        if (ownedProvinceCounts[playerId] == 0)
            return false;
    }
    for (const auto& [knowledgePlayerId, provinceId, level] : restoredKnowledge)
    {
        if (!playerHandler.players.contains(knowledgePlayerId))
            return false;
        const auto* province = globalMap.FindProvince(provinceId);
        const auto* buildable = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr ||
            (level == ProvinceKnowledgeLevel::Owned &&
             (buildable == nullptr || buildable->GetOwnerId() != knowledgePlayerId)))
            return false;
    }

    auto primaryPlayerIt = playerHandler.players.find(0);
    if (primaryPlayerIt == playerHandler.players.end() || primaryPlayerIt->second == nullptr)
        return false;
    auto* primaryHome = dynamic_cast<BuildableProvince*>(
        globalMap.FindProvince(primaryPlayerIt->second->homeProvinceId));
    if (primaryHome == nullptr)
        return false;
    ProvinceSimulation& primarySimulation = primaryHome->CreateSimulation();
    TileMap& primaryMap = primarySimulation.GetTileMap();
    primaryMap.params = primaryMapParams;

    // Attach every owned province's canonical local context before reading
    // buildings. This is required for PlaceLoadedBuilding to register each
    // object in the correct province-owned registry instead of falling back
    // to a detached player compatibility path.
    for (auto& [playerId, player] : playerHandler.players)
    {
        if (player == nullptr)
            return false;
        bool boundHome = false;
        for (const ProvinceId provinceId : globalMap.GetProvinceIds())
        {
            auto* province = globalMap.FindBuildableProvince(provinceId);
            if (province == nullptr || province->GetOwnerId() != playerId)
                continue;
            if (province->GetSimulation() == nullptr)
                return false;
            player->BindProvince(provinceId, *province->GetSimulation());
            boundHome |= provinceId == player->homeProvinceId;
        }
        if (!boundHome)
            return false;
        player->SetActiveProvince(player->homeProvinceId);
    }

    std::vector<PendingConnection> pendingConnections;
    auto loadMapState = [&](TileMap& targetMap, std::size_t expectedTiles) -> bool
    {
        TileMap& tilemap = targetMap;
        int tileCount = 0;
        in >> tag >> tileCount;
    if (tag != "TILES" || !PersistenceLimits::IsCountInRange(tileCount, PersistenceLimits::MaxMapTiles) ||
        static_cast<std::size_t>(tileCount) != expectedTiles)
        return false;

    tilemap.tilemap.clear();
    tilemap.tilemap.resize(tileCount);
    for (int i = 0; i < tileCount; i++)
    {
        int id = 0;
        int tileType = 0;
        int terrainTextureId = 0;
        int resourceOverlayTextureId = -1;
        in >> tag >> id >> tileType >> terrainTextureId;
        if (version >= 31)
            in >> resourceOverlayTextureId;
        if (tag != "T")
            return false;
        if (id < 0 || static_cast<std::size_t>(id) >= expectedTiles || id != i)
            return false;

        Tile tile{id};
        tile.tileType = static_cast<TileType>(tileType);
        tile.terrainTextureId = terrainTextureId;
        tile.resourceOverlayTextureId = resourceOverlayTextureId;
        if (version >= 3)
            in >> tile.resourceRichness;
        else
            tile.resourceRichness = tile.tileType == TileType::GRASS ? 0 : tilemap.params.resourceRichness;
        if (version >= 16)
        {
            int biome = 0;
            in >> biome;
            tile.biome = static_cast<BiomeType>(biome);
        }
        tilemap.tilemap[id] = std::move(tile);
    }

    int buildingCount = 0;
    in >> tag >> buildingCount;
    if (tag != "BUILDINGS" || !PersistenceLimits::IsCountInRange(buildingCount, PersistenceLimits::MaxBuildings))
        return false;

    for (int i = 0; i < buildingCount; i++)
    {
        int positionId = 0;
        int buildingType = 0;
        int buildingId = 0;
        int ownerId = -1;
        int textureId = 0;
        Vec2i footprint{};
        bool productionBlocked = false;
        double lifetime = 0.0;
        double activeTime = 0.0;
        int totalProduced = 0;
        double transportTime = 0.0;
        int buildCostState = static_cast<int>(BuildCostRecordState::Free);
        int paidCostCount = 0;

        in >> tag >> positionId >> buildingType >> buildingId >> ownerId >> textureId
            >> footprint.x >> footprint.y >> productionBlocked >> lifetime >> activeTime
            >> totalProduced >> transportTime;
        if (tag != "B")
            return false;

        std::vector<ResourceAmountDefinition> paidBuildCosts;
        if (version >= 35)
        {
            in >> buildCostState >> paidCostCount;
            if (!in || buildCostState < static_cast<int>(BuildCostRecordState::Free) ||
                buildCostState > static_cast<int>(BuildCostRecordState::LegacyUnknown) ||
                !PersistenceLimits::IsCountInRange(paidCostCount, PersistenceLimits::MaxBufferEntries))
                return false;
            paidBuildCosts.reserve(static_cast<std::size_t>(paidCostCount));
            for (int costIndex = 0; costIndex < paidCostCount; ++costIndex)
            {
                int resourceType = 0;
                int amount = 0;
                in >> tag >> resourceType >> amount;
                if (!in || tag != "PAID" || amount < 0)
                    return false;
                paidBuildCosts.push_back({static_cast<ResourceType>(resourceType), amount});
            }
        }

        auto ownerIt = playerHandler.players.find(ownerId);
        Player* owner = ownerIt != playerHandler.players.end() ? ownerIt->second.get() : nullptr;
        auto building = CreateBuildingFromType(static_cast<BuildingType>(buildingType), buildingId);
        if (building == nullptr || owner == nullptr)
            return false;

        building->textureId = textureId;
        building->footprint = footprint;
        Building* placed = tilemap.PlaceLoadedBuilding(positionId, owner, std::move(building));
        if (placed == nullptr)
            return false;

        placed->id = buildingId;
        placed->textureId = textureId;
        placed->footprint = footprint;
        placed->productionBlocked = productionBlocked;
        placed->lifetime = lifetime;
        placed->activeTime = activeTime;
        placed->totalProduced = totalProduced;
        placed->transportTime = transportTime;
        if (version >= 35)
        {
            placed->buildCostRecordState = static_cast<BuildCostRecordState>(buildCostState);
            placed->buildCostWasPaid = placed->buildCostRecordState == BuildCostRecordState::PaidRecorded;
            placed->paidBuildCosts = std::move(paidBuildCosts);
        }
        else
        {
            // v34 did not persist construction payments. Keep this explicit
            // rather than pretending the current balance modifiers were the
            // historical price; salvage resolves the documented fallback.
            placed->buildCostRecordState = placed->buildingType == BuildingType::Headquarters
                ? BuildCostRecordState::Free : BuildCostRecordState::LegacyUnknown;
            placed->buildCostWasPaid = false;
        }

        while (in >> tag)
        {
            if (tag == "ENDB")
                break;

            if (tag == "CONSTRUCTION")
            {
                in >> placed->buildTime >> placed->constructionRemaining;
            }
            else if (tag == "PROD")
            {
                auto* prod = placed->GetComponent<ProductionComponent>();
                auto* workers = placed->GetComponent<WorkerComponent>();
                auto* recipes = placed->GetComponent<RecipeComponent>();
                // May be null — only University has one (see the matching
                // comment in SaveToFile).
                auto* research = placed->GetComponent<ResearchComponent>();
                auto* logistics = placed->GetComponent<LogisticsComponent>();
                if (prod == nullptr || workers == nullptr || recipes == nullptr || logistics == nullptr)
                    return false;

                int tileType = 0;
                in >> tileType >> prod->cycleTime >> prod->elapsed >> prod->started;
                if (version >= 32)
                    in >> prod->totalProduced;
                prod->terrainType = static_cast<TileType>(tileType);

                int count = 0;
                in >> tag >> count;
                if (version >= 5 && tag == "WORKERS")
                {
                    // The generic prefetch above already consumed the first of
                    // WORKERS' two payload values (capacity) into `count` — a
                    // pre-existing bug used to re-read it as a *second* value,
                    // shifting every field after it by one token (silently
                    // corrupting RECIPE/RESEARCH/INGREDIENTS parsing for any
                    // production building, never caught before because no test
                    // exercised a full save/load round trip with one).
                    workers->capacity = count;
                    in >> workers->assigned;
                    in >> tag >> count;
                }
                if (version >= 12 && tag == "RECIPE")
                {
                    recipes->SetActiveRecipe(count, *placed, *prod, *logistics, *workers);
                    in >> tag;
                }
                if (version >= 12 && tag == "RESEARCH")
                {
                    std::string technologyId;
                    double remaining = 0.0;
                    double total = 0.0;
                    in >> std::quoted(technologyId) >> remaining >> total;
                    if (research != nullptr)
                    {
                        research->technologyId = technologyId;
                        research->remaining = remaining;
                        research->total = total;
                    }
                    in >> tag >> count;
                }
                if (tag != "INGREDIENTS")
                    return false;
                prod->ingredients.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, amount = 0;
                    in >> tag >> type >> amount;
                    if (tag != "ING") return false;
                    prod->ingredients[static_cast<ResourceType>(type)] = amount;
                }

                in >> tag >> count;
                if (tag != "PRODUCTS") return false;
                prod->products.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, amount = 0;
                    in >> tag >> type >> amount;
                    if (tag != "PRODUCT") return false;
                    prod->products[static_cast<ResourceType>(type)] = amount;
                }

                in >> tag >> count;
                if (tag != "INPUTS") return false;
                prod->inputBuffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, capacity = 0, amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "INPUT") return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    prod->inputBuffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag >> count;
                if (tag != "OUTPUTS") return false;
                prod->outputBuffers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, capacity = 0, amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "OUTPUT") return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    prod->outputBuffers[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag >> count;
                if (tag != "SUPPLIERS") return false;
                logistics->suppliers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, target = -1;
                    in >> tag >> type >> target;
                    if (tag != "SUP") return false;
                    pendingConnections.push_back({&tilemap, positionId, static_cast<ResourceType>(type), target, false, false});
                }

                in >> tag >> count;
                if (tag != "RECEIVERS") return false;
                logistics->receivers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, target = -1;
                    in >> tag >> type >> target;
                    if (tag != "REC") return false;
                    pendingConnections.push_back({&tilemap, positionId, static_cast<ResourceType>(type), target, true, false});
                }

                in >> tag;
                if (version >= 13 && tag == "ALT_RECEIVERS")
                {
                    int altCount = 0;
                    in >> altCount;
                    logistics->altReceivers.clear();
                    if (!PersistenceLimits::IsCountInRange(altCount, PersistenceLimits::MaxBufferEntries))
                        return false;
                    for (int n = 0; n < altCount; n++)
                    {
                        int type = 0, target = -1;
                        in >> tag >> type >> target;
                        if (tag != "ALTREC") return false;
                        pendingConnections.push_back({&tilemap, positionId, static_cast<ResourceType>(type), target, true, true});
                    }
                    in >> tag;
                }
                if (tag != "ENDPROD") return false;
            }
            else if (tag == "STOR" || tag == "LOCALBUF")
            {
                const bool localBlock = tag == "LOCALBUF";
                auto* storage = placed->GetComponent<StorageComponent>();
                auto* local = placed->GetComponent<LocalResourceBufferComponent>();
                // v30-v33 encoded Barracks private buffers as STOR, so
                // an old STOR block may fall back to the new local component.
                // A v34 LOCALBUF block, however, must never populate a real
                // warehouse if a future specialized building owns both.
                std::map<ResourceType, ResourceBuffer>* buffers = localBlock
                    ? (local != nullptr ? &local->buffers : nullptr)
                    : (storage != nullptr ? &storage->buffers
                                          : (local != nullptr ? &local->buffers : nullptr));
                if (buffers == nullptr) return false;

                int count = 0;
                in >> count;
                buffers->clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, capacity = 0, amount = 0;
                    in >> tag >> type >> capacity >> amount;
                    if (tag != "BUF") return false;
                    ResourceBuffer buffer{static_cast<ResourceType>(type), capacity};
                    LoadResourceBuffer(buffer, static_cast<ResourceType>(type), capacity, amount);
                    (*buffers)[static_cast<ResourceType>(type)] = std::move(buffer);
                }

                in >> tag;
                if (tag != (localBlock ? "ENDLOCALBUF" : "ENDSTOR")) return false;
            }
            else if (tag == "VIL")
            {
                auto* population = placed->GetComponent<PopulationComponent>();
                if (population == nullptr) return false;
                auto& pop = *population;
                in >> pop.manpowerRate >> pop.upkeepTimer >> pop.upkeepInterval
                   >> pop.foodPackageUpkeep >> pop.hasFood;
                if (version >= 5)
                    in >> pop.populationCap;
                if (version >= 8)
                {
                    int foodSupplyAmount = 0;
                    in >> pop.foodSupplyLevel >> pop.foodBuffer.bufferSize >> foodSupplyAmount;
                    pop.foodBuffer.Clear();
                    pop.foodBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, pop.foodBuffer.bufferSize};
                    pop.foodBuffer.SetStoredAmount(foodSupplyAmount);
                    pop.hasFood = pop.foodSupplyLevel > 0.0;
                }
                int householdAmount = 0;
                int urbanAmount = 0;
                in >> pop.settlementLevel
                   >> pop.householdSupplyLevel >> pop.householdGoodsBuffer.bufferSize >> householdAmount
                   >> pop.urbanSupplyLevel >> pop.urbanGoodsBuffer.bufferSize >> urbanAmount;
                if (version >= 33)
                {
                    in >> pop.householdUpkeepTimer >> pop.urbanUpkeepTimer;
                }
                else
                {
                    // Before v33 all active Village supplies shared the food
                    // timer, so preserve that phase when migrating an old save.
                    pop.householdUpkeepTimer = pop.upkeepTimer;
                    pop.urbanUpkeepTimer = pop.upkeepTimer;
                }
                if (version >= 58)
                {
                    in >> pop.assignedResidents;
                    int hasAssignedResidents = 0;
                    in >> hasAssignedResidents;
                    if (!in || !std::isfinite(pop.assignedResidents) || pop.assignedResidents < 0.0 ||
                        (hasAssignedResidents != 0 && hasAssignedResidents != 1))
                        return false;
                    pop.hasAssignedResidents = hasAssignedResidents != 0;
                }
                pop.householdGoodsBuffer.Clear();
                pop.householdGoodsBuffer = ResourceBuffer{
                    ResourceType::HOUSEHOLD_GOODS, pop.householdGoodsBuffer.bufferSize};
                pop.householdGoodsBuffer.SetStoredAmount(householdAmount);
                pop.urbanGoodsBuffer.Clear();
                pop.urbanGoodsBuffer = ResourceBuffer{
                    ResourceType::URBAN_GOODS, pop.urbanGoodsBuffer.bufferSize};
                pop.urbanGoodsBuffer.SetStoredAmount(urbanAmount);
                pop.SetSettlementLevel(pop.settlementLevel);
            }
            else if (tag == "VIL_DEBT")
            {
                auto* population = placed->GetComponent<PopulationComponent>();
                if (population == nullptr) return false;
                int count = 0;
                in >> count;
                if (!in || count < 0 || count > 8)
                    return false;
                population->supplyDebt.clear();
                for (int n = 0; n < count; n++)
                {
                    int resourceType = 0;
                    double debt = 0.0;
                    in >> resourceType >> debt;
                    if (!in || resourceType < 0 || resourceType > 255 ||
                        !std::isfinite(debt) || debt < 0.0)
                        return false;
                    population->supplyDebt[static_cast<ResourceType>(resourceType)] = debt;
                }
            }
            else if (tag == "RECRUIT")
            {
                auto* recruitment = placed->GetComponent<RecruitmentComponent>();
                if (recruitment == nullptr) return false;

                int count = 0;
                in >> count;
                recruitment->queue.clear();
                for (int n = 0; n < count; n++)
                {
                    std::string unitDefId;
                    double total = 0.0, remaining = 0.0;
                    int resourcesReady = 1;
                    in >> tag >> std::quoted(unitDefId) >> total >> remaining >> resourcesReady;
                    if (tag != "RQ") return false;
                    recruitment->queue.push_back(RecruitmentQueueEntry{unitDefId, total, remaining, resourcesReady != 0});
                }
            }
            else if (tag == "UPG")
            {
                auto* upgrade = placed->GetComponent<UpgradeComponent>();
                if (upgrade == nullptr) return false;
                int isUpgrading = 0;
                in >> upgrade->level >> isUpgrading >> upgrade->upgradeRemaining;
                upgrade->isUpgrading = isUpgrading != 0;
                if (auto* population = placed->GetComponent<PopulationComponent>())
                    population->SetSettlementLevel(upgrade->level);
            }
            else if (tag == "ROADPRIORITY")
            {
                auto* road = placed->GetComponent<RoadComponent>();
                if (road == nullptr)
                    return false;
                int resourceType = 0;
                in >> resourceType;
                if (!in || resourceType < 0 || resourceType > 255)
                    return false;
                const ResourceType resource = static_cast<ResourceType>(resourceType);
                if (!RoadComponent::IsValidPriorityResource(resource))
                    return false;
                road->SetPriorityResource(resource);
            }
            else if (tag == "DEFENSE")
            {
                auto* coverage = placed->GetComponent<DefenseCoverageComponent>();
                auto* garrison = placed->GetComponent<GarrisonComponent>();
                auto* upkeep = placed->GetComponent<GarrisonUpkeepComponent>();
                auto* safety = placed->GetComponent<SafetyComponent>();
                double radius = 0.0;
                double protection = 0.0;
                std::string requiredState;
                int capacity = 0;
                double timer = 0.0;
                std::int64_t debtMicros = 0;
                double interval = 0.0;
                double packageSize = 0.0;
                int requested = 0;
                int status = 0;
                double resilience = 0.0;
                int raidDestructible = 0;
                int raidStockLossTarget = 0;
                in >> radius >> protection >> std::quoted(requiredState) >> capacity >> timer
                   >> debtMicros >> interval >> packageSize >> requested >> status >> resilience
                   >> raidDestructible >> raidStockLossTarget;
                if (!in || coverage == nullptr || garrison == nullptr || upkeep == nullptr || safety == nullptr ||
                    !std::isfinite(radius) || !std::isfinite(protection) || radius < 0.0 || protection < 0.0 ||
                    capacity < 0 || !std::isfinite(timer) || timer < 0.0 || debtMicros < 0 ||
                    !std::isfinite(interval) || interval <= 0.0 || !std::isfinite(packageSize) || packageSize <= 0.0 ||
                    requested < 0 || status < static_cast<int>(GarrisonSupplyStatus::Supplied) ||
                    status > static_cast<int>(GarrisonSupplyStatus::RequestPending) ||
                    !std::isfinite(resilience) || resilience < 0.0 || resilience > 1.0 ||
                    (raidDestructible != 0 && raidDestructible != 1) ||
                    (raidStockLossTarget != 0 && raidStockLossTarget != 1))
                    return false;
                coverage->radius = radius;
                coverage->baseProtection = protection;
                coverage->requiredState = std::move(requiredState);
                garrison->capacity = capacity;
                upkeep->timer = timer;
                upkeep->debtMicros = debtMicros;
                upkeep->intervalSeconds = interval;
                upkeep->packageSize = packageSize;
                upkeep->requestedAmount = requested;
                upkeep->supplyStatus = static_cast<GarrisonSupplyStatus>(status);
                safety->intrinsicResilience = resilience;
                safety->raidDestructible = raidDestructible != 0;
                safety->raidStockLossTarget = raidStockLossTarget != 0;
            }
            else
            {
                return false;
            }
        }
    }
        return true;
    };

    if (!loadMapState(primaryMap, expectedTileCount))
        return false;

    int provinceMapCount = 0;
    in >> tag >> provinceMapCount;
    if (tag != "PROVINCE_MAPS" ||
        !PersistenceLimits::IsCountInRange(provinceMapCount, PersistenceLimits::MaxProvinceMaps - 1))
        return false;
    std::set<ProvinceId> loadedProvinceMaps;
    if (playerHandler.players.contains(0))
        loadedProvinceMaps.insert(playerHandler.players.at(0)->homeProvinceId);
    std::size_t totalCampaignTiles = expectedTileCount;
    for (int i = 0; i < provinceMapCount; ++i)
    {
        std::uint64_t provinceIdValue = 0;
        MapParameters provinceMapParams = primaryMapParams;
        int presetValue = 0;
        in >> tag >> provinceIdValue >> provinceMapParams.sizeX >> provinceMapParams.sizeY
           >> provinceMapParams.seed >> presetValue >> provinceMapParams.resourceDensity
           >> provinceMapParams.resourceFieldSize >> provinceMapParams.resourceRichness
           >> provinceMapParams.aiOpponentCount >> provinceMapParams.aiDifficulty
           >> provinceMapParams.debugMode;
        if (!in || tag != "PROVINCE_MAP" || provinceIdValue == InvalidProvinceId ||
            provinceIdValue > std::numeric_limits<ProvinceId>::max())
            return false;
        if (!IsValidMapSizePresetValue(presetValue))
            return false;
        provinceMapParams.sizePreset = static_cast<MapSizePreset>(presetValue);
        std::size_t provinceTileCount = 0;
        if (!PersistenceLimits::CheckedArea(provinceMapParams.sizeX, provinceMapParams.sizeY,
                                            provinceTileCount))
            return false;
        const ProvinceId provinceId = static_cast<ProvinceId>(provinceIdValue);
        if (!loadedProvinceMaps.insert(provinceId).second)
            return false;
        auto* province = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(provinceId));
        if (province == nullptr || province->GetSimulation() == nullptr ||
            !province->GetSimulation()->OwnsTileMap())
            return false;
        province->GetSimulation()->GetTileMap().params = provinceMapParams;
        if (!loadMapState(province->GetSimulation()->GetTileMap(), provinceTileCount))
            return false;
        in >> tag;
        if (tag != "ENDPROVINCE_MAP")
            return false;
        const std::size_t loadedProvinceTileCount = province->GetSimulation()->GetTileMap().tilemap.size();
        if (loadedProvinceTileCount > PersistenceLimits::MaxCampaignTiles - totalCampaignTiles)
            return false;
        totalCampaignTiles += loadedProvinceTileCount;
    }

    int shipmentProvinceCount = 0;
    in >> tag >> shipmentProvinceCount;
    if (!in || tag != "SHIPMENT_PROVINCES" ||
        !PersistenceLimits::IsCountInRange(shipmentProvinceCount,
                                           PersistenceLimits::MaxProvinceMaps))
        return false;
    std::set<ProvinceId> shipmentProvinceIds;
    std::size_t totalShipmentCount = 0;
    restoredProvinceShipments.reserve(static_cast<std::size_t>(shipmentProvinceCount));
    for (int provinceIndex = 0; provinceIndex < shipmentProvinceCount; ++provinceIndex)
    {
        std::uint64_t provinceIdValue = 0;
        PendingProvinceShipments pending;
        int shipmentCount = 0;
        in >> tag >> provinceIdValue >> pending.nextShipmentId >> shipmentCount;
        if (!in || tag != "SHIPMENT_PROVINCE" ||
            provinceIdValue == InvalidProvinceId ||
            provinceIdValue > std::numeric_limits<ProvinceId>::max() ||
            pending.nextShipmentId == 0 ||
            !PersistenceLimits::IsCountInRange(shipmentCount,
                                               PersistenceLimits::MaxActiveShipments) ||
            static_cast<std::size_t>(shipmentCount) >
                PersistenceLimits::MaxActiveShipments - totalShipmentCount)
            return false;
        pending.provinceId = static_cast<ProvinceId>(provinceIdValue);
        auto* province = globalMap.FindBuildableProvince(pending.provinceId);
        if (!shipmentProvinceIds.insert(pending.provinceId).second || province == nullptr ||
            province->GetOwnerId() == InvalidPlayerId || province->GetSimulation() == nullptr)
            return false;

        totalShipmentCount += static_cast<std::size_t>(shipmentCount);
        pending.shipments.reserve(static_cast<std::size_t>(shipmentCount));
        std::set<ShipmentId> shipmentIds;
        for (int shipmentIndex = 0; shipmentIndex < shipmentCount; ++shipmentIndex)
        {
            ResourceShipment shipment;
            int typeValue = 0;
            int stateValue = 0;
            int pathCount = 0;
            in >> tag >> shipment.id >> typeValue >> shipment.quantity
               >> shipment.sourceBuildingId >> shipment.targetBuildingId
               >> shipment.currentPathStep >> shipment.elapsedTime
               >> shipment.transportTime >> stateValue >> pathCount;
            if (!in || tag != "SHIPMENT" || shipment.id == 0 ||
                !shipmentIds.insert(shipment.id).second || shipment.id >= pending.nextShipmentId ||
                typeValue < 0 || typeValue > static_cast<int>(ResourceType::CATAPULT) ||
                shipment.quantity != 1 || shipment.sourceBuildingId < 0 ||
                shipment.targetBuildingId < 0 || shipment.currentPathStep < 0 ||
                !std::isfinite(shipment.elapsedTime) || shipment.elapsedTime < 0.0 ||
                !std::isfinite(shipment.transportTime) || shipment.transportTime < 0.0 ||
                stateValue != static_cast<int>(ResourceShipmentState::InTransit) ||
                !PersistenceLimits::IsCountInRange(pathCount,
                                                   PersistenceLimits::MaxRouteTiles) ||
                pathCount < 2 || shipment.currentPathStep >= pathCount)
                return false;
            shipment.type = static_cast<ResourceType>(typeValue);
            shipment.state = static_cast<ResourceShipmentState>(stateValue);
            shipment.pathTileIds.resize(static_cast<std::size_t>(pathCount));
            for (int& tileId : shipment.pathTileIds)
                if (!(in >> tileId) || tileId < 0)
                    return false;
            pending.shipments.push_back(std::move(shipment));
        }
        in >> tag;
        if (!in || tag != "ENDSHIPMENT_PROVINCE")
            return false;
        restoredProvinceShipments.push_back(std::move(pending));
    }
    in >> tag;
    if (!in || tag != "ENDSHIPMENTS")
        return false;

    in >> tag >> restoredNextJourneyId;
    int journeyCount = 0;
    in >> journeyCount;
    if (!in || tag != "JOURNEYS" || restoredNextJourneyId == InvalidWorldJourneyId ||
        !PersistenceLimits::IsCountInRange(journeyCount,
                                            PersistenceLimits::MaxWorldJourneys))
        return false;
    for (int index = 0; index < journeyCount; ++index)
    {
        WorldJourney journey;
        int status = 0;
        int kind = 0;
        int legPlanCount = 0;
        int payloadKind = 0;
        in >> tag >> journey.id >> journey.ownerId >> journey.sourceProvinceId >>
            journey.targetProvinceId >> status >> journey.currentLeg >> journey.startTick >>
            journey.legCompletionTick >> journey.deterministicAttemptCounter >>
            kind >> journey.rules.baseLegDurationTicks >> journey.rules.routeTravelSpeedMultiplier >>
            journey.rules.speedProfile.baseUnitsPerMinute >>
            journey.rules.speedProfile.moverSpeedBasisPoints >>
            journey.rules.speedProfile.playerRouteSpeedBasisPoints >>
            journey.rules.speedProfile.operationSpeedBasisPoints >>
            journey.rules.speedProfile.extraLegDistanceBasisPoints >> legPlanCount;
        if (!in || tag != "JOURNEY" || journey.id == InvalidWorldJourneyId ||
            status < static_cast<int>(WorldJourneyStatus::Planned) ||
            status > static_cast<int>(WorldJourneyStatus::Cancelled) ||
            kind < static_cast<int>(WorldJourneyKind::Unknown) ||
            kind > static_cast<int>(WorldJourneyKind::ArmyTransfer) ||
            !PersistenceLimits::IsCountInRange(
                legPlanCount, PersistenceLimits::MaxJourneyPathConnections) ||
            !std::isfinite(journey.rules.routeTravelSpeedMultiplier) ||
            journey.rules.routeTravelSpeedMultiplier <= 0.0 ||
            journey.rules.speedProfile.baseUnitsPerMinute <= 0 ||
            journey.rules.speedProfile.moverSpeedBasisPoints <= 0 ||
            journey.rules.speedProfile.playerRouteSpeedBasisPoints <= 0 ||
            journey.rules.speedProfile.operationSpeedBasisPoints <= 0 ||
            journey.rules.speedProfile.extraLegDistanceBasisPoints <= 0)
            return false;
        journey.status = static_cast<WorldJourneyStatus>(status);
        journey.kind = static_cast<WorldJourneyKind>(kind);
        std::set<ProvinceConnectionId> uniqueConnections;
        if (legPlanCount <= 0)
            return false;
        journey.legPlan.resize(static_cast<std::size_t>(legPlanCount));
        for (auto& leg : journey.legPlan)
        {
            in >> leg.connectionId >> leg.lengthUnits >> leg.routeTimeBasisPoints
               >> leg.routeLevelAtStart >> leg.incidentReductionBasisPoints >> leg.durationTicks;
            if (!in || leg.connectionId == InvalidProvinceConnectionId ||
                !uniqueConnections.insert(leg.connectionId).second ||
                leg.lengthUnits <= 0 || leg.routeTimeBasisPoints <= 0 ||
                leg.routeLevelAtStart < 0 || leg.incidentReductionBasisPoints < 0 ||
                leg.incidentReductionBasisPoints > 10000 || leg.durationTicks == 0)
                return false;
        }
        auto readLoadoutResources = [&in](std::vector<ResourceAmount>& resources)
        {
            int count = 0;
            in >> count;
            if (!in || count < 0 || count > 16)
                return false;
            resources.resize(static_cast<std::size_t>(count));
            ResourceType previous = ResourceType::Null;
            for (auto& resource : resources)
            {
                int type = 0;
                in >> type >> resource.amount;
                resource.type = static_cast<ResourceType>(type);
                if (!in || type < 0 || type == static_cast<int>(ResourceType::Null) ||
                    type > static_cast<int>(ResourceType::CATAPULT) || resource.amount <= 0 ||
                    (previous != ResourceType::Null && static_cast<int>(previous) >= type))
                    return false;
                previous = resource.type;
            }
            return true;
        };
        if (!readLoadoutResources(journey.loadout.resources) ||
            !readLoadoutResources(journey.loadout.minimumResources))
            return false;
        int modifierCount = 0;
        in >> journey.loadout.supplyRatioBasisPoints >> modifierCount;
        if (!in || journey.loadout.supplyRatioBasisPoints < 10000 ||
            journey.loadout.supplyRatioBasisPoints > 20000 || modifierCount < 0 ||
            modifierCount > 16)
            return false;
        journey.loadout.modifiers.resize(static_cast<std::size_t>(modifierCount));
        for (auto& modifier : journey.loadout.modifiers)
        {
            int stat = 0;
            in >> stat >> modifier.multiplierBasisPoints >> modifier.source;
            if (!in || stat < 0 || stat >= static_cast<int>(BalanceStat::Count) ||
                modifier.multiplierBasisPoints <= 0 ||
                modifier.multiplierBasisPoints > 20000 || modifier.source.size() > 128)
                return false;
            modifier.stat = static_cast<BalanceStat>(stat);
        }
        if (!journey.loadout.IsSortedUnique())
            return false;
        in >> payloadKind;
        if (!in || payloadKind < 0 || payloadKind > 5)
            return false;
        if (payloadKind == 0 || payloadKind == 2)
        {
            int unitCount = 0;
            in >> unitCount;
            if (!in || !PersistenceLimits::IsCountInRange(unitCount, PersistenceLimits::MaxExpeditionUnits))
                return false;
            std::vector<int> unitIds(static_cast<std::size_t>(unitCount));
            std::set<int> uniqueUnits;
            for (int& unitId : unitIds)
            {
                in >> unitId;
                if (!in || unitId <= 0 || !uniqueUnits.insert(unitId).second)
                    return false;
            }
            journey.payload = payloadKind == 0 ? WorldJourneyPayload{ScoutParty{std::move(unitIds)}}
                                                : WorldJourneyPayload{ArmyParty{std::move(unitIds)}};
        }
        else if (payloadKind == 1)
        {
            int offerType = 0;
            int requestType = 0;
            int amount = 0;
            in >> offerType >> requestType >> amount;
            if (!in || offerType < 0 || offerType > static_cast<int>(ResourceType::CATAPULT) ||
                requestType < 0 || requestType > static_cast<int>(ResourceType::CATAPULT) || amount < 0)
                return false;
            journey.payload = TradeCargo{static_cast<ResourceType>(offerType),
                                          static_cast<ResourceType>(requestType), amount};
        }
        else if (payloadKind == 3)
        {
            int householdCount = 0;
            in >> householdCount;
            if (!in || householdCount < 0)
                return false;
            journey.payload = Colonists{householdCount};
        }
        else if (payloadKind == 4)
        {
            int cargoCount = 0;
            in >> cargoCount;
            if (!in || !PersistenceLimits::IsCountInRange(cargoCount, 16))
                return false;
            ResourceConvoy convoy;
            std::set<ResourceType> uniqueTypes;
            for (int cargoIndex = 0; cargoIndex < cargoCount; ++cargoIndex)
            {
                int resourceType = 0;
                int amount = 0;
                in >> resourceType >> amount;
                const ResourceType type = static_cast<ResourceType>(resourceType);
                if (!in || resourceType < 0 || resourceType > static_cast<int>(ResourceType::CATAPULT) ||
                    amount <= 0 || !uniqueTypes.insert(type).second)
                    return false;
                convoy.cargo.push_back({type, amount});
            }
            if (convoy.cargo.empty())
                return false;
            journey.payload = std::move(convoy);
        }
        else
        {
            int destinationBarracksId = 0;
            int unitCount = 0;
            in >> destinationBarracksId >> unitCount;
            if (!in || destinationBarracksId <= 0 ||
                !PersistenceLimits::IsCountInRange(unitCount, PersistenceLimits::MaxExpeditionUnits))
                return false;
            ArmyTransferParty transfer;
            transfer.destinationBarracksBuildingId = destinationBarracksId;
            transfer.unitInstanceIds.resize(static_cast<std::size_t>(unitCount));
            std::set<int> uniqueUnits;
            for (int& unitId : transfer.unitInstanceIds)
            {
                in >> unitId;
                if (!in || unitId <= 0 || !uniqueUnits.insert(unitId).second)
                    return false;
            }
            if (transfer.unitInstanceIds.empty())
                return false;
            journey.payload = std::move(transfer);
        }
        restoredJourneys.push_back(std::move(journey));
    }

    in >> tag >> restoredNextTradeOrderId;
    int tradeCount = 0;
    in >> tradeCount;
    if (!in || tag != "TRADES" || restoredNextTradeOrderId == 0 ||
        !PersistenceLimits::IsCountInRange(tradeCount, static_cast<int>(MaxActiveTradeOrders)))
        return false;
    std::set<std::uint64_t> uniqueTradeIds;
    for (int index = 0; index < tradeCount; ++index)
    {
        TradeOrder order;
        int offerType = 0;
        int requestType = 0;
        int mode = 0;
        int status = 0;
        in >> tag >> order.id >> order.playerId >> order.originProvinceId
            >> order.cityProvinceId >> offerType >> requestType
            >> order.request.requestedAmount >> order.request.offeredAmount
            >> order.offeredAmount >> order.cargoAmount >> order.cityRevisionAtStart
            >> order.journeyId >> mode >> status;
        if (!in || tag != "TRADE" || order.id == 0 ||
            !uniqueTradeIds.insert(order.id).second ||
            order.playerId == InvalidPlayerId || order.originProvinceId == InvalidProvinceId ||
            order.cityProvinceId == InvalidProvinceId || order.journeyId == InvalidWorldJourneyId ||
            offerType < 0 || offerType > static_cast<int>(ResourceType::CATAPULT) ||
            requestType < 0 || requestType > static_cast<int>(ResourceType::CATAPULT) ||
            offerType == requestType || order.request.requestedAmount <= 0 ||
            order.request.offeredAmount < 0 || order.offeredAmount <= 0 ||
            order.cargoAmount <= 0 || order.cityRevisionAtStart == 0 ||
            mode < static_cast<int>(TradeMode::Coin) ||
            mode > static_cast<int>(TradeMode::Barter) ||
            status != static_cast<int>(TradeOrderStatus::InTransit) &&
                status != static_cast<int>(TradeOrderStatus::AwaitingUnload))
            return false;
        order.request.offerType = static_cast<ResourceType>(offerType);
        order.request.requestType = static_cast<ResourceType>(requestType);
        order.request.mode = static_cast<TradeMode>(mode);
        order.status = static_cast<TradeOrderStatus>(status);
        restoredTradeOrders.push_back(std::move(order));
    }
    in >> tag;
    if (tag != "END_TRADES")
        return false;
    for (const auto& order : restoredTradeOrders)
        if (order.id >= restoredNextTradeOrderId)
            return false;

    in >> tag >> restoredNextBattleId;
    int battleCount = 0;
    int battleReportCount = 0;
    in >> battleCount >> battleReportCount;
    if (!in || tag != "BATTLES" || restoredNextBattleId == InvalidBattleId ||
        !PersistenceLimits::IsCountInRange(
            battleCount, BattleLifecycleSystem::MaxBattleRecords) ||
        !PersistenceLimits::IsCountInRange(
            battleReportCount, BattleLifecycleSystem::MaxBattleReports))
        return false;
    const auto readBattleIds = [&in, &tag](const char* expectedTag,
                                           std::vector<int>& ids) -> bool
    {
        int count = 0;
        if (!(in >> tag >> count) || tag != expectedTag ||
            !PersistenceLimits::IsCountInRange(count, PersistenceLimits::MaxExpeditionUnits))
            return false;
        ids.resize(static_cast<std::size_t>(count));
        std::set<int> uniqueIds;
        for (int& id : ids)
        {
            in >> id;
            if (!in || id <= 0 || !uniqueIds.insert(id).second)
                return false;
        }
        return true;
    };
    const auto readBattleSide = [&in, &tag](const char* expectedTag,
                                             BattleSideSnapshot& side) -> bool
    {
        int count = 0;
        if (!(in >> tag >> side.ownerId >> side.defensiveBonus >> count) || tag != expectedTag ||
            !PersistenceLimits::IsCountInRange(count, PersistenceLimits::MaxExpeditionUnits) ||
            !std::isfinite(side.defensiveBonus) || side.defensiveBonus < 0.0)
            return false;
        side.units.resize(static_cast<std::size_t>(count));
        std::set<int> uniqueIds;
        for (auto& unit : side.units)
        {
            if (!(in >> tag >> unit.instanceId >> std::quoted(unit.unitDefId) >> unit.effectiveFieldAttack) ||
                tag != "SIDE_UNIT" || unit.instanceId <= 0 || unit.unitDefId.empty() ||
                !std::isfinite(unit.effectiveFieldAttack) || unit.effectiveFieldAttack < 0.0 ||
                !uniqueIds.insert(unit.instanceId).second)
                return false;
        }
        return true;
    };
    const auto readBattleOutcome = [&in, &tag](std::optional<BattleOutcome>& destination) -> bool
    {
        int hasOutcome = 0;
        if (!(in >> tag >> hasOutcome) || tag != "OUTCOME" || (hasOutcome != 0 && hasOutcome != 1))
            return false;
        if (hasOutcome == 0)
        {
            destination.reset();
            return true;
        }
        BattleOutcome outcome;
        int valid = 0;
        int winner = 0;
        int crushing = 0;
        int attackerLossCount = 0;
        int defenderLossCount = 0;
        if (!(in >> valid >> winner >> outcome.attackerStrength >> outcome.defenderStrength >>
              outcome.attackerCasualtyBudget >> outcome.defenderCasualtyBudget >> outcome.lootValue >>
              crushing >> outcome.durationTicks >> attackerLossCount) || valid < 0 || valid > 1 ||
            winner < static_cast<int>(BattleWinner::Invalid) || winner > static_cast<int>(BattleWinner::Defender) ||
            (crushing != 0 && crushing != 1) || !std::isfinite(outcome.attackerStrength) ||
            !std::isfinite(outcome.defenderStrength) || outcome.attackerStrength < 0.0 ||
            outcome.defenderStrength < 0.0 || outcome.attackerCasualtyBudget < 0 ||
            outcome.defenderCasualtyBudget < 0 || !std::isfinite(outcome.lootValue) ||
            outcome.lootValue < 0.0 ||
            !PersistenceLimits::IsCountInRange(attackerLossCount, PersistenceLimits::MaxExpeditionUnits))
            return false;
        outcome.valid = valid != 0;
        outcome.winner = static_cast<BattleWinner>(winner);
        outcome.crushingVictory = crushing != 0;
        outcome.attackerLostUnitIds.resize(static_cast<std::size_t>(attackerLossCount));
        std::set<int> uniqueLosses;
        for (int& id : outcome.attackerLostUnitIds)
        {
            in >> id;
            if (!in || id <= 0 || !uniqueLosses.insert(id).second)
                return false;
        }
        in >> defenderLossCount;
        if (!in || !PersistenceLimits::IsCountInRange(defenderLossCount, PersistenceLimits::MaxExpeditionUnits))
            return false;
        outcome.defenderLostUnitIds.resize(static_cast<std::size_t>(defenderLossCount));
        uniqueLosses.clear();
        for (int& id : outcome.defenderLostUnitIds)
        {
            in >> id;
            if (!in || id <= 0 || !uniqueLosses.insert(id).second)
                return false;
        }
        destination = std::move(outcome);
        return true;
    };
    for (int index = 0; index < battleCount; ++index)
    {
        BattleInstance battle;
        int status = 0;
        int raid = 0;
        in >> tag >> battle.id >> battle.attackerId >> battle.defenderId >>
            battle.sourceProvinceId >> battle.targetProvinceId >> battle.journeyId >> status >>
            battle.startTick >> battle.endTick >> raid >> battle.raidStrength >>
            battle.rules.baseLossFraction >> battle.rules.casualtyCapFraction >>
            battle.rules.drawBand >> battle.rules.crushingRatio >> battle.rules.lootFraction >>
            battle.rules.durationTicks;
        if (!in || tag != "BATTLE" || battle.id == InvalidBattleId ||
            status < static_cast<int>(BattleLifecycleStatus::InTransit) ||
            status > static_cast<int>(BattleLifecycleStatus::Cancelled) ||
            (raid != 0 && raid != 1) || battle.raidStrength < 0 ||
            !std::isfinite(battle.rules.baseLossFraction) || battle.rules.baseLossFraction < 0.0 ||
            battle.rules.baseLossFraction >= 1.0 ||
            !std::isfinite(battle.rules.casualtyCapFraction) || battle.rules.casualtyCapFraction < 0.0 ||
            battle.rules.casualtyCapFraction >= 1.0 ||
            !std::isfinite(battle.rules.drawBand) || battle.rules.drawBand < 0.0 ||
            battle.rules.drawBand > 1.0 || !std::isfinite(battle.rules.crushingRatio) ||
            battle.rules.crushingRatio < 1.0 || !std::isfinite(battle.rules.lootFraction) ||
            battle.rules.lootFraction < 0.0 || battle.rules.lootFraction > 1.0 ||
            battle.rules.durationTicks == 0)
            return false;
        battle.status = static_cast<BattleLifecycleStatus>(status);
        battle.isRaid = raid != 0;
        if (!readBattleIds("ATTACK_IDS", battle.attackerUnitIds) ||
            !readBattleIds("DEFENDER_IDS", battle.defenderUnitIds) ||
            !readBattleSide("ATTACK_SIDE", battle.attackerSnapshot) ||
            !readBattleSide("DEFENDER_SIDE", battle.defenderSnapshot) ||
            !readBattleOutcome(battle.outcome))
            return false;
        in >> tag;
        if (tag != "END_BATTLE")
            return false;
        restoredBattles.push_back(std::move(battle));
    }
    for (int index = 0; index < battleReportCount; ++index)
    {
        BattleReport report;
        int transformed = 0;
        int cityDamaged = 0;
        int raid = 0;
        int destroyedCount = 0;
        int resourceCount = 0;
        in >> tag >> report.battleId >> report.attackerId >> report.defenderId >>
            report.sourceProvinceId >> report.targetProvinceId >> transformed >> cityDamaged >> raid >> destroyedCount;
        if (!in || tag != "REPORT" || report.battleId == InvalidBattleId ||
            (transformed != 0 && transformed != 1) || (cityDamaged != 0 && cityDamaged != 1) ||
            (raid != 0 && raid != 1) || !PersistenceLimits::IsCountInRange(destroyedCount, 64))
            return false;
        report.banditTransformed = transformed != 0;
        report.cityDamaged = cityDamaged != 0;
        report.raid = raid != 0;
        report.destroyedBuildingIds.resize(static_cast<std::size_t>(destroyedCount));
        for (int& id : report.destroyedBuildingIds)
            if (!(in >> id) || id <= 0)
                return false;
        in >> resourceCount;
        if (!in || !PersistenceLimits::IsCountInRange(resourceCount, PersistenceLimits::MaxBufferEntries))
            return false;
        for (int resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex)
        {
            int type = 0;
            int amount = 0;
            in >> type >> amount;
            if (!in || type < 0 || type > static_cast<int>(ResourceType::CATAPULT) || amount < 0 ||
                !report.lostResources.emplace(static_cast<ResourceType>(type), amount).second)
                return false;
        }
        std::optional<BattleOutcome> reportOutcome;
        if (!readBattleOutcome(reportOutcome) || !reportOutcome.has_value())
            return false;
        report.outcome = *reportOutcome;
        restoredBattleReports.push_back(std::move(report));
    }
    in >> tag;
    if (tag != "END_BATTLES")
        return false;

    in >> tag >> restoredNextEventId;
    int schedulerPlayerCount = 0;
    in >> schedulerPlayerCount;
    if (!in || tag != "EVENT_RUNTIME" || restoredNextEventId == InvalidWorldEventInstanceId ||
        !PersistenceLimits::IsCountInRange(schedulerPlayerCount, PersistenceLimits::MaxSupportedPlayers))
        return false;
    std::vector<std::pair<PlayerId, PeriodicEventSchedulerState>> restoredSchedulerStates;
    std::set<PlayerId> restoredSchedulerPlayers;
    for (int index = 0; index < schedulerPlayerCount; ++index)
    {
        PlayerId playerId = InvalidPlayerId;
        PeriodicEventSchedulerState schedulerState;
        int cooldownCount = 0;
        in >> tag >> playerId >> schedulerState.nextCheckTick >>
            schedulerState.nextAllowedEventTick >> schedulerState.attemptCounter >> cooldownCount;
        if (!in || tag != "EVENT_SCHEDULER" || playerId == InvalidPlayerId ||
            !playerHandler.players.contains(static_cast<int>(playerId)) ||
            !PersistenceLimits::IsCountInRange(cooldownCount, PersistenceLimits::MaxBufferEntries) ||
            !restoredSchedulerPlayers.insert(playerId).second)
            return false;
        std::string previousDefinitionId;
        for (int cooldownIndex = 0; cooldownIndex < cooldownCount; ++cooldownIndex)
        {
            std::string definitionId;
            std::uint64_t cooldownUntil = 0;
            in >> tag >> std::quoted(definitionId) >> cooldownUntil;
            const auto* definition = FindWorldEventDefinition(definitionId);
            if (!in || tag != "SCHEDULER_COOLDOWN" || definitionId.empty() ||
                definitionId.size() > PersistenceLimits::MaxStringBytes ||
                (!previousDefinitionId.empty() && definitionId <= previousDefinitionId) ||
                definition == nullptr ||
                definition->trigger != WorldEventTriggerDomain::ProvincePeriodic ||
                cooldownUntil < schedulerState.nextAllowedEventTick)
                return false;
            previousDefinitionId = definitionId;
            schedulerState.definitionCooldownUntil.emplace(std::move(definitionId), cooldownUntil);
        }
        restoredSchedulerStates.emplace_back(playerId, std::move(schedulerState));
    }
    int eventCount = 0;
    in >> tag >> eventCount;
    if (!in || tag != "EVENT_INSTANCES" ||
        !PersistenceLimits::IsCountInRange(
            eventCount, PersistenceLimits::MaxWorldEventInstances))
        return false;
    for (int index = 0; index < eventCount; ++index)
    {
        WorldEventInstance instance;
        int trigger = 0;
        int expired = 0;
        int raidStarted = 0;
        int journeyEffectApplied = 0;
        int appliedEffectCount = 0;
        in >> tag >> instance.id >> instance.ownerId >> instance.provinceId >>
            instance.secondaryProvinceId >> std::quoted(instance.definitionId) >> trigger >>
            instance.startTick >> instance.endTick >> instance.outcomeRoll >>
            instance.deterministicAttemptCounter >> instance.journeyId >> expired >> raidStarted >>
            journeyEffectApplied >> appliedEffectCount;
        if (!in || tag != "EVENT_INSTANCE" || instance.id == InvalidWorldEventInstanceId ||
            instance.definitionId.empty() || FindWorldEventDefinition(instance.definitionId) == nullptr ||
            trigger < 0 || trigger > static_cast<int>(WorldEventTriggerDomain::Raid) ||
            (expired != 0 && expired != 1) || (raidStarted != 0 && raidStarted != 1) ||
            (journeyEffectApplied != 0 && journeyEffectApplied != 1) ||
            !PersistenceLimits::IsCountInRange(appliedEffectCount, 32))
            return false;
        instance.trigger = static_cast<WorldEventTriggerDomain>(trigger);
        instance.expired = expired != 0;
        instance.raidStarted = raidStarted != 0;
        instance.journeyEffectApplied = journeyEffectApplied != 0;
        instance.effects = FindWorldEventDefinition(instance.definitionId)->effects;
        for (int effectIndex = 0; effectIndex < appliedEffectCount; ++effectIndex)
        {
            AppliedWorldEventEffect effect;
            WorldEventInstanceId appliedInstanceId = InvalidWorldEventInstanceId;
            int kind = 0;
            int resourceType = 0;
            int stat = 0;
            in >> tag >> appliedInstanceId >> kind >> resourceType >> effect.amount >> stat >> effect.additive >>
                effect.multiplier >> effect.durationTicks;
            if (!in || tag != "EVENT_APPLIED" || appliedInstanceId != instance.id ||
                kind < 0 || kind > static_cast<int>(AppliedWorldEventEffectKind::RaidStarted) || resourceType < 0 ||
                (resourceType != static_cast<int>(ResourceType::Null) &&
                 resourceType > static_cast<int>(ResourceType::CATAPULT)) ||
                stat < static_cast<int>(BalanceStat::BuildTime) ||
                stat > static_cast<int>(BalanceStat::ColonizationDuration) ||
                !std::isfinite(effect.additive) || !std::isfinite(effect.multiplier) ||
                effect.multiplier <= 0.0)
                return false;
            effect.kind = static_cast<AppliedWorldEventEffectKind>(kind);
            effect.resourceType = static_cast<ResourceType>(resourceType);
            effect.stat = static_cast<BalanceStat>(stat);
            instance.appliedEffects.push_back(effect);
        }
        restoredEvents.push_back(std::move(instance));
    }
    int eventFeedCount = 0;
    in >> tag >> eventFeedCount;
    if (!in || tag != "EVENT_FEED" ||
        !PersistenceLimits::IsCountInRange(
            eventFeedCount, PersistenceLimits::MaxWorldEventFeedEntries))
        return false;
    for (int index = 0; index < eventFeedCount; ++index)
    {
        WorldEventNotificationView notification;
        int trigger = 0;
        int expired = 0;
        int appliedEffectCount = 0;
        in >> tag >> notification.instanceId >> notification.ownerId >> notification.provinceId >>
            notification.secondaryProvinceId >> std::quoted(notification.definitionId) >>
            std::quoted(notification.title) >> std::quoted(notification.description) >> trigger >>
            notification.startTick >> notification.endTick >> notification.outcomeRoll >> expired;
        in >> appliedEffectCount;
        if (!in || tag != "EVENT_NOTIFICATION" || notification.instanceId == InvalidWorldEventInstanceId ||
            FindWorldEventDefinition(notification.definitionId) == nullptr ||
            trigger < 0 || trigger > static_cast<int>(WorldEventTriggerDomain::Raid) ||
            (expired != 0 && expired != 1) || notification.definitionId.size() > PersistenceLimits::MaxStringBytes ||
            notification.title.size() > PersistenceLimits::MaxStringBytes ||
            notification.description.size() > PersistenceLimits::MaxStringBytes ||
            !PersistenceLimits::IsCountInRange(appliedEffectCount, 32))
            return false;
        notification.trigger = static_cast<WorldEventTriggerDomain>(trigger);
        notification.expired = expired != 0;
        for (int effectIndex = 0; effectIndex < appliedEffectCount; ++effectIndex)
        {
            AppliedWorldEventEffect effect;
            WorldEventInstanceId appliedInstanceId = InvalidWorldEventInstanceId;
            int kind = 0;
            int resourceType = 0;
            int stat = 0;
            in >> tag >> appliedInstanceId >> kind >> resourceType >> effect.amount >> stat >> effect.additive >>
                effect.multiplier >> effect.durationTicks;
            if (!in || tag != "EVENT_FEED_APPLIED" || appliedInstanceId != notification.instanceId ||
                kind < 0 || kind > static_cast<int>(AppliedWorldEventEffectKind::RaidStarted) || resourceType < 0 ||
                (resourceType != static_cast<int>(ResourceType::Null) &&
                 resourceType > static_cast<int>(ResourceType::CATAPULT)) ||
                stat < static_cast<int>(BalanceStat::BuildTime) ||
                stat > static_cast<int>(BalanceStat::ColonizationDuration) ||
                !std::isfinite(effect.additive) || !std::isfinite(effect.multiplier) ||
                effect.multiplier <= 0.0)
                return false;
            effect.kind = static_cast<AppliedWorldEventEffectKind>(kind);
            effect.resourceType = static_cast<ResourceType>(resourceType);
            effect.stat = static_cast<BalanceStat>(stat);
            notification.appliedEffects.push_back(effect);
        }
        restoredEventNotifications.push_back(std::move(notification));
    }
    in >> tag;
    if (tag != "END_EVENT_RUNTIME")
        return false;

    if (loadedProvinceMaps.size() != static_cast<std::size_t>(provinceMapCount + 1) ||
        totalCampaignTiles > PersistenceLimits::MaxCampaignTiles)
        return false;

    if (!playerHandler.players.contains(0))
        return false;

    std::size_t expectedExtraProvinceMaps = 0;
    for (const ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr || province->GetOwnerId() == InvalidPlayerId)
            continue;
        if (!playerHandler.players.contains(province->GetOwnerId()) ||
            province->GetSimulation() == nullptr)
            return false;
        if (provinceId != playerHandler.players.at(0)->homeProvinceId)
        {
            if (!province->GetSimulation()->OwnsTileMap() ||
                !loadedProvinceMaps.contains(provinceId))
                return false;
            ++expectedExtraProvinceMaps;
        }
    }
    if (static_cast<std::size_t>(provinceMapCount) != expectedExtraProvinceMaps)
        return false;

    // Local building IDs are namespaced by owner and province. Recompute the
    // next counter from the loaded tilemaps so a post-load build cannot reuse
    // an existing ID even though each ProvinceSimulation starts at zero.
    for (const ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        auto* province = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr || province->GetSimulation() == nullptr)
            continue;
        auto& economy = province->GetSimulation()->GetEconomy();
        const int prefix = province->GetOwnerId() * 5'000'000 +
                           static_cast<int>(provinceId) * 300'000;
        int nextLocalId = 0;
        for (const auto& tile : economy.ownedTilemap.tilemap)
        {
            if (tile.building == nullptr || tile.building->id < prefix)
                continue;
            const int localId = tile.building->id - prefix;
            if (localId >= 0 && localId < 300'000)
                nextLocalId = std::max(nextLocalId, localId + 1);
        }
        economy.build.buildingId = nextLocalId;
    }

    for (auto& [playerId, player] : playerHandler.players)
    {
        if (player == nullptr)
            continue;
        auto* home = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(player->homeProvinceId));
        if (home == nullptr || home->GetSimulation() == nullptr)
            return false;
        ProvinceSimulation& simulation = *home->GetSimulation();
        const std::uint64_t savedProvinceTick = simulation.GetEconomy().simulationTick;
        simulation.RestoreSimulationTick(savedProvinceTick);
        player->SetActiveProvince(player->homeProvinceId);
        player->RebindTileMap(simulation.GetTileMap());
        simulation.GetEconomy().dataTracker.processedCommands = pendingCommandStats[playerId];
        for (const ProvinceId provinceId : globalMap.GetProvinceIds())
        {
            auto* province = globalMap.FindBuildableProvince(provinceId);
            if (province == nullptr || province->GetOwnerId() != playerId ||
                province->GetSimulation() == nullptr)
                continue;
            player->BindProvince(provinceId, *province->GetSimulation());
            player->RebindTileMap(provinceId, province->GetSimulation()->GetTileMap());
            const auto statsPlayerIt = pendingProvinceCommandStats.find(playerId);
            if (statsPlayerIt != pendingProvinceCommandStats.end())
            {
                const auto statsIt = statsPlayerIt->second.find(provinceId);
                if (statsIt != statsPlayerIt->second.end())
                    province->GetSimulation()->GetEconomy().dataTracker.processedCommands = statsIt->second;
                else if (provinceId == player->homeProvinceId)
                    province->GetSimulation()->GetEconomy().dataTracker.processedCommands = pendingCommandStats[playerId];
            }
            else if (provinceId == player->homeProvinceId)
                province->GetSimulation()->GetEconomy().dataTracker.processedCommands = pendingCommandStats[playerId];
        }
    }

    std::map<PlayerId, Player*> restoredPlayers;
    for (const auto& [playerId, player] : playerHandler.players)
        if (player != nullptr)
            restoredPlayers.emplace(playerId, player.get());
    std::set<std::pair<PlayerId, int>> journeyUnitOwners;
    for (const auto& journey : restoredJourneys)
    {
        if (!std::holds_alternative<ScoutParty>(journey.payload) &&
            !std::holds_alternative<ArmyParty>(journey.payload) &&
            !std::holds_alternative<ArmyTransferParty>(journey.payload))
            continue;
        const auto* playerIt = restoredPlayers.contains(journey.ownerId)
            ? restoredPlayers.at(journey.ownerId) : nullptr;
        if (playerIt == nullptr)
            return false;
        const auto checkUnit = [&](int unitId) -> bool
        {
            return playerIt->roster.FindUnit(unitId) != nullptr &&
                   journeyUnitOwners.insert({journey.ownerId, unitId}).second;
        };
        if (const auto* scout = std::get_if<ScoutParty>(&journey.payload))
        {
            for (const int unitId : scout->unitInstanceIds)
                if (!checkUnit(unitId))
                    return false;
        }
        else if (const auto* army = std::get_if<ArmyParty>(&journey.payload))
        {
            for (const int unitId : army->unitInstanceIds)
                if (!checkUnit(unitId))
                    return false;
        }
        else if (const auto* transfer = std::get_if<ArmyTransferParty>(&journey.payload))
        {
            for (const int unitId : transfer->unitInstanceIds)
                if (!checkUnit(unitId))
                    return false;
        }
    }
    std::string runtimeFailure;
    if (!armyJourneySystem.Restore(restoredNextJourneyId, std::move(restoredJourneys),
                                   globalMap, runtimeFailure) ||
        !battleSystem.Restore(restoredNextBattleId, std::move(restoredBattles),
                              std::move(restoredBattleReports), globalMap,
                              restoredPlayers, armyJourneySystem, runtimeFailure))
    {
        Log::Msg("[Persistence] runtime restore rejected: ", runtimeFailure);
        return false;
    }
    activeTradeOrders.clear();
    nextTradeOrderId = restoredNextTradeOrderId;
    for (auto& order : restoredTradeOrders)
    {
        const auto playerIt = restoredPlayers.find(order.playerId);
        const auto* source = globalMap.FindBuildableProvince(order.originProvinceId);
        const auto* city = dynamic_cast<const NeutralCityProvince*>(
            globalMap.FindProvince(order.cityProvinceId));
        const auto journeyIt = armyJourneySystem.GetJourneys().find(order.journeyId);
        if (playerIt == restoredPlayers.end() || playerIt->second == nullptr ||
            source == nullptr || source->GetOwnerId() != order.playerId ||
            source->GetSimulation() == nullptr || city == nullptr ||
            city->GetState().revision < order.cityRevisionAtStart ||
            journeyIt == armyJourneySystem.GetJourneys().end() ||
            journeyIt->second.ownerId != order.playerId ||
            journeyIt->second.sourceProvinceId != order.originProvinceId ||
            journeyIt->second.targetProvinceId != order.cityProvinceId ||
            (order.status == TradeOrderStatus::InTransit &&
             journeyIt->second.status != WorldJourneyStatus::InTransit) ||
            (order.status == TradeOrderStatus::AwaitingUnload &&
             journeyIt->second.status != WorldJourneyStatus::Succeeded &&
             journeyIt->second.status != WorldJourneyStatus::AwaitingUnload) ||
            !std::holds_alternative<TradeCargo>(journeyIt->second.payload) ||
            std::get<TradeCargo>(journeyIt->second.payload).offerType != order.request.offerType ||
            std::get<TradeCargo>(journeyIt->second.payload).requestType != order.request.requestType ||
            std::get<TradeCargo>(journeyIt->second.payload).amount != order.cargoAmount ||
            !activeTradeOrders.emplace(order.id, std::move(order)).second)
            return false;
    }
    eventSystem.SetCampaignSeed(restoredCampaignMap.seed);
    eventSystem.SetNextInstanceId(restoredNextEventId);
    for (auto& [playerId, schedulerState] : restoredSchedulerStates)
        if (!eventSystem.RestorePeriodicSchedulerState(playerId, std::move(schedulerState)))
            return false;
    for (auto& instance : restoredEvents)
    {
        if (globalMap.FindProvince(instance.provinceId) == nullptr ||
            (instance.secondaryProvinceId != InvalidProvinceId &&
             globalMap.FindProvince(instance.secondaryProvinceId) == nullptr) ||
            (instance.journeyId != InvalidWorldJourneyId &&
             !armyJourneySystem.GetJourneys().contains(instance.journeyId)) ||
            !eventSystem.RestoreInstance(globalMap, std::move(instance)))
            return false;
    }
    for (auto& notification : restoredEventNotifications)
        eventSystem.GetFeed().RestoreNotification(std::move(notification));

    for (const auto& operation : restoredColonizations)
    {
        const auto playerIt = playerHandler.players.find(static_cast<int>(operation.playerId));
        const auto* source = globalMap.FindBuildableProvince(operation.sourceProvinceId);
        const auto* target = globalMap.FindBuildableProvince(operation.targetProvinceId);
        if (playerIt == playerHandler.players.end() || playerIt->second == nullptr ||
            source == nullptr || target == nullptr || source->GetOwnerId() != operation.playerId ||
            source->GetSimulation() == nullptr || target->GetOwnerId() != InvalidPlayerId ||
            target->GetSimulation() != nullptr ||
            target->GetKnowledge(operation.playerId) < ProvinceKnowledgeLevel::Scouted ||
            !armyJourneySystem.GetJourneys().contains(operation.journeyId) ||
            armyJourneySystem.GetJourneys().at(operation.journeyId).ownerId != operation.playerId ||
            armyJourneySystem.GetJourneys().at(operation.journeyId).sourceProvinceId != operation.sourceProvinceId ||
            armyJourneySystem.GetJourneys().at(operation.journeyId).targetProvinceId != operation.targetProvinceId ||
            !std::holds_alternative<Colonists>(
                armyJourneySystem.GetJourneys().at(operation.journeyId).payload) ||
            pendingColonizations.contains(operation.targetProvinceId))
            return false;
        pendingColonizations.emplace(operation.targetProvinceId, operation);
    }

    // Upgrade modifiers are derived state, just like technology and focus
    // modifiers. Rebuild them only after every building has been registered;
    // doing it while parsing one UPG block makes the result depend on the
    // order of serialized buildings and cannot remove stale entries from a
    // reused player state.
    for (auto& [id, player] : playerHandler.players)
    {
        (void)id;
        player->RefreshUpgradeModifiers();
    }

    for (auto& [id, player] : playerHandler.players)
    {
        if (player == nullptr)
            return false;
        for (const ProvinceId provinceId : globalMap.GetProvinceIds())
        {
            auto* province = globalMap.FindBuildableProvince(provinceId);
            if (province == nullptr || province->GetOwnerId() != id ||
                province->GetSimulation() == nullptr)
                continue;
            TileMap& provinceMap = province->GetSimulation()->GetTileMap();
            ProvinceEconomy* economy = player->GetProvinceEconomy(provinceId);
            if (economy == nullptr || economy->roadNetwork == nullptr)
                return false;
            for (auto& tile : provinceMap.tilemap)
                if (tile.building != nullptr && tile.building->owner == player.get() &&
                    !tile.building->IsUnderConstruction())
                    economy->roadNetwork->UpdateNavMap(tile.id, tile.building.get());
        }
    }

    for (const auto& [id, player] : playerHandler.players)
    {
        (void)id;
        if (player == nullptr)
            return false;
        for (const auto& [groupId, group] : player->taskGroups.GetGroups())
        {
            (void)groupId;
            ProvinceEconomy* economy = player->GetProvinceEconomy(group.stationProvinceId);
            Building* barracks = economy != nullptr && economy->tilemap != nullptr
                ? economy->tilemap->GetBuilding(group.homeBarracksBuildingId) : nullptr;
            if (barracks == nullptr || barracks->owner != player.get() ||
                barracks->buildingType != BuildingType::Barracks)
                return false;
        }
        for (const auto& [unitId, unit] : player->roster.units)
        {
            (void)unitId;
            if (unit.taskGroupId == InvalidTaskGroupId)
                continue;
            const TaskGroup* group = player->taskGroups.Find(unit.taskGroupId);
            if (group == nullptr || unit.assignment.kind == UnitAssignmentKind::Unassigned ||
                (unit.assignment.kind == UnitAssignmentKind::BarracksReserve &&
                 (unit.assignment.provinceId != group->stationProvinceId ||
                  unit.assignment.buildingId != group->homeBarracksBuildingId)))
                return false;
        }
    }

    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        auto* province = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr || province->GetSimulation() == nullptr)
            continue;
        TileMap& provinceMap = province->GetSimulation()->GetTileMap();
        for (auto& tile : provinceMap.tilemap)
        {
            if (tile.building != nullptr && tile.building->buildingType == BuildingType::Road)
                provinceMap.RefreshRoadTilesAround(provinceMap.GetCoordsFromId(tile.id));
        }
        provinceMap.terrainDirty = true;
        provinceMap.buildingsDirty = true;
    }

    for (const auto& pending : pendingConnections)
    {
        if (pending.map == nullptr)
            return false;
        Building* source = pending.map->GetBuilding(pending.sourcePosition);
        Building* target = pending.targetPosition >= 0 ? pending.map->GetBuilding(pending.targetPosition) : nullptr;
        if (source == nullptr || target == nullptr || source->IsUnderConstruction() || target->IsUnderConstruction())
            continue;

        if (pending.receiver && pending.alternative)
            source->SetAlternativeReceiver(pending.resource, target);
        else if (pending.receiver)
            source->SetReceiver(pending.resource, target);
        else
            source->SetSupplier(pending.resource, target);
    }

    for (const auto& pending : restoredProvinceShipments)
    {
        auto* province = globalMap.FindBuildableProvince(pending.provinceId);
        ProvinceEconomy* economy = province != nullptr && province->GetSimulation() != nullptr
            ? &province->GetSimulation()->GetEconomy() : nullptr;
        if (economy == nullptr || economy->roadNetwork == nullptr || economy->tilemap == nullptr)
            return false;

        auto findBuilding = [&](int buildingId) -> Building*
        {
            for (Building* building : economy->dataTracker.buildings)
                if (building != nullptr && building->id == buildingId)
                    return building;
            return nullptr;
        };
        for (const auto& shipment : pending.shipments)
        {
            for (int tileId : shipment.pathTileIds)
                if (tileId >= static_cast<int>(economy->tilemap->tilemap.size()))
                    return false;
            Building* source = findBuilding(shipment.sourceBuildingId);
            Building* target = findBuilding(shipment.targetBuildingId);
            if (!economy->roadNetwork->RestoreShipment(shipment, source, target))
                return false;
        }
        if (!economy->roadNetwork->RestoreNextShipmentId(pending.nextShipmentId))
            return false;
    }

    in >> std::ws;
    if (!in.eof())
        return false;

    UpdateFogOfWar();
    initialized = true;
    initializationError.clear();
    return true;
}
