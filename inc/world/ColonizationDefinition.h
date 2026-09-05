#ifndef WORLD_COLONIZATION_DEFINITION_H
#define WORLD_COLONIZATION_DEFINITION_H

#include "data/Resource.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct ColonizationCost
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

struct ColonizationDefinition
{
    std::string id;
    std::uint64_t durationTicks{0};
    std::vector<ColonizationCost> cost;
};

using ColonizationDefinitionCatalog = std::map<std::string, ColonizationDefinition>;

struct ColonizationDefinitionDiagnostic
{
    std::string path;
    std::size_t line{0};
    std::string message;
};

struct ColonizationDefinitionLoadResult
{
    ColonizationDefinitionCatalog definitions;
    std::vector<ColonizationDefinitionDiagnostic> diagnostics;

    bool IsValid() const { return diagnostics.empty() && !definitions.empty(); }
};

ColonizationDefinitionLoadResult LoadColonizationDefinitionsFromFile(
    const std::string& path);
const ColonizationDefinitionCatalog& GetColonizationDefinitions();
const ColonizationDefinition* FindColonizationDefinition(std::string_view id);

#endif
