#ifndef EQUIPMENT_H
#define EQUIPMENT_H

#include "data/Resource.h"

#include <cstdint>
#include <vector>

// ─── Equipment taxonomy ───────────────────────────────────────────────────────
// Equipment resources (swords, shields, …) are ordinary ResourceTypes produced
// by the Smith and stored in warehouses. This taxonomy describes *what role* an
// equipment resource fills and *how good* it is, so ResourceCategoryOf() (see
// Resource.h/.cpp) can classify individual sword/bow/armor resource types into
// their broad category for tech/focus bonuses ("+10% Sword production") without
// hard-coding each resource id.
//
// Adding a new piece of equipment = add a ResourceType + one row in the profile
// table (Equipment.cpp). Nothing else needs to know about it.

enum class EquipmentCategory : uint8_t
{
    None = 0,
    Sword,      // primary melee
    Spear,      // primary melee, anti-cavalry flavour later
    Bow,        // primary ranged
    Crossbow,   // primary ranged, higher piercing
    Firearm,    // primary ranged, gunpowder — highest tier (Phase 3)
    Shield,     // defensive
    Armor,      // defensive
    Ammo,       // consumed by ranged weapons
    Count
};

// Material progression: stone -> copper -> bronze -> iron -> steel (plus wood/leather
// for shields/armor). Higher tiers yield higher quality.
enum class EquipmentMaterial : uint8_t
{
    None = 0,
    Wood,
    Stone,
    Leather,
    Copper,
    Bronze,
    Iron,
    Steel,
    Blackpowder,   // gunpowder-era firearms, above steel (Phase 3)
    Count
};

// Describes one equipment resource: its role, material and relative quality.
struct EquipmentProfile
{
    ResourceType     resource{ResourceType::Null};
    EquipmentCategory category{EquipmentCategory::None};
    EquipmentMaterial material{EquipmentMaterial::None};
    float             quality{1.0f};   // effectiveness multiplier vs a baseline tier-1 item
};

// All known equipment profiles, ordered ascending by quality within a category.
const std::vector<EquipmentProfile>& GetEquipmentProfiles();

// Returns the profile for a resource, or nullptr when it is not equipment.
const EquipmentProfile* FindEquipmentProfile(ResourceType type);

#endif
