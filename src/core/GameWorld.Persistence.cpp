#include "core/GameWorldInternal.h"
#include "core/PersistenceLimits.h"
#include "platform/AtomicFile.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace GameWorldInternal;

// Serializes current runtime state.
bool GameWorld::SaveToFile(const std::string& path) const
{
    namespace fs = std::filesystem;
    const fs::path destination(path);
    std::ostringstream serialized;
    if (!SaveToStream(serialized) || !serialized.good())
        return false;

    std::error_code ignored;
    if (fs::exists(destination, ignored))
    {
        const fs::path backup1 = destination.string() + ".bak.1";
        const fs::path backup2 = destination.string() + ".bak.2";
        const fs::path backup3 = destination.string() + ".bak.3";
        fs::remove(backup3, ignored);
        fs::rename(backup2, backup3, ignored);
        ignored.clear();
        fs::rename(backup1, backup2, ignored);
        ignored.clear();
        fs::copy_file(destination, backup1, fs::copy_options::overwrite_existing, ignored);
    }

    std::string error;
    return AtomicFileTransaction::Write(destination,
        [&](std::ostream& out)
        {
            out << serialized.str();
            return out.good();
        }, &error);
}

std::string GameWorld::SerializeSimulationState() const
{
    std::ostringstream out;
    if (!SaveToStream(out))
        return {};
    return out.str();
}

bool GameWorld::SaveToStream(std::ostream& out) const
{

    // Save v36: RoadComponent product-priority state.
    // Save v35: exact construction payment records for deterministic salvage.
    // Save v34: private Barracks/tower buffers separated from StorageComponent.
    // Save v33: independent household/urban Village upkeep timers.
    // Save v32: runtime identifiers and simulation tick for multiplayer restore.
    // Save v31: transparent per-tile resource-overlay cells.
    // Save v30: settlement tiers and their household/urban supply buffers.
    // Save v29: added TowerCombatComponent target priority.
    // Save v28: added the UPG block (UpgradeComponent — generic per-instance
    // building upgrade progression, introduced for Road).
    // Snapshot recovery must preserve values tightly enough to reproduce the
    // deterministic checksum; the default stream precision silently rounds
    // timers and resource rates after a few ticks.
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "RTS_SAVE " << SerializationVersion::GameWorldSaveVersion << '\n';
    out << "WORLD " << std::quoted(worldName) << '\n';
    out << "RUNTIME " << localPlayerId << ' ' << simulationTick << ' ' << nextCommandId << ' '
        << nextProjectileId << '\n';
    out << "PARAMS " << tilemap.params.sizeX << ' ' << tilemap.params.sizeY << ' '
        << tilemap.params.seed << ' ' << static_cast<int>(tilemap.params.sizePreset) << ' '
        << tilemap.params.resourceDensity << ' ' << tilemap.params.resourceFieldSize << ' '
        << tilemap.params.resourceRichness << ' ' << tilemap.params.aiOpponentCount << ' '
        << tilemap.params.aiDifficulty << ' ' << tilemap.params.debugMode << '\n';
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
        // Save v27 (AI rework czystka, TODO #2): the dead DiplomaticState —
        // never read by any gameplay logic post-pivot — was removed, and the
        // DIPLO/WAR blocks (and their counts on this line) with it.
        out << "PLAYER " << id << ' ' << player->strategicResources.values.size() << ' '
            << player->technologies.GetUnlocked().size() << ' '
            << player->focuses.GetUnlocked().size() << ' '
            << (player->defeated ? 1 : 0) << ' '
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

        // TD(etap-3): recruited-but-not-deployed BattleUnit roster. Equipment
        // is always an empty list in v1 (ETAP 3.4 seam) but its count is
        // written from day one so a future DLC equipment system doesn't need
        // another breaking save-format change.
        out << "ROSTER " << player->nextUnitInstanceId << ' ' << player->roster.units.size() << '\n';
        for (const auto& [instanceId, unit] : player->roster.units)
        {
            out << "UNIT " << unit.instanceId << ' ' << unit.ownerPlayerId << ' '
                << std::quoted(unit.unitDefId) << ' ' << unit.currentHp << ' '
                << static_cast<int>(unit.state) << ' '
                << unit.routeFromPlayerId << ' ' << unit.routeToPlayerId << ' '
                << unit.tileIndex << ' ' << unit.tileProgress << ' ' << unit.attackTimer << ' '
                << unit.equipment.size() << '\n';
        }

        // TD(etap-6.3): productivity ramps on buildings captured from an
        // eliminated player.
        out << "CONQUERED " << player->conqueredEconomy.GetRamps().size() << '\n';
        for (const auto& ramp : player->conqueredEconomy.GetRamps())
            out << "RAMP " << ramp.buildingId << ' ' << ramp.elapsed << ' ' << ramp.rampDuration << '\n';

        out << "COMMANDSTATS " << player->dataTracker.processedCommands.size() << '\n';
        for (const auto& [commandType, count] : player->dataTracker.processedCommands)
            out << "CMDSTAT " << static_cast<int>(commandType) << ' ' << count << '\n';
        out << "ENDPLAYER\n";
    }

    out << "TILES " << tilemap.tilemap.size() << '\n';
    for (const auto& tile : tilemap.tilemap)
    {
        int ownerId = tile.owner != nullptr ? tile.owner->id : -1;
        out << "T " << tile.id << ' ' << static_cast<int>(tile.tileType) << ' '
            << tile.terrainTextureId << ' ' << tile.resourceOverlayTextureId << ' ' << ownerId << ' ' << tile.resourceRichness << ' '
            << static_cast<int>(tile.biome) << '\n';
    }

    // Military road ring (TD etap-2): written explicitly rather than
    // regenerated from seed, so generator changes never invalidate an
    // existing save's ring layout.
    const auto& militaryRoutes = militaryRoads.GetRoutes();
    out << "MILROADS " << militaryRoutes.size() << '\n';
    for (const auto& route : militaryRoutes)
    {
        out << "MROUTE " << route.playerA << ' ' << route.playerB << ' ' << route.tiles.size();
        for (int tileId : route.tiles)
            out << ' ' << tileId;
        out << '\n';
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

        int ownerId = building->owner != nullptr ? building->owner->id : -1;
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

        if (const auto* hq = building->GetComponent<HqComponent>())
        {
            out << "HQ " << hq->maxHp.GetBase() << ' ' << hq->currentHp << ' '
                << hq->hardDefense.GetBase() << ' ' << hq->thornsDamage.GetBase() << ' '
                << hq->thornsInterval << ' ' << hq->thornsTimer << ' '
                << hq->captureStockFraction << ' ' << hq->conquestRampDuration << '\n';
        }

        if (const auto* tower = building->GetComponent<TowerCombatComponent>())
        {
            // Ammo itself is already covered by the LOCALBUF block above;
            // only the attack cooldown and targeting policy live here.
            out << "TOWER " << tower->damage.GetBase() << ' ' << tower->range.GetBase() << ' '
                << tower->attackSpeed.GetBase() << ' ' << tower->attackTimer << ' '
                << static_cast<int>(tower->ammoResource) << ' ' << tower->ammoPerShot.GetBase() << ' '
                << static_cast<int>(tower->targetMode) << '\n';
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
                << pop->householdUpkeepTimer << ' ' << pop->urbanUpkeepTimer << '\n';
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

        out << "ENDB\n";
    }

    // TD(etap-4): deployed (marching/fighting/arrived) units, world-scoped
    // since a column can include units from either side of a route.
    out << "DEPLOYEDUNITS " << deployedUnits.size() << '\n';
    for (const auto& [instanceId, unit] : deployedUnits)
    {
        out << "DUNIT " << unit.instanceId << ' ' << unit.ownerPlayerId << ' '
            << std::quoted(unit.unitDefId) << ' ' << unit.currentHp << ' '
            << static_cast<int>(unit.state) << ' '
            << unit.routeFromPlayerId << ' ' << unit.routeToPlayerId << ' '
            << unit.tileIndex << ' ' << unit.tileProgress << ' ' << unit.attackTimer << ' '
            << unit.equipment.size() << '\n';
    }

    out << "SPAWNQUEUES " << spawnQueues.size() << '\n';
    for (const auto& [routeKey, queue] : spawnQueues)
    {
        out << "SQ " << routeKey.first << ' ' << routeKey.second << ' ' << queue.size();
        for (int unitInstanceId : queue)
            out << ' ' << unitInstanceId;
        out << '\n';
    }

    out << "PROJECTILES " << projectiles.size() << '\n';
    for (const auto& [id, projectile] : projectiles)
    {
        out << "PROJECTILE " << id << ' ' << projectile.sourcePlayerId << ' '
            << projectile.sourceUnitInstanceId << ' ' << projectile.position.x << ' ' << projectile.position.y << ' '
            << projectile.damage << ' ' << static_cast<int>(projectile.damageType) << ' '
            << static_cast<int>(projectile.filter) << ' ' << projectile.ticksRemaining << ' '
            << projectile.targetUnitInstanceId << ' ' << projectile.speed << '\n';
    }

    return true;
}

// Loads the requested data into runtime state.
bool GameWorld::LoadFromFile(const std::string& path, Renderer* renderer, AudioSystem* a)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;

    std::string payload((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (payload.empty() || payload.size() > 64u * 1024u * 1024u)
        return false;

    try
    {
        GameWorld candidate;
        std::istringstream state(payload);
        if (!candidate.LoadFromStream(state, renderer, a, -1))
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
    constexpr std::size_t MaxSimulationStateBytes = 64u * 1024u * 1024u;
    if (payload.empty() || payload.size() > MaxSimulationStateBytes)
        return false;

    try
    {
        GameWorld candidate;
        std::istringstream in{std::string(payload)};
        if (!candidate.LoadFromStream(in, render, audio, localPlayerIdOverride))
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
        (void)playerId;
        if (player != nullptr)
            player->RebindTileMap(tilemap);
        if (player != nullptr && player->roadNetwork != nullptr)
            player->roadNetwork->RebindWorld(tilemap);
    }
}

bool GameWorld::LoadFromStream(std::istream& in, Renderer* renderer, AudioSystem* a,
                               int localPlayerIdOverride)
{

    combatTelemetry.Clear();
    projectiles.clear();

    std::string tag;
    int version = 0;
    in >> tag >> version;
    // TD(etap-1): the old war system's save fields (HQ/MIL/DIVS/RECRUIT) were
    // dropped, not merely extended — a breaking change per the rework plan.
    // Older saves are rejected outright rather than partially parsed.
    // v27 (AI rework czystka): DiplomaticState removed from the format.
    // v35: exact construction payment records for deterministic salvage.
    // v34: private Barracks/tower buffers separated from StorageComponent.
    // v33: independent household/urban Village upkeep timers.
    // v32: runtime identifiers and simulation tick for multiplayer restore.
    // v31: transparent per-tile resource-overlay cells.
    // v30: settlement tiers and advanced supply buffers.
    // v29: added tower target priority.
    // v28: added the UPG block (UpgradeComponent).
    if (tag != "RTS_SAVE" ||
        (version != 30 && version != 31 && version != 32 && version != 33 &&
         version != 34 && version != 35 && version != 36))
        return false;

    render = renderer;
    audio  = a;

    in >> tag >> std::quoted(worldName);
    if (tag != "WORLD")
        return false;

    int serializedLocalPlayerId = localPlayerId;
    if (version >= 32)
    {
        in >> tag >> serializedLocalPlayerId >> simulationTick >> nextCommandId >> nextProjectileId;
        if (tag != "RUNTIME")
            return false;
    }
    else
    {
        simulationTick = 0;
        nextCommandId = 1;
        nextProjectileId = 1;
    }
    localPlayerId = localPlayerIdOverride >= 0 ? localPlayerIdOverride : serializedLocalPlayerId;

    int preset = 0;
    in >> tag >> tilemap.params.sizeX >> tilemap.params.sizeY >> tilemap.params.seed >> preset;
    if (tag != "PARAMS")
        return false;
    std::size_t expectedTileCount = 0;
    if (!PersistenceLimits::CheckedArea(tilemap.params.sizeX, tilemap.params.sizeY, expectedTileCount))
        return false;
    tilemap.params.sizePreset = static_cast<MapSizePreset>(preset);
    if (version >= 3)
    {
        in >> tilemap.params.resourceDensity >> tilemap.params.resourceFieldSize
            >> tilemap.params.resourceRichness >> tilemap.params.aiOpponentCount
            >> tilemap.params.aiDifficulty;
        if (version >= 4)
            in >> tilemap.params.debugMode;
        else
            tilemap.params.debugMode = false;
    }

    if (version >= 2)
    {
        Camera2D camera{};
        in >> tag >> camera.target.x >> camera.target.y >> camera.zoom >> camera.rotation;
        if (tag != "CAMERA")
            return false;
        if (render != nullptr)
        {
            render->camera = camera;
            render->ClampCameraToMap({tilemap.params.sizeX, tilemap.params.sizeY});
        }
    }

    playerHandler.players.clear();
    controllers.clear();
    int playerCount = 0;
    in >> tag >> playerCount;
    if (tag != "PLAYERS" || !PersistenceLimits::IsCountInRange(playerCount, PersistenceLimits::MaxSupportedPlayers))
        return false;

    for (int i = 0; i < playerCount; i++)
    {
        int playerId = 0;
        int strategicCount = 0;
        int technologyCount = 0;
        int focusCount = 0;
        int defeatedFlag = 0;
        in >> tag >> playerId >> strategicCount >> technologyCount >> focusCount >> defeatedFlag;
        if (tag != "PLAYER")
            return false;

        auto player = std::make_unique<Player>(playerId, tilemap);
        player->defeated = defeatedFlag != 0;
        if (version >= 32)
        {
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
            player->color = Color{static_cast<unsigned char>(red), static_cast<unsigned char>(green),
                                  static_cast<unsigned char>(blue), static_cast<unsigned char>(alpha)};
        }
        else
        {
            player->name = playerId == localPlayerId ? "Player" : "AI Opponent";
            player->controllerType = playerId == localPlayerId ? PlayerControllerType::LocalHuman : PlayerControllerType::AI;
            player->color = playerId == localPlayerId ? Color{66, 154, 255, 255} : Color{220, 72, 72, 255};
        }
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
        if (version >= 32)
        {
            std::string activeFocusId;
            double activeFocusRemaining = 0.0;
            in >> tag >> std::quoted(activeFocusId) >> activeFocusRemaining;
            if (tag != "ACTIVE_FOCUS" || !player->focuses.RestoreActiveFocus(activeFocusId, activeFocusRemaining))
                return false;
        }
        player->RefreshTechnologyModifiers();

        int rosterCount = 0;
        in >> tag >> player->nextUnitInstanceId >> rosterCount;
        if (tag != "ROSTER" || !PersistenceLimits::IsCountInRange(rosterCount, PersistenceLimits::MaxUnits))
            return false;
        for (int u = 0; u < rosterCount; u++)
        {
            int instanceId = 0;
            int ownerPlayerId = 0;
            std::string unitDefId;
            double currentHp = 0.0;
            int state = 0;
            int routeFromPlayerId = -1;
            int routeToPlayerId = -1;
            int tileIndex = 0;
            double tileProgress = 0.0;
            double attackTimer = 0.0;
            size_t equipmentCount = 0;
            in >> tag >> instanceId >> ownerPlayerId >> std::quoted(unitDefId) >> currentHp
               >> state >> routeFromPlayerId >> routeToPlayerId >> tileIndex >> tileProgress
               >> attackTimer >> equipmentCount;
            if (tag != "UNIT")
                return false;

            BattleUnit unit(instanceId, ownerPlayerId, unitDefId);
            unit.currentHp = currentHp;
            unit.state = static_cast<BattleUnitState>(state);
            unit.routeFromPlayerId = routeFromPlayerId;
            unit.routeToPlayerId = routeToPlayerId;
            unit.tileIndex = tileIndex;
            unit.tileProgress = tileProgress;
            unit.attackTimer = attackTimer;
            // Equipment is always empty in v1 (ETAP 3.4 seam) — equipmentCount
            // is read but not yet parsed into instances.
            player->roster.AddUnit(std::move(unit));
        }

        // TD(etap-6.3): productivity ramps on buildings captured from an
        // eliminated player.
        int conqueredCount = 0;
        in >> tag >> conqueredCount;
        if (tag != "CONQUERED")
            return false;
        std::vector<ConqueredBuildingRamp> ramps;
        if (!PersistenceLimits::IsCountInRange(conqueredCount, PersistenceLimits::MaxBuildings))
            return false;
        ramps.reserve(conqueredCount);
        for (int r = 0; r < conqueredCount; r++)
        {
            ConqueredBuildingRamp ramp;
            in >> tag >> ramp.buildingId >> ramp.elapsed >> ramp.rampDuration;
            if (tag != "RAMP")
                return false;
            ramps.push_back(ramp);
        }
        player->conqueredEconomy.SetRamps(std::move(ramps));
        // Re-derive each ramp's BalanceModifier from the restored elapsed
        // time immediately (zero-dt tick), mirroring RefreshTechnologyModifiers()
        // above rather than leaving the captured buildings' cycle time
        // unmodified until the next real simulation tick.
        player->conqueredEconomy.Tick(*player, 0.0);

        if (version >= 32)
        {
            int commandStatCount = 0;
            in >> tag >> commandStatCount;
            if (tag != "COMMANDSTATS" || commandStatCount < 0)
                return false;
            player->dataTracker.processedCommands.clear();
            for (int c = 0; c < commandStatCount; ++c)
            {
                int commandType = 0;
                int count = 0;
                in >> tag >> commandType >> count;
                if (tag != "CMDSTAT" || count < 0)
                    return false;
                player->dataTracker.processedCommands[static_cast<GameCommandType>(commandType)] = count;
            }
        }

        in >> tag;
        if (tag != "ENDPLAYER")
            return false;
        playerHandler.players[playerId] = std::move(player);
        AttachControllerForPlayer(playerHandler.players[playerId].get());
    }

    int tileCount = 0;
    in >> tag >> tileCount;
    if (tag != "TILES" || !PersistenceLimits::IsCountInRange(tileCount, PersistenceLimits::MaxMapTiles) ||
        static_cast<std::size_t>(tileCount) != expectedTileCount)
        return false;

    tilemap.tilemap.clear();
    tilemap.tilemap.resize(tileCount);
    for (int i = 0; i < tileCount; i++)
    {
        int id = 0;
        int tileType = 0;
        int terrainTextureId = 0;
        int resourceOverlayTextureId = -1;
        int ownerId = -1;
        in >> tag >> id >> tileType >> terrainTextureId;
        if (version >= 31)
            in >> resourceOverlayTextureId;
        in >> ownerId;
        if (tag != "T")
            return false;
        if (id < 0 || static_cast<std::size_t>(id) >= expectedTileCount || id != i)
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
        // Tile::owner is a relic of the pre-pivot territory system (ETAP 1
        // removed it; nothing in production sets it anymore, see
        // docs/post_pivot_audit_2026-07-12.md T2) — restored here only
        // because it's still part of the save format. Left in place rather
        // than bumping the save version to drop the field; ownerId always
        // reads back as whatever was written (effectively unused/-1 today).
        auto ownerIt = playerHandler.players.find(ownerId);
        tile.owner = ownerIt != playerHandler.players.end() ? ownerIt->second.get() : nullptr;
        tilemap.tilemap[id] = std::move(tile);
    }

    // Military road ring (TD etap-2): restored verbatim, not regenerated —
    // reapply Tile::isMilitaryRoad from the saved tile lists.
    int militaryRouteCount = 0;
    in >> tag >> militaryRouteCount;
    if (tag != "MILROADS" || !PersistenceLimits::IsCountInRange(militaryRouteCount, PersistenceLimits::MaxRouteCount))
        return false;

    std::vector<MilitaryRoute> loadedRoutes;
    loadedRoutes.reserve(militaryRouteCount);
    for (int i = 0; i < militaryRouteCount; i++)
    {
        MilitaryRoute route;
        size_t tileCountInRoute = 0;
        in >> tag >> route.playerA >> route.playerB >> tileCountInRoute;
        if (tag != "MROUTE")
            return false;

        if (!PersistenceLimits::IsCountInRange(tileCountInRoute, PersistenceLimits::MaxRouteTiles))
            return false;
        route.tiles.reserve(tileCountInRoute);
        for (size_t t = 0; t < tileCountInRoute; t++)
        {
            int tileId = 0;
            in >> tileId;
            if (tileId < 0 || static_cast<std::size_t>(tileId) >= expectedTileCount)
                return false;
            tilemap.tilemap[tileId].isMilitaryRoad = true;
            route.tiles.push_back(tileId);
        }
        loadedRoutes.push_back(std::move(route));
    }
    militaryRoads.RestoreRoutes(std::move(loadedRoutes));

    std::vector<PendingConnection> pendingConnections;
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
                    pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, false, false});
                }

                in >> tag >> count;
                if (tag != "RECEIVERS") return false;
                logistics->receivers.clear();
                for (int n = 0; n < count; n++)
                {
                    int type = 0, target = -1;
                    in >> tag >> type >> target;
                    if (tag != "REC") return false;
                    pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, true, false});
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
                        pendingConnections.push_back({positionId, static_cast<ResourceType>(type), target, true, true});
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
                // v30-v33 encoded Barracks/tower private buffers as STOR, so
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
            else if (tag == "HQ")
            {
                auto* hq = placed->GetComponent<HqComponent>();
                if (hq == nullptr) return false;
                in >> hq->maxHp >> hq->currentHp >> hq->hardDefense >> hq->thornsDamage >>
                      hq->thornsInterval >> hq->thornsTimer >> hq->captureStockFraction >>
                      hq->conquestRampDuration;
            }
            else if (tag == "TOWER")
            {
                auto* tower = placed->GetComponent<TowerCombatComponent>();
                if (tower == nullptr) return false;
                int ammoResource = 0;
                int targetMode = 0;
                in >> tower->damage >> tower->range >> tower->attackSpeed >> tower->attackTimer >>
                      ammoResource >> tower->ammoPerShot >> targetMode;
                tower->ammoResource = static_cast<ResourceType>(ammoResource);
                if (targetMode < static_cast<int>(TowerTargetMode::NearestToHq) ||
                    targetMode > static_cast<int>(TowerTargetMode::StrongestUnit))
                    return false;
                tower->targetMode = static_cast<TowerTargetMode>(targetMode);
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
            else
            {
                return false;
            }
        }
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
        player->roadNetwork = std::make_unique<RoadNetwork>(tilemap);

        for (auto& tile : tilemap.tilemap)
        {
            if (tile.building != nullptr && tile.building->owner == player.get() && !tile.building->IsUnderConstruction())
                player->roadNetwork->UpdateNavMap(tile.id, tile.building.get());
        }
    }

    for (auto& tile : tilemap.tilemap)
    {
        if (tile.building != nullptr && tile.building->buildingType == BuildingType::Road)
            tilemap.RefreshRoadTilesAround(tilemap.GetCoordsFromId(tile.id));
    }
    tilemap.terrainDirty = true;
    tilemap.buildingsDirty = true;

    for (const auto& pending : pendingConnections)
    {
        Building* source = tilemap.GetBuilding(pending.sourcePosition);
        Building* target = pending.targetPosition >= 0 ? tilemap.GetBuilding(pending.targetPosition) : nullptr;
        if (source == nullptr || target == nullptr || source->IsUnderConstruction() || target->IsUnderConstruction())
            continue;

        if (pending.receiver && pending.alternative)
            source->SetAlternativeReceiver(pending.resource, target);
        else if (pending.receiver)
            source->SetReceiver(pending.resource, target);
        else
            source->SetSupplier(pending.resource, target);
    }

    // TD(etap-4): deployed units, world-scoped.
    int deployedCount = 0;
    in >> tag >> deployedCount;
    if (tag != "DEPLOYEDUNITS" || !PersistenceLimits::IsCountInRange(deployedCount, PersistenceLimits::MaxUnits))
        return false;

    deployedUnits.clear();
    for (int i = 0; i < deployedCount; i++)
    {
        int instanceId = 0;
        int ownerPlayerId = 0;
        std::string unitDefId;
        double currentHp = 0.0;
        int state = 0;
        int routeFromPlayerId = -1;
        int routeToPlayerId = -1;
        int tileIndex = 0;
        double tileProgress = 0.0;
        double attackTimer = 0.0;
        size_t equipmentCount = 0;
        in >> tag >> instanceId >> ownerPlayerId >> std::quoted(unitDefId) >> currentHp
           >> state >> routeFromPlayerId >> routeToPlayerId >> tileIndex >> tileProgress
           >> attackTimer >> equipmentCount;
        if (tag != "DUNIT")
            return false;

        BattleUnit unit(instanceId, ownerPlayerId, unitDefId);
        unit.currentHp = currentHp;
        unit.state = static_cast<BattleUnitState>(state);
        unit.routeFromPlayerId = routeFromPlayerId;
        unit.routeToPlayerId = routeToPlayerId;
        unit.tileIndex = tileIndex;
        unit.tileProgress = tileProgress;
        unit.attackTimer = attackTimer;
        deployedUnits[instanceId] = std::move(unit);
    }

    int spawnQueueCount = 0;
    in >> tag >> spawnQueueCount;
    if (tag != "SPAWNQUEUES" || !PersistenceLimits::IsCountInRange(spawnQueueCount, PersistenceLimits::MaxSupportedPlayers * PersistenceLimits::MaxSupportedPlayers))
        return false;

    spawnQueues.clear();
    for (int i = 0; i < spawnQueueCount; i++)
    {
        int fromPlayerId = 0, toPlayerId = 0;
        size_t queueLength = 0;
        in >> tag >> fromPlayerId >> toPlayerId >> queueLength;
        if (tag != "SQ")
            return false;

        std::deque<int> queue;
        if (!PersistenceLimits::IsCountInRange(queueLength, PersistenceLimits::MaxQueueEntries))
            return false;
        for (size_t q = 0; q < queueLength; q++)
        {
            int unitInstanceId = 0;
            in >> unitInstanceId;
            queue.push_back(unitInstanceId);
        }
        spawnQueues[{fromPlayerId, toPlayerId}] = std::move(queue);
    }

    if (version >= 32)
    {
        int projectileCount = 0;
        in >> tag >> projectileCount;
        if (tag != "PROJECTILES" || !PersistenceLimits::IsCountInRange(projectileCount, PersistenceLimits::MaxProjectiles))
            return false;
        for (int i = 0; i < projectileCount; ++i)
        {
            int id = 0;
            AttackEmission projectile;
            int damageType = 0;
            int targetFilter = 0;
            in >> tag >> id >> projectile.sourcePlayerId >> projectile.sourceUnitInstanceId
               >> projectile.position.x >> projectile.position.y >> projectile.damage
               >> damageType >> targetFilter >> projectile.ticksRemaining
               >> projectile.targetUnitInstanceId >> projectile.speed;
            if (tag != "PROJECTILE" || id < 1 || projectile.ticksRemaining < 0 ||
                damageType != static_cast<int>(DamageType::Physical) ||
                targetFilter < static_cast<int>(AttackTargetFilter::EnemiesOnly) ||
                targetFilter > static_cast<int>(AttackTargetFilter::Everyone) ||
                !std::isfinite(projectile.damage) || !std::isfinite(projectile.speed))
                return false;
            projectile.damageType = static_cast<DamageType>(damageType);
            projectile.filter = static_cast<AttackTargetFilter>(targetFilter);
            projectiles[id] = std::move(projectile);
        }
    }

    in >> std::ws;
    if (!in.eof())
        return false;

    UpdateFogOfWar();
    initialized = true;
    initializationError.clear();
    return true;
}

