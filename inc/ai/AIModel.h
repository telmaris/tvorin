#ifndef AI_MODEL_H
#define AI_MODEL_H

#include "ai/AIActions.h"
#include "warfare/CombatTelemetry.h"

#include <array>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

class GameWorld;
struct UnitDefinition;
struct TechnologyDefinition;

// Utility-based tower-defense AI (AI rework, TODO #2 — replaces the removed
// 3-tier axis/goal/milestone PrimitiveAIModel, whose priority-axis framing
// didn't fit the TD loop). One decision cycle:
//   1. Sense()  — read-only AISituation snapshot (throttled).
//   2. Score each AINeed category to a utility in [0,1].
//   3. Try needs in (score desc, fixed priority) order until one executes a
//      concrete action through AIActions (build / road / recruit / deploy /
//      research / focus — all real GameCommands).
// Deterministic by construction: reads + SubmitCommand only; every
// first-match iteration over tracked buildings is sorted by building id
// (see docs/tech_debt.md); the only randomness is a small personality skew
// seeded from (map seed, player id) — identical across same-seed worlds.
enum class AINeed
{
    Defense,        // towers + ammo + emergency deploy (etap 3)
    RecruitDeploy,  // the prime objective: units on the track (etap 3)
    EconomySustain, // production chains for food/manpower and unit costs
    LogisticsRepair,// every building connected to the road network
    Research,       // University / technologies / focuses (etap 5)
    Count
};

// Read-only world snapshot the utility scores work from. Rebuilt on a
// throttle, never mutated by execution.
struct AISituation
{
    // Track state.
    int myDeployedCount{0};
    double myDeployedStrength{0.0};
    int enemyIncomingCount{0};
    double enemyIncomingStrength{0.0};
    double hqHpRatio{1.0};
    int rosterCount{0};
    // Roster headcount per unit definition id — feeds the composition rule.
    std::map<std::string, int> rosterByDef;
    int towerCount{0};
    // Completed towers' damage x attack-speed sum — the static-defense half
    // of "how hard is my lane to walk down".
    double towerStrength{0.0};
    int arrowsStored{0};
    int arrowsRate{0};
    int barracksCount{0};
    int smithCount{0};

    // Enemy pressure vs. units committed to this lane and the HQ's towers:
    // units marching offensively elsewhere are not defensive strength.
    double Threat() const
    {
        // A tower attacks every hostile unit in range without occupying the
        // marching column, so raw DPS understates its defensive value.
        double defense = myDeployedStrength + towerStrength * 1.75;
        return enemyIncomingStrength / (defense > 1.0 ? defense : 1.0);
    }
    // Posture for roster composition: under attack -> defensive picks.
    bool UnderAttack() const { return enemyIncomingCount > myDeployedCount; }

    double manpower{0.0};
    // Population cap / total population (Player::GetPopulationCap /
    // GetTotalPopulation) — feeds the manpower-reserve emergency check
    // (user design 2026-07-19): existing villages at capacity won't grow
    // manpower any further, so a low reserve there means "build another
    // Village now", not "wait, it'll recover on its own".
    double populationCap{0.0};
    double totalPopulation{0.0};
    int villageCount{0};
    int productionBuildingCount{0};
    int universityCount{0};
    // A completed University with no research running (AIActions::FindUniversity).
    bool hasIdleUniversity{false};
    // FOOD_PROVISIONS is the manpower lifeline — "alive" = positive
    // production rate in current telemetry.
    bool foodProductionAlive{false};
    // A finite reserve can still bootstrap one more Village before the
    // renewable Inn chain exists. This matters when every citizen has already
    // become a worker: the old food-alive-only check made that state permanent.
    int foodProvisionsStored{0};
    // Some production footing (>= ai.rtsdata's tower_readiness_buildings
    // producers) + food alive (user design 2026-07-20): once true, ai.rtsdata's
    // tier-2 `priority` discount (IRON/TOOLS/swords) stops being applied — the
    // AI stops treating those as permanently second-class — and
    // ExecuteRecruitDeploy's cost-chain builder is allowed to fire. The
    // discount only exists to win the OPENING build order (see AIEconomyBias);
    // it was never meant to suppress tier-2 forever, but the tier-1 amortized
    // bias never naturally lets go on its own (construction keeps draining
    // stock below the "low reserve" threshold indefinitely), so without this
    // flag tier-2 never got a fair shot at the deficit ladder. Deliberately
    // NOT gated on the full opening plan (harness catch 2026-07-20): that bar
    // is high enough it could take many minutes to clear, leaving both
    // dependents dead in practice; a handful of standing producers is enough
    // evidence the fragile tick-1 bootstrap stretch has passed (see
    // ExecuteRecruitDeploy for what going in too early wedged).
    bool economyEstablished{false};
    // Lighter military-unlock gate: enough production footing to start
    // reserving resources for Smith/Barracks, without waiting for the entire
    // FOOD_PROVISIONS chain to come alive.
    bool basicEconomyEstablished{false};
    // What stopped the most recently evaluated offensive wave. These shares
    // feed RankUnitChoices so tower attrition favors durable/mobile units,
    // while enemy-unit attrition favors lane fighters.
    double offensiveTowerDamageShare{0.0};
    double offensiveUnitDamageShare{0.0};
    double offensiveHqDamageShare{0.0};

    // Resource shortfalls, worst first (urgency desc, then enum asc) —
    // deterministic ordering.
    struct Deficit
    {
        ResourceType resource{ResourceType::Null};
        double urgency{0.0};
    };
    std::vector<Deficit> deficits;

    // Own buildings with a LogisticsComponent that have no road path to any
    // storage/HQ — anchor positionIds, ordered by building id at sense time.
    // Stored as tile ids (not Building*) so a building destroyed between the
    // sense and the decision can't dangle; execution re-resolves via the
    // tilemap and skips anything gone.
    std::vector<int> unconnectedPositionIds;
};

class UtilityAIModel
{
public:
    explicit UtilityAIModel(int controlledPlayerId);

    void Update(GameWorld& world, Player* player, double dt);

    // Roster-composition rule (etap 3), best pick first. Deterministic: pure
    // function of the static unit catalog + the situation's posture and
    // roster mix. Defensive posture maximizes staying power per cost
    // (roadAttack + armor + hp); offensive posture keeps a ~2:1 mix of
    // lane-clearers (moveSpeed x roadAttack) to siege (siegeAttack).
    // Public + static so the rule is unit-testable without a world.
    static std::vector<const UnitDefinition*> RankUnitChoices(const AISituation& s);

    // Manpower-reserve emergency (user design 2026-07-19): manpower below
    // ai.rtsdata's `manpower_reserve` AND existing villages already at
    // capacity (so manpower won't recover on its own) AND either the food
    // chain is alive or a finite FOOD_PROVISIONS reserve can bootstrap the
    // next Village. Used by both ScoreNeed and ExecuteEconomy, so
    // they can never disagree about whether the emergency is live. Public +
    // static (same reasoning as RankUnitChoices) for direct unit testing.
    static bool ManpowerEmergency(const AISituation& s, double manpowerReserve);
    // Failed waves must not be repeated at identical strength. Kept pure for
    // direct regression testing of the escalation rule.
    static double FailedWaveStrengthThreshold(double currentThreshold, double launchedStrength);
    // Strategic value of one focus in the current posture. Tags express the
    // designer's intent; modifiers are the fallback/confirmation, so an
    // untagged recruitment-time focus is still recognized as mobilization.
    // Public and pure for direct policy regression tests.
    static double ScoreFocusChoice(const TechnologyDefinition& definition,
                                   const AISituation& situation);
    // Discounted value of a focus and every reachable branch up to
    // `lookAheadDepth` decisions later. Completed nodes are ignored and
    // cycles are cut, making this safe for data-driven focus graphs.
    static double ScoreFocusPlan(const TechnologyDefinition& root,
                                 const std::vector<TechnologyDefinition>& definitions,
                                 const std::set<std::string>& completed,
                                 const AISituation& situation,
                                 int lookAheadDepth = 3);

    // Test seam (2026-07-20): read-only access to the seeded personality bias
    // — only populated after the first Update() call (that's where seeding
    // happens). Used by UtilityAIModelTests to confirm two players draw
    // different values from the same map seed, and that re-seeding the same
    // (seed, player id) pair reproduces them exactly.
    double GetPersonalityNeedBias(AINeed need) const { return personalityNeedBias[static_cast<int>(need)]; }
    int GetPersonalityWaveBias() const { return personalityWaveBias; }
    // Bounded failure diagnostics for the long-running AI harness. The model
    // records snapshots only every few simulated seconds; callers should
    // print this after a failed scenario, never on every tick.
    std::string GetDecisionTrace() const;

private:
    AISituation Sense(GameWorld& world, Player* player);
    double ScoreNeed(AINeed need, const AISituation& s) const;
    bool ExecuteNeed(AINeed need, GameWorld& world, Player* player, const AISituation& s);

    bool ExecuteDefense(GameWorld& world, Player* player, const AISituation& s);
    bool ExecuteRecruitDeploy(GameWorld& world, Player* player, const AISituation& s);
    bool ExecuteEconomy(GameWorld& world, Player* player, const AISituation& s);
    bool ExecuteLogistics(GameWorld& world, Player* player, const AISituation& s);
    bool ExecuteResearch(GameWorld& world, Player* player, const AISituation& s);
    // Focuses run in parallel with ordinary actions and cost no resources.
    // This still uses the same action cadence budget as every other command.
    bool TryStartBestFocus(GameWorld& world, Player* player, const AISituation& s);
    void ConsumeActionCadence();
    int GetCachedAttackTargetPlayer(GameWorld& world, Player* player);
    // Builds the first affordable producer of `resource` (or of the deepest
    // missing input in its chain). Returns false when nothing can be placed
    // or afforded right now.
    bool TryBuildProducerFor(GameWorld& world, Player* player, ResourceType resource);
    // Keeps a renewable FOOD_PROVISIONS producer reachable by Barracks after
    // villages have claimed the producer's primary receiver. The route is a
    // normal command/road delivery, not a resource grant.
    bool EnsureFoodRecoveryRoute(GameWorld& world, Player* player, Building* barracks);
    // Repairs the food lane after the first military wave. Route recovery is
    // intentionally handled before scaling producers: a disconnected healthy
    // chain must not be mistaken for a capacity deficit.
    bool TryScaleFoodRecovery(GameWorld& world, Player* player);
    // Fixed, deterministic opening build order that bootstraps the food/
    // manpower chain before telemetry has any consumption signal to react to.
    bool TryOpeningPlan(GameWorld& world, Player* player);
    // Once Barracks exists, recover the three food buildings independently of
    // an earlier opening step that may still be blocked (for example Foundry).
    bool TryFoodRecoveryPlan(GameWorld& world, Player* player);
    bool TryUnlockBarracks(GameWorld& world, Player* player, const AISituation& s);
    void UpdateWaveEvaluation(GameWorld& world, Player* player);
    void StartWaveEvaluation(GameWorld& world, Player* player, int targetPlayerId,
                             const std::vector<int>& unitIds, double waveStrength);
    double RosterOffensiveStrength(Player* player, const std::vector<int>* selectedIds = nullptr) const;

    int playerId{0};
    AIActions::AIActionState actions;
    AISituation situation;
    // One shared budget for every command-producing action: build, recruit,
    // deploy, road maintenance, research and focus start.
    double actionCadenceTimer{0.0};
    double senseTimer{0.0};
    double decisionTimer{0.0};
    double roadTimer{0.0};
    // Gates RecruitDeploy's "build toward the top pick's missing cost" action
    // (2026-07-20) to at most once per interval — RecruitDeploy is the
    // highest-scored need and runs nearly every decision cycle, so without
    // this gate a deep, currently-unaffordable cost chain (e.g. a siege
    // unit's steel sword -> iron -> ore) kept "succeeding" (submitting SOME
    // command, even one the simulation later rejects) every single cycle and
    // permanently starved the recruit fallback below it — a harness-caught
    // regression (roster frozen, zero deploys for the whole run). See
    // ExecuteRecruitDeploy in AIModel.cpp.
    double recruitEconomyBuildTimer{0.0};
    // Connectivity audits are the expensive part of Sense (road BFS per
    // storage per building) — they run on their own slower cadence and the
    // last answer is carried between audits.
    double connectivityTimer{0.0};
    std::vector<int> lastUnconnectedPositionIds;
    // AreHqsConnected is a real pathing query — cached (perf fix inherited
    // from the C1-era model, where calling it per tick cost ~32 ms/tick).
    double attackTargetCacheTimer{0.0};
    int cachedAttackTargetPlayer{-1};
    // Deterministic personality RNG, seeded once from (map seed, player id).
    // Difficulty does not alter decisions; it selects only action cadence.
    std::mt19937 noiseRng;
    bool noiseSeeded{false};
    int difficulty{0};
    // AI economy bias (ai/AIEconomyBias.h, user design 2026-07-17), consumed
    // by Sense's deficit diagnosis and the producer-chain walk. Production
    // configuration uses the same scale for every difficulty.
    std::map<ResourceType, int> consumptionBias;
    // Build-order priority weights (ai/AIEconomyBias.h's `priority` table,
    // user design 2026-07-19), normalized [0,1] — NOT difficulty-scaled
    // (build order is a design choice, unlike the consumption bias's
    // magnitude). Cached alongside consumptionBias in Update.
    std::map<ResourceType, double> priorityWeights;
    std::array<std::string, 64> decisionTrace{};
    std::size_t decisionTraceNext{0};
    std::size_t decisionTraceCount{0};
    double decisionTraceTimer{0.0};
    std::string lastDecisionAction;
    std::string lastScoreSummary;
    std::string lastRejectedActions;
    std::string lastFocusSummary;
    std::string lastFocusChoiceId;
    // Personality bias (user design 2026-07-20: "2 AI nie gra identycznie") —
    // a small, PERMANENT per-need skew (+/-2.5%, see AIModel.cpp for why that
    // exact bound) drawn once from noiseRng right after it's seeded. Sized to the tightest
    // hard-won gap between ScoreNeed's floors so it can only break near-ties
    // and shift pace, never invert the priority architecture. Two same-seed
    // worlds draw the identical values (seed XOR player id), so lockstep
    // holds.
    std::array<double, static_cast<int>(AINeed::Count)> personalityNeedBias{};
    int personalityWaveBias{0};

    struct ActiveWaveEvaluation
    {
        int targetPlayerId{-1};
        std::vector<int> unitIds;
        std::map<int, UnitDamageBreakdown> damageBaseline;
        double hqDamageBaseline{0.0};
        double launchedStrength{0.0};
    };
    std::optional<ActiveWaveEvaluation> activeWave;
    bool hasLaunchedWave{false};
    // A failed zero-HQ-damage wave raises the next launch threshold by 8%
    // relative to the real strength that was sent, capped by natural roster
    // composition rather than an arbitrary extra-unit count.
    double adaptiveMinimumWaveStrength{0.0};
    double lastOffensiveTowerDamageShare{0.0};
    double lastOffensiveUnitDamageShare{0.0};
    double lastOffensiveHqDamageShare{0.0};
};

#endif
