#include "world/ProvinceDefinition.h"

#include "data/RtsDataFile.h"
#include "economy/BalanceStatCatalog.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

namespace
{
    constexpr const char* DefaultProvinceDataPath = "assets/data/provinces.rtsdata";

    void AddDiagnostic(ProvinceDefinitionCatalogLoadResult& result,
                       const std::string& path, std::size_t line, std::string message)
    {
        result.diagnostics.push_back({path, line, std::move(message)});
    }

    std::optional<ProvinceDefinitionArchetype> ParseArchetype(const std::string& value)
    {
        if (value == "Buildable") return ProvinceDefinitionArchetype::Buildable;
        if (value == "NeutralCity" || value == "NeutralSettlement")
            return ProvinceDefinitionArchetype::NeutralCity;
        if (value == "BanditCamp") return ProvinceDefinitionArchetype::BanditCamp;
        if (value == "EventSite" || value == "TreasureSite")
            return ProvinceDefinitionArchetype::EventSite;
        return std::nullopt;
    }

    std::optional<ProvinceEffectTarget> ParseEffectTarget(const std::string& value)
    {
        if (value == "Self" || value == "SelfProvince")
            return ProvinceEffectTarget::SelfProvince;
        if (value == "Adjacent" || value == "AdjacentProvince")
            return ProvinceEffectTarget::AdjacentProvince;
        if (value == "Connection" || value == "IncidentConnection")
            return ProvinceEffectTarget::IncidentConnection;
        return std::nullopt;
    }

    std::optional<BalanceStat> ParseBalanceStatName(const std::string& value)
    {
        return TryParseBalanceStat(value);
    }

    bool ParseInt(const RtsDataLine& tokens, std::size_t index, int& value)
    {
        return index < tokens.size() && ParseRtsDataInt(tokens[index], value);
    }

    bool ParseDouble(const RtsDataLine& tokens, std::size_t index, double& value)
    {
        return index < tokens.size() && ParseRtsDataDouble(tokens[index], value) &&
               std::isfinite(value);
    }

    bool ParseIntRange(const RtsDataLine& tokens, ProvinceIntRange& range)
    {
        return tokens.size() == 3 && ParseInt(tokens, 1, range.min) &&
               ParseInt(tokens, 2, range.max);
    }

    bool ParseValueRange(const RtsDataLine& tokens, ProvinceValueRange& range)
    {
        return tokens.size() == 3 && ParseDouble(tokens, 1, range.min) &&
               ParseDouble(tokens, 2, range.max);
    }

    bool IsValid(const ProvinceIntRange& range)
    {
        return range.min >= 0 && range.max >= 0 && range.min <= range.max;
    }

    bool IsValid(const ProvinceValueRange& range)
    {
        return std::isfinite(range.min) && std::isfinite(range.max) &&
               range.min >= 0.0 && range.max >= 0.0 && range.min <= range.max;
    }

    bool IsValidDefinition(const ProvinceDefinition& definition,
                           ProvinceDefinitionCatalogLoadResult& result,
                           const std::string& path, std::size_t line)
    {
        bool valid = true;
        const auto report = [&](const std::string& message)
        {
            AddDiagnostic(result, path, line, "province '" + definition.id + "': " + message);
            valid = false;
        };

        if (definition.id.empty()) report("ID must not be empty");
        if (definition.displayName.empty()) report("display_name must not be empty");
        if (definition.generationWeight < 0) report("weight must be non-negative");
        if (!IsValid(definition.sizeX) || !IsValid(definition.sizeY) ||
            definition.sizeX.min < 1 || definition.sizeY.min < 1)
            report("size range is invalid");
        if (!IsValid(definition.resourceWealth) || !IsValid(definition.resourceDensity) ||
            !IsValid(definition.resourceFieldSize) || !IsValid(definition.resourceRichness) ||
            !IsValid(definition.waterAmount) || !IsValid(definition.mountainAmount) ||
            !IsValid(definition.ruggedness))
            report("numeric range is invalid");
        if (!IsValid(definition.cityWealth) || !IsValid(definition.banditStrength) ||
            definition.banditRaidWeight < 0)
            report("integer range is invalid");
        if (definition.traitCount.has_value() &&
            (!IsValid(*definition.traitCount) || definition.traitCount->min < 0 ||
             definition.traitCount->max > static_cast<int>(definition.traits.size())))
            report("trait_count range is invalid");
        for (const auto& deposit : definition.resourceDeposits)
        {
            if (deposit.resource == ResourceType::Null || !IsValid(deposit.richness) ||
                deposit.weight <= 0)
                report("resource deposit is invalid");
        }

        std::set<std::string> traitIds;
        for (const auto& trait : definition.traits)
        {
            if (trait.id.empty() || !traitIds.insert(trait.id).second)
                report("trait ID is empty or duplicated");
            if (trait.weight <= 0)
                report("trait weight must be positive");
            for (const auto& effect : trait.effects)
            {
                if (!std::isfinite(effect.additive) || !std::isfinite(effect.multiplier) ||
                    effect.multiplier < 0.0)
                    report("trait effect value is invalid");
            }
        }
        return valid;
    }
}

ProvinceKind ProvinceDefinition::Kind() const
{
    switch (archetype)
    {
        case ProvinceDefinitionArchetype::Buildable: return ProvinceKind::Buildable;
        case ProvinceDefinitionArchetype::NeutralCity: return ProvinceKind::NeutralSettlement;
        case ProvinceDefinitionArchetype::BanditCamp: return ProvinceKind::BanditCamp;
        case ProvinceDefinitionArchetype::EventSite: return ProvinceKind::TreasureSite;
    }
    return ProvinceKind::Buildable;
}

ProvinceDefinitionCatalogLoadResult LoadProvinceDefinitionCatalog(const std::string& path)
{
    ProvinceDefinitionCatalogLoadResult result;
    const RtsDataDocument document = ReadRtsDataDocument(path);
    for (const auto& diagnostic : document.diagnostics)
        result.diagnostics.push_back({diagnostic.path, diagnostic.line, diagnostic.message});
    if (!document.IsValid())
        return result;

    std::optional<ProvinceDefinition> current;
    std::size_t currentLine = 0;
    bool blockValid = true;
    std::set<std::string> seenFields;
    ProvinceTraitDefinition* activeTrait = nullptr;

    const auto error = [&](std::size_t line, const std::string& message)
    {
        AddDiagnostic(result, path, line, message);
        blockValid = false;
    };
    const auto duplicateField = [&](std::size_t line, const std::string& field)
    {
        if (!seenFields.insert(field).second)
        {
            error(line, "duplicate field '" + field + "'");
            return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < document.lines.size(); ++i)
    {
        const RtsDataLine& tokens = document.lines[i];
        const std::size_t line = document.sourceLines[i];
        if (tokens.empty())
            continue;

        if (tokens[0] == "province")
        {
            if (current.has_value())
            {
                error(line, "province block started before previous block ended");
                current.reset();
            }
            if (tokens.size() != 2 || tokens[1].empty())
            {
                AddDiagnostic(result, path, line, "province requires a non-empty ID");
                blockValid = false;
                continue;
            }
            current.emplace();
            current->id = tokens[1];
            currentLine = line;
            blockValid = true;
            seenFields.clear();
            activeTrait = nullptr;
            continue;
        }

        if (!current.has_value())
        {
            AddDiagnostic(result, path, line, "unknown top-level token '" + tokens[0] + "'");
            continue;
        }

        if (tokens[0] == "end")
        {
            if (tokens.size() != 1)
                error(line, "end does not accept arguments");
            if (blockValid && IsValidDefinition(*current, result, path, currentLine))
            {
                if (!result.definitions.emplace(current->id, *current).second)
                    AddDiagnostic(result, path, currentLine,
                                  "duplicate province definition ID '" + current->id + "'");
            }
            current.reset();
            activeTrait = nullptr;
            continue;
        }

        auto& definition = *current;
        if (tokens[0] == "archetype")
        {
            if (duplicateField(line, tokens[0]) || tokens.size() != 2)
            {
                if (tokens.size() != 2) error(line, "archetype requires one value");
                continue;
            }
            const auto archetype = ParseArchetype(tokens[1]);
            if (!archetype.has_value())
                error(line, "unknown archetype '" + tokens[1] + "'");
            else
                definition.archetype = *archetype;
        }
        else if (tokens[0] == "display_name")
        {
            if (duplicateField(line, tokens[0]) || tokens.size() != 2)
            {
                if (tokens.size() != 2) error(line, "display_name requires one value");
                continue;
            }
            definition.displayName = tokens[1];
        }
        else if (tokens[0] == "weight")
        {
            if (duplicateField(line, tokens[0]) || tokens.size() != 2)
            {
                if (tokens.size() != 2) error(line, "weight requires one integer");
                continue;
            }
            if (!ParseInt(tokens, 1, definition.generationWeight))
                error(line, "weight must be an integer");
        }
        else if (tokens[0] == "trait_count")
        {
            ProvinceIntRange range;
            if (duplicateField(line, tokens[0]) || !ParseIntRange(tokens, range))
                error(line, "trait_count requires two integers in ascending order");
            else
                definition.traitCount = range;
        }
        else if (tokens[0] == "size_x")
        {
            if (duplicateField(line, tokens[0]) || !ParseIntRange(tokens, definition.sizeX))
                error(line, "size_x requires two integers in ascending order");
        }
        else if (tokens[0] == "size_y")
        {
            if (duplicateField(line, tokens[0]) || !ParseIntRange(tokens, definition.sizeY))
                error(line, "size_y requires two integers in ascending order");
        }
        else if (tokens[0] == "resource_wealth")
        {
            if (duplicateField(line, tokens[0]) || !ParseValueRange(tokens, definition.resourceWealth))
                error(line, "resource_wealth requires two finite non-negative numbers");
        }
        else if (tokens[0] == "resource_density")
        {
            if (duplicateField(line, tokens[0]) || !ParseValueRange(tokens, definition.resourceDensity))
                error(line, "resource_density requires two finite non-negative numbers");
        }
        else if (tokens[0] == "resource_field_size")
        {
            if (duplicateField(line, tokens[0]) || !ParseValueRange(tokens, definition.resourceFieldSize))
                error(line, "resource_field_size requires two finite non-negative numbers");
        }
        else if (tokens[0] == "resource_richness")
        {
            if (duplicateField(line, tokens[0]) || !ParseValueRange(tokens, definition.resourceRichness))
                error(line, "resource_richness requires two finite non-negative numbers");
        }
        else if (tokens[0] == "deposit")
        {
            if (tokens.size() != 5)
            {
                error(line, "deposit requires resource, two richness values and weight");
                continue;
            }
            ResourceType resource = ResourceType::Null;
            ProvinceValueRange richness;
            int weight = 0;
            if (!TryParseResourceType(tokens[1], resource) || resource == ResourceType::Null ||
                !ParseDouble(tokens, 2, richness.min) || !ParseDouble(tokens, 3, richness.max) ||
                !ParseInt(tokens, 4, weight) || !IsValid(richness) || weight <= 0)
            {
                error(line, "deposit has invalid resource, richness range or weight");
                continue;
            }
            definition.resourceDeposits.push_back({resource, richness, weight});
        }
        else if (tokens[0] == "water_amount" || tokens[0] == "mountain_amount" ||
                 tokens[0] == "ruggedness")
        {
            ProvinceValueRange* range = tokens[0] == "water_amount" ? &definition.waterAmount
                : tokens[0] == "mountain_amount" ? &definition.mountainAmount : &definition.ruggedness;
            if (duplicateField(line, tokens[0]) || !ParseValueRange(tokens, *range))
                error(line, tokens[0] + " requires two finite non-negative numbers");
        }
        else if (tokens[0] == "city_wealth")
        {
            if (duplicateField(line, tokens[0]) || !ParseIntRange(tokens, definition.cityWealth))
                error(line, "city_wealth requires two integers in ascending order");
        }
        else if (tokens[0] == "city_stock")
        {
            if (tokens.size() != 4)
            {
                error(line, "city_stock requires resource ID and two integers");
                continue;
            }
            ProvinceIntRange range;
            if (!ParseInt(tokens, 2, range.min) || !ParseInt(tokens, 3, range.max) ||
                !IsValid(range) || !definition.cityInitialStock.emplace(tokens[1], range).second)
                error(line, "city_stock has invalid or duplicated resource data");
        }
        else if (tokens[0] == "buy_price" || tokens[0] == "trade_price")
        {
            if (tokens.size() != 4)
            {
                error(line, tokens[0] + " requires resource ID and two numbers");
                continue;
            }
            ProvinceValueRange range;
            if (!ParseDouble(tokens, 2, range.min) || !ParseDouble(tokens, 3, range.max) ||
                !IsValid(range))
            {
                error(line, tokens[0] + " has invalid resource data");
                continue;
            }
            auto& target = tokens[0] == "buy_price"
                ? definition.cityBuyPrices : definition.cityTradePrices;
            if (!target.emplace(tokens[1], range).second)
                error(line, tokens[0] + " has duplicated resource data");
        }
        else if (tokens[0] == "barter_penalty")
        {
            if (duplicateField(line, tokens[0]) || tokens.size() != 2 ||
                !ParseDouble(tokens, 1, definition.barterPenaltyMultiplier) ||
                definition.barterPenaltyMultiplier < 1.0)
                error(line, "barter_penalty requires one finite number >= 1");
        }
        else if (tokens[0] == "bandit_strength")
        {
            if (duplicateField(line, tokens[0]) || !ParseIntRange(tokens, definition.banditStrength))
                error(line, "bandit_strength requires two integers in ascending order");
        }
        else if (tokens[0] == "loot_table")
        {
            if (duplicateField(line, tokens[0]) || tokens.size() != 2)
            {
                if (tokens.size() != 2) error(line, "loot_table requires one ID");
                continue;
            }
            definition.banditLootTableId = tokens[1];
        }
        else if (tokens[0] == "raid_weight")
        {
            if (duplicateField(line, tokens[0]) || tokens.size() != 2 ||
                !ParseInt(tokens, 1, definition.banditRaidWeight))
                error(line, "raid_weight requires one non-negative integer");
        }
        else if (tokens[0] == "event_pool")
        {
            if (duplicateField(line, tokens[0]) || tokens.size() != 2)
            {
                if (tokens.size() != 2) error(line, "event_pool requires one ID");
                continue;
            }
            definition.eventPoolId = tokens[1];
        }
        else if (tokens[0] == "trait")
        {
            if (tokens.size() < 2 || tokens.size() > 4 || tokens[1].empty())
            {
                error(line, "trait requires an ID and optional display name");
                activeTrait = nullptr;
                continue;
            }
            if (std::any_of(definition.traits.begin(), definition.traits.end(),
                            [&](const ProvinceTraitDefinition& trait) { return trait.id == tokens[1]; }))
            {
                error(line, "duplicate trait ID '" + tokens[1] + "'");
                activeTrait = nullptr;
                continue;
            }
            ProvinceTraitDefinition trait;
            trait.id = tokens[1];
            trait.displayName = tokens.size() >= 3 ? tokens[2] : tokens[1];
            if (tokens.size() == 4 && !ParseInt(tokens, 3, trait.weight))
            {
                error(line, "trait weight must be an integer");
                activeTrait = nullptr;
                continue;
            }
            definition.traits.push_back(std::move(trait));
            activeTrait = &definition.traits.back();
        }
        else if (tokens[0] == "effect")
        {
            if (activeTrait == nullptr || (tokens.size() != 5 && tokens.size() != 6))
            {
                error(line, "effect requires an active trait, target, stat, additive and multiplier");
                continue;
            }
            const auto target = ParseEffectTarget(tokens[1]);
            const auto stat = ParseBalanceStatName(tokens[2]);
            ProvinceEffectDefinition effect;
            if (!target.has_value() || !stat.has_value() ||
                !ParseDouble(tokens, 3, effect.additive) ||
                !ParseDouble(tokens, 4, effect.multiplier))
            {
                error(line, !stat.has_value() ? "unknown BalanceStat '" + tokens[2] + "'"
                                               : "invalid effect values");
                continue;
            }
            effect.target = *target;
            effect.stat = *stat;
            if (tokens.size() == 6)
                effect.condition = tokens[5];
            activeTrait->effects.push_back(std::move(effect));
        }
        else
        {
            error(line, "unknown province token '" + tokens[0] + "'");
        }
    }

    if (current.has_value())
        AddDiagnostic(result, path, currentLine, "province block is missing 'end'");
    if (!result.diagnostics.empty())
        result.definitions.clear();
    return result;
}

const ProvinceDefinitionCatalog& GetProvinceDefinitions()
{
    static const ProvinceDefinitionCatalogLoadResult loaded =
        LoadProvinceDefinitionCatalog(DefaultProvinceDataPath);
    return loaded.definitions;
}

const ProvinceDefinition* FindProvinceDefinition(std::string_view id)
{
    const auto& catalog = GetProvinceDefinitions();
    const auto it = catalog.find(std::string(id));
    return it == catalog.end() ? nullptr : &it->second;
}
