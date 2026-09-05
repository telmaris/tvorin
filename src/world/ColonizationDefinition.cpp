#include "world/ColonizationDefinition.h"

#include "data/RtsDataFile.h"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

namespace
{
    constexpr const char* DefaultPath = "assets/data/colonization.rtsdata";

    void AddDiagnostic(ColonizationDefinitionLoadResult& result,
                       const std::string& path, std::size_t line, std::string message)
    {
        result.diagnostics.push_back({path, line, std::move(message)});
    }

    bool ParseInt(const RtsDataLine& tokens, std::size_t index, int& value)
    {
        return index < tokens.size() && ParseRtsDataInt(tokens[index], value);
    }
}

ColonizationDefinitionLoadResult LoadColonizationDefinitionsFromFile(
    const std::string& path)
{
    ColonizationDefinitionLoadResult result;
    const RtsDataDocument document = ReadRtsDataDocument(path);
    for (const auto& diagnostic : document.diagnostics)
        AddDiagnostic(result, diagnostic.path, diagnostic.line, diagnostic.message);
    if (!document.IsValid())
        return result;

    std::optional<ColonizationDefinition> current;
    std::size_t currentLine = 0;
    bool blockValid = true;
    std::set<std::string> seenFields;
    std::set<ResourceType> seenCosts;
    const auto error = [&](std::size_t line, std::string message)
    {
        AddDiagnostic(result, path, line, std::move(message));
        blockValid = false;
    };

    for (std::size_t index = 0; index < document.lines.size(); ++index)
    {
        const RtsDataLine& tokens = document.lines[index];
        const std::size_t line = document.sourceLines[index];
        if (tokens.empty())
            continue;
        if (tokens[0] == "colonization")
        {
            if (current.has_value())
                error(line, "colonization block started before previous block ended");
            if (tokens.size() != 2 || tokens[1].empty())
            {
                AddDiagnostic(result, path, line, "colonization requires a non-empty ID");
                current.reset();
                blockValid = false;
                continue;
            }
            current = ColonizationDefinition{};
            current->id = tokens[1];
            currentLine = line;
            blockValid = true;
            seenFields.clear();
            seenCosts.clear();
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
            if (!seenFields.contains("duration"))
                error(line, "colonization requires duration");
            if (current->durationTicks == 0)
                error(line, "colonization duration must be positive");
            if (current->cost.empty())
                error(line, "colonization requires at least one cost");
            if (blockValid && !result.definitions.emplace(current->id, *current).second)
                AddDiagnostic(result, path, currentLine,
                              "duplicate colonization definition ID '" + current->id + "'");
            current.reset();
            continue;
        }
        if (tokens[0] == "duration")
        {
            if (tokens.size() != 2 || !seenFields.insert(tokens[0]).second)
            {
                error(line, "duration requires one unique positive integer");
                continue;
            }
            int duration = 0;
            if (!ParseInt(tokens, 1, duration) || duration <= 0)
                error(line, "duration must be positive");
            else
                current->durationTicks = static_cast<std::uint64_t>(duration);
        }
        else if (tokens[0] == "cost")
        {
            if (tokens.size() != 3)
            {
                error(line, "cost requires resource and positive amount");
                continue;
            }
            ResourceType type = ResourceType::Null;
            int amount = 0;
            if (!TryParseResourceType(tokens[1], type) || type == ResourceType::Null ||
                !ParseInt(tokens, 2, amount) || amount <= 0 ||
                !seenCosts.insert(type).second)
                error(line, "cost contains invalid or duplicated data");
            else
                current->cost.push_back({type, amount});
        }
        else
            error(line, "unknown colonization token '" + tokens[0] + "'");
    }

    if (current.has_value())
        AddDiagnostic(result, path, currentLine, "colonization block is missing 'end'");
    if (!result.diagnostics.empty())
        result.definitions.clear();
    return result;
}

const ColonizationDefinitionCatalog& GetColonizationDefinitions()
{
    static const ColonizationDefinitionLoadResult loaded =
        LoadColonizationDefinitionsFromFile(DefaultPath);
    return loaded.definitions;
}

const ColonizationDefinition* FindColonizationDefinition(std::string_view id)
{
    const auto& catalog = GetColonizationDefinitions();
    const auto it = catalog.find(std::string(id));
    return it == catalog.end() ? nullptr : &it->second;
}
