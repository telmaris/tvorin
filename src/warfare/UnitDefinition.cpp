#include "warfare/UnitDefinition.h"
#include "data/RtsDataFile.h"
#include "core/Log.h"

#include <algorithm>

namespace
{
    constexpr const char* unitDataPath = "assets/data/units.rtsdata";

    // Initializes ParseBuildingType. Mirrors economy/BuildingConfig.cpp's copy —
    // duplicated rather than shared, consistent with the existing (pre-ETAP-3)
    // convention of a small per-file parser helper.
    BuildingType ParseBuildingType(const std::string& value)
    {
        if (value == "Barracks") return BuildingType::Barracks;
        return BuildingType::Barracks;
    }

    // Initializes ParseResourceType. Mirrors economy/BuildingConfig.cpp's copy.
    ResourceType ParseResourceType(const std::string& value)
    {
        if (value == "WOOD") return ResourceType::WOOD;
        if (value == "PLANKS") return ResourceType::PLANKS;
        if (value == "COAL") return ResourceType::COAL;
        if (value == "STONE") return ResourceType::STONE;
        if (value == "IRON_ORE") return ResourceType::IRON_ORE;
        if (value == "IRON") return ResourceType::IRON;
        if (value == "COPPER_ORE") return ResourceType::COPPER_ORE;
        if (value == "COPPER") return ResourceType::COPPER;
        if (value == "SILVER_ORE") return ResourceType::SILVER_ORE;
        if (value == "SILVER") return ResourceType::SILVER;
        if (value == "GOLD_ORE") return ResourceType::GOLD_ORE;
        if (value == "GOLD") return ResourceType::GOLD;
        if (value == "LEATHER") return ResourceType::LEATHER;
        if (value == "MEAT") return ResourceType::MEAT;
        if (value == "WHEAT") return ResourceType::WHEAT;
        if (value == "BREAD") return ResourceType::BREAD;
        if (value == "FLOUR") return ResourceType::FLOUR;
        if (value == "WATER") return ResourceType::WATER;
        if (value == "BEER") return ResourceType::BEER;
        if (value == "COINS") return ResourceType::COINS;
        if (value == "FOOD_PROVISIONS") return ResourceType::FOOD_PROVISIONS;
        if (value == "PAPER") return ResourceType::PAPER;
        if (value == "TOOLS") return ResourceType::TOOLS;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "STEEL_SWORD") return ResourceType::STEEL_SWORD;
        if (value == "BOW") return ResourceType::BOW;
        if (value == "ARROWS") return ResourceType::ARROWS;
        if (value == "HORSE") return ResourceType::HORSE;
        if (value == "BRONZE_SWORD") return ResourceType::BRONZE_SWORD;
        if (value == "SPEAR") return ResourceType::SPEAR;
        if (value == "CROSSBOW") return ResourceType::CROSSBOW;
        if (value == "BOLTS") return ResourceType::BOLTS;
        if (value == "WOODEN_SHIELD") return ResourceType::WOODEN_SHIELD;
        if (value == "IRON_SHIELD") return ResourceType::IRON_SHIELD;
        if (value == "LEATHER_ARMOR") return ResourceType::LEATHER_ARMOR;
        if (value == "IRON_ARMOR") return ResourceType::IRON_ARMOR;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "HEAVY_ARMOR") return ResourceType::HEAVY_ARMOR;
        if (value == "HEAVY_BOW") return ResourceType::HEAVY_BOW;
        if (value == "BALLISTA") return ResourceType::BALLISTA;
        if (value == "BATTERING_RAM") return ResourceType::BATTERING_RAM;
        if (value == "CATAPULT") return ResourceType::CATAPULT;
        return ResourceType::Null;
    }

    // Initializes ParseUnit.
    UnitDefinition ParseUnit(const std::vector<std::vector<std::string>>& lines, size_t& index)
    {
        UnitDefinition definition;
        definition.id = lines[index].size() > 1 ? lines[index][1] : "";
        definition.displayName = definition.id;

        while (++index < lines.size())
        {
            const auto& tokens = lines[index];
            const auto& command = tokens[0];
            if (command == "end")
                return definition;

            if (command == "name" && tokens.size() >= 2)
                definition.displayName = tokens[1];
            else if (command == "texture_id" && tokens.size() >= 2)
                definition.textureId = RtsDataIntOr(tokens[1]);
            else if (command == "max_hp" && tokens.size() >= 2)
                definition.maxHp = RtsDataDoubleOr(tokens[1]);
            else if (command == "field_attack" && tokens.size() >= 2)
                definition.fieldAttack = RtsDataDoubleOr(tokens[1]);
            else if (command == "siege_power" && tokens.size() >= 2)
                definition.siegePower = RtsDataDoubleOr(tokens[1]);
            else if (command == "armor" && tokens.size() >= 2)
                definition.armor = RtsDataDoubleOr(tokens[1]);
            else if (command == "move_speed" && tokens.size() >= 2)
                definition.moveSpeed = RtsDataDoubleOr(tokens[1]);
            else if (command == "attack_speed" && tokens.size() >= 2)
                definition.attackSpeed = RtsDataDoubleOr(tokens[1]);
            else if (command == "role" && tokens.size() >= 2)
            {
                if (tokens[1] == "Scout") definition.role = UnitRole::Scout;
                else if (tokens[1] == "Supply") definition.role = UnitRole::Supply;
                else if (tokens[1] == "Garrison") definition.role = UnitRole::Garrison;
                else
                {
                    definition.id.clear();
                    Log::Msg("[UnitCatalog]", "unknown unit role '", tokens[1], "' near line ", index + 1);
                }
            }
            else if (command == "attack_range" && tokens.size() >= 2)
                definition.attackRange = RtsDataDoubleOr(tokens[1]);
            else if (command == "movement" && tokens.size() >= 2)
                definition.movementType = tokens[1] == "flying" ? MovementType::Flying : MovementType::Ground;
            else if (command == "can_target_flying" && tokens.size() >= 2)
                definition.canTargetFlying = RtsDataIntOr(tokens[1]) != 0;
            else if (command == "cavalry" && tokens.size() >= 2)
                definition.cavalry = RtsDataIntOr(tokens[1]) != 0;
            else if (command == "anti_cavalry_multiplier" && tokens.size() >= 2)
                definition.antiCavalryMultiplier = RtsDataDoubleOr(tokens[1]);
            else if (command == "area_targets" && tokens.size() >= 2)
                definition.areaTargets = std::max(1, RtsDataIntOr(tokens[1]));
            else if (command == "collider_radius" && tokens.size() >= 2)
                definition.colliderRadius = RtsDataDoubleOr(tokens[1]);
            else if (command == "ability" && tokens.size() >= 2)
                definition.abilities.push_back(tokens[1]);
            else if (command == "equipment_slot" && tokens.size() >= 2)
                definition.equipmentSlots.push_back(tokens[1]);
            else if (command == "recruit_building" && tokens.size() >= 2)
                definition.recruitBuilding = ParseBuildingType(tokens[1]);
            else if (command == "requires_tech" && tokens.size() >= 2)
                definition.requiredTechnology = tokens[1];
            else if (command == "recruit_time" && tokens.size() >= 2)
                definition.recruitTime = RtsDataDoubleOr(tokens[1]);
            else if (command == "manpower_cost" && tokens.size() >= 2)
                definition.manpowerCost = RtsDataDoubleOr(tokens[1]);
            else if (command == "garrison_food_upkeep_per_minute" && tokens.size() >= 2)
                definition.garrisonFoodUpkeepPerMinute = std::max(0.0, RtsDataDoubleOr(tokens[1]));
            else if (command == "cost" && tokens.size() >= 3)
                definition.cost.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2])});
        }

        return definition;
    }

    std::map<std::string, UnitDefinition> ParseUnitDefinitions(const std::vector<std::vector<std::string>>& lines)
    {
        std::map<std::string, UnitDefinition> definitions;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i][0] != "unit" || lines[i].size() < 2)
                continue;

            UnitDefinition definition = ParseUnit(lines, i);
            if (!definition.IsValid())
            {
                Log::Msg("[UnitCatalog]", "Rejected invalid unit definition: ",
                         definition.id.empty() ? "<missing id>" : definition.id);
                continue;
            }
            if (definitions.contains(definition.id))
            {
                Log::Msg("[UnitCatalog]", "Duplicate unit id ignored: ", definition.id);
                continue;
            }
            definitions[definition.id] = std::move(definition);
        }
        return definitions;
    }
}

std::map<std::string, UnitDefinition> LoadUnitDefinitionsFromFile(const std::string& path)
{
    return ParseUnitDefinitions(ReadRtsDataLines(path));
}

const std::map<std::string, UnitDefinition>& GetUnitCatalog()
{
    static const std::map<std::string, UnitDefinition> catalog = LoadUnitDefinitionsFromFile(unitDataPath);
    return catalog;
}

const UnitDefinition* FindUnitDefinition(const std::string& id)
{
    const auto& catalog = GetUnitCatalog();
    auto it = catalog.find(id);
    return it != catalog.end() ? &it->second : nullptr;
}
