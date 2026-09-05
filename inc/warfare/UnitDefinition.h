#ifndef UNIT_DEFINITION_H
#define UNIT_DEFINITION_H

#include "economy/Building.h"
#include "data/Resource.h"

#include <map>
#include <string>
#include <vector>

// Damage/movement taxonomy — a single Physical/Ground value today, but the
// enum (and the resistances map below) exist from day one so future unit
// types (ranged, flying, elemental damage) are pure data additions, not
// architecture changes.
enum class DamageType
{
    Physical
};

enum class MovementType
{
    Ground,
    Flying
};

enum class UnitRole
{
    Line,
    Scout,
    Supply,
    Garrison
};

struct UnitCostEntry
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

// Immutable, data-driven unit type (assets/data/units.rtsdata). One
// UnitDefinition per unit *type* (e.g. "swordsman"); BattleUnit is the
// per-instance runtime state referencing a definition by id.
struct UnitDefinition
{
    std::string id;
    std::string displayName;
    int textureId{0};

    // Base stats — effective values go through BalanceModifierSet (see
    // BattleUnit::GetEffective*), never used directly by gameplay code.
    double maxHp{1.0};
    double fieldAttack{0.0};
    double siegePower{0.0};
    double armor{0.0};
    double moveSpeed{1.0};
    double attackSpeed{1.0};
    UnitRole role{UnitRole::Line};

    // Extensibility seams (ETAP 5+ consumes these; parser accepts them now so
    // future units are pure data additions).
    double attackRange{0.0}; // 0 = melee
    DamageType damageType{DamageType::Physical};
    std::map<DamageType, float> resistances;
    MovementType movementType{MovementType::Ground};
    bool canTargetFlying{false};
    bool cavalry{false};
    double antiCavalryMultiplier{1.0};
    int areaTargets{1};
    double colliderRadius{0.4};
    std::vector<std::string> abilities; // accepted, ignored until an ability system exists

    BuildingType recruitBuilding{BuildingType::Barracks};
    // Empty means available from the start; otherwise recruitment is gated by
    // a completed technology id from technologies.rtsdata.
    std::string requiredTechnology;
    double recruitTime{10.0};
    std::vector<UnitCostEntry> cost;
    double manpowerCost{0.0};
    double garrisonFoodUpkeepPerMinute{0.5};

    std::vector<std::string> equipmentSlots; // accepted, ignored until ETAP 3.4's equipment system lands

    bool IsValid() const { return !id.empty() && maxHp > 0.0; }
};

// Returns every loaded unit definition, keyed by id in deterministic
// (std::map) order. Invalid entries are logged and dropped, never crash.
const std::map<std::string, UnitDefinition>& GetUnitCatalog();

// Loads unit definitions from a specific data file (test seam / hot-reload).
std::map<std::string, UnitDefinition> LoadUnitDefinitionsFromFile(const std::string& path);

// Returns the definition matching an id, or nullptr if unknown.
const UnitDefinition* FindUnitDefinition(const std::string& id);

#endif
