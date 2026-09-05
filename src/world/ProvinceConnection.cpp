#include "world/ProvinceConnection.h"

#include "data/RtsDataFile.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

namespace
{
    constexpr const char* DefaultRouteDataPath = "assets/data/routes.rtsdata";

    void AddDiagnostic(ProvinceConnectionDefinitionLoadResult& result,
                       const std::string& path, std::size_t line, std::string message)
    {
        result.diagnostics.push_back({path, line, std::move(message)});
    }

    bool ParseInt(const RtsDataLine& tokens, std::size_t index, int& value)
    {
        return index < tokens.size() && ParseRtsDataInt(tokens[index], value);
    }

    bool ParseUInt64(const RtsDataLine& tokens, std::size_t index, std::uint64_t& value)
    {
        int parsed = 0;
        if (!ParseInt(tokens, index, parsed) || parsed < 0)
            return false;
        value = static_cast<std::uint64_t>(parsed);
        return true;
    }

    bool ParseDouble(const RtsDataLine& tokens, std::size_t index, double& value)
    {
        return index < tokens.size() && ParseRtsDataDouble(tokens[index], value) &&
               std::isfinite(value);
    }

    bool IsValidLevel(const ProvinceConnectionLevelDefinition& level)
    {
        if (level.level < 0 || !std::isfinite(level.traversalTimeMultiplier) ||
            level.traversalTimeMultiplier <= 0.0 ||
            level.incidentChanceReductionBasisPoints < 0 ||
            level.incidentChanceReductionBasisPoints > 10000)
            return false;
        for (const auto& cost : level.upgradeCost)
            if (cost.type == ResourceType::Null || cost.amount <= 0)
                return false;
        return true;
    }

    ProvinceConnectionDefinition MakeFallbackRouteDefinition()
    {
        return {
            "land_route",
            {
                {0, 1.0, 0, 0, {}},
                {1, 0.85, 1000, 120, {{ResourceType::WOOD, 50}, {ResourceType::STONE, 25}}},
                {2, 0.70, 2200, 240, {{ResourceType::PLANKS, 75}, {ResourceType::STONE, 50}}}
            }};
    }
}

ProvinceConnectionDefinitionLoadResult LoadProvinceConnectionDefinitionsFromFile(
    const std::string& path)
{
    ProvinceConnectionDefinitionLoadResult result;
    const RtsDataDocument document = ReadRtsDataDocument(path);
    for (const auto& diagnostic : document.diagnostics)
        result.diagnostics.push_back({diagnostic.path, diagnostic.line, diagnostic.message});
    if (!document.IsValid())
        return result;

    std::optional<ProvinceConnectionDefinition> current;
    std::size_t currentLine = 0;
    bool blockValid = true;
    std::set<int> seenLevels;

    const auto error = [&](std::size_t line, const std::string& message)
    {
        AddDiagnostic(result, path, line, message);
        blockValid = false;
    };

    for (std::size_t i = 0; i < document.lines.size(); ++i)
    {
        const RtsDataLine& tokens = document.lines[i];
        const std::size_t line = document.sourceLines[i];
        if (tokens.empty())
            continue;

        if (tokens[0] == "route")
        {
            if (current.has_value())
            {
                error(line, "route block started before previous block ended");
                current.reset();
            }
            if (tokens.size() != 2 || tokens[1].empty())
            {
                AddDiagnostic(result, path, line, "route requires a non-empty ID");
                blockValid = false;
                continue;
            }
            current.emplace();
            current->id = tokens[1];
            currentLine = line;
            blockValid = true;
            seenLevels.clear();
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
            if (current->levels.empty())
                error(line, "route must define at least one level");
            if (blockValid)
            {
                std::sort(current->levels.begin(), current->levels.end(),
                          [](const auto& left, const auto& right) { return left.level < right.level; });
                for (std::size_t levelIndex = 0; levelIndex < current->levels.size(); ++levelIndex)
                {
                    const auto& level = current->levels[levelIndex];
                    if (level.level != static_cast<int>(levelIndex) || !IsValidLevel(level))
                        error(line, "route levels must be contiguous from 0 and valid");
                }
            }
            if (blockValid && !result.definitions.emplace(current->id, *current).second)
                AddDiagnostic(result, path, currentLine,
                              "duplicate route definition ID '" + current->id + "'");
            current.reset();
            continue;
        }

        if (tokens[0] == "level")
        {
            if (tokens.size() != 5)
            {
                error(line, "level requires number, time multiplier, incident reduction and duration");
                continue;
            }
            ProvinceConnectionLevelDefinition level;
            if (!ParseInt(tokens, 1, level.level) ||
                !ParseDouble(tokens, 2, level.traversalTimeMultiplier) ||
                !ParseInt(tokens, 3, level.incidentChanceReductionBasisPoints) ||
                !ParseUInt64(tokens, 4, level.upgradeDurationTicks) ||
                !seenLevels.insert(level.level).second)
            {
                error(line, "level contains invalid or duplicated data");
                continue;
            }
            current->levels.push_back(std::move(level));
        }
        else if (tokens[0] == "cost")
        {
            if (tokens.size() != 4)
            {
                error(line, "cost requires target level, resource and amount");
                continue;
            }
            int targetLevel = 0;
            int amount = 0;
            ResourceType resource = ResourceType::Null;
            if (!ParseInt(tokens, 1, targetLevel) || !TryParseResourceType(tokens[2], resource) ||
                !ParseInt(tokens, 3, amount) || targetLevel <= 0 || amount <= 0)
            {
                error(line, "cost contains invalid level, resource or amount");
                continue;
            }
            auto level = std::find_if(current->levels.begin(), current->levels.end(),
                                      [targetLevel](const auto& candidate)
                                      {
                                          return candidate.level == targetLevel;
                                      });
            if (level == current->levels.end())
            {
                error(line, "cost refers to an undefined level");
                continue;
            }
            if (std::any_of(level->upgradeCost.begin(), level->upgradeCost.end(),
                            [resource](const auto& candidate) { return candidate.type == resource; }))
            {
                error(line, "duplicate cost resource for level");
                continue;
            }
            level->upgradeCost.push_back({resource, amount});
        }
        else
        {
            error(line, "unknown route token '" + tokens[0] + "'");
        }
    }

    if (current.has_value())
        AddDiagnostic(result, path, currentLine, "route block is missing 'end'");
    if (!result.diagnostics.empty())
        result.definitions.clear();
    return result;
}

const ProvinceConnectionDefinitionCatalog& GetProvinceConnectionDefinitions()
{
    static const ProvinceConnectionDefinitionLoadResult loaded =
        LoadProvinceConnectionDefinitionsFromFile(DefaultRouteDataPath);
    return loaded.definitions;
}

const ProvinceConnectionDefinition* FindProvinceConnectionDefinition(std::string_view id)
{
    const auto& catalog = GetProvinceConnectionDefinitions();
    const auto it = catalog.find(std::string(id));
    return it == catalog.end() ? nullptr : &it->second;
}

LandRouteConnection::LandRouteConnection(ProvinceConnectionId connectionId,
                                         ProvinceId first, ProvinceId second,
                                         std::string routeDefinitionId,
                                         int routeLengthUnits)
    : id(connectionId),
      firstProvinceId(std::min(first, second)),
      secondProvinceId(std::max(first, second)),
      definitionId(std::move(routeDefinitionId)),
      lengthUnits(routeLengthUnits > 0 ? routeLengthUnits : 1)
{
}

const ProvinceConnectionLevelDefinition* LandRouteConnection::GetLevelDefinition(
    int requestedLevel) const
{
    const auto* definition = FindProvinceConnectionDefinition(definitionId);
    if (definition == nullptr)
        return nullptr;
    const auto it = std::find_if(definition->levels.begin(), definition->levels.end(),
                                 [requestedLevel](const auto& candidate)
                                 {
                                     return candidate.level == requestedLevel;
                                 });
    return it == definition->levels.end() ? nullptr : &*it;
}

const ProvinceConnectionLevelDefinition* LandRouteConnection::GetNextLevelDefinition() const
{
    return GetLevelDefinition(level + 1);
}

int LandRouteConnection::GetMaxLevel() const
{
    const auto* definition = FindProvinceConnectionDefinition(definitionId);
    return definition == nullptr || definition->levels.empty()
        ? -1 : definition->levels.back().level;
}

RouteTraversalStats LandRouteConnection::ResolveTraversalStats(
    const RouteModifierContext& context) const
{
    (void)context;
    const auto* levelDefinition = GetLevelDefinition(level);
    if (levelDefinition == nullptr)
        return {};
    return {levelDefinition->traversalTimeMultiplier,
            levelDefinition->incidentChanceReductionBasisPoints};
}

bool LandRouteConnection::BeginUpgrade(int requestedLevel, std::uint64_t durationTicks)
{
    if (IsUpgradeInProgress() || requestedLevel != level + 1 ||
        GetLevelDefinition(requestedLevel) == nullptr || durationTicks == 0)
        return false;
    upgradeTargetLevel = requestedLevel;
    upgradeRemainingTicks = durationTicks;
    return true;
}

void LandRouteConnection::Update()
{
    if (!IsUpgradeInProgress())
        return;
    if (upgradeRemainingTicks > 0)
        --upgradeRemainingTicks;
    if (upgradeRemainingTicks == 0)
    {
        level = upgradeTargetLevel;
        upgradeTargetLevel = -1;
    }
}

bool LandRouteConnection::SetRuntimeState(int currentLevel, int targetLevel,
                                          std::uint64_t remainingTicks)
{
    if (GetLevelDefinition(currentLevel) == nullptr || currentLevel < 0 ||
        targetLevel < -1 || targetLevel > GetMaxLevel() ||
        (targetLevel >= 0 && targetLevel != currentLevel + 1) ||
        (targetLevel < 0 && remainingTicks != 0) ||
        (targetLevel >= 0 && remainingTicks == 0))
        return false;
    level = currentLevel;
    upgradeTargetLevel = targetLevel;
    upgradeRemainingTicks = remainingTicks;
    return true;
}
