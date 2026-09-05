#include "data/RtsDataFile.h"
#include "world/ProvinceConnection.h"
#include "world/ProvinceDefinition.h"
#include "world/WorldEventDefinition.h"
#include "warfare/BattleSimulator.h"

#include <array>
#include <iostream>
#include <string>

#ifndef RTS_ASSETS_DIR
#define RTS_ASSETS_DIR "assets"
#endif

int main()
{
    const std::array<std::string, 10> files = {
        std::string(RTS_ASSETS_DIR) + "/data/buildings.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/focuses.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/technologies.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/textures.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/units.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/expeditions.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/provinces.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/routes.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/events.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/battle_rules.rtsdata"};

    bool valid = true;
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
