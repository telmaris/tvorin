#include "ai/AIModel.h"
#include "ai/AIDifficulty.h"
#include "ai/AIEconomyBias.h"
#include "core/GameWorld.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>

namespace
{
    // Sense cadence (seconds). Cheap aggregate reads.
    constexpr double SenseInterval = 1.0;
    // Decision cadence (one concrete action attempt per cycle) comes from
    // ai.rtsdata's `decision_interval` (AIEconomyBias) — the pace lever the
    // user tunes; 1.5 s was the old hardcoded value and remains the default
    // when the line is absent.
    // Road-network maintenance cadence (AIActions::TryBuildRoads), kept on
    // its own timer like the old model so the network stays healthy
    // independently of what the decision core is busy with.
    constexpr double RoadMaintenanceInterval = 2.0;
    // Connectivity audit cadence — IsConnectedToRoadNetwork pays a road-BFS
    // per storage per building, so it runs slower than the rest of Sense.
    constexpr double ConnectivityInterval = 3.0;

    // Needs are tried in (score desc, enum asc) order — the enum order IS the
    // fixed priority tie-break: Defense > RecruitDeploy > EconomySustain >
    // LogisticsRepair > Research.
    constexpr double MinActionableScore = 0.05;
    // Three edges means the selected decision can be justified by a payoff
    // three later in its chain. Future value is discounted at every edge so
    // an urgent direct benefit still beats a merely comparable distant one.
    constexpr int FocusLookAheadDepth = 3;
    constexpr double FocusFutureDiscount = 0.65;
    constexpr double FocusHysteresisMargin = 3.0;

    // Deterministic opening build order that bootstraps the FOOD_PROVISIONS
    // (manpower) chain, a wood base, AND the iron chain before telemetry has
    // any consumption signal to react to. Each step carries the terrain its
    // extractor needs (GRASS for plain buildings) and, for the terrain-specific
    // mines, the RESOURCE that gates it — because a Mine's type alone can't
    // tell a coal mine from an iron-ore or stone one (2026-07-20 fix: the old
    // type-count gate let the deficit ladder's STONE mine satisfy the COAL
    // step, so the iron chain never got its ore).
    struct OpeningStep
    {
        BuildingType type;
        int target;              // required building/terrain-resolved producer count
        TileType preferredTile;  // terrain the extractor needs
        ResourceType gateResource;  // Null => gate by building-type count; else by a producer of this resource
    };
    constexpr std::array<OpeningStep, 17> OpeningPlan{{
        // Basic materials, then the IRON CHAIN, then the (expensive) food chain.
        // Rationale:
        //  - BOTH woodcutters before the LumberMill: one Woodcutter (30 WOOD/min)
        //    exactly feeds one LumberMill (30 WOOD/min) with ZERO surplus for
        //    construction, so a single one starves the rest for WOOD and the plan
        //    wedges (2026-07-20). Two give a real surplus.
        //  - Iron chain (coal + iron-ore mines + Foundry) BEFORE the food chain:
        //    it's cheap (mines cost no iron; a first Foundry's 10 IRON is covered
        //    by the HQ's starting 30) and it's the user's actual goal ("AI buduje
        //    produkcję żelaza/węgla"), so stand it up FAST off the opening stock
        //    rather than after grinding out the whole PLANKS/STONE-heavy food
        //    chain (WheatFarm/Windmill/Bakery/Inn ~ 150 PLANKS), which is a
        //    slower, manpower-oriented investment.
        {BuildingType::Woodcutter,      1, TileType::WOOD,     ResourceType::Null},
        {BuildingType::Woodcutter,      2, TileType::WOOD,     ResourceType::Null},
        {BuildingType::LumberMill,      1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Mine,            1, TileType::STONE,    ResourceType::STONE},
        {BuildingType::Mine,            1, TileType::COAL,     ResourceType::COAL},
        {BuildingType::Mine,            1, TileType::IRON_ORE, ResourceType::IRON_ORE},
        // Secure the first food input before tier-two construction can drain
        // the opening stock on a sparse/debug map.
        {BuildingType::Well,            2, TileType::GRASS,    ResourceType::Null},
        // Water must exist before the farm: otherwise the farm consumes
        // workers while waiting on an input the opening plan has deliberately
        // postponed, making the food bootstrap slower and more fragile.
        {BuildingType::WheatFarm,       1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Windmill,        1, TileType::GRASS,    ResourceType::Null},
        // The Foundry must precede the Smith: otherwise the Smith consumes the
        // finite HQ iron reserve while Barracks is waiting for tools, creating
        // a circular opening deadlock. Both follow the first food workers so
        // the metal chain cannot consume the entire worker pool first.
        {BuildingType::Foundry,         1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::Smith,           1, TileType::GRASS,    ResourceType::Null},
        // Military foothold follows the renewable foundation so the first
        // wave does not consume the finite starting food reserve.
        {BuildingType::Barracks,        1, TileType::GRASS,    ResourceType::Null},
        // Complete one renewable food line before recruitment can consume
        // its manpower. This is one deterministic chain, not a food bias:
        // duplicates remain locked out by the transactional opening plan.
        {BuildingType::Bakery,          1, TileType::GRASS,    ResourceType::Null},
        // HuntersHut makes MEAT on WOOD terrain — it was gated GRASS before and
        // so could NEVER be placed (silently breaking the MEAT->Inn food link).
        {BuildingType::HuntersHut,      1, TileType::WOOD,     ResourceType::Null},
        {BuildingType::Inn,             1, TileType::GRASS,    ResourceType::Null},
        // Defer the second village until the first military wave has a food
        // reserve; two villages otherwise consume the entire early Inn output
        // and leave Barracks' real transport queue waiting forever.
        {BuildingType::Village,         1, TileType::GRASS,    ResourceType::Null},
        {BuildingType::StorageBuilding, 1, TileType::GRASS,    ResourceType::Null},
    }};

    bool HasCompletedFoodFoundation(Player* player)
    {
        if (player == nullptr)
            return false;
        constexpr std::array<BuildingType, 3> FoodFoundation{{
            BuildingType::Well,
            BuildingType::WheatFarm,
            BuildingType::Windmill,
        }};
        return player->GetTrackedBuildingCount(BuildingType::Village, true) >= 1 &&
            std::all_of(FoodFoundation.begin(), FoodFoundation.end(), [&](BuildingType type)
        {
            return player->HasTrackedBuilding(type, true);
        });
    }

    bool HasCompletedFoodChain(Player* player)
    {
        if (player == nullptr)
            return false;
        constexpr std::array<BuildingType, 5> FoodChain{{
            BuildingType::WheatFarm,
            BuildingType::Windmill,
            BuildingType::Bakery,
            BuildingType::HuntersHut,
            BuildingType::Inn,
        }};
        return player->GetTrackedBuildingCount(BuildingType::Well, true) >= 3 &&
            std::all_of(FoodChain.begin(), FoodChain.end(), [&](BuildingType type)
        {
            return player->HasTrackedBuilding(type, true);
        });
    }

    int RemainingExtractorRichness(Player* player, ResourceType resource)
    {
        if (player == nullptr)
            return 0;
        int remaining = 0;
        for (Building* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
        {
            auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
            if (production == nullptr || building->owner != player || building->IsUnderConstruction() ||
                !production->consumesTerrain || production->terrainType == TileType::GRASS ||
                !production->products.contains(resource))
                continue;
            Vec2i anchor = player->tilemap->GetCoordsFromId(building->positionId);
            for (int y = 0; y < building->GetFootprint().y; y++)
                for (int x = 0; x < building->GetFootprint().x; x++)
                {
                    Vec2i pos{anchor.x + x, anchor.y + y};
                    if (!player->tilemap->IsInside(pos))
                        continue;
                    const Tile& tile = (*player->tilemap)[player->tilemap->GetIdFromCoords(pos)];
                    if (tile.tileType == production->terrainType)
                        remaining += std::max(0, tile.resourceRichness);
                }
        }
        return remaining;
    }

    // Rough per-unit combat weight for track-pressure comparisons: damage
    // output plus a slice of staying power. Effective stats so tech buffs
    // count.
    double UnitStrength(const BattleUnit& unit, const Player& owner)
    {
        return unit.GetEffectiveRoadAttack(owner) + unit.currentHp * 0.1;
    }

    double OffensiveUnitStrength(const BattleUnit& unit, const Player& owner)
    {
        return unit.GetEffectiveRoadAttack(owner) + unit.GetEffectiveSiegeAttack(owner) +
               unit.currentHp * 0.1;
    }

    // Deploy the whole roster once it reaches this headcount (etap 4 may
    // scale it by difficulty).
    constexpr int WaveSize = 6;
    // Threat above this = the lane is being lost — deploy whatever exists.
    constexpr double EmergencyThreat = 1.0;
    // How often RecruitDeploy may spend a decision cycle building toward the
    // top pick's missing cost instead of recruiting (2026-07-20) — bounds a
    // deep, currently-unaffordable chain (steel sword -> iron -> ore) to an
    // occasional nudge rather than a permanent recruit-fallback lockout.
    constexpr double RecruitEconomyBuildInterval = 8.0;

    // A unit is a siege specialist when breaching beats lane-fighting.
    bool IsSiegeUnit(const UnitDefinition& def)
    {
        return def.siegeAttack > def.roadAttack;
    }

    int TotalResourceCost(const UnitDefinition& def)
    {
        int total = 0;
        for (const auto& cost : def.cost)
            total += cost.amount;
        return total;
    }

    struct AIFocusPriorities
    {
        double economy{0.0};
        double logistics{0.0};
        double manpower{0.0};
        double mobilization{0.0};
        double defense{0.0};
        double offense{0.0};
        double expansion{0.0};
    };

    AIFocusPriorities BuildFocusPriorities(const AISituation& s)
    {
        AIFocusPriorities priorities;
        const double threat = std::clamp(std::max(
            s.Threat(),
            s.enemyIncomingCount > 0 ? 0.75 : 0.0), 0.0, 1.0);
        const double hqDanger = std::clamp(1.0 - s.hqHpRatio, 0.0, 1.0);
        const double reserve = std::max(1.0, GetAIEconomyBias().manpowerReserve);
        const double manpowerShortfall = std::clamp((reserve - s.manpower) / reserve, 0.0, 1.0);
        const double capacityPressure = s.populationCap > 0.0
            ? std::clamp(s.totalPopulation / s.populationCap, 0.0, 1.0)
            : 0.0;

        priorities.defense = std::max(threat, hqDanger);
        priorities.manpower = std::max(manpowerShortfall, capacityPressure * 0.65);
        priorities.mobilization = std::max({
            threat,
            priorities.manpower * (s.barracksCount > 0 ? 0.9 : 0.55),
            s.basicEconomyEstablished && s.rosterCount < WaveSize ? 0.55 : 0.0});
        if (s.enemyIncomingCount > 0)
        {
            // An attack in progress is the short-term plan: make the next
            // reinforcements both faster and cheaper in manpower.
            priorities.mobilization = 1.0;
            priorities.manpower = std::max(priorities.manpower, 0.9);
        }

        priorities.logistics = s.unconnectedPositionIds.empty()
            ? 0.15
            : std::min(1.0, 0.65 + 0.08 * static_cast<double>(s.unconnectedPositionIds.size() - 1));

        const double deficitUrgency = s.deficits.empty()
            ? 0.0
            : std::clamp(s.deficits.front().urgency, 0.0, 1.0);
        if (!s.basicEconomyEstablished)
            priorities.economy = 1.0;
        else if (!s.economyEstablished)
            priorities.economy = 0.8;
        else
            priorities.economy = std::max(0.3, deficitUrgency);

        // Once the economic footing exists, the long-term plan shifts toward
        // building a force and converting it into pressure. Live danger
        // suppresses this in favor of the short-term defense plan above.
        priorities.offense = s.economyEstablished && s.enemyIncomingCount == 0
            ? (s.rosterCount >= WaveSize ? 0.85 : 0.6)
            : 0.2;
        priorities.expansion = s.economyEstablished && priorities.defense < 0.5 ? 0.55 : 0.15;
        return priorities;
    }

    bool HasTag(const TechnologyDefinition& definition, const char* tag)
    {
        return std::find(definition.tags.begin(), definition.tags.end(), tag) != definition.tags.end();
    }

    bool LowerIsBetter(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime:
            case BalanceStat::BuildCost:
            case BalanceStat::ProductionCycleTime:
            case BalanceStat::TransportTime:
            case BalanceStat::TransportDispatchDelay:
            case BalanceStat::UnitRecruitTime:
            case BalanceStat::UnitRecruitManpowerCost:
            case BalanceStat::TowerAmmoEfficiency:
                return true;
            default:
                return false;
        }
    }

    bool IsHelpful(const BalanceModifier& modifier)
    {
        if (LowerIsBetter(modifier.stat))
            return modifier.additive < 0.0 || modifier.multiplier < 1.0;
        return modifier.additive > 0.0 || modifier.multiplier > 1.0;
    }

    double ModifierImpactWeight(const BalanceModifier& modifier)
    {
        const double multiplierImpact = LowerIsBetter(modifier.stat)
            ? std::max(0.0, 1.0 - modifier.multiplier)
            : std::max(0.0, modifier.multiplier - 1.0);
        // Additives have stat-specific units, so use them only as a bounded
        // quality nudge. Strategic relevance remains more important than
        // comparing incomparable raw values such as +1 armor and +50 HQ HP.
        const double additiveImpact = std::min(1.0, std::abs(modifier.additive) * 0.03);
        return 0.75 + std::min(0.75, multiplierImpact * 3.0 + additiveImpact);
    }

    double ModifierStrategicValue(const BalanceModifier& modifier,
                                  const AIFocusPriorities& priorities)
    {
        if (!IsHelpful(modifier))
            return 0.0;

        switch (modifier.stat)
        {
            case BalanceStat::BuildTime:
            case BalanceStat::BuildCost:
            case BalanceStat::BuilderAmount:
                return 24.0 * priorities.economy;
            case BalanceStat::ProductionCycleTime:
            case BalanceStat::ProductionOutputAmount:
            case BalanceStat::WorkerCapacity:
                return 30.0 * priorities.economy;
            case BalanceStat::TransportTime:
            case BalanceStat::TransportDispatchDelay:
            case BalanceStat::RoadCapacity:
            case BalanceStat::RoadSpeed:
                return 36.0 * priorities.logistics;
            case BalanceStat::ManpowerRate:
                return 34.0 * priorities.manpower + 12.0 * priorities.mobilization;
            case BalanceStat::PopulationCap:
                return 28.0 * priorities.manpower;
            case BalanceStat::VillageSupplyConsumption:
                return 30.0 * std::max(priorities.manpower, priorities.logistics);
            case BalanceStat::UnitRecruitTime:
                return 42.0 * priorities.mobilization + 10.0 * priorities.manpower;
            case BalanceStat::UnitRecruitManpowerCost:
                return 38.0 * priorities.mobilization + 18.0 * priorities.manpower;
            case BalanceStat::UnitHp:
            case BalanceStat::UnitRoadAttack:
            case BalanceStat::UnitArmor:
            case BalanceStat::UnitAttackSpeed:
                return 25.0 * std::max(priorities.defense, priorities.offense);
            case BalanceStat::UnitSiegeAttack:
                return 32.0 * priorities.offense;
            case BalanceStat::UnitMoveSpeed:
                return 22.0 * std::max(priorities.mobilization, priorities.offense);
            case BalanceStat::HqMaxHp:
            case BalanceStat::HqDefense:
            case BalanceStat::HqThorns:
            case BalanceStat::TowerDamage:
            case BalanceStat::TowerRange:
            case BalanceStat::TowerAttackSpeed:
            case BalanceStat::TowerAmmoEfficiency:
                return 34.0 * priorities.defense;
            case BalanceStat::ConquestSpoilsFraction:
                return 30.0 * std::max(priorities.offense, priorities.expansion);
        }
        return 0.0;
    }

    // Whether a unit's full resource cost already sits in the building's own
    // storage buffer (the only stock a roadless Barracks can ever consume).
    bool CostLocallyBuffered(const Building& barracks, const UnitDefinition& def)
    {
        const auto* storage = barracks.GetComponent<LocalResourceBufferComponent>();
        if (storage == nullptr)
            return false;
        for (const auto& cost : def.cost)
        {
            auto it = storage->buffers.find(cost.type);
            int have = it != storage->buffers.end() ? static_cast<int>(it->second.buffer.size()) : 0;
            if (have < cost.amount)
                return false;
        }
        return true;
    }

    // How many towers this AI wants standing right now. None until a minimal
    // economy exists; then a small garrison that grows under pressure. The
    // readiness threshold is a build-order lever like `priority`, but towers
    // aren't a resource — it's ai.rtsdata's `tower_readiness_buildings`
    // instead (user design 2026-07-19: defense should start "in the
    // meantime" once the economy has SOME footing, tunable from the same file).
    int DesiredTowerCount(const AISituation& s)
    {
        // A live attack overrides the peacetime economy gate. Waiting for a
        // Smith/Barracks while enemies are already on the lane is precisely
        // the passive failure this guard was meant to avoid.
        if (s.enemyIncomingCount > 0)
        {
            int emergencyDesired = 1;
            if (s.Threat() > 0.7)
                emergencyDesired++;
            if (s.hqHpRatio < 0.7)
                emergencyDesired++;
            return std::min(emergencyDesired, 3);
        }
        // Static defense must not consume the Smith/Barracks reserve before
        // the AI has unlocked its primary objective: sending units.
        if (s.barracksCount == 0 || s.smithCount == 0 ||
            s.productionBuildingCount < GetAIEconomyBias().towerReadinessBuildings)
            return 0;
        int desired = 2;
        if (s.Threat() > 0.5)
            desired++;
        if (s.hqHpRatio < 0.7)
            desired++;
        return std::min(desired, 4);
    }
}

UtilityAIModel::UtilityAIModel(int controlledPlayerId)
    : playerId(controlledPlayerId)
{
}

void UtilityAIModel::Update(GameWorld& world, Player* player, double dt)
{
    if (player == nullptr || player->defeated)
        return;

    // Difficulty comes straight from map params (UI -> MapParameters ->
    // save). The RNG only supplies a stable per-player personality; every
    // difficulty executes the same decisions and economy targets.
    difficulty = std::clamp(world.GetTileMap().params.aiDifficulty, 0, 3);
    if (!noiseSeeded)
    {
        noiseRng.seed(static_cast<unsigned int>(world.GetTileMap().params.seed) ^
                      (0x9E3779B9u * static_cast<unsigned int>(playerId + 1)));
        // Production data intentionally uses the same scale at every level.
        // Keep the parameter here so custom data files remain compatible.
        consumptionBias = GetAIEconomyBias().ScaledMap(difficulty);
        // Build-order priority — not difficulty-scaled (see AIModel.h).
        priorityWeights = GetAIEconomyBias().NormalizedPriorityMap();

        // Personality bias (user design 2026-07-20): drawn HERE, in a fixed
        // number of calls right after the seed, so it's identical between two
        // same-seed worlds regardless of anything that happens later (map
        // events, other players' actions) — same reasoning as the rest of
        // this block. Active at every difficulty.
        //
        // Amplitude bounded by the TIGHTEST hard-won score gap in ScoreNeed,
        // not chosen freely: EconomySustain's food-dead critical (0.75) must
        // stay below RecruitDeploy's quiet-lane value (0.8) — an
        // independent +x/-x swing on both must not invert that, i.e.
        // (1+x)*0.75 < (1-x)*0.8 => x < 0.0323. 0.025 leaves real margin
        // (worst case 0.76875 vs 0.78) while still being a visible, permanent
        // per-AI skew — do not widen this without re-checking every floor
        // pairing documented in ScoreNeed (EconomySustain's routine cap 0.6 vs
        // LogisticsRepair's 0.65 floor is the other tight one, gap 0.65/0.6).
        std::uniform_real_distribution<double> needSkew(-0.025, 0.025);
        for (double& bias : personalityNeedBias)
            bias = needSkew(noiseRng);
        std::uniform_int_distribution<int> waveSkew(-1, 2);
        personalityWaveBias = waveSkew(noiseRng);

        noiseSeeded = true;
    }

    UpdateWaveEvaluation(world, player);

    senseTimer -= dt;
    actionCadenceTimer -= dt;
    decisionTimer -= dt;
    roadTimer -= dt;
    recruitEconomyBuildTimer -= dt;
    attackTargetCacheTimer -= dt;
    actions.Decay(dt);

    if (senseTimer <= 0.0)
    {
        senseTimer = SenseInterval;
        situation = Sense(world, player);
    }

    decisionTraceTimer -= dt;
    if (decisionTraceTimer <= 0.0)
    {
        decisionTraceTimer = 5.0;
        std::array<std::pair<double, int>, static_cast<int>(AINeed::Count)> topNeeds{};
        for (int i = 0; i < static_cast<int>(AINeed::Count); i++)
            topNeeds[i] = {ScoreNeed(static_cast<AINeed>(i), situation) *
                               (1.0 + personalityNeedBias[i]), i};
        std::stable_sort(topNeeds.begin(), topNeeds.end(),
                         [](const auto& lhs, const auto& rhs)
                         {
                             return lhs.first > rhs.first;
                         });
        const AIFocusPriorities focusPriorities = BuildFocusPriorities(situation);
        const char* posture = focusPriorities.defense >= focusPriorities.mobilization &&
                focusPriorities.defense >= focusPriorities.economy
            ? "defense"
            : focusPriorities.mobilization >= focusPriorities.economy
                ? "mobilization"
                : focusPriorities.logistics > focusPriorities.economy
                    ? "logistics"
                    : focusPriorities.offense > focusPriorities.economy
                        ? "offense"
                        : "economy";
        std::ostringstream trace;
        trace << "tick=" << world.GetSimulationTick()
              << " buildings=" << player->GetTrackedBuildings().size()
              << " roads=" << AIActions::CountOwnedBuildings(player, BuildingType::Road)
              << " production=" << situation.productionBuildingCount
              << " well=" << AIActions::CountOwnedBuildings(player, BuildingType::Well)
              << " farm=" << AIActions::CountOwnedBuildings(player, BuildingType::WheatFarm)
              << " windmill=" << AIActions::CountOwnedBuildings(player, BuildingType::Windmill)
              << " smith=" << situation.smithCount
              << " barracks=" << situation.barracksCount
              << " foodAlive=" << situation.foodProductionAlive
              << " foodRate=" << AIActions::GetResourceRate(
                     player->economyTelemetry.current.productionRatesPerMinute,
                     ResourceType::FOOD_PROVISIONS)
              << "/" << AIActions::GetResourceRate(
                     player->economyTelemetry.current.consumptionRatesPerMinute,
                     ResourceType::FOOD_PROVISIONS)
              << " roster=" << situation.rosterCount
              << " manpower=" << situation.manpower
              << " unconnected=" << situation.unconnectedPositionIds.size()
              << " actionCooldown=" << std::max(0.0, actionCadenceTimer)
              << " action=" << (lastDecisionAction.empty() ? "none" : lastDecisionAction)
              << " scores=" << (lastScoreSummary.empty() ? "none" : lastScoreSummary)
              << " topNeeds=" << topNeeds[0].second << ':' << topNeeds[0].first
              << ',' << topNeeds[1].second << ':' << topNeeds[1].first
              << ',' << topNeeds[2].second << ':' << topNeeds[2].first
              << " rejected=" << (lastRejectedActions.empty() ? "none" : lastRejectedActions)
              << " pressures={threat:" << situation.Threat()
              << ",hq:" << (1.0 - situation.hqHpRatio)
              << ",economy:" << focusPriorities.economy
              << ",logistics:" << focusPriorities.logistics
              << ",mobilization:" << focusPriorities.mobilization << '} '
              << " posture=" << posture
              << " focuses=" << (lastFocusSummary.empty() ? "none" : lastFocusSummary)
              << " stored={wood:" << AIActions::CountStoredResource(player, ResourceType::WOOD)
              << ",stone:" << AIActions::CountStoredResource(player, ResourceType::STONE)
              << ",planks:" << AIActions::CountStoredResource(player, ResourceType::PLANKS)
              << ",food:" << AIActions::CountStoredResource(player, ResourceType::FOOD_PROVISIONS)
              << ",tools:" << AIActions::CountStoredResource(player, ResourceType::TOOLS) << "}"
              << " foodState=";
        for (Building* building : player->GetTrackedBuildings())
        {
            if (building == nullptr || building->owner != player)
                continue;
            const bool foodBuilding = building->buildingType == BuildingType::Well ||
                building->buildingType == BuildingType::WheatFarm ||
                building->buildingType == BuildingType::Windmill ||
                building->buildingType == BuildingType::Bakery ||
                building->buildingType == BuildingType::HuntersHut ||
                building->buildingType == BuildingType::Inn;
            if (!foodBuilding)
                continue;
            const auto* production = building->GetComponent<ProductionComponent>();
            const auto* workers = building->GetComponent<WorkerComponent>();
            const auto* logistics = building->GetComponent<LogisticsComponent>();
            const auto* recipe = building->GetComponent<RecipeComponent>();
            trace << static_cast<int>(building->buildingType) << ':'
                  << (building->IsUnderConstruction() ? 'c' : 'r')
                  << ':' << (workers != nullptr ? workers->assigned : -1) << '/'
                  << (workers != nullptr ? workers->GetModifiedCapacity(*building) : -1)
                  << ':' << (recipe != nullptr ? recipe->activeRecipeIndex : -1) << ':';
            if (production != nullptr)
            {
                trace << (production->started ? 's' : 'i') << ':';
                for (const auto& [type, buffer] : production->inputBuffers)
                    trace << 'i' << rt2s(type) << '=' << buffer.buffer.size() << '/' << buffer.bufferSize;
                for (const auto& [type, buffer] : production->outputBuffers)
                    trace << 'o' << rt2s(type) << '=' << buffer.buffer.size() << '/' << buffer.bufferSize;
            }
            if (logistics != nullptr)
                for (const auto& [type, amount] : logistics->pendingRequests)
                    trace << 'p' << rt2s(type) << '=' << amount;
            if (building->buildingType == BuildingType::Inn)
            {
                trace << "r=";
                for (const auto& receiver : building->GetReceiverViews())
                {
                    if (receiver.type != ResourceType::FOOD_PROVISIONS || receiver.building == nullptr)
                        continue;
                    trace << receiver.building->id << (receiver.alternative ? 'a' : 'p') << ',';
                }
            }
            trace << ';';
        }
        trace << " smithState=";
        for (Building* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
        {
            if (building == nullptr || building->owner != player ||
                building->buildingType != BuildingType::Smith || building->IsUnderConstruction())
                continue;
            const auto* production = building->GetComponent<ProductionComponent>();
            const auto* workers = building->GetComponent<WorkerComponent>();
            const auto* recipe = building->GetComponent<RecipeComponent>();
            trace << building->id << ":w=" << (workers != nullptr ? workers->assigned : -1)
                  << '/' << (workers != nullptr ? workers->GetModifiedCapacity(*building) : -1)
                  << ":start=" << (production != nullptr && production->started ? 1 : 0)
                  << ":r=" << (recipe != nullptr ? recipe->activeRecipeIndex : -1);
            if (production != nullptr)
            {
                for (const auto& [type, buffer] : production->inputBuffers)
                    trace << ":i" << rt2s(type) << '=' << buffer.buffer.size() << '/' << buffer.bufferSize;
                for (const auto& [type, buffer] : production->outputBuffers)
                    trace << ":o" << rt2s(type) << '=' << buffer.buffer.size() << '/' << buffer.bufferSize;
            }
            trace << ';';
        }
        trace << " barracksState=";
        for (Building* building : player->GetTrackedBuildingsWithComponent<RecruitmentComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;
            const auto* recruitment = building->GetComponent<RecruitmentComponent>();
            const auto* local = building->GetComponent<LocalResourceBufferComponent>();
            trace << building->id << ":food=";
            if (local != nullptr)
            {
                auto it = local->buffers.find(ResourceType::FOOD_PROVISIONS);
                trace << (it != local->buffers.end() ? static_cast<int>(it->second.buffer.size()) : -1);
            }
            else
                trace << -1;
            trace << ":q=";
            if (recruitment != nullptr)
                for (const auto& entry : recruitment->queue)
                    trace << entry.unitDefId << (entry.resourcesReady ? '+' : '-') << ',';
            if (recruitment != nullptr)
            {
                trace << ":blocks=";
                for (const UnitDefinition* def : RankUnitChoices(situation))
                {
                    if (def == nullptr)
                        continue;
                    const std::string reason = recruitment->DiagnoseRecruitmentBlock(*building, def->id);
                    if (!reason.empty())
                        trace << def->id << '=' << reason << ',';
                }
            }
            trace << ';';
        }
        decisionTrace[decisionTraceNext] = trace.str();
        decisionTraceNext = (decisionTraceNext + 1) % decisionTrace.size();
        decisionTraceCount = std::min(decisionTraceCount + 1, decisionTrace.size());
    }

    // Focuses are zero-cost in resources, but starting one is still a real
    // command and consumes the same cadence budget as every other action.
    if (actionCadenceTimer <= 0.0 && player->focuses.GetActiveFocusId().empty() &&
        TryStartBestFocus(world, player, situation))
    {
        lastDecisionAction = "focus";
        ConsumeActionCadence();
        return;
    }

    if (roadTimer <= 0.0 && actionCadenceTimer <= 0.0)
    {
        roadTimer = RoadMaintenanceInterval;
        if (AIActions::TryBuildRoads(world, player, actions))
        {
            lastDecisionAction = "roads";
            ConsumeActionCadence();
            return;
        }
    }

    // The final pre-Barracks reserve is a transactional opening step. Once
    // the food foundation and Smith exist, do not let a lower-scored deficit
    // spend the same WOOD/STONE/PLANKS on another producer or tower while the
    // military milestone is merely waiting for its exact cost. Production and
    // the road-maintenance path continue to run; the next decision retries the
    // Barracks transaction deterministically.
    if (actionCadenceTimer <= 0.0 && situation.barracksCount == 0 && situation.basicEconomyEstablished &&
        AIActions::CountCompletedOrQueuedBuildings(world, player, BuildingType::Smith) > 0 &&
        HasCompletedFoodFoundation(player))
    {
        if (TryUnlockBarracks(world, player, situation))
        {
            lastDecisionAction = "barracks-opening";
            ConsumeActionCadence();
            return;
        }
        lastRejectedActions = "barracks=not_feasible";
        return;
    }

    if (actionCadenceTimer > 0.0 || decisionTimer > 0.0)
        return;
    decisionTimer = GetAIEconomyBias().decisionIntervalSeconds;

    // Score every need, then try them in (score desc, enum asc) order until
    // one produces a real command — mirrors the old unified pool's "best
    // executable candidate wins" without the scoring soup.
    std::array<int, static_cast<int>(AINeed::Count)> order{};
    std::array<double, static_cast<int>(AINeed::Count)> scores{};
    for (int i = 0; i < static_cast<int>(AINeed::Count); i++)
    {
        order[i] = i;
        scores[i] = ScoreNeed(static_cast<AINeed>(i), situation) * (1.0 + personalityNeedBias[i]);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return scores[a] > scores[b]; });

    lastScoreSummary.clear();
    for (int index : order)
    {
        if (!lastScoreSummary.empty())
            lastScoreSummary += ',';
        lastScoreSummary += std::to_string(index) + '=' + std::to_string(scores[index]);
    }

    lastDecisionAction = "none";
    lastRejectedActions.clear();
    for (int index : order)
    {
        if (scores[index] < MinActionableScore)
            break;
        if (ExecuteNeed(static_cast<AINeed>(index), world, player, situation))
        {
            lastDecisionAction = std::to_string(index);
            ConsumeActionCadence();
            return;
        }
        if (!lastRejectedActions.empty())
            lastRejectedActions += ',';
        lastRejectedActions += std::to_string(index) + "=not_feasible";
    }
    if (lastRejectedActions.empty())
        lastRejectedActions = "none";
}

void UtilityAIModel::ConsumeActionCadence()
{
    actionCadenceTimer = GetAIDifficultyProfile(difficulty).actionIntervalSeconds;
}

std::string UtilityAIModel::GetDecisionTrace() const
{
    std::ostringstream output;
    const std::size_t first = decisionTraceCount == decisionTrace.size()
        ? decisionTraceNext
        : 0;
    for (std::size_t i = 0; i < decisionTraceCount; ++i)
    {
        if (i != 0)
            output << '\n';
        output << decisionTrace[(first + i) % decisionTrace.size()];
    }
    return output.str();
}

AISituation UtilityAIModel::Sense(GameWorld& world, Player* player)
{
    AISituation s;

    // Track state: deployed units, split mine vs. heading-at-me. std::map
    // iteration — deterministic.
    std::set<int> incomingOpponents;
    for (const auto& [instanceId, unit] : world.GetDeployedUnits())
    {
        if (unit.state == BattleUnitState::Dying)
            continue;
        auto ownerIt = world.GetPlayerHandler().players.find(unit.ownerPlayerId);
        if (ownerIt == world.GetPlayerHandler().players.end() || ownerIt->second == nullptr)
            continue;

        if (unit.ownerPlayerId != player->id && unit.routeToPlayerId == player->id)
        {
            s.enemyIncomingCount++;
            s.enemyIncomingStrength += UnitStrength(unit, *ownerIt->second);
            incomingOpponents.insert(unit.ownerPlayerId);
        }
    }

    for (const auto& [instanceId, unit] : world.GetDeployedUnits())
    {
        if (unit.state == BattleUnitState::Dying || unit.ownerPlayerId != player->id ||
            !incomingOpponents.contains(unit.routeToPlayerId))
            continue;
        s.myDeployedCount++;
        s.myDeployedStrength += UnitStrength(unit, *player);
    }

    Building* hq = AIActions::FindOwnedHeadquarters(player);
    if (hq != nullptr)
    {
        const auto* hqComponent = hq->GetComponent<HqComponent>();
        if (hqComponent != nullptr)
        {
            double maxHp = hqComponent->GetModifiedMaxHp(*hq);
            s.hqHpRatio = maxHp > 0.0 ? std::clamp(hqComponent->currentHp / maxHp, 0.0, 1.0) : 1.0;
        }
    }

    s.rosterCount = static_cast<int>(player->roster.units.size());
    for (const auto& [instanceId, unit] : player->roster.units)  // std::map — deterministic
        s.rosterByDef[unit.unitDefId]++;
    s.towerCount = AIActions::CountOwnedBuildings(player, BuildingType::DefenseTower);
    s.barracksCount = AIActions::CountOwnedBuildings(player, BuildingType::Barracks);
    s.smithCount = AIActions::CountOwnedBuildings(player, BuildingType::Smith);
    s.villageCount = AIActions::CountOwnedBuildings(player, BuildingType::Village);
    s.manpower = player->strategicResources.Get(StrategicResourceType::Manpower);

    // Static-defense strength + ammo state. Pure aggregation — order-safe.
    for (const auto* building : player->GetTrackedBuildingsWithComponent<TowerCombatComponent>())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        const auto* combat = building->GetComponent<TowerCombatComponent>();
        if (combat != nullptr)
        {
            int coverage = AIActions::CountTowerTrackCoverage(world, player, building);
            double coverageFactor = std::clamp(coverage / 6.0, 0.0, 1.0);
            s.towerStrength += combat->GetModifiedDamage(*building) *
                               combat->GetModifiedAttackSpeed(*building) * coverageFactor;
        }
    }
    s.arrowsStored = AIActions::CountStoredResource(player, ResourceType::ARROWS);
    s.arrowsRate = AIActions::GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute,
                                              ResourceType::ARROWS);

    for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction())
            s.productionBuildingCount++;  // pure count — order-independent

    // Rate alone false-positives "dead" once storage tops up: a healthy Inn
    // with a full output buffer legitimately reads 0/min production (nothing
    // to push out) even though the chain works fine (playtest 2026-07-19: AI
    // stopped developing — no more Mines, no University — after its early
    // buildout filled every buffer, since Research's University trigger also
    // gates on foodProductionAlive). A raw "stored > 0" check was tried and
    // reverted (harness catch): a starting FOOD_PROVISIONS stockpile made
    // that reads "alive" for a long time regardless of whether the chain
    // works at all, which changed Economy's behavior enough to spam rejected
    // commands on the stress-test seed.
    // Checking producer HEALTH instead of a stock snapshot sidesteps that
    // confound entirely: a completed, connected, manned Inn is alive even
    // mid-topped-up-buffer; one with any real problem flag isn't, regardless
    // of leftover starting stock.
    AIActions::AIResourceDiagnosis foodDiagnosis = AIActions::DiagnoseResourceNeed(
        player, ResourceType::FOOD_PROVISIONS, 0, &consumptionBias, &priorityWeights);
    s.foodProductionAlive =
        AIActions::GetResourceRate(player->economyTelemetry.current.productionRatesPerMinute,
                                   ResourceType::FOOD_PROVISIONS) > 0 ||
        (foodDiagnosis.hasProducerBuilding && !foodDiagnosis.logisticsProblem &&
         !foodDiagnosis.storageProblem && !foodDiagnosis.manpowerProblem);
    s.foodProvisionsStored = AIActions::CountStoredResource(player, ResourceType::FOOD_PROVISIONS);
    s.populationCap = player->GetPopulationCap();
    s.totalPopulation = player->GetTotalPopulation();

    s.universityCount = AIActions::CountOwnedBuildings(player, BuildingType::University);
    s.hasIdleUniversity = AIActions::FindUniversity(player) != nullptr;
    // Tier-2 priority handoff (2026-07-20): ai.rtsdata's `priority` discount
    // exists to win the OPENING build order (tier-1 wood/stone/food ties
    // against tier-2 iron/tools/swords), not to suppress tier-2 forever. But
    // the tier-1 amortized consumption bias is permanent — construction keeps
    // draining stock below the "low reserve" threshold indefinitely — so
    // without an explicit handoff tier-2 never naturally got a turn.
    //
    // Threshold is `tower_readiness_buildings` (default 4, ai.rtsdata) — the
    // SAME "economy has some footing" gate Defense already uses — not the
    // full 12-step OpeningPlan. Harness catch (2026-07-20): requiring the
    // entire opening plan complete meant this handoff (and the RecruitDeploy
    // cost-chain builder in ExecuteRecruitDeploy, gated on the same flag)
    // never engaged within a realistic playtest/test window at all — Foundry
    // never got built despite everything else in Tasks 1-3 being correct,
    // simply because the gate never opened. A handful of standing producers
    // is enough evidence the opening bootstrap is past its most fragile
    // stretch (see ExecuteRecruitDeploy's comment for what that fragility was).
    s.economyEstablished = s.foodProductionAlive &&
        s.productionBuildingCount >= GetAIEconomyBias().towerReadinessBuildings;
    s.basicEconomyEstablished =
        s.productionBuildingCount >= GetAIEconomyBias().towerReadinessBuildings;
    s.offensiveTowerDamageShare = lastOffensiveTowerDamageShare;
    s.offensiveUnitDamageShare = lastOffensiveUnitDamageShare;
    s.offensiveHqDamageShare = lastOffensiveHqDamageShare;
    const std::map<ResourceType, double>* effectiveWeights = s.economyEstablished ? nullptr : &priorityWeights;

    // Resource deficits. Candidate list is deterministic: the manpower
    // lifeline first, then every unit-cost resource (catalog is a std::map),
    // tower ammo when towers exist, then everything currently consumed.
    std::vector<ResourceType> candidates;
    auto pushCandidate = [&](ResourceType type)
    {
        if (type == ResourceType::Null)
            return;
        if (std::find(candidates.begin(), candidates.end(), type) == candidates.end())
            candidates.push_back(type);
    };
    pushCandidate(ResourceType::FOOD_PROVISIONS);
    for (const auto& [defId, def] : GetUnitCatalog())
        for (const auto& cost : def.cost)
            pushCandidate(cost.type);
    if (s.towerCount > 0)
        pushCandidate(ResourceType::ARROWS);
    // Every amortized-cost resource is always a candidate — the bias exists
    // precisely so these are provisioned even with zero real consumption yet.
    for (const auto& [type, amount] : consumptionBias)
        pushCandidate(type);
    for (const auto& [type, rate] : player->economyTelemetry.current.consumptionRatesPerMinute)
        if (rate > 0)
            pushCandidate(type);

    for (ResourceType type : candidates)
    {
        AIActions::AIResourceDiagnosis diagnosis =
            AIActions::DiagnoseResourceNeed(player, type, 0, &consumptionBias, effectiveWeights);
        if (diagnosis.urgency > 0.2)
            s.deficits.push_back({type, diagnosis.urgency});
    }

    // Provision toward the composition's preferred pick before telemetry has
    // any consumption signal for it: a cost resource with (almost) no stock
    // and no production gets a fixed mid-urgency deficit, driving
    // EconomySustain to raise its chain (e.g. Smith for swords) while the
    // recruiter falls back to whatever IS affordable in the meantime. Also
    // priority-weighted (2026-07-19) so a low-priority preferred cost (a
    // sword tier, weight ~0.4) doesn't jump the queue ahead of the actual
    // tier-1 economy just because it's the composition's top unit pick —
    // unless the economy is already established, in which case the full-weight
    // handoff above applies here too.
    std::vector<const UnitDefinition*> ranked = RankUnitChoices(s);
    if (!ranked.empty() && ranked.front() != nullptr)
    {
        for (const auto& cost : ranked.front()->cost)
        {
            int stored = AIActions::CountStoredResource(player, cost.type);
            int rate = AIActions::GetResourceRate(
                player->economyTelemetry.current.productionRatesPerMinute, cost.type);
            bool alreadyListed = std::any_of(s.deficits.begin(), s.deficits.end(),
                [&](const AISituation::Deficit& d) { return d.resource == cost.type; });
            if (!alreadyListed && stored < cost.amount * 2 && rate == 0)
            {
                double weight = 1.0;
                if (effectiveWeights != nullptr)
                {
                    auto it = effectiveWeights->find(cost.type);
                    weight = it != effectiveWeights->end() ? it->second : 1.0;
                }
                s.deficits.push_back({cost.type, std::clamp(0.5 * weight, 0.0, 1.0)});
            }
        }
    }

    std::stable_sort(s.deficits.begin(), s.deficits.end(),
                     [](const AISituation::Deficit& a, const AISituation::Deficit& b)
                     {
                         if (a.urgency != b.urgency)
                             return a.urgency > b.urgency;
                         return static_cast<int>(a.resource) < static_cast<int>(b.resource);
                     });
    // Widened 4 -> 6 (2026-07-20): with tier-1's bias permanently live, tier-2
    // entries used to fall off the end of the list even in cycles where
    // tier-1 only transiently spiked, never getting a turn against the
    // deficit ladder at all.
    if (s.deficits.size() > 6)
        s.deficits.resize(6);

    // Connectivity audit on its own (slower) cadence — carry the previous
    // answer between audits.
    connectivityTimer -= SenseInterval;
    if (connectivityTimer <= 0.0)
    {
        connectivityTimer = ConnectivityInterval;
        // Sorted by building id — the repair action serves the FIRST entry,
        // so order is simulation-visible (see docs/tech_debt.md).
        std::vector<Building*> candidates2(player->GetTrackedBuildings().begin(),
                                           player->GetTrackedBuildings().end());
        std::sort(candidates2.begin(), candidates2.end(),
                  [](Building* a, Building* b) { return a->id < b->id; });
        lastUnconnectedPositionIds.clear();
        for (Building* building : candidates2)
        {
            if (building == nullptr || building->owner != player)
                continue;
            if (IsRoadLike(building->buildingType) || AIActions::IsStorageHub(building))
                continue;
            const auto* logistics = building->GetComponent<LogisticsComponent>();
            if (logistics == nullptr)
                continue;
            if (!logistics->IsConnectedToRoadNetwork(*building))
                lastUnconnectedPositionIds.push_back(building->positionId);
        }
    }
    s.unconnectedPositionIds = lastUnconnectedPositionIds;

    return s;
}

double UtilityAIModel::ScoreNeed(AINeed need, const AISituation& s) const
{
    switch (need)
    {
        case AINeed::Defense:
        {
            // Standing garrison gap + the live-pressure formula from the
            // plan: half lane pressure, half HQ damage taken.
            double score = s.towerCount < DesiredTowerCount(s) ? 0.3 : 0.0;
            if (s.towerCount > 0 && s.arrowsStored == 0 && s.arrowsRate == 0)
                score = std::max(score, 0.45);  // towers without ammo are decoration
            if (s.enemyIncomingCount > 0 && s.towerCount < DesiredTowerCount(s))
                score = std::max(score, 0.98);  // immediate, buildable defense wins this decision
            double pressure = std::clamp(0.5 * s.Threat() + 0.5 * (1.0 - s.hqHpRatio), 0.0, 1.0);
            return std::max(score, pressure);
        }
        case AINeed::RecruitDeploy:
        {
            // The prime TD objective — permanently high, higher still when
            // the lane is quiet (push!), and dominant when an emergency
            // deploy is possible.
            double score = 0.55 + 0.25 * (1.0 - std::clamp(s.Threat(), 0.0, 1.0));
            if (s.Threat() > EmergencyThreat && s.rosterCount > 0)
                score = 0.95;
            return score;
        }
        case AINeed::EconomySustain:
        {
            // Manpower-reserve emergency (user design 2026-07-19) deliberately
            // bypasses the 0.75 hard cap below: recruitment is already blocked
            // without manpower (DiagnoseRecruitmentBlock refuses it), so
            // pushing this above RecruitDeploy's floor doesn't starve
            // militaries — it unblocks their one missing resource.
            if (ManpowerEmergency(s, GetAIEconomyBias().manpowerReserve))
                return 0.9;

            // Critical: the manpower lifeline is dead. Deliberately the only
            // path allowed to score above LogisticsRepair's floor (0.65) —
            // see the routine cap below for why.
            double criticalScore = 0.0;
            if (!s.foodProductionAlive)
                criticalScore = s.barracksCount > 0 ? 0.9 : 0.75;

            double routineScore = 0.0;
            if (s.productionBuildingCount < static_cast<int>(OpeningPlan.size()))
                routineScore = 0.6;  // opening: build out the base before anything subtler
            if (!s.deficits.empty())
                routineScore = std::max(routineScore, std::clamp(s.deficits.front().urgency, 0.0, 1.0));
            // Routine score capped BELOW LogisticsRepair's floor (0.65), not
            // just RecruitDeploy's (0.8). Harness catch 2026-07-19: chasing a
            // brand-new resource chain (e.g. IRON, now reachable via the
            // COAL/IRON_ORE starting patches) can drive deficit urgency up to
            // the old 0.75 cap, exactly tying a 2-building LogisticsRepair
            // score — and the stable-sort tie-break (enum order) always
            // favored Economy, so it kept greenlighting the new chain forever
            // and Logistics never got a turn: a permanent bridge-
            // affordability deadlock (starting stock burned on the new
            // producer instead of the one connection still unfinished). Only
            // the true food-dead emergency above is allowed to preempt
            // logistics now.
            routineScore = std::min(routineScore, 0.6);

            return std::max(criticalScore, routineScore);
        }
        case AINeed::LogisticsRepair:
        {
            // Deadlock fix (playtest 2026-07-19, user report: "AI can't build
            // bridges when a road cuts buildings off"): a lone disconnected
            // building used to score only 0.4 — BELOW EconomySustain's
            // routine "opening: build out the base" floor (0.6) — so the AI
            // kept greenlighting new producers (each with a real, larger
            // build cost) before ever finishing the connection on the one it
            // just placed. On a map where that connection needs a Bridge
            // (PLANKS+STONE, cheap per tile but still real), the starting
            // stock got fully spent on 5-6 new buildings within 15 seconds,
            // and once WOOD/PLANKS production itself depends on the very
            // building stuck across the track, the deadlock is permanent: no
            // stock -> can't afford the bridge -> can't connect -> that
            // building's own output stays stranded -> still no stock.
            // Any disconnected building now outranks Economy's routine
            // opening score, so the AI finishes what it built before piling
            // on more — Economy's CRITICAL escalations (dead food chain,
            // high real deficit, both capped at 0.75) still win when they
            // should.
            if (s.unconnectedPositionIds.empty())
                return 0.0;
            return std::min(1.0, 0.65 + 0.1 * static_cast<double>(s.unconnectedPositionIds.size() - 1));
        }
        case AINeed::Research:
        {
            if (s.Threat() > 0.5)
                return 0.0;  // guns before books
            double score = 0.0;
            if (s.hasIdleUniversity)
                score = 0.35;  // a standing idle University is sunk cost — use it
            // Building a University is an investment — only once the economy
            // demonstrably carries itself.
            if (s.universityCount == 0 && s.foodProductionAlive && s.productionBuildingCount >= 6)
                score = std::max(score, 0.3);
            return score;
        }
        case AINeed::Count:
            break;
    }
    return 0.0;
}

bool UtilityAIModel::ExecuteNeed(AINeed need, GameWorld& world, Player* player, const AISituation& s)
{
    switch (need)
    {
        case AINeed::Defense:
            return ExecuteDefense(world, player, s);
        case AINeed::RecruitDeploy:
            return ExecuteRecruitDeploy(world, player, s);
        case AINeed::EconomySustain:
            return ExecuteEconomy(world, player, s);
        case AINeed::LogisticsRepair:
            return ExecuteLogistics(world, player, s);
        case AINeed::Research:
            return ExecuteResearch(world, player, s);
        case AINeed::Count:
            break;
    }
    return false;
}

bool UtilityAIModel::ExecuteEconomy(GameWorld& world, Player* player, const AISituation& s)
{
    // Manpower-reserve emergency (user design 2026-07-19): villages already
    // near capacity won't grow manpower further on their own, so another
    // Village — converting food into manpower — is the immediate fix. Same
    // condition ScoreNeed uses to escalate above the 0.75 cap, so the two
    // can never disagree about whether this is live.
    const bool manpowerEmergency = ManpowerEmergency(s, GetAIEconomyBias().manpowerReserve);
    if (manpowerEmergency &&
        player->CanBuildDefinition(GetBuildingDefinition(BuildingType::Village)))
    {
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::Village,
                                                  TileType::GRASS, nullptr, actions);
        if (anchor.x >= 0 && AIActions::TrySubmitBuild(world, player, BuildingType::Village, anchor, actions))
            return true;
    }
    // Save the stock being accumulated for the capacity fix. A finite food
    // reserve is enough to make the new Village useful even before the
    // renewable Inn chain has been completed.
    if (manpowerEmergency)
        return false;

    // Keep the smelter on IRON regardless of what else is going on (a fresh
    // Foundry defaults to the COPPER recipe — index 0 — and the AI never mines
    // COPPER_ORE, so without this it stalls forever and produces no iron; see
    // AIActions::TrySwitchRecipeFor). Fires once, as soon as a Foundry stands,
    // even while the opening plan is still grinding out the food chain — the
    // recipe switch must NOT wait behind the deficit ladder below.
    if (AIActions::TryAssignRecipe(world, player, BuildingType::Foundry, 0, ResourceType::IRON))
        return true;

    // A village/storage auto-connection can claim the Inn's primary output
    // after the opening wave. Repair the direct food lanes before diagnosing
    // zero production; otherwise the chain looks dead and the reactive ladder
    // keeps adding duplicate Inns while the Barracks queue starves.
    // Every level uses the full recovery policy; starting stock is the only
    // difficulty lever.
    if (s.barracksCount > 0 && HasCompletedFoodChain(player) &&
        TryScaleFoodRecovery(world, player))
        return true;

    // After Barracks, EconomySustain must still be able to finish the
    // renewable food chain. Without this handoff, a Hard start can score this
    // need above RecruitDeploy, then return from the ordered plan's
    // unaffordable step forever while the military remains on militia-only
    // fallback.
    if (s.barracksCount > 0 && (!HasCompletedFoodChain(player) || !s.foodProductionAlive) &&
        TryFoodRecoveryPlan(world, player))
        return true;

    if (s.barracksCount > 0 && s.foodProductionAlive == false &&
        TryScaleFoodRecovery(world, player))
        return true;

    // Opening bootstrap runs FIRST, before the reactive deficit ladder. The
    // plan front-loads the basic materials and the (cheap) iron chain, so those
    // get built off the opening stock before anything competes for it — the AI
    // reliably stands up real coal/iron production instead of leaning on a big
    // starting stockpile (user report 2026-07-20: "AI wciąż nie buduje żelaza/
    // węgla"). When the plan places a step it consumes the cycle; when it's
    // saving up (unaffordable next step) or complete it returns false and the
    // deficit ladder below takes over reactive scaling/sustain.
    if (TryOpeningPlan(world, player))
        return true;

    // An incomplete opening plan that could not afford its next step is a
    // SAVING state. Falling through to the reactive deficit ladder here used
    // the reserved WOOD/PLANKS on duplicate farms, wells and mills, pushing
    // the Inn farther away every cycle. Keep the bootstrap transactional:
    // one of each required building first, scaling only after the chain is
    // structurally complete.
    bool openingComplete = true;
    for (const OpeningStep& step : OpeningPlan)
    {
        bool satisfied = step.gateResource != ResourceType::Null
            ? AIActions::CountProducersOrPendingForResource(player, step.gateResource) >= step.target
            : AIActions::CountCompletedOrQueuedBuildings(world, player, step.type) >= step.target;
        if (!satisfied)
        {
            openingComplete = false;
            break;
        }
    }
    if (!openingComplete)
    {
        // The opening is ordered, but it must still have a recovery path when
        // the next building is temporarily unaffordable. Diagnose the first
        // missing step's actual cost and produce its missing input; otherwise
        // Primitive gets stuck forever behind a valid plan that can never
        // reach its own next transaction.
        for (const OpeningStep& step : OpeningPlan)
        {
            const bool satisfied = step.gateResource != ResourceType::Null
                ? AIActions::CountProducersOrPendingForResource(player, step.gateResource) >= step.target
                : AIActions::CountCompletedOrQueuedBuildings(world, player, step.type) >= step.target;
            if (satisfied)
                continue;

            const auto& definition = GetBuildingDefinition(step.type);
            if (player->CanBuildDefinition(definition))
            {
                Vec2i anchor = AIActions::FindBuildAnchor(
                    world, player, step.type, step.preferredTile, nullptr, actions);
                if (anchor.x < 0)
                    continue; // defer a locally full district and inspect the next step
                break;
            }
            for (const auto& cost : player->GetEffectiveBuildCosts(definition))
            {
                if (AIActions::CountStoredResource(player, cost.type) >= cost.amount)
                    continue;
                const int rate = AIActions::GetResourceRate(
                    player->economyTelemetry.current.productionRatesPerMinute, cost.type);
                if (rate <= 0 && TryBuildProducerFor(world, player, cost.type))
                    return true;
            }
            return false;
        }
        return false;
    }

    // Finite extractors must be replaced BEFORE their own output reaches
    // zero, but never at the expense of the one-time progression bootstrap.
    // Both Woodcutter and Mine require materials derived from their own
    // output, so this maintenance runs immediately AFTER the opening becomes
    // structurally complete and before ordinary scaling deficits.
    if (s.basicEconomyEstablished)
    {
        constexpr int WoodRenewalReserve = 160;
        constexpr int StoneRenewalReserve = 80;
        if (RemainingExtractorRichness(player, ResourceType::WOOD) <= WoodRenewalReserve &&
            TryBuildProducerFor(world, player, ResourceType::WOOD))
            return true;
        if (RemainingExtractorRichness(player, ResourceType::STONE) <= StoneRenewalReserve &&
            TryBuildProducerFor(world, player, ResourceType::STONE))
            return true;
    }

    // Deficit backoff (2026-07-19): a resource that just failed (every
    // producer option unaffordable, not merely a missing anchor — see
    // AIActionState::deficitBackoff) is skipped for a few cycles so a
    // persistently-stuck top deficit can't starve every lower-priority one
    // of a turn forever. Without this, a resource whose OWN chain is wedged
    // (e.g. its producer's output never gets hauled away — a pre-existing
    // logistics issue, not something the priority ladder can fix) retries
    // itself every single cycle and the AI never tries anything else.
    for (const auto& deficit : s.deficits)
    {
        if (actions.deficitBackoff.count(deficit.resource) > 0)
            continue;
        if (TryBuildProducerFor(world, player, deficit.resource))
        {
            // Rotation fix (2026-07-20): a SUCCESS used to leave the very top
            // deficit free to win again next cycle too (nothing but the new
            // producer's own construction time slowed it down), so a lower-
            // priority deficit further down the ladder could starve for many
            // cycles in a row even though it would have succeeded on its own.
            // Short cooldown (not the 12s failure one — this resource isn't
            // stuck, it just went) lets the next 2-3 cycles serve other
            // deficits before this one is eligible again.
            actions.deficitBackoff[deficit.resource] = 4.0;
            return true;
        }
        actions.deficitBackoff[deficit.resource] = 12.0;
    }

    return false;
}

bool UtilityAIModel::TryBuildProducerFor(GameWorld& world, Player* player, ResourceType resource)
{
    // Tier-2 priority handoff (2026-07-20, see Sense's economyEstablished):
    // once the economy has some real footing and food is alive, stop
    // discounting tier-2's urgency here too — otherwise IRON/TOOLS/swords
    // could win a slot on the deficit ladder (Sense) but still lose the
    // chain-walk/duplicate-guard math below to a tier-1 resource that
    // out-weighs them 1.0 vs 0.4.
    const std::map<ResourceType, double>* effectiveWeights =
        situation.economyEstablished ? nullptr : &priorityWeights;

    // Walk down the input chain: if the producer of `resource` is starved of
    // an input, build toward that input instead (bounded depth). missingInputs
    // order comes from the static building catalog — deterministic.
    ResourceType target = resource;
    for (int depth = 0; depth < 3; depth++)
    {
        AIActions::AIResourceDiagnosis diagnosis =
            AIActions::DiagnoseResourceNeed(player, target, depth, &consumptionBias, effectiveWeights);
        if (diagnosis.missingInputs.empty())
            break;
        target = diagnosis.missingInputs.front();
    }

    // Deadlock fix (rdzeń zgłoszenia 2026-07-19: "AI stawia podejrzanie dużo
    // chat drwala, nie rozwija się"): re-diagnose the resolved target and
    // check whether an EXISTING producer is the problem before building
    // another one. Without this, a stalled/bottlenecked/unmanned producer's
    // output reads as 0, urgency stays high, and every cycle answers with
    // "build one more" — the new producer inherits the same disconnection/
    // manpower drought, stalls too, and the ladder just grows a forest of
    // producers instead of ever developing. Fixing an EXISTING producer is
    // LogisticsRepair's (road/storage bottleneck) or the manpower-reserve
    // emergency's (unmanned) job, not this function's.
    //
    // Exactly one redundant producer is still allowed through (harness catch
    // 2026-07-19, AIBehaviorHarnessTests regression from the initial
    // unconditional block): when the lone existing producer is stuck behind a
    // logistics problem LogisticsRepair itself can't clear yet (a Bridge one
    // tile away that's still unaffordable — a real, not-instantly-fixable
    // deadlock), refusing ANY duplicate leaves the AI with no path forward at
    // all while it waits, since the thing that would unblock it (the bridge)
    // needs the very resource that's now stuck. A second producer placed on
    // fresh, already-connected ground routes around the stuck one instead of
    // literally being a copy of it. Once two are unhealthy, that path is
    // exhausted too and the hard block resumes.
    // Cause-D fix (2026-07-20): before building a new producer for `target`,
    // check whether an EXISTING multi-recipe building can produce it by
    // switching recipe. A Foundry defaults to COPPER and a Smith to TOOLS
    // (recipe index 0), so a Foundry the AI built expressly to make IRON sits
    // on the Copper recipe forever, and CountProducersOfResource(IRON) reads 0
    // — which would otherwise make this function build ANOTHER Copper Foundry
    // every cycle. Switching the recipe is the actual fix; it also stops the
    // redundant-Foundry spam.
    if (AIActions::TrySwitchRecipeFor(world, player, target))
        return true;

    std::vector<AIActions::AIProducerOption> options = AIActions::FindProducerOptions(target);
    // Counted per-PRODUCT (2026-07-20 fix), not per-BuildingType: a Mine on a
    // COAL tile and a Mine on an IRON_ORE tile are different producers as far
    // as this guard (and the diversify-sort below) are concerned — see
    // CountProducersOfResource.
    int ownedProducers = AIActions::CountProducersOfResource(player, target);
    // During bootstrap, four identical producers are already a strong signal
    // that the bottleneck is elsewhere. This prevents one noisy deficit from
    // dissolving the thematic base into an extractor carpet before the wider
    // economy has come online; mature economies may scale past it.
    if (!situation.economyEstablished && ownedProducers >= 4)
        return false;

    AIActions::AIResourceDiagnosis targetDiagnosis =
        AIActions::DiagnoseResourceNeed(player, target, 0, &consumptionBias, effectiveWeights);
    if (targetDiagnosis.hasProducerBuilding && ownedProducers >= 2 &&
        (targetDiagnosis.logisticsProblem || targetDiagnosis.storageProblem || targetDiagnosis.manpowerProblem))
        return false;
    std::stable_sort(options.begin(), options.end(),
                     [&](const AIActions::AIProducerOption& a, const AIActions::AIProducerOption& b)
                     {
                         int ownedA = AIActions::CountOwnedBuildings(player, a.buildingType);
                         int ownedB = AIActions::CountOwnedBuildings(player, b.buildingType);
                         if (ownedA != ownedB)
                             return ownedA < ownedB;  // diversify before duplicating
                         return static_cast<int>(a.buildingType) < static_cast<int>(b.buildingType);
                     });

    for (const auto& option : options)
    {
        // One multi-recipe workshop is enough before the first raid. A second
        // Smith used to consume more WOOD/STONE/PLANKS than the missing Inn,
        // delaying both food and offense so it could specialize in a recipe
        // the army was not yet able to use. Additional Smiths become valid
        // after the first real launch, when parallel tools/weapons/ammunition
        // lines are an actual throughput need.
        if (option.buildingType == BuildingType::Smith && !hasLaunchedWave &&
            AIActions::CountOwnedBuildings(player, BuildingType::Smith) >= 1)
            continue;
        if (!player->CanBuildDefinition(GetBuildingDefinition(option.buildingType)))
            continue;
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, option.buildingType,
                                                  option.terrain, nullptr, actions);
        if (anchor.x < 0)
            continue;
        if (AIActions::TrySubmitBuild(world, player, option.buildingType, anchor, actions))
            return true;
    }
    return false;
}

bool UtilityAIModel::TryOpeningPlan(GameWorld& world, Player* player)
{
    for (const OpeningStep& step : OpeningPlan)
    {
        // Terrain-specific mines gate on an actual producer of their resource
        // (matched by terrain-resolved product, pending builds included) — a
        // type-count gate can't tell a coal mine from a stone/iron-ore one, so
        // the deficit ladder's STONE mine used to satisfy the COAL step and the
        // iron chain never got its ore (2026-07-20 user report).
        bool satisfied = step.gateResource != ResourceType::Null
            ? AIActions::CountProducersOrPendingForResource(player, step.gateResource) >= step.target
            : AIActions::CountCompletedOrQueuedBuildings(world, player, step.type) >= step.target;
        if (satisfied)
            continue;

        // Build and connect the first woodcutter before placing the second.
        // Otherwise the two opening orders can independently route to the
        // same nearby forest and lay parallel road corridors before either
        // sees the other's infrastructure.
        if (step.type == BuildingType::Woodcutter && step.target > 1)
        {
            bool firstWoodcutterConnected = false;
            for (Building* building : player->GetTrackedBuildings())
            {
                if (building == nullptr || building->owner != player ||
                    building->buildingType != BuildingType::Woodcutter || building->IsUnderConstruction())
                    continue;
                const auto* logistics = building->GetComponent<LogisticsComponent>();
                if (logistics != nullptr && logistics->IsConnectedToRoadNetwork(*building))
                {
                    firstWoodcutterConnected = true;
                    break;
                }
            }
            if (!firstWoodcutterConnected)
                return false;
        }

        // The plan is strictly ordered: an unaffordable step means "save up",
        // not "skip ahead and spend on something later" — deterministic
        // patience beats churning the buffer on cheap filler.
        if (!player->CanBuildDefinition(GetBuildingDefinition(step.type)))
            return false;

        Vec2i anchor = AIActions::FindBuildAnchor(world, player, step.type, step.preferredTile, nullptr, actions);
        if (anchor.x < 0)
            // A full local district is a placement problem, not an economic
            // prerequisite failure. Defer this step and let the next legal
            // opening building keep the economy moving; otherwise one
            // temporarily unavailable 4x4 footprint can freeze the whole AI.
            continue;
        if (AIActions::TrySubmitBuild(world, player, step.type, anchor, actions))
            return true;
        // Cooldown after a just-submitted order of this type — move on.
    }
    return false;
}

bool UtilityAIModel::TryFoodRecoveryPlan(GameWorld& world, Player* player)
{
    if (player == nullptr)
        return false;

    constexpr std::array<OpeningStep, 4> FoodRecoveryPlan{{
        {BuildingType::Well,        3, TileType::GRASS, ResourceType::Null},
        {BuildingType::Bakery,     1, TileType::GRASS, ResourceType::Null},
        {BuildingType::HuntersHut, 1, TileType::WOOD,  ResourceType::Null},
        {BuildingType::Inn,        1, TileType::GRASS, ResourceType::Null},
    }};

    for (const OpeningStep& step : FoodRecoveryPlan)
    {
        if (AIActions::CountCompletedOrQueuedBuildings(world, player, step.type) >= step.target)
            continue;

        const auto& definition = GetBuildingDefinition(step.type);
        if (!player->CanBuildDefinition(definition))
        {
            for (const auto& cost : player->GetEffectiveBuildCosts(definition))
            {
                if (AIActions::CountStoredResource(player, cost.type) >= cost.amount)
                    continue;
                if (TryBuildProducerFor(world, player, cost.type))
                    return true;
            }
            return false;
        }

        Vec2i anchor = AIActions::FindBuildAnchor(world, player, step.type,
                                                  step.preferredTile, nullptr, actions);
        if (anchor.x < 0)
            continue;
        if (AIActions::TrySubmitBuild(world, player, step.type, anchor, actions))
            return true;
    }
    return false;
}

bool UtilityAIModel::TryScaleFoodRecovery(GameWorld& world, Player* player)
{
    if (player == nullptr)
        return false;

    // Pick the same Barracks deterministically as ExecuteRecruitDeploy. The
    // route command is the first recovery action, because all later checks
    // depend on deliveries reaching the actual consumers.
    const auto& recruitCapable = player->GetTrackedBuildingsWithComponent<RecruitmentComponent>();
    std::vector<Building*> barracksCandidates(recruitCapable.begin(), recruitCapable.end());
    std::sort(barracksCandidates.begin(), barracksCandidates.end(),
              [](Building* a, Building* b) { return a->id < b->id; });
    Building* barracks = nullptr;
    for (Building* building : barracksCandidates)
    {
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction())
        {
            barracks = building;
            break;
        }
    }
    if (barracks != nullptr && EnsureFoodRecoveryRoute(world, player, barracks))
        return true;

    // FOOD_PROVISIONS is a three-input product. When its output goes dead,
    // asking TryBuildProducerFor(FOOD_PROVISIONS) can resolve to another Inn
    // if the diagnosis sees no active input request. Probe the upstream chain
    // explicitly and in recipe order so the recovery action adds capacity to
    // the first genuinely missing lane instead of duplicating the sink.
    constexpr std::array<ResourceType, 5> FoodInputs{{
        ResourceType::BREAD,
        ResourceType::FLOUR,
        ResourceType::WHEAT,
        ResourceType::WATER,
        ResourceType::MEAT,
    }};
    for (ResourceType resource : FoodInputs)
    {
        const int rate = AIActions::GetResourceRate(
            player->economyTelemetry.current.productionRatesPerMinute, resource);
        const int stored = AIActions::CountStoredResource(player, resource);
        const bool producerMissing = AIActions::CountProducersOrPendingForResource(player, resource) == 0;
        if (!producerMissing && (rate > 0 || stored > 0))
            continue;
        if (TryBuildProducerFor(world, player, resource))
            return true;
    }
    return false;
}

bool UtilityAIModel::ManpowerEmergency(const AISituation& s, double manpowerReserve)
{
    // Villages already near capacity won't grow manpower any further on
    // their own — a low reserve there means "build another Village now",
    // not "wait, it'll recover" (user design 2026-07-19). A finite stored
    // food reserve can bootstrap that Village; with neither stock nor a live
    // chain, the existing "food is dead" escalation remains the real fix.
    if (!s.foodProductionAlive && s.foodProvisionsStored <= 0)
        return false;
    if (s.manpower >= manpowerReserve)
        return false;
    if (s.populationCap <= 0.0)
        return false;  // no village to be "full" yet — not this emergency
    return s.totalPopulation >= s.populationCap * 0.95;
}

double UtilityAIModel::FailedWaveStrengthThreshold(double currentThreshold, double launchedStrength)
{
    return std::max(currentThreshold, launchedStrength * 1.08);
}

double UtilityAIModel::RosterOffensiveStrength(Player* player, const std::vector<int>* selectedIds) const
{
    if (player == nullptr)
        return 0.0;
    double strength = 0.0;
    if (selectedIds != nullptr)
    {
        for (int id : *selectedIds)
        {
            const BattleUnit* unit = player->roster.FindUnit(id);
            if (unit != nullptr)
                strength += OffensiveUnitStrength(*unit, *player);
        }
        return strength;
    }
    for (const auto& [id, unit] : player->roster.units)
        strength += OffensiveUnitStrength(unit, *player);
    return strength;
}

void UtilityAIModel::StartWaveEvaluation(GameWorld& world, Player* player, int targetPlayerId,
                                         const std::vector<int>& unitIds, double waveStrength)
{
    if (player == nullptr)
        return;
    ActiveWaveEvaluation evaluation;
    evaluation.targetPlayerId = targetPlayerId;
    evaluation.unitIds = unitIds;
    evaluation.hqDamageBaseline = world.GetCombatTelemetry().GetHqDamage(player->id, targetPlayerId);
    evaluation.launchedStrength = waveStrength;
    for (int id : unitIds)
        evaluation.damageBaseline[id] = world.GetCombatTelemetry().GetUnitDamage(id);
    activeWave = std::move(evaluation);
    hasLaunchedWave = true;
}

void UtilityAIModel::UpdateWaveEvaluation(GameWorld& world, Player* player)
{
    if (player == nullptr || !activeWave.has_value())
        return;

    const ActiveWaveEvaluation& wave = *activeWave;
    double hqDamage = world.GetCombatTelemetry().GetHqDamage(player->id, wave.targetPlayerId) -
                      wave.hqDamageBaseline;
    bool targetDefeated = world.IsPlayerDefeated(wave.targetPlayerId);
    bool anyUnitActive = false;
    for (int id : wave.unitIds)
        if (world.GetDeployedUnits().contains(id))
        {
            anyUnitActive = true;
            break;
        }

    bool succeeded = targetDefeated || hqDamage > 0.001;
    if (!succeeded && anyUnitActive)
        return; // the verdict is not known yet

    UnitDamageBreakdown waveDamage;
    for (int id : wave.unitIds)
        waveDamage += world.GetCombatTelemetry().GetUnitDamage(id) - wave.damageBaseline.at(id);
    double totalDamage = waveDamage.TotalDamage();
    if (totalDamage > 0.001)
    {
        lastOffensiveTowerDamageShare = waveDamage.fromTowers / totalDamage;
        lastOffensiveUnitDamageShare = waveDamage.fromUnits / totalDamage;
        lastOffensiveHqDamageShare = waveDamage.fromHeadquarters / totalDamage;
    }

    if (succeeded)
    {
        adaptiveMinimumWaveStrength = 0.0;
    }
    else
    {
        // A zero-HQ-damage wave was too weak. The next launch must exceed its
        // measured combat strength by 8%; discrete roster sizes may turn that
        // into one extra unit, while upgraded compositions can satisfy it with
        // the same headcount.
        adaptiveMinimumWaveStrength =
            FailedWaveStrengthThreshold(adaptiveMinimumWaveStrength, wave.launchedStrength);
    }
    activeWave.reset();
}

bool UtilityAIModel::TryUnlockBarracks(GameWorld& world, Player* player, const AISituation& s)
{
    if (player == nullptr || !s.basicEconomyEstablished)
        return false;

    const bool foodBootstrapComplete = HasCompletedFoodFoundation(player);

    // Smith may rise in parallel and accumulate tools while the food
    // bootstrap is being completed. Once the basic production footing exists,
    // the military milestone must be allowed to happen before the full food
    // chain: on Primitive the ordered food opening can otherwise consume the
    // whole acceptance window and leave the AI with no way to demonstrate a
    // functioning Barracks/recruitment pipeline. The manpower reserve below
    // still prevents a first raid from consuming the last recruitable people.

    // Three militia need 15 manpower. A Barracks placed after every citizen
    // has already become a worker is only a visual milestone: it cannot train
    // the opening wave. Preserve its materials and let EconomySustain add
    // population capacity (or finish the food bootstrap) first. Lower
    // difficulties reach this state because they do not get Hard's manpower
    // head start.
    constexpr double firstRaidManpowerReserve = 15.0;
    const bool populationCapacityBlocked =
        s.populationCap > 0.0 && s.totalPopulation >= s.populationCap * 0.95 &&
        s.manpower < firstRaidManpowerReserve;
    if (s.barracksCount == 0 && populationCapacityBlocked)
        return false;

    const auto& barracksDefinition = GetBuildingDefinition(BuildingType::Barracks);
    if (s.barracksCount == 0 && player->CanBuildDefinition(barracksDefinition))
    {
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::Barracks,
                                                  TileType::GRASS, nullptr, actions);
        return anchor.x >= 0 &&
               AIActions::TrySubmitBuild(world, player, BuildingType::Barracks, anchor, actions);
    }

    const auto& smithDefinition = GetBuildingDefinition(BuildingType::Smith);
    int smithCount = AIActions::CountCompletedOrQueuedBuildings(world, player, BuildingType::Smith);
    if (smithCount == 0)
    {
        // Do not sink the opening iron reserve into a forge before the smelter
        // has renewable inputs. Otherwise Smith construction plus its first
        // tool batch consumes every unit of IRON that Barracks itself needs,
        // and both buildings wait on a chain the progression planner never
        // explicitly finished.
        for (ResourceType raw : {ResourceType::IRON_ORE, ResourceType::COAL})
        {
            if (s.barracksCount > 0)
                break; // early Barracks already creates pressure; Smith may use the current iron reserve
            if (AIActions::HasProducerOrPendingForResource(player, raw))
                continue;
            if (TryBuildProducerFor(world, player, raw))
                return true;
            return false;
        }

        if (!player->CanBuildDefinition(smithDefinition))
        {
            for (const auto& cost : player->GetEffectiveBuildCosts(smithDefinition))
            {
                if (AIActions::CountStoredResource(player, cost.type) >= cost.amount)
                    continue;
                int rate = AIActions::GetResourceRate(
                    player->economyTelemetry.current.productionRatesPerMinute, cost.type);
                // An existing producer is not proof of a working chain: a
                // Foundry without ore/coal reports zero flow. Let the chain
                // walker diagnose its missing inputs instead of waiting
                // forever beside a structurally present but stalled building.
                if (rate <= 0 && TryBuildProducerFor(world, player, cost.type))
                    return true;
                return false; // save the military reserve while production catches up
            }
            return false;
        }
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::Smith,
                                                  TileType::GRASS, nullptr, actions);
        return anchor.x >= 0 && AIActions::TrySubmitBuild(world, player, BuildingType::Smith, anchor, actions);
    }

    // Smith defaults to Tools, but this repairs a player- or save-selected
    // recipe before concluding the Barracks prerequisite is unavailable.
    if (AIActions::TryAssignRecipe(world, player, BuildingType::Smith, 0, ResourceType::TOOLS))
        return true;

    if (!foodBootstrapComplete)
        return false;

    if (!player->CanBuildDefinition(barracksDefinition))
    {
        for (const auto& cost : player->GetEffectiveBuildCosts(barracksDefinition))
        {
            if (AIActions::CountStoredResource(player, cost.type) >= cost.amount)
                continue;
            // A positive flow is not enough while a construction reserve is
            // below the next military building's cost. Ask the normal chain
            // walker to add capacity even when the existing producer is alive;
            // otherwise a small positive trickle can strand the opening just
            // below the Barracks threshold forever.
            if (TryBuildProducerFor(world, player, cost.type))
                return true;
            return false;
        }
        return false;
    }

    Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::Barracks,
                                              TileType::GRASS, nullptr, actions);
    return anchor.x >= 0 && AIActions::TrySubmitBuild(world, player, BuildingType::Barracks, anchor, actions);
}

bool UtilityAIModel::EnsureFoodRecoveryRoute(GameWorld& world, Player* player, Building* barracks)
{
    if (player == nullptr || barracks == nullptr)
        return false;

    std::vector<Building*> foodBuildings;
    for (Building* building : player->GetTrackedBuildings())
    {
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction())
            foodBuildings.push_back(building);
    }
    std::sort(foodBuildings.begin(), foodBuildings.end(),
              [](const Building* a, const Building* b) { return a->id < b->id; });

    auto firstOf = [&](BuildingType type, int ordinal = 0) -> Building*
    {
        int seen = 0;
        for (Building* building : foodBuildings)
        {
            if (building->buildingType != type)
                continue;
            if (seen++ == ordinal)
                return building;
        }
        return nullptr;
    };

    auto ensureRoute = [&](Building* source, ResourceType resource, Building* receiver,
                           bool alternative) -> bool
    {
        if (source == nullptr || receiver == nullptr)
            return false;
        for (const auto& view : source->GetReceiverViews())
        {
            if (view.type == resource && view.building == receiver)
                return false;
        }
        world.SubmitCommand(GameCommand::SetReceiver(
            player->id, source->positionId, receiver->positionId, alternative));
        return true;
    };

    // The default AutoConnectBuilding policy sends every new output to a
    // warehouse. The renewable chain needs explicit, deterministic links, or
    // the single well's water is consumed by the farm while Bakery/Inn remain
    // permanently empty. Three wells give each major consumer its own
    // deterministic water lane and prevent one consumer from monopolizing the
    // shared output buffer.
    Building* wellA = firstOf(BuildingType::Well, 0);
    Building* wellB = firstOf(BuildingType::Well, 1);
    Building* wellC = firstOf(BuildingType::Well, 2);
    Building* farm = firstOf(BuildingType::WheatFarm);
    Building* windmill = firstOf(BuildingType::Windmill);
    Building* bakery = firstOf(BuildingType::Bakery);
    Building* huntersHut = firstOf(BuildingType::HuntersHut);
    Building* inn = firstOf(BuildingType::Inn);
    Building* smith = firstOf(BuildingType::Smith);
    Building* village = firstOf(BuildingType::Village);

    if (ensureRoute(wellA, ResourceType::WATER, farm, false) ||
        ensureRoute(wellB, ResourceType::WATER, bakery, false) ||
        ensureRoute(wellC, ResourceType::WATER, inn, false) ||
        ensureRoute(farm, ResourceType::WHEAT, windmill, false) ||
        ensureRoute(windmill, ResourceType::FLOUR, bakery, false) ||
        ensureRoute(bakery, ResourceType::BREAD, inn, false) ||
        ensureRoute(huntersHut, ResourceType::MEAT, inn, false) ||
        ensureRoute(inn, ResourceType::FOOD_PROVISIONS, village, false) ||
        ensureRoute(smith, ResourceType::IRON_SWORD, barracks, true))
        return true;

    std::vector<Building*> producers;
    for (Building* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
    {
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            continue;
        const auto* production = building->GetComponent<ProductionComponent>();
        if (production == nullptr || !production->products.contains(ResourceType::FOOD_PROVISIONS))
            continue;
        producers.push_back(building);
    }
    std::sort(producers.begin(), producers.end(),
              [](const Building* a, const Building* b) { return a->id < b->id; });

    for (Building* producer : producers)
    {
        bool alreadyRouted = false;
        for (const auto& receiver : producer->GetReceiverViews())
        {
            if (receiver.type == ResourceType::FOOD_PROVISIONS && receiver.building == barracks)
            {
                alreadyRouted = true;
                break;
            }
        }
        if (alreadyRouted)
            return false;

        world.SubmitCommand(GameCommand::SetReceiver(
            player->id, producer->positionId, barracks->positionId, true));
        return true;
    }
    return false;
}

std::vector<const UnitDefinition*> UtilityAIModel::RankUnitChoices(const AISituation& s)
{
    // Offensive mix bookkeeping: how much siege the roster already holds.
    int siegeCount = 0;
    for (const auto& [defId, count] : s.rosterByDef)
    {
        const UnitDefinition* def = FindUnitDefinition(defId);
        if (def != nullptr && IsSiegeUnit(*def))
            siegeCount += count;
    }
    // Keep roughly one siege unit per two lane-fighters. Strict `<` against
    // rosterCount alone makes an EMPTY roster start with fighters — siege
    // units are escort-dependent (see units.rtsdata's ram note) and must
    // never be the first recruit.
    bool wantSiege = siegeCount * 3 < s.rosterCount;
    bool defensive = s.UnderAttack();

    std::vector<std::pair<const UnitDefinition*, double>> scored;
    for (const auto& [defId, def] : GetUnitCatalog())  // std::map — deterministic id order
    {
        double score = 0.0;
        if (defensive)
        {
            // Siege engines cannot protect a road column, no matter how much
            // raw HP they have. Prefer actual lane fighters, then rank their
            // staying power, road damage and ability to reinforce a breach
            // per cost.
            score = (def.roadAttack + def.armor + 0.5 * def.maxHp + 5.0 * def.moveSpeed) /
                    std::max(1.0, def.manpowerCost + TotalResourceCost(def));
            if (IsSiegeUnit(def))
                score -= 1000.0;
        }
        else
        {
            // Push: fill whichever class the 2:1 mix is short on; rank
            // within the class by what that class is for. The last evaluated
            // wave then bends the roster toward the obstacle that actually
            // stopped it, rather than guessing from enemy presence alone.
            bool preferredClass = IsSiegeUnit(def) == wantSiege;
            // A ram's durability lets it keep damaging the HQ under fire;
            // raw siege damage alone incorrectly promoted fragile artillery
            // ahead of the first useful breach engine.
            double classValue = IsSiegeUnit(def)
                ? def.siegeAttack + 0.25 * def.maxHp + def.armor
                : def.moveSpeed * def.roadAttack;
            if (s.offensiveTowerDamageShare > 0.55)
            {
                // Towers punish time spent in range. Effective staying power
                // times movement speed rewards both armor/HP and exposure
                // reduction without hard-coding a particular unit id.
                double towerSurvival = (def.maxHp + def.armor * 6.0) * def.moveSpeed /
                                       std::max(1.0, def.manpowerCost + TotalResourceCost(def));
                classValue += towerSurvival * 4.0;
            }
            else if (s.offensiveUnitDamageShare > 0.55)
            {
                // Enemy columns are beaten by lane damage and enough armor to
                // win repeated front-vs-front trades.
                classValue += def.roadAttack * 5.0 + def.armor * 2.0 + def.maxHp * 0.1;
            }
            else if (s.offensiveHqDamageShare > 0.55)
            {
                // Reaching the gate but dying to thorns calls for siege damage
                // and health, not more lane-clearing specialization.
                classValue += def.siegeAttack * 3.0 + def.maxHp * 0.15;
            }
            score = (preferredClass ? 1000.0 : 0.0) + classValue;
        }
        scored.emplace_back(&def, score);
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<const UnitDefinition*> ranked;
    ranked.reserve(scored.size());
    for (const auto& [def, score] : scored)
        ranked.push_back(def);
    return ranked;
}

bool UtilityAIModel::ExecuteDefense(GameWorld& world, Player* player, const AISituation& s)
{
    // Do not let a speculative tower programme steal the only Smith's recipe
    // from TOOLS/weapons before the AI has launched anything. Live incoming
    // pressure is the exception: survival may legitimately pre-empt offense.
    if (!hasLaunchedWave && s.enemyIncomingCount == 0)
        return false;

    // 1. Missing towers — anchor near the HQ (the lane's endpoint).
    if (s.towerCount < DesiredTowerCount(s))
    {
        Building* hq = AIActions::FindOwnedHeadquarters(player);
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::DefenseTower,
                                                  TileType::GRASS, hq, actions);
        if (anchor.x >= 0)
        {
            const auto& towerDefinition = GetBuildingDefinition(BuildingType::DefenseTower);
            if (player->CanBuildDefinition(towerDefinition) &&
                AIActions::TrySubmitBuild(world, player, BuildingType::DefenseTower, anchor, actions))
                return true;

            // A detected attack is an emergency reservation: establish a
            // missing input producer, otherwise wait for existing production
            // instead of spending tower stock on a lower-priority build.
            if (s.enemyIncomingCount > 0)
            {
                for (const auto& cost : player->GetEffectiveBuildCosts(towerDefinition))
                {
                    if (AIActions::CountStoredResource(player, cost.type) >= cost.amount)
                        continue;
                    int rate = AIActions::GetResourceRate(
                        player->economyTelemetry.current.productionRatesPerMinute, cost.type);
                    if (rate <= 0 && TryBuildProducerFor(world, player, cost.type))
                        return true;
                }
                return true;
            }
        }
    }

    // 2. Towers standing but nothing feeds them — build toward the ARROWS
    //    chain (Smith + inputs).
    if (s.towerCount > 0 && s.arrowsStored == 0 && s.arrowsRate == 0)
        return TryBuildProducerFor(world, player, ResourceType::ARROWS);

    return false;
}

bool UtilityAIModel::ExecuteRecruitDeploy(GameWorld& world, Player* player, const AISituation& s)
{
    // No recruiting without a Barracks — that IS the recruit-deploy action
    // until one stands.
    if (s.barracksCount == 0)
        return TryUnlockBarracks(world, player, s);

    bool smithMissing =
        AIActions::CountCompletedOrQueuedBuildings(world, player, BuildingType::Smith) == 0;
    const bool foodChainComplete = HasCompletedFoodChain(player);

    // A standing Barracks is the handoff from tools to weapons. With one
    // Smith it becomes the weapon line; once a second exists, deterministic
    // ordinals keep Smith #0 on tools and Smith #1 on swords. Generic deficit
    // recovery is not allowed to repurpose either last unique line.
    if (!smithMissing)
    {
        int completedSmiths = 0;
        for (const Building* building : player->GetTrackedBuildingsWithComponent<RecipeComponent>())
            if (building != nullptr && building->owner == player &&
                building->buildingType == BuildingType::Smith && !building->IsUnderConstruction())
                completedSmiths++;
        if (completedSmiths >= 2 &&
            AIActions::TryAssignRecipe(world, player, BuildingType::Smith, 0, ResourceType::TOOLS))
            return true;
        const int swordSmithOrdinal = completedSmiths >= 2 ? 1 : 0;
        if (AIActions::TryAssignRecipe(world, player, BuildingType::Smith,
                                       swordSmithOrdinal, ResourceType::IRON_SWORD))
            return true;
    }

    // Establish the weapon workshop before the food recovery chain claims the
    // remaining worker pool. Hard can reach Barracks early; postponing Smith
    // until Bakery/HuntersHut/Inn are complete leaves the workshop permanently
    // staffed at zero and makes every non-militia candidate unreachable.
    if (smithMissing && recruitEconomyBuildTimer <= 0.0)
    {
        recruitEconomyBuildTimer = RecruitEconomyBuildInterval;
        if (TryUnlockBarracks(world, player, s))
            return true;
    }

    // Barracks can legitimately arrive before Bakery/HuntersHut/Inn on Hard
    // because of its production head start. Keep RecruitDeploy recoverable in
    // that state by resuming the ordered food steps instead of returning false
    // and letting the same need starve every following decision cycle.
    if (!foodChainComplete)
    {
        // Do not resume the full opening order here: Barracks may have been
        // completed before Foundry, and that earlier blocked step would hide
        // the still-missing Bakery/HuntersHut/Inn forever.
        if (TryFoodRecoveryPlan(world, player))
            return true;
    }

    // Barracks creates early pressure; Smith is the immediate follow-up that
    // turns that militia foothold into a real weapon economy. Keep it inside
    // RecruitDeploy's high-priority lane instead of hoping a generic deficit
    // eventually selects it while cheap extractors keep winning ties.
    // Wave ready (or the lane is being lost and anything helps) — deploy the
    // whole roster at the reachable enemy. Ids in instanceId order
    // (std::map) — deterministic.
    bool emergency = s.Threat() > EmergencyThreat && s.rosterCount > 0;
    // Personality bias (2026-07-20): +/- a couple units on the wave threshold
    // (5..8) so two AIs on the same map don't deploy in visually identical
    // lockstep.
    int effectiveWaveSize = WaveSize + personalityWaveBias;
    // The first raid deliberately leaves earlier: it makes the match active
    // while the economy is still scaling. Later waves use the normal
    // personality threshold, unless the previous wave failed to touch the HQ
    // and its measured +8% strength target is reached first.
    // Opening raids must concentrate enough force to survive a defended lane:
    // hold the first five units and deploy them together. Emergency pressure
    // remains the deliberate exception below.
    // A naturally empty roster should launch its first recruit immediately;
    // a test or restored game that already has two units still gets the
    // historical three-unit opening concentration and one more recruitment
    // opportunity before committing the wave.
    const int openingWaveSize = (!hasLaunchedWave && s.rosterCount >= 2) ? 3 : 1;
    // After the first contact, keep the follow-up threshold modest enough to
    // replace losses and launch a second small wave on low-resource maps;
    // higher difficulties still retain their larger personality-scaled waves.
    int desiredWaveSize = hasLaunchedWave ? std::max(3, effectiveWaveSize - 2) : openingWaveSize;
    double rosterStrength = RosterOffensiveStrength(player);
    bool adaptiveWaveReady = adaptiveMinimumWaveStrength > 0.0 &&
                             rosterStrength >= adaptiveMinimumWaveStrength;
    bool waveReady = s.rosterCount >= desiredWaveSize || adaptiveWaveReady || emergency;
    if (!activeWave.has_value() && waveReady)
    {
        int target = GetCachedAttackTargetPlayer(world, player);
        if (target >= 0)
        {
            std::vector<int> orderedIds;
            orderedIds.reserve(player->roster.units.size());
            for (const auto& [instanceId, unit] : player->roster.units)
                orderedIds.push_back(instanceId);
            if (!orderedIds.empty())
            {
                double launchedStrength = RosterOffensiveStrength(player, &orderedIds);
                StartWaveEvaluation(world, player, target, orderedIds, launchedStrength);
                world.SubmitCommand(GameCommand::DeployUnits(player->id, target, std::move(orderedIds)));
                return true;
            }
        }
        if (!emergency)
            return false;  // full wave but no reachable enemy — don't recruit past the cap
    }

    // Dążenie do ataku (2026-07-20, user report: "AI wciąż nie buduje żelaza/
    // węgla/narzędzi broni"): RecruitDeploy is the highest-scored need, but it
    // used to just recruit whatever's affordable and quietly fall back to bare
    // militia forever — nothing ever asked for the sword/tools/iron economy
    // the TOP-ranked unit choice actually needs. Build toward the top pick's
    // missing costs here; a fully-costed unit (e.g. a swordsman) is what
    // finally drives Foundry/Smith/Mine construction from the need that
    // should want them most.
    //
    // GATED on economyEstablished (harness catch 2026-07-20): without this,
    // the very FIRST decision cycle of the game (empty roster -> "knight",
    // needing IRON_SWORD) tried to walk the WHOLE iron chain -- Mine(IRON_ORE)
    // -> Foundry -> Smith -- before TryOpeningPlan ever got a turn, spending
    // the AI's limited starting stock and its shared per-type build cooldown
    // on an out-of-order producer. That one early hijack was enough to wedge
    // the deterministic opening bootstrap for the ENTIRE rest of the game
    // (roster froze, zero deploys, every later build request rejected for
    // lack of resources) -- rate-limiting the ATTEMPT (recruitEconomyBuildTimer)
    // did nothing because the damage was already done on that first,
    // unthrottled call. Waiting for the economy to clear its first-footing
    // threshold (same `economyEstablished` flag Task 2 uses for the priority
    // handoff, ai.rtsdata's `tower_readiness_buildings`) keeps this action a
    // genuine complement to a working economy instead of a competitor for its
    // bootstrap — a handful of standing producers is enough to be past the
    // fragile stretch, without waiting for the entire opening plan (which
    // could take many minutes and left this need permanently dead in
    // practice).
    std::vector<const UnitDefinition*> ranked = RankUnitChoices(s);
    if (foodChainComplete && !smithMissing && s.basicEconomyEstablished &&
        s.rosterCount >= desiredWaveSize && recruitEconomyBuildTimer <= 0.0 &&
        !ranked.empty() && ranked.front() != nullptr)
    {
        for (const auto& cost : ranked.front()->cost)
        {
            if (actions.deficitBackoff.count(cost.type) > 0)
                continue;
            int stored = AIActions::CountStoredResource(player, cost.type);
            int rate = AIActions::GetResourceRate(
                player->economyTelemetry.current.productionRatesPerMinute, cost.type);
            if (stored >= cost.amount || rate > 0)
                continue;  // already covered — nothing to build toward
            // Gate consumed on the FIRST real attempt regardless of outcome —
            // otherwise a chain with several missing costs could exhaust its
            // whole per-resource backoff table in one cycle and effectively
            // recreate the lockout the interval exists to prevent.
            recruitEconomyBuildTimer = RecruitEconomyBuildInterval;
            if (TryBuildProducerFor(world, player, cost.type))
            {
                // Short success cooldown (not the 12s failure one) — lets the
                // ladder rotate to the pick's NEXT missing cost across the
                // next couple of gated attempts instead of hammering the same
                // one every time.
                actions.deficitBackoff[cost.type] = 4.0;
                return true;
            }
            actions.deficitBackoff[cost.type] = 12.0;
            break;
        }
    }

    // Otherwise recruit toward the wave. First completed Barracks by id —
    // which one trains is simulation-visible state, so the pick must not
    // depend on heap order (see docs/tech_debt.md).
    const auto& recruitCapable = player->GetTrackedBuildingsWithComponent<RecruitmentComponent>();
    std::vector<Building*> sortedBarracks(recruitCapable.begin(), recruitCapable.end());
    std::sort(sortedBarracks.begin(), sortedBarracks.end(),
              [](Building* a, Building* b) { return a->id < b->id; });
    Building* barracks = nullptr;
    for (Building* building : sortedBarracks)
    {
        if (building != nullptr && building->owner == player && !building->IsUnderConstruction())
        {
            barracks = building;
            break;
        }
    }
    if (barracks == nullptr)
        return false;

    // Villages legitimately consume the Inn's primary output. Once a Barracks
    // exists, give it the producer as an alternative receiver so queued
    // recruitment can pull real packages over the road network instead of
    // waiting forever for a warehouse reserve that may never accumulate.
    if (foodChainComplete && EnsureFoodRecoveryRoute(world, player, barracks))
        return true;

    auto* recruitment = barracks->GetComponent<RecruitmentComponent>();
    if (recruitment == nullptr)
        return false;
    // Progress gates: never stack orders behind one that is still waiting on
    // deliveries (strict-FIFO queue — everything behind it waits too), and
    // keep the queue short so orders track the situation, not a backlog.
    if (!recruitment->queue.empty() && !recruitment->queue.front().resourcesReady)
        return false;
    if (recruitment->queue.size() >= 2)
        return false;

    auto* localStorage = barracks->GetComponent<LocalResourceBufferComponent>();
    auto availableForRecruitment = [&](ResourceType type)
    {
        int available = AIActions::CountStoredResource(player, type);
        if (localStorage != nullptr)
        {
            auto it = localStorage->buffers.find(type);
            if (it != localStorage->buffers.end())
                available += static_cast<int>(it->second.buffer.size());
        }
        return available;
    };

    // DiagnoseRecruitmentBlock is a GLOBAL stock scan — resources counted
    // there still have to physically reach this Barracks over roads. Without
    // a road connection only locally-buffered costs can ever be consumed, so
    // gate on that (learned the hard way: a globally-affordable siege unit
    // queued into a roadless Barracks starves the whole queue forever).
    bool connected = std::find(s.unconnectedPositionIds.begin(), s.unconnectedPositionIds.end(),
                               barracks->positionId) == s.unconnectedPositionIds.end();

    for (const UnitDefinition* def : ranked)
    {
        if (def == nullptr)
            continue;
        if (smithMissing && def->id != "militia")
        {
            // Every difficulty starts with the same small finished-equipment
            // reserve. Spend that finite reserve before the Smith is ready;
            // after it runs out, the ordinary producer-chain logic
            // re-establishes the workshop.
            bool hasFinishedEquipment = true;
            for (const auto& cost : def->cost)
            {
                if (cost.type == ResourceType::FOOD_PROVISIONS)
                    continue;
                if (availableForRecruitment(cost.type) < cost.amount)
                {
                    hasFinishedEquipment = false;
                    break;
                }
            }
            if (!hasFinishedEquipment)
                continue;
        }
        if (!connected && !CostLocallyBuffered(*barracks, *def))
            continue;
        int foodCost = 0;
        for (const auto& cost : def->cost)
            if (cost.type == ResourceType::FOOD_PROVISIONS)
                foodCost = cost.amount;
        const bool renewableMilitia = def->id == "militia" && s.foodProductionAlive &&
                                      s.foodProvisionsStored >= foodCost && foodCost > 0 &&
                                      s.manpower >= def->manpowerCost;
        // A strict all-costs-now gate deadlocks the first stronger unit: the
        // Barracks cannot request FOOD_PROVISIONS until an order exists, while
        // the AI refuses to create that order until food is already in stock.
        // Queue only when every non-food cost already exists in reachable
        // stock; the recruitment component then reserves manpower and
        // requests the renewable food input through the normal logistics
        // path. A producer's mere existence is insufficient: it may be
        // stalled on an input, and a strict-FIFO order would then block the
        // Barracks indefinitely (regression seed 20260718).
        bool renewableFoodUnit = false;
        if (def->id != "militia" && s.foodProductionAlive && s.manpower >= def->manpowerCost &&
            foodCost > 0)
        {
            renewableFoodUnit = true;
            for (const auto& cost : def->cost)
            {
                if (cost.type != ResourceType::FOOD_PROVISIONS &&
                    availableForRecruitment(cost.type) < cost.amount)
                {
                    renewableFoodUnit = false;
                    break;
                }
            }
        }
        if (!recruitment->DiagnoseRecruitmentBlock(*barracks, def->id).empty() &&
            !renewableMilitia && !renewableFoodUnit)
            continue;  // otherwise wait for stock or try the next affordable pick
        world.SubmitCommand(GameCommand::RecruitUnit(player->id, barracks->positionId, def->id));
        return true;
    }
    return false;
}

bool UtilityAIModel::ExecuteResearch(GameWorld& world, Player* player, const AISituation& s)
{
    // Focus decisions have their own every-tick path in Update(). Research
    // remains throttled because it consumes a University and may require
    // building/material investment.
    // 1. No University yet: building one is the research action.
    if (s.universityCount == 0)
    {
        if (!player->CanBuildDefinition(GetBuildingDefinition(BuildingType::University)))
            return false;
        Vec2i anchor = AIActions::FindBuildAnchor(world, player, BuildingType::University,
                                                  TileType::GRASS, nullptr, actions);
        return anchor.x >= 0 &&
               AIActions::TrySubmitBuild(world, player, BuildingType::University, anchor, actions);
    }

    // 2. Idle University: pick a technology using the posture-preferred tag
    //    (military under pressure, production otherwise), then cheapest.
    //    Deterministic: catalog iteration order breaks ties (strict >).
    Building* university = AIActions::FindUniversity(player);
    if (university == nullptr)
        return false;
    const char* preferredTag = (s.Threat() > 0.0 || s.UnderAttack()) ? "military" : "production";
    const TechnologyDefinition* best = nullptr;
    double bestScore = -1e18;
    for (const auto& def : GetTechnologyDefinitions())
    {
        if (player->technologies.HasTechnology(def.id) || player->IsTechnologyInProgress(def.id))
            continue;
        if (!player->CanResearchTechnology(def.id))
            continue;
        bool tagged = std::find(def.tags.begin(), def.tags.end(), preferredTag) != def.tags.end();
        double score = (tagged ? 1000.0 : 0.0) - def.researchTime;
        if (score > bestScore)
        {
            bestScore = score;
            best = &def;
        }
    }
    if (best == nullptr)
        return false;
    world.SubmitCommand(GameCommand::StartTechnologyResearch(player->id, best->id, university->positionId));
    return true;
}

double UtilityAIModel::ScoreFocusChoice(const TechnologyDefinition& definition,
                                        const AISituation& s)
{
    const AIFocusPriorities priorities = BuildFocusPriorities(s);

    // Every available focus remains selectable. A shorter focus wins a close
    // strategic tie because it realizes its benefit and opens its child
    // sooner, but duration never outweighs a genuinely relevant plan.
    double score = 1.0 - 0.01 * std::max(0.0, definition.researchTime);

    if (HasTag(definition, "military"))
        score += 18.0 * std::max({priorities.mobilization, priorities.defense, priorities.offense});
    if (HasTag(definition, "mobilization") || HasTag(definition, "recruitment"))
        score += 32.0 * priorities.mobilization;
    if (HasTag(definition, "manpower") || HasTag(definition, "population"))
        score += 26.0 * priorities.manpower + 10.0 * priorities.mobilization;
    if (HasTag(definition, "defense") || HasTag(definition, "fortification"))
        score += 30.0 * priorities.defense;
    if (HasTag(definition, "offense") || HasTag(definition, "offensive"))
        score += 28.0 * priorities.offense;
    if (HasTag(definition, "logistics"))
        score += 42.0 * priorities.logistics;
    if (HasTag(definition, "production") || HasTag(definition, "economy"))
        score += 24.0 * priorities.economy;
    if (HasTag(definition, "construction"))
        score += 18.0 * priorities.economy;
    if (HasTag(definition, "expansion"))
        score += 22.0 * priorities.expansion;

    for (const auto& modifier : definition.modifiers)
        score += ModifierStrategicValue(modifier, priorities) * ModifierImpactWeight(modifier);
    return score;
}

double UtilityAIModel::ScoreFocusPlan(const TechnologyDefinition& root,
                                      const std::vector<TechnologyDefinition>& definitions,
                                      const std::set<std::string>& completed,
                                      const AISituation& s,
                                      int lookAheadDepth)
{
    const int boundedDepth = std::clamp(lookAheadDepth, 0, 8);
    std::set<std::string> path;

    auto scoreBranch = [&](auto&& self,
                           const TechnologyDefinition& node,
                           int remainingDepth) -> double
    {
        if (completed.contains(node.id) || !path.insert(node.id).second)
            return 0.0;

        double total = ScoreFocusChoice(node, s);
        if (remainingDepth > 0)
        {
            // Sum every outgoing route rather than keeping only the best
            // child. A root that opens two useful strategic branches is worth
            // more than one opening only a single equivalent branch.
            for (const auto& child : definitions)
            {
                if (completed.contains(child.id) || path.contains(child.id) ||
                    std::find(child.prerequisites.begin(), child.prerequisites.end(), node.id) ==
                        child.prerequisites.end())
                    continue;

                int missingOtherPrerequisites = 0;
                for (const auto& prerequisite : child.prerequisites)
                    if (!completed.contains(prerequisite) && !path.contains(prerequisite))
                        missingOtherPrerequisites++;

                // For an AND-node requiring several unfinished branches, this
                // route receives only its share of the future payoff. It can
                // still see valuable nodes beyond the convergence without
                // claiming their full value independently for every parent.
                const double prerequisiteShare =
                    1.0 / static_cast<double>(1 + missingOtherPrerequisites);
                const double timeDiscount =
                    1.0 / (1.0 + std::max(0.0, child.researchTime) / 120.0);
                total += FocusFutureDiscount * prerequisiteShare * timeDiscount *
                         self(self, child, remainingDepth - 1);
            }
        }

        path.erase(node.id);
        return total;
    };

    return scoreBranch(scoreBranch, root, boundedDepth);
}

bool UtilityAIModel::TryStartBestFocus(GameWorld& world, Player* player, const AISituation& s)
{
    if (player == nullptr || !player->focuses.GetActiveFocusId().empty())
        return false;

    const auto& definitions = GetFocusDefinitions();
    struct FocusCandidate
    {
        const TechnologyDefinition* definition{nullptr};
        double choiceScore{0.0};
        double planScore{0.0};
    };
    std::vector<FocusCandidate> candidates;

    for (const auto& definition : definitions)
    {
        if (!player->CanUnlockFocus(definition.id))
            continue;

        const double choiceScore = ScoreFocusChoice(definition, s);
        const double planScore = ScoreFocusPlan(
            definition, definitions, player->focuses.GetUnlocked(), s, FocusLookAheadDepth);
        candidates.push_back({&definition, choiceScore, planScore});
    }

    if (candidates.empty())
    {
        lastFocusSummary = "none";
        return false;
    }

    // Stable sort preserves catalog order as the deterministic final tie
    // break. A previously chosen branch stays preferred while it remains
    // close to the current best; a clear change in pressure can still switch
    // strategy immediately.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const FocusCandidate& lhs, const FocusCandidate& rhs)
                     {
                         return lhs.planScore > rhs.planScore;
                     });

    const double bestScore = candidates.front().planScore;
    const FocusCandidate* best = &candidates.front();
    for (const FocusCandidate& candidate : candidates)
    {
        if (candidate.definition->id == lastFocusChoiceId &&
            candidate.planScore + FocusHysteresisMargin >= bestScore)
        {
            best = &candidate;
            break;
        }
    }

    std::ostringstream focusSummary;
    const std::size_t count = std::min<std::size_t>(3, candidates.size());
    for (std::size_t i = 0; i < count; i++)
    {
        if (i != 0)
            focusSummary << ',';
        focusSummary << candidates[i].definition->id << ':'
                     << candidates[i].choiceScore << '/' << candidates[i].planScore;
    }
    lastFocusSummary = focusSummary.str();
    lastFocusChoiceId = best->definition->id;
    // Controllers run before ProcessCommands, so current-tick targeting is
    // still command-only mutation while avoiding a pending-command gap (and
    // duplicate submissions on the next tick).
    world.SubmitCommand(GameCommand::StartFocus(player->id, best->definition->id),
                        world.GetSimulationTick());
    return true;
}

int UtilityAIModel::GetCachedAttackTargetPlayer(GameWorld& world, Player* player)
{
    if (attackTargetCacheTimer <= 0.0)
    {
        cachedAttackTargetPlayer = AIActions::FindAttackTargetPlayer(world, player);
        attackTargetCacheTimer = 3.0;
    }
    return cachedAttackTargetPlayer;
}

bool UtilityAIModel::ExecuteLogistics(GameWorld& world, Player* player, const AISituation& s)
{
    // General maintenance first (storage/HQ road stubs, first roadless
    // producer) — it already submits at most one order.
    if (AIActions::TryBuildRoads(world, player, actions))
        return true;

    // Then path-level repairs TryBuildRoads can't see: a building that HAS an
    // adjacent road but no route to any storage (orphaned stub).
    for (int positionId : s.unconnectedPositionIds)
    {
        Building* building = world.GetTileMap().GetBuilding(positionId);
        if (building == nullptr || building->owner != player)
            continue;  // destroyed/replaced since the audit

        Building* targetRoad = AIActions::FindNearestStorageConnectedRoad(world, player, building);
        if (targetRoad != nullptr && AIActions::SubmitRoadPath(world, player, building, targetRoad, actions))
            return true;

        Building* storage = world.GetTileMap().FindNearestStorage(building, player);
        if (storage != nullptr && AIActions::SubmitRoadPath(world, player, building, storage, actions))
            return true;
    }
    return false;
}
