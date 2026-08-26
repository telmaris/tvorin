#ifndef AI_ECONOMY_BIAS_H
#define AI_ECONOMY_BIAS_H

#include "data/Resource.h"

#include <array>
#include <map>
#include <string>

// User design (2026-07-17): virtual, amortized per-minute consumption of the
// non-constant costs (construction, recruitment, ammunition), loaded from
// assets/data/ai.rtsdata and added to the consumption side of what the AI
// senses — the AI then works to keep production >= consumption, which makes
// it stand up and sustain production for those costs instead of stalling
// when its starting stock runs dry. Every difficulty uses the same targets
// and the same common starting package.
// Ceiling a `priority` line in ai.rtsdata is normalized against — see
// AIEconomyBias::NormalizedPriority.
constexpr int PriorityCeiling = 100;

struct AIEconomyBias
{
    // Retained in the data format for compatibility and tuning experiments.
    // Production data keeps all entries at 1.0 so every level plans equally.
    std::array<double, 4> difficultyScale{1.0, 1.0, 1.0, 1.0};
    std::map<ResourceType, int> virtualConsumptionPerMinute;

    // Build-order priority weight per resource (0..PriorityCeiling, user
    // design 2026-07-19), serialized in the same block as `consumption` for
    // one-file fine-tuning. Every resource's DiagnoseResourceNeed urgency is
    // multiplied by its normalized weight before the deficit ladder ranks
    // candidates — this is what actually decides build ORDER (which resource
    // wins a tie), separate from `consumption`'s bias MAGNITUDE (which decides
    // how hard the AI works to sustain a resource once it's already the
    // priority). Not difficulty-scaled: order is a design choice, not a
    // difficulty lever. A resource with no `priority` line defaults to the
    // ceiling (full weight, no penalty) — additive/opt-in, so forgetting to
    // tune something never silently deprioritizes it.
    std::map<ResourceType, int> priorityWeight;
    // Minimum completed production buildings before the Defense need starts
    // wanting a garrison at all (UtilityAIModel's DesiredTowerCount gate).
    // Towers aren't a resource so they don't fit the `priority` table above —
    // this is the equivalent build-order lever for "start defense in the
    // meantime, once the economy has SOME footing" (user design 2026-07-19).
    int towerReadinessBuildings{4};
    // Seconds between retry evaluations after no command was produced. The
    // actual command cadence is difficulty-specific and lives in
    // AIDifficultyProfile, so all command-producing actions share one budget.
    double decisionIntervalSeconds{1.5};
    // Manpower floor (user design 2026-07-19): below this, with villages
    // already near population capacity (so manpower won't recover on its
    // own) and the food chain alive, building another Village becomes the
    // AI's immediate priority — see UtilityAIModel::ManpowerEmergency.
    double manpowerReserve{20.0};

    // Bias for one resource at a difficulty level, floor-rounded; 0 for
    // resources without an entry.
    int ScaledConsumption(ResourceType type, int difficulty) const;
    // The whole per-resource map scaled for a difficulty level (entries that
    // scale to 0 are dropped).
    std::map<ResourceType, int> ScaledMap(int difficulty) const;
    // Normalized [0,1] priority weight for one resource; PriorityCeiling (1.0)
    // for anything without an explicit `priority` line.
    double NormalizedPriority(ResourceType type) const;
    // The whole priority table normalized to [0,1] (only resources with an
    // explicit line — DiagnoseResourceNeed treats an absent entry as 1.0).
    std::map<ResourceType, double> NormalizedPriorityMap() const;
};

// Cached catalog accessor — loads assets/data/ai.rtsdata once. A missing or
// empty file yields a zero bias (the AI just plays without amortization).
const AIEconomyBias& GetAIEconomyBias();

// Test seam / hot-reload: parse a specific file.
AIEconomyBias LoadAIEconomyBiasFromFile(const std::string& path);

#endif
