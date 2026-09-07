#include "simulation/MapGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
    // Smoothstep easing for noise interpolation.
    float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

    // Deterministic value-noise field in [0,1], one sample per tile. A coarse
    // random lattice (controlled by 'scale') is bilinearly interpolated to full
    // resolution — lower scale yields larger, smoother regions.
    std::vector<float> MakeNoiseField(int w, int h, float scale, std::mt19937& rng)
    {
        scale = std::clamp(scale, 0.005f, 0.5f);
        int gw = std::max(2, static_cast<int>(std::ceil(w * scale)) + 2);
        int gh = std::max(2, static_cast<int>(std::ceil(h * scale)) + 2);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<float> lattice(static_cast<size_t>(gw) * gh);
        for (auto& v : lattice)
            v = dist(rng);

        std::vector<float> field(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float gx = x * scale;
                float gy = y * scale;
                int x0 = static_cast<int>(gx);
                int y0 = static_cast<int>(gy);
                int x1 = std::min(x0 + 1, gw - 1);
                int y1 = std::min(y0 + 1, gh - 1);
                float tx = Smooth(gx - x0);
                float ty = Smooth(gy - y0);
                float a = lattice[x0 + y0 * gw];
                float b = lattice[x1 + y0 * gw];
                float c = lattice[x0 + y1 * gw];
                float d = lattice[x1 + y1 * gw];
                float top = a + (b - a) * tx;
                float bot = c + (d - c) * tx;
                field[x + y * w] = top + (bot - top) * ty;
            }
        }
        return field;
    }

    BiomeType ClassifyBiome(float elevation, float moisture, const BiomeParameters& p)
    {
        if (elevation >= p.mountainElevation)
            return BiomeType::MOUNTAINS;
        if (elevation >= p.hillElevation)
            return BiomeType::HILLS;
        // Lowland: split by moisture.
        if (moisture <= p.desertMoisture)
            return BiomeType::DESERT;
        if (moisture >= p.wetlandMoisture)
            return BiomeType::WETLAND;
        if (moisture >= p.forestMoisture)
            return BiomeType::FOREST;
        return BiomeType::PLAINS;
    }
}

void MapGenerator::GenerateTileMap(TileMap& tilemap, MapParameters& params)
{
    int presetSize = SizeFromPreset(params.sizePreset);
    // Callers that select a non-default preset often leave the historical S
    // dimensions untouched. Treat that pair as the implicit default so the
    // preset remains authoritative without taking away support for explicit
    // custom dimensions used by tests and tools.
    const bool implicitDefaultSize = params.sizeX == SizeFromPreset(MapSizePreset::S) &&
                                     params.sizeY == SizeFromPreset(MapSizePreset::S) &&
                                     params.sizePreset != MapSizePreset::S;
    if (params.sizeX <= 0 || params.sizeY <= 0 || implicitDefaultSize)
    {
        params.sizeX = presetSize;
        params.sizeY = presetSize;
    }
    if (params.sizeX % 2 == 0) params.sizeX++;
    if (params.sizeY % 2 == 0) params.sizeY++;

    int size = params.sizeX*params.sizeY;
    tilemap.tilemap.clear();
    tilemap.tilemap.reserve(size);
    tilemap.params = params;
    tilemap.terrainDirty = true;
    tilemap.buildingsDirty = true;
    std::mt19937 rng(params.seed);

    for(int i = 0; i < size; i++)
    {
        tilemap.tilemap.emplace_back(i);
        tilemap.tilemap.back().terrainTextureId = tilemap.PickTerrainTexture(tilemap.tilemap.back().tileType, rng);
    }

    GenerateBiomes(tilemap, params, rng);
    GenerateResourcePatches(tilemap, params, rng);
}

// Assigns a biome to every tile from elevation + moisture noise. Biomes gate where
// resource patches can spawn so the map stays geographically coherent.
void MapGenerator::GenerateBiomes(TileMap& tilemap, const MapParameters& params, std::mt19937& rng)
{
    int w = params.sizeX;
    int h = params.sizeY;
    const BiomeParameters& bp = params.biome;

    // Two octaves of elevation for more organic mountain/coast shapes; one moisture band.
    std::vector<float> elevLow  = MakeNoiseField(w, h, bp.noiseScale, rng);
    std::vector<float> elevHigh = MakeNoiseField(w, h, bp.noiseScale * 2.3f, rng);
    std::vector<float> moisture = MakeNoiseField(w, h, bp.noiseScale * 1.4f, rng);

    for (int i = 0; i < w * h; i++)
    {
        float elevation = elevLow[i] * 0.65f + elevHigh[i] * 0.35f;
        tilemap.tilemap[i].biome = ClassifyBiome(elevation, moisture[i], bp);
    }
}

int MapGenerator::SizeFromPreset(MapSizePreset preset)
{
    switch (preset)
    {
        case MapSizePreset::S: return 201;
        case MapSizePreset::M: return 301;
        case MapSizePreset::L: return 401;
        case MapSizePreset::XL: return 501;
        default: return 201;
    }
}

namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kPi = 3.141592653589793;

    Vec2i ClampAnchorToMap(Vec2i anchor, Vec2i footprint, const MapParameters& params)
    {
        return Vec2i{
            std::clamp(anchor.x, 1, std::max(1, params.sizeX - footprint.x - 2)),
            std::clamp(anchor.y, 1, std::max(1, params.sizeY - footprint.y - 2))};
    }

    int AnchorManhattanDistance(Vec2i a, Vec2i b)
    {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    }

    // Builds one candidate n-gon layout (jittered if jitter>0.0, exact regular
    // polygon if jitter==0.0) for `playerCount` HQs around `mapCenter`, using
    // `rng` for the per-vertex jitter draws.
    std::vector<Vec2i> BuildRingLayout(Vec2i mapCenter, Vec2i footprint, const MapParameters& params,
                                        int playerCount, double radius, double rotation, double jitter,
                                        std::mt19937& rng)
    {
        std::vector<Vec2i> anchors;
        anchors.reserve(playerCount);
        double sector = kTwoPi / playerCount;
        std::uniform_real_distribution<double> angleJitterDist(-jitter * 0.25 * sector, jitter * 0.25 * sector);
        std::uniform_real_distribution<double> radiusJitterDist(1.0 - jitter * 0.15, 1.0);

        for (int i = 0; i < playerCount; i++)
        {
            double angle = rotation + i * sector + angleJitterDist(rng);
            double r = radius * radiusJitterDist(rng);
            Vec2i center{
                static_cast<int>(std::lround(mapCenter.x + r * std::cos(angle))),
                static_cast<int>(std::lround(mapCenter.y + r * std::sin(angle)))};
            Vec2i anchor{center.x - footprint.x / 2, center.y - footprint.y / 2};
            anchors.push_back(ClampAnchorToMap(anchor, footprint, params));
        }
        return anchors;
    }

    bool AllPairsClearMinDistance(const std::vector<Vec2i>& anchors, int minSafeDistance)
    {
        for (size_t i = 0; i < anchors.size(); i++)
            for (size_t j = i + 1; j < anchors.size(); j++)
                if (AnchorManhattanDistance(anchors[i], anchors[j]) < minSafeDistance)
                    return false;
        return true;
    }
}

// Deterministically places `playerCount` HQ anchors on an n-gon. The radius makes adjacent
// ring vertices clear minSafeDistance by construction (chord = 2R sin(pi/n),
// the smallest chord in a regular polygon — every non-adjacent pair is
// therefore automatically farther apart), then a handful of deterministic
// jittered layouts are tried for a less mechanically regular look; if none
// of them happen to clear the safety margin (jitter got unlucky), the exact
// zero-jitter regular polygon is used as a guaranteed-safe fallback.
std::vector<Vec2i> MapGenerator::PickHeadquartersAnchors(const MapParameters& params, int playerCount)
{
    Vec2i footprint = HeadquartersFootprint();
    if (playerCount <= 0)
        return {};
    if (playerCount == 1)
        return {Vec2i{params.sizeX / 2 - footprint.x / 2, params.sizeY / 2 - footprint.y / 2}};

    Vec2i mapCenter{params.sizeX / 2, params.sizeY / 2};
    int margin = std::max(14, HeadquartersTerritorySize() / 2 + 4);
    double maxRadius = std::max(20.0, static_cast<double>(std::min(params.sizeX, params.sizeY)) / 2.0 - margin);

    double minSafeDistance = std::max(70.0, static_cast<double>(std::min(params.sizeX, params.sizeY)) / 3.0);
    double nominalRadius = minSafeDistance / (2.0 * std::sin(kPi / playerCount));
    double radius = std::clamp(nominalRadius, 20.0, maxRadius);

    std::mt19937 rng(params.seed ^ 0x51ED270Bu);
    std::uniform_real_distribution<double> rotationDist(0.0, kTwoPi);
    double rotation = rotationDist(rng);

    constexpr int kJitteredAttempts = 16;
    for (int attempt = 0; attempt < kJitteredAttempts; attempt++)
    {
        std::vector<Vec2i> candidate = BuildRingLayout(mapCenter, footprint, params, playerCount, radius, rotation, 1.0, rng);
        if (AllPairsClearMinDistance(candidate, static_cast<int>(minSafeDistance)))
            return candidate;
    }

    // Fallback: exact regular polygon, no jitter — guaranteed to clear
    // minSafeDistance by the chord-length argument above (modulo integer
    // rounding, comfortably covered by clamping radius conservatively).
    // jitter=0.0 collapses both distributions to a fixed point, so the RNG
    // state fed in here doesn't actually affect the result.
    std::mt19937 fallbackRng(0);
    return BuildRingLayout(mapCenter, footprint, params, playerCount, radius, rotation, 0.0, fallbackRng);
}

void MapGenerator::GenerateResourcePatches(TileMap& tilemap, const MapParameters& params, std::mt19937& rng)
{
    constexpr double referenceMapArea = 401.0 * 401.0;
    const double mapArea = static_cast<double>(std::max(1, params.sizeX)) *
                           static_cast<double>(std::max(1, params.sizeY));
    const double areaScale = mapArea / referenceMapArea;
    const float densityScale = std::clamp(params.resourceDensity, 0.0f, 1.0f);
    float sizeScale = 0.65f + std::clamp(params.resourceFieldSize, 0.0f, 1.0f) * 1.35f;
    for (auto patch : params.resourcePatches)
    {
        patch.patchCount = std::max(0, static_cast<int>(std::round(
            patch.patchCount * areaScale * densityScale)));
        if (patch.patchCount == 0)
            continue;
        patch.minRadius = std::max(1, static_cast<int>(std::round(patch.minRadius * sizeScale)));
        patch.maxRadius = std::max(patch.minRadius, static_cast<int>(std::round(patch.maxRadius * sizeScale)));
        GeneratePatch(tilemap, patch, rng);
    }
    RefreshResourceOverlayEdges(tilemap, rng);
}

ResourceType MapGenerator::ResourceTypeFromTileType(TileType type)
{
    switch (type)
    {
        case TileType::WOOD: return ResourceType::WOOD;
        case TileType::COAL: return ResourceType::COAL;
        case TileType::IRON_ORE: return ResourceType::IRON_ORE;
        case TileType::STONE: return ResourceType::STONE;
        case TileType::COPPER_ORE: return ResourceType::COPPER_ORE;
        case TileType::SAND: return ResourceType::SAND;
        case TileType::CLAY: return ResourceType::CLAY;
        default: return ResourceType::Null;
    }
}

void MapGenerator::FilterResourcePatchesForProfile(
    MapParameters& params, const std::vector<ResourceType>& naturalResourceTypes)
{
    std::vector<ResourceType> sorted = naturalResourceTypes;
    std::sort(sorted.begin(), sorted.end(),
              [](ResourceType left, ResourceType right)
              {
                  return static_cast<int>(left) < static_cast<int>(right);
              });
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    params.resourcePatches.erase(
        std::remove_if(params.resourcePatches.begin(), params.resourcePatches.end(),
                       [&sorted](const ResourcePatchParameters& patch)
                       {
                           const ResourceType resource = ResourceTypeFromTileType(patch.type);
                           return resource == ResourceType::Null ||
                               !std::binary_search(sorted.begin(), sorted.end(), resource,
                                   [](ResourceType left, ResourceType right)
                                   {
                                       return static_cast<int>(left) < static_cast<int>(right);
                                   });
                       }),
        params.resourcePatches.end());
}

void MapGenerator::ApplyResourceProfile(
    MapParameters& params, const std::vector<ResourceProfileDeposit>& deposits)
{
    std::map<ResourceType, float> richnessByResource;
    for (const auto& deposit : deposits)
    {
        if (deposit.resource == ResourceType::Null ||
            !std::isfinite(deposit.richnessScale) || deposit.richnessScale <= 0.0f)
            continue;
        richnessByResource[deposit.resource] = std::clamp(
            deposit.richnessScale, 0.10f, 4.0f);
    }

    std::vector<ResourceType> resources;
    resources.reserve(richnessByResource.size());
    for (const auto& [resource, scale] : richnessByResource)
    {
        (void)scale;
        resources.push_back(resource);
    }
    FilterResourcePatchesForProfile(params, resources);
    for (auto& patch : params.resourcePatches)
    {
        const ResourceType resource = ResourceTypeFromTileType(patch.type);
        const auto it = richnessByResource.find(resource);
        if (it != richnessByResource.end())
            patch.richnessScale = std::clamp(patch.richnessScale * it->second,
                                             0.10f, 4.0f);
    }
}

// Produces rounded resource deposits as ellipses with randomized
// eccentricity and rotation, plus a smooth low-frequency boundary wobble.
//
// The biome constraint applies only to the patch center so noisy biome borders
// do not clip the oval.
// Small patches additionally scale the wobble down: on a radius-2 deposit a
// full-amplitude wobble is just integer-rounding teeth, not an organic edge.
void MapGenerator::GeneratePatch(TileMap& tilemap, const ResourcePatchParameters& patch, std::mt19937& rng)
{
    if (patch.patchCount <= 0 || patch.maxRadius <= 0)
        return;

    std::uniform_int_distribution<int> radiusDist(std::max(1, patch.minRadius), std::max(patch.minRadius, patch.maxRadius));
    std::uniform_int_distribution<int> xDist(0, tilemap.params.sizeX - 1);
    std::uniform_int_distribution<int> yDist(0, tilemap.params.sizeY - 1);
    std::uniform_real_distribution<double> axisScaleDist(0.9, 1.15);
    std::uniform_real_distribution<double> eccentricityDist(0.68, 1.0); // minor/major axis ratio
    std::uniform_real_distribution<double> rotationDist(0.0, kTwoPi);
    std::uniform_real_distribution<double> phaseDist(0.0, kTwoPi);
    constexpr double kWobbleAmplitude = 0.16;
    constexpr int kWobbleLobesA = 3;
    constexpr int kWobbleLobesB = 5;
    constexpr int kCenterDrawAttempts = 16;

    auto biomeAllowed = [&](Vec2i pos)
    {
        if (patch.allowedBiomes.empty())
            return true;
        const Tile& tile = tilemap[pos];
        return std::find(patch.allowedBiomes.begin(), patch.allowedBiomes.end(), tile.biome) != patch.allowedBiomes.end();
    };

    for (int patchIndex = 0; patchIndex < patch.patchCount; patchIndex++)
    {
        int radius = radiusDist(rng);
        int diameter = radius * 2 + 1;

        double majorAxis = std::max(1.0, radius * axisScaleDist(rng));
        double minorAxis = majorAxis * eccentricityDist(rng);
        double rotation = rotationDist(rng);
        double cosR = std::cos(-rotation);
        double sinR = std::sin(-rotation);
        double phaseA = phaseDist(rng);
        double phaseB = phaseDist(rng);
        double wobbleScale = std::min(1.0, radius / 5.0);
        double wobbleA = kWobbleAmplitude * wobbleScale;
        double wobbleB = kWobbleAmplitude * 0.6 * wobbleScale;

        // Deterministic center hunt: keep drawing until the CENTER lands in
        // an allowed biome; give up on this patch after a bounded number of
        // attempts (map may simply lack that biome for this seed).
        Vec2i center{-1, -1};
        for (int attempt = 0; attempt < kCenterDrawAttempts; attempt++)
        {
            Vec2i candidate{xDist(rng), yDist(rng)};
            if (biomeAllowed(candidate))
            {
                center = candidate;
                break;
            }
        }
        if (center.x < 0)
            continue;

        for (int y = 0; y < diameter; y++)
        {
            for (int x = 0; x < diameter; x++)
            {
                double dx = x - radius;
                double dy = y - radius;
                double distFromCenter = std::sqrt(dx * dx + dy * dy);
                double patchBoundary = 1.0;
                double normalizedDistance = 0.0;

                if (dx != 0.0 || dy != 0.0)
                {
                    double rx = dx * cosR - dy * sinR;
                    double ry = dx * sinR + dy * cosR;
                    normalizedDistance = std::sqrt((rx * rx) / (majorAxis * majorAxis) + (ry * ry) / (minorAxis * minorAxis));
                    double angle = std::atan2(ry, rx);
                    double wobble = 1.0 + wobbleA * std::sin(kWobbleLobesA * angle + phaseA)
                                        + wobbleB * std::sin(kWobbleLobesB * angle + phaseB);
                    patchBoundary = wobble;
                    if (normalizedDistance > patchBoundary)
                        continue;
                }

                Vec2i mapPos{center.x + static_cast<int>(dx), center.y + static_cast<int>(dy)};
                if (!tilemap.IsInside(mapPos))
                    continue;

                auto& tile = tilemap[mapPos];
                // Gentle richness gradient — richest at the deposit's core,
                // tapering toward its (already gently undulating) edge.
                double edgeFalloff = std::clamp(1.0 - 0.3 * (distFromCenter / (majorAxis + 1.0)), 0.7, 1.0);
                tile.tileType = patch.type;
                const bool usesOverlay = tilemap.HasResourceOverlay(patch.type);
                tile.terrainTextureId = tilemap.PickTerrainTexture(
                    usesOverlay ? TileType::GRASS : patch.type, rng);
                tile.resourceOverlayTextureId = usesOverlay
                    ? tilemap.PickResourceOverlayTexture(patch.type, ResourceOverlayEdgeDirection::None, rng)
                    : -1;
                tile.resourceRichness = std::max(1, static_cast<int>(std::round(tilemap.params.resourceRichness * patch.richnessScale * edgeFalloff)));
            }
        }
    }
}

void MapGenerator::RefreshResourceOverlayEdges(TileMap& tilemap, std::mt19937& rng)
{
    const std::array<std::pair<Vec2i, ResourceOverlayEdgeDirection>, 4> neighbors = {{
        {Vec2i{0, -1}, ResourceOverlayEdgeDirection::North},
        {Vec2i{1, 0}, ResourceOverlayEdgeDirection::East},
        {Vec2i{0, 1}, ResourceOverlayEdgeDirection::South},
        {Vec2i{-1, 0}, ResourceOverlayEdgeDirection::West}
    }};

    for (int y = 0; y < tilemap.params.sizeY; ++y)
    {
        for (int x = 0; x < tilemap.params.sizeX; ++x)
        {
            const Vec2i pos{x, y};
            auto& tile = tilemap[pos];
            if (!tilemap.HasResourceOverlay(tile.tileType) || tile.resourceRichness <= 0)
                continue;

            std::vector<ResourceOverlayEdgeDirection> exposedSides;
            for (const auto& [offset, direction] : neighbors)
            {
                const Vec2i adjacent{pos.x + offset.x, pos.y + offset.y};
                if (!tilemap.IsInside(adjacent) || tilemap[adjacent].tileType != tile.tileType
                    || tilemap[adjacent].resourceRichness <= 0)
                {
                    exposedSides.push_back(direction);
                }
            }

            // A diagonal/corner has two exposed sides.  With the current
            // cardinal atlas, alternate between them instead of forcing every
            // corner into the same blocky diagonal treatment.
            ResourceOverlayEdgeDirection edge = ResourceOverlayEdgeDirection::None;
            if (!exposedSides.empty())
            {
                std::uniform_int_distribution<size_t> sideRoll(0, exposedSides.size() - 1);
                edge = exposedSides[sideRoll(rng)];
            }
            // Forest interiors are a contiguous 2x2 canopy pattern, rather
            // than independently rolled 64px sprites.  This prevents the
            // pronounced square grid that random dense forest variants create.
            if (tile.tileType == TileType::WOOD && edge == ResourceOverlayEdgeDirection::None)
            {
                const int canopyColumn = x & 1;
                const int canopyRow = y & 1;
                tile.resourceOverlayTextureId = 48 + canopyRow * 2 + canopyColumn;
            }
            else
            {
                tile.resourceOverlayTextureId = tilemap.PickResourceOverlayTexture(tile.tileType, edge, rng);
            }
        }
    }
}

void MapGenerator::PrepareStartingArea(TileMap& tilemap, Vec2i hqAnchor, std::mt19937& rng)
{
    Vec2i hqFootprint = HeadquartersFootprint();
    int territorySize = HeadquartersTerritorySize();
    Vec2i center{hqAnchor.x + hqFootprint.x / 2, hqAnchor.y + hqFootprint.y / 2};
    int half = territorySize / 2;

    for (int y = -half; y <= half; y++)
    {
        for (int x = -half; x <= half; x++)
        {
            Vec2i pos{center.x + x, center.y + y};
            if (!tilemap.IsInside(pos))
                continue;

            auto& tile = tilemap[pos];
            tile.tileType = TileType::GRASS;
            tile.biome = BiomeType::PLAINS;
            tile.terrainTextureId = tilemap.PickTerrainTexture(TileType::GRASS, rng);
            tile.resourceOverlayTextureId = -1;
            tile.resourceRichness = 0;
        }
    }

    tilemap.terrainDirty = true;
}
