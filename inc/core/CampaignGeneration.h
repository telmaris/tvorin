#ifndef CAMPAIGN_GENERATION_H
#define CAMPAIGN_GENERATION_H

#include "simulation/MapGenerator.h"
#include "world/GlobalMap.h"

#include <utility>

// Complete deterministic input for a new campaign. MapParameters describes
// one local tilemap; globalMap describes the finite province graph around it.
// Keeping the two domains separate prevents global sliders from accidentally
// changing local terrain or the historical MapParameters wire layout.
struct CampaignGenerationParameters
{
    MapParameters localMap{};
    GlobalMapGenerationParameters globalMap{};

    CampaignGenerationParameters() = default;
    // Source compatibility for old callers that only supplied local settings.
    CampaignGenerationParameters(MapParameters legacyLocalMap)
        : localMap(std::move(legacyLocalMap))
    {
        globalMap.seed = localMap.seed;
    }
};

inline CampaignGenerationParameters MakeCampaignGenerationParameters(
    const MapParameters& localMap)
{
    return CampaignGenerationParameters{localMap};
}

#endif
