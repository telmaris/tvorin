#include "data/RtsDataFile.h"
#include "data/TextureConfig.h"
#include "world/ColonizationDefinition.h"
#include "world/ProvinceConnection.h"
#include "world/ProvinceDefinition.h"
#include "world/WorldEventDefinition.h"
#include "warfare/BattleSimulator.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

#ifndef RTS_ASSETS_DIR
#define RTS_ASSETS_DIR "assets"
#endif

int main()
{
    const std::array<std::string, 11> files = {
        std::string(RTS_ASSETS_DIR) + "/data/buildings.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/focuses.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/technologies.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/textures.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/units.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/expeditions.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/provinces.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/routes.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/events.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/battle_rules.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/colonization.rtsdata"};

    bool valid = true;
    std::set<std::string> checkedAssetPaths;
    const std::filesystem::path repositoryRoot =
        std::filesystem::path(RTS_ASSETS_DIR).parent_path();
    for (const std::string& path : files)
    {
        const RtsDataDocument document = ReadRtsDataDocument(path);
        for (const RtsDataDiagnostic& diagnostic : document.diagnostics)
        {
            valid = false;
            std::cerr << path;
            if (diagnostic.line != 0)
                std::cerr << ':' << diagnostic.line;
            std::cerr << ": " << diagnostic.message << '\n';
        }

        for (std::size_t lineIndex = 0; lineIndex < document.lines.size(); ++lineIndex)
        {
            for (const std::string& token : document.lines[lineIndex])
            {
                if (!token.starts_with("assets/") || !checkedAssetPaths.insert(token).second)
                    continue;
                std::error_code error;
                if (!std::filesystem::is_regular_file(repositoryRoot / token, error))
                {
                    valid = false;
                    std::cerr << path << ':' << document.sourceLines[lineIndex]
                              << ": referenced asset does not exist: " << token << '\n';
                }
            }
        }
    }

    std::string textureError;
    const TextureConfig textures = LoadTextureConfig(
        std::string(RTS_ASSETS_DIR) + "/data/textures.rtsdata", &textureError);
    if (textures.IsEmpty() || !textureError.empty())
    {
        valid = false;
        std::cerr << "textures.rtsdata: "
                  << (textureError.empty() ? "texture catalog is empty" : textureError) << '\n';
    }

    const ColonizationDefinitionLoadResult colonization =
        LoadColonizationDefinitionsFromFile(
            std::string(RTS_ASSETS_DIR) + "/data/colonization.rtsdata");
    for (const ColonizationDefinitionDiagnostic& diagnostic : colonization.diagnostics)
    {
        valid = false;
        std::cerr << diagnostic.path;
        if (diagnostic.line != 0)
            std::cerr << ':' << diagnostic.line;
        std::cerr << ": " << diagnostic.message << '\n';
    }

    const ProvinceDefinitionCatalogLoadResult provinces =
        LoadProvinceDefinitionCatalog(std::string(RTS_ASSETS_DIR) + "/data/provinces.rtsdata");
    for (const ProvinceDefinitionDiagnostic& diagnostic : provinces.diagnostics)
    {
        valid = false;
        std::cerr << diagnostic.path;
        if (diagnostic.line != 0)
            std::cerr << ':' << diagnostic.line;
        std::cerr << ": " << diagnostic.message << '\n';
    }

    const ProvinceConnectionDefinitionLoadResult routes =
        LoadProvinceConnectionDefinitionsFromFile(std::string(RTS_ASSETS_DIR) + "/data/routes.rtsdata");
    for (const ProvinceConnectionDefinitionDiagnostic& diagnostic : routes.diagnostics)
    {
        valid = false;
        std::cerr << diagnostic.path;
        if (diagnostic.line != 0)
            std::cerr << ':' << diagnostic.line;
        std::cerr << ": " << diagnostic.message << '\n';
    }

    const WorldEventDefinitionLoadResult events =
        LoadWorldEventDefinitionsFromFile(std::string(RTS_ASSETS_DIR) + "/data/events.rtsdata");
    for (const WorldEventDefinitionDiagnostic& diagnostic : events.diagnostics)
    {
        valid = false;
        std::cerr << diagnostic.path;
        if (diagnostic.line != 0)
            std::cerr << ':' << diagnostic.line;
        std::cerr << ": " << diagnostic.message << '\n';
    }

    const BattleRulesLoadResult battleRules =
        LoadBattleRulesFromFile(std::string(RTS_ASSETS_DIR) + "/data/battle_rules.rtsdata");
    for (const BattleRulesDiagnostic& diagnostic : battleRules.diagnostics)
    {
        valid = false;
        std::cerr << diagnostic.path;
        if (diagnostic.line != 0)
            std::cerr << ':' << diagnostic.line;
        std::cerr << ": " << diagnostic.message << '\n';
    }

    return valid ? 0 : 1;
}
