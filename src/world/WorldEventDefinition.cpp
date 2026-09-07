#include "world/WorldEventDefinition.h"

#include "data/RtsDataFile.h"
#include "economy/BalanceStatCatalog.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

namespace
{
    constexpr const char* DefaultEventDataPath = "assets/data/events.rtsdata";

    void Error(WorldEventDefinitionLoadResult& result, const std::string& path,
               std::size_t line, std::string message)
    {
        result.diagnostics.push_back({path, line, std::move(message)});
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

    bool ParseTicks(const RtsDataLine& tokens, std::size_t index, std::uint64_t& value)
    {
        int parsed = 0;
        if (!ParseInt(tokens, index, parsed) || parsed < 0)
            return false;
        value = static_cast<std::uint64_t>(parsed);
        return true;
    }

    bool ParseTrigger(const std::string& value, WorldEventTriggerDomain& out)
    {
        if (value == "ProvincePeriodic" || value == "Periodic")
            out = WorldEventTriggerDomain::ProvincePeriodic;
        else if (value == "Discovery")
            out = WorldEventTriggerDomain::Discovery;
        else if (value == "Route")
            out = WorldEventTriggerDomain::Route;
        else if (value == "Raid")
            out = WorldEventTriggerDomain::Raid;
        else
            return false;
        return true;
    }

    bool ParseKind(const std::string& value, ProvinceKind& out)
    {
        if (value == "Buildable") out = ProvinceKind::Buildable;
        else if (value == "NeutralCity" || value == "NeutralSettlement") out = ProvinceKind::NeutralSettlement;
        else if (value == "BanditCamp") out = ProvinceKind::BanditCamp;
        else if (value == "EventSite" || value == "TreasureSite") out = ProvinceKind::TreasureSite;
        else return false;
        return true;
    }

    bool ParseStat(const std::string& value, BalanceStat& out)
    {
        const auto stat = TryParseBalanceStat(value);
        if (!stat.has_value())
            return false;
        out = *stat;
        return true;
    }

    bool ParsePolarity(const std::string& value, WorldEventPolarity& out)
    {
        if (value == "Negative") out = WorldEventPolarity::Negative;
        else if (value == "Positive") out = WorldEventPolarity::Positive;
        else if (value == "Neutral") out = WorldEventPolarity::Neutral;
        else return false;
        return true;
    }

    bool ParseEffect(const RtsDataLine& tokens, WorldEventDefinition& definition)
    {
        if (tokens.size() < 2)
            return false;
        if (tokens[1] == "ModifyResource" || tokens[1] == "DamageCityStock" ||
            tokens[1] == "DamageProvinceStock")
        {
            if (tokens.size() != 4)
                return false;
            ResourceType type = ResourceType::Null;
            int amount = 0;
            if (!TryParseResourceType(tokens[2], type) || !ParseInt(tokens, 3, amount) || amount <= 0)
                return false;
            if (tokens[1] == "ModifyResource") definition.effects.emplace_back(ModifyResourceEffect{type, amount});
            else if (tokens[1] == "DamageCityStock") definition.effects.emplace_back(DamageCityStockEffect{type, amount});
            else definition.effects.emplace_back(DamageProvinceStockEffect{type, amount});
            return true;
        }
        if (tokens[1] == "AddTimedModifier")
        {
            AddTimedModifierEffect effect;
            if (tokens.size() != 6 || !ParseStat(tokens[2], effect.stat) ||
                !ParseDouble(tokens, 3, effect.additive) || !ParseDouble(tokens, 4, effect.multiplier) ||
                !ParseTicks(tokens, 5, effect.durationTicks) || effect.durationTicks == 0 ||
                effect.multiplier < 0.0)
                return false;
            definition.effects.emplace_back(effect);
            return true;
        }
        if (tokens[1] == "ModifyTradeScore")
        {
            int amount = 0;
            if (tokens.size() != 3 || !ParseInt(tokens, 2, amount) || amount == 0)
                return false;
            definition.effects.emplace_back(ModifyTradeScoreEffect{amount});
            return true;
        }
        if (tokens[1] == "KillJourneyUnitsPercent")
        {
            KillJourneyUnitsEffect effect;
            if ((tokens.size() != 4 && tokens.size() != 5 && tokens.size() != 6) ||
                !ParseInt(tokens, 2, effect.minimumFractionBasisPoints) ||
                !ParseInt(tokens, 3, effect.maximumFractionBasisPoints) ||
                effect.minimumFractionBasisPoints < 0 ||
                effect.minimumFractionBasisPoints > effect.maximumFractionBasisPoints ||
                effect.maximumFractionBasisPoints > 10000)
                return false;
            effect.minimumUnits = 1;
            effect.maximumUnits = 0;
            if (tokens.size() >= 5 && (!ParseInt(tokens, 4, effect.minimumUnits) ||
                                       effect.minimumUnits < 0))
                return false;
            if (tokens.size() == 6 && (!ParseInt(tokens, 5, effect.maximumUnits) ||
                                       effect.maximumUnits < 0 ||
                                       effect.maximumUnits > 0 &&
                                           effect.maximumUnits < effect.minimumUnits))
                return false;
            definition.effects.emplace_back(effect);
            return true;
        }
        if (tokens[1] == "KillJourneyUnits" || tokens[1] == "StartRaid" || tokens[1] == "DestroyBuilding")
        {
            int amount = 0;
            if (tokens.size() != 3 || !ParseInt(tokens, 2, amount) || amount <= 0)
                return false;
            if (tokens[1] == "KillJourneyUnits") definition.effects.emplace_back(KillJourneyUnitsEffect{amount});
            else if (tokens[1] == "StartRaid") definition.effects.emplace_back(StartRaidEffect{amount});
            else
            {
                if (definition.trigger != WorldEventTriggerDomain::Raid)
                    return false;
                definition.effects.emplace_back(DestroyBuildingEffect{amount});
            }
            return true;
        }
        return false;
    }
}

WorldEventDefinitionLoadResult LoadWorldEventDefinitionsFromFile(const std::string& path)
{
    WorldEventDefinitionLoadResult result;
    const RtsDataDocument document = ReadRtsDataDocument(path);
    for (const auto& diagnostic : document.diagnostics)
        result.diagnostics.push_back({diagnostic.path, diagnostic.line, diagnostic.message});
    if (!document.IsValid())
        return result;

    std::optional<WorldEventDefinition> current;
    std::size_t currentLine = 0;
    std::set<std::string> seenFields;
    bool blockValid = true;
    bool periodicScheduleSeen = false;
    const auto fail = [&](std::size_t line, const std::string& message)
    {
        Error(result, path, line, "event: " + message);
        blockValid = false;
    };
    for (std::size_t index = 0; index < document.lines.size(); ++index)
    {
        const auto& tokens = document.lines[index];
        const std::size_t line = document.sourceLines[index];
        if (tokens.empty()) continue;
        if (tokens[0] == "periodic_schedule")
        {
            if (current.has_value())
            {
                Error(result, path, line, "periodic_schedule must be top-level");
                continue;
            }
            if (periodicScheduleSeen || tokens.size() != 9 ||
                tokens[1] != "initial_delay_ticks" ||
                tokens[3] != "check_interval_ticks" ||
                tokens[5] != "chance_bp" ||
                tokens[7] != "minimum_gap_ticks")
            {
                Error(result, path, line, "invalid or duplicate periodic_schedule");
                continue;
            }
            PeriodicEventScheduleDefinition schedule;
            int chance = 0;
            if (!ParseTicks(tokens, 2, schedule.initialDelayTicks) ||
                !ParseTicks(tokens, 4, schedule.checkIntervalTicks) ||
                !ParseInt(tokens, 6, chance) || chance < 0 || chance > 10000 ||
                !ParseTicks(tokens, 8, schedule.minimumGapTicks) ||
                !schedule.IsValid())
            {
                Error(result, path, line, "periodic_schedule values are invalid");
                continue;
            }
            schedule.occurrenceChanceBasisPoints = chance;
            result.periodicSchedule = schedule;
            periodicScheduleSeen = true;
            continue;
        }
        if (tokens[0] == "event")
        {
            if (current.has_value()) fail(line, "previous block is missing end");
            if (tokens.size() != 2 || tokens[1].empty())
            {
                Error(result, path, line, "event requires a non-empty ID");
                current.reset(); blockValid = false; continue;
            }
            current.emplace();
            current->id = tokens[1];
            currentLine = line;
            current->name = current->id;
            current->title = current->id;
            seenFields.clear();
            blockValid = true;
            continue;
        }
        if (!current.has_value())
        {
            Error(result, path, line, "unknown top-level token '" + tokens[0] + "'");
            continue;
        }
        if (tokens[0] == "end")
        {
            if (tokens.size() != 1) fail(line, "end does not accept arguments");
            if (current->trigger == WorldEventTriggerDomain::ProvincePeriodic &&
                current->repeatCooldownTicks == 0)
                fail(line, "periodic event requires repeat_cooldown_ticks");
            if (current->trigger == WorldEventTriggerDomain::ProvincePeriodic &&
                current->chanceBasisPoints != 0)
                fail(line, "periodic event chance_bp must be 0; use periodic_schedule");
            if (current->effects.size() > 64) fail(line, "too many effects");
            if (std::any_of(current->allowedKinds.begin(), current->allowedKinds.end(),
                            [&](ProvinceKind kind)
                            { return std::find(current->excludedKinds.begin(), current->excludedKinds.end(), kind) != current->excludedKinds.end(); }))
                fail(line, "province kind cannot be both allowed and excluded");
            if (blockValid && current->name.empty()) fail(line, "name must not be empty");
            if (blockValid && current->title.empty()) fail(line, "title must not be empty");
            if (blockValid && result.definitions.contains(current->id)) fail(currentLine, "duplicate event ID");
            if (blockValid) result.definitions.emplace(current->id, *current);
            current.reset();
            continue;
        }
        auto& definition = *current;
        const bool scalar = tokens[0] != "allow_kind" && tokens[0] != "exclude_kind" &&
                            tokens[0] != "effect";
        if (scalar && !seenFields.insert(tokens[0]).second)
        {
            fail(line, "duplicate field '" + tokens[0] + "'");
            continue;
        }
        if (tokens[0] == "name" || tokens[0] == "title" || tokens[0] == "description" ||
            tokens[0] == "follow_up_pool" || tokens[0] == "require_trait" ||
            tokens[0] == "require_adjacent_trait")
        {
            if (tokens.size() != 2) { fail(line, tokens[0] + " requires one value"); continue; }
            if (tokens[0] == "name") definition.name = tokens[1];
            else if (tokens[0] == "title") definition.title = tokens[1];
            else if (tokens[0] == "description") definition.description = tokens[1];
            else if (tokens[0] == "follow_up_pool") definition.followUpPoolId = tokens[1];
            else if (tokens[0] == "require_trait") definition.requiredTrait = tokens[1];
            else definition.requiredAdjacentTrait = tokens[1];
        }
        else if (tokens[0] == "require_produced_resource")
        {
            if (tokens.size() != 2 || !TryParseResourceType(tokens[1],
                                                              definition.requiredProducedResource))
                fail(line, "require_produced_resource must name a valid resource");
        }
        else if (tokens[0] == "trigger")
        {
            if (tokens.size() != 2 || !ParseTrigger(tokens[1], definition.trigger)) fail(line, "unknown trigger");
        }
        else if (tokens[0] == "polarity")
        {
            if (tokens.size() != 2 || !ParsePolarity(tokens[1], definition.polarity))
                fail(line, "polarity must be Negative, Positive or Neutral");
        }
        else if (tokens[0] == "chance_bp")
        {
            if (tokens.size() != 2 || !ParseInt(tokens, 1, definition.chanceBasisPoints) ||
                definition.chanceBasisPoints < 0 || definition.chanceBasisPoints > 10000)
                fail(line, "chance_bp must be between 0 and 10000");
        }
        else if (tokens[0] == "weight")
        {
            if (tokens.size() != 2 || !ParseInt(tokens, 1, definition.weight) || definition.weight < 0)
                fail(line, "weight must be non-negative");
        }
        else if (tokens[0] == "duration_ticks" || tokens[0] == "repeat_cooldown_ticks")
        {
            std::uint64_t value = 0;
            if (tokens.size() != 2 || !ParseTicks(tokens, 1, value) ||
                (tokens[0] == "repeat_cooldown_ticks" && value == 0))
                fail(line, tokens[0] + " must be a valid tick count");
            else if (tokens[0] == "duration_ticks") definition.durationTicks = value;
            else definition.repeatCooldownTicks = value;
        }
        else if (tokens[0] == "allow_kind" || tokens[0] == "exclude_kind")
        {
            ProvinceKind kind;
            if (tokens.size() != 2 || !ParseKind(tokens[1], kind)) { fail(line, "unknown province kind"); continue; }
            auto& target = tokens[0] == "allow_kind" ? definition.allowedKinds : definition.excludedKinds;
            if (std::find(target.begin(), target.end(), kind) != target.end()) fail(line, "duplicate province kind");
            else target.push_back(kind);
        }
        else if (tokens[0] == "min_route_level")
        {
            if (tokens.size() != 2 || !ParseInt(tokens, 1, definition.minimumRouteLevel) ||
                definition.minimumRouteLevel < 0) fail(line, "min_route_level must be non-negative");
        }
        else if (tokens[0] == "effect")
        {
            if (!ParseEffect(tokens, definition)) fail(line, "unknown or invalid effect");
        }
        else
            fail(line, "unknown token '" + tokens[0] + "'");
    }
    if (current.has_value()) Error(result, path, currentLine, "event block is missing end");
    if (!result.diagnostics.empty()) result.definitions.clear();
    return result;
}

namespace
{
    const WorldEventDefinitionLoadResult& LoadedWorldEventDefinitions()
    {
        static const WorldEventDefinitionLoadResult loaded =
            LoadWorldEventDefinitionsFromFile(DefaultEventDataPath);
        return loaded;
    }
}

const WorldEventCatalog& GetWorldEventCatalog()
{
    return LoadedWorldEventDefinitions().definitions;
}

const PeriodicEventScheduleDefinition& GetPeriodicEventScheduleDefinition()
{
    return LoadedWorldEventDefinitions().periodicSchedule;
}

const WorldEventDefinition* FindWorldEventDefinition(std::string_view id)
{
    const auto& catalog = GetWorldEventCatalog();
    const auto it = catalog.find(std::string(id));
    return it == catalog.end() ? nullptr : &it->second;
}
