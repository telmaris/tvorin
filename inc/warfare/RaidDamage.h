#ifndef RAID_DAMAGE_H
#define RAID_DAMAGE_H

#include "economy/Building.h"
#include "data/Resource.h"
#include "world/WorldIds.h"

#include <cstdint>
#include <map>
#include <vector>

class Player;
struct ProvinceEconomy;
class TileMap;

struct RaidResourceSnapshot
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

struct RaidBuildingSnapshot
{
    int buildingId{0};
    BuildingType buildingType{BuildingType::Building};
    bool completed{false};
    bool owned{false};
    bool raidDestructible{false};
    bool raidStockLossTarget{false};
    bool coveredByGarrison{false};
    int intrinsicResilienceBasisPoints{0};
    int coverageProtectionBasisPoints{0};
    int garrisonStrengthBasisPoints{0};
    int readinessBasisPoints{10000};
    // Negative means "use RaidDamageRules"; zero is a deliberate no-risk
    // value and must not silently fall back to the default.
    int baseDestructionChanceBasisPoints{-1};
    int stockLossFractionBasisPoints{7000};
    std::vector<RaidResourceSnapshot> resources;
};

struct RaidDamageRules
{
    int baseDestructionChanceBasisPoints{800};
    int uncoveredStockLossFractionBasisPoints{7000};
    int coveredStockLossFractionBasisPoints{2500};
};

struct RaidBuildingDamage
{
    int buildingId{0};
    bool destroyed{false};
    std::map<ResourceType, int> lostResources;
};

struct RaidDamageOutcome
{
    bool valid{false};
    std::vector<RaidBuildingDamage> buildings;
    std::map<ResourceType, int> lostResources;
};

// Pointer-free deterministic raid damage plan. Runtime code first copies live
// buildings into RaidBuildingSnapshot values, then this resolver performs all
// rolls without mutating the building collection.
class RaidDamageResolver
{
public:
    static RaidDamageOutcome Resolve(const std::vector<RaidBuildingSnapshot>& buildings,
                                     BattleId battleId, ProvinceId provinceId,
                                     std::uint64_t seed,
                                     const RaidDamageRules& rules = {});

    static int CalculateDestructionChanceBasisPoints(
        const RaidBuildingSnapshot& building, const RaidDamageRules& rules);
};

// Builds the pointer-free input from a live player province. This is the only
// place that reads SafetyComponent, defense coverage, garrison assignments and
// balance modifiers together.
std::vector<RaidBuildingSnapshot> BuildRaidBuildingSnapshots(
    const Player& player, const ProvinceEconomy& province,
    const std::vector<Building*>& defenseBuildings = {},
    const RaidDamageRules& rules = {});

// Applies an already resolved plan in stable ID order through TileMap's normal
// unregister/logistics cleanup path. It never refunds or salvages the player.
bool ApplyRaidDamage(TileMap& tilemap, Player& player,
                     const RaidDamageOutcome& outcome);

#endif
