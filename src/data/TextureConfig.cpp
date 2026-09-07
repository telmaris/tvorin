#include "data/TextureConfig.h"
#include "platform/AtomicFile.h"

#include "data/RtsDataFile.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace
{
    // Values are written as "key value" pairs after the command word, so a line
    // reads like prose ("variant atlas 0 texture 9 weight 1") and gaining a new
    // field never shifts the meaning of the existing ones.
    bool FindValue(const RtsDataLine& tokens, const std::string& key, std::string& out)
    {
        for (size_t i = 0; i + 1 < tokens.size(); i++)
        {
            if (tokens[i] == key)
            {
                out = tokens[i + 1];
                return true;
            }
        }
        return false;
    }

    int IntValue(const RtsDataLine& tokens, const std::string& key, int fallback)
    {
        std::string raw;
        if (!FindValue(tokens, key, raw))
            return fallback;
        try
        {
            return RtsDataIntOr(raw, fallback);
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    double DoubleValue(const RtsDataLine& tokens, const std::string& key, double fallback)
    {
        std::string raw;
        if (!FindValue(tokens, key, raw))
            return fallback;
        try
        {
            return RtsDataDoubleOr(raw, fallback);
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    bool HasFlag(const RtsDataLine& tokens, const std::string& flag)
    {
        return std::find(tokens.begin(), tokens.end(), flag) != tokens.end();
    }

    TextureRef ParseRef(const RtsDataLine& tokens)
    {
        TextureRef ref;
        ref.atlasId = IntValue(tokens, "atlas", -1);
        ref.textureId = IntValue(tokens, "texture", 0);
        return ref;
    }

    std::string FormatDouble(double value)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", value);
        return buffer;
    }
}

bool TextureAnimationDefinition::operator==(const TextureAnimationDefinition& other) const
{
    if (enabled != other.enabled)
        return false;
    if (!enabled)
        return true;  // Disabled clips carry no meaning; don't compare stale numbers.

    return frames == other.frames && looping == other.looping &&
           std::abs(frameTime - other.frameTime) < 1e-9;
}

const TextureAtlasDefinition* TextureConfig::FindAtlas(int id) const
{
    auto it = std::find_if(atlases.begin(), atlases.end(),
                           [id](const TextureAtlasDefinition& atlas) { return atlas.id == id; });
    return it != atlases.end() ? &*it : nullptr;
}

const TextureAtlasDefinition* TextureConfig::FindAtlasByPath(const std::string& path) const
{
    auto it = std::find_if(atlases.begin(), atlases.end(),
                           [&path](const TextureAtlasDefinition& atlas) { return atlas.path == path; });
    return it != atlases.end() ? &*it : nullptr;
}

bool TextureConfig::IsEmpty() const
{
    return atlases.empty() && terrain.empty() && resourceOverlays.empty() && buildings.empty() && resources.empty();
}

TextureConfig LoadTextureConfig(const std::string& path, std::string* outError)
{
    if (outError != nullptr)
        outError->clear();

    TextureConfig config;

    std::ifstream probe(path);
    if (!probe.is_open())
    {
        if (outError != nullptr)
            *outError = "file not found: " + path;
        return config;
    }
    probe.close();

    RtsDataLines lines = ReadRtsDataLines(path);

    enum class Block { None, Terrain, ResourceOverlay, Building, Resource };
    Block block = Block::None;

    TerrainTextureDefinition terrain;
    ResourceOverlayTextureDefinition resourceOverlay;
    BuildingTextureDefinition building;
    ResourceTextureDefinition resource;

    for (const auto& tokens : lines)
    {
        const std::string& command = tokens[0];

        if (command == "end")
        {
            switch (block)
            {
                case Block::Terrain: config.terrain.push_back(terrain); break;
                case Block::ResourceOverlay: config.resourceOverlays.push_back(resourceOverlay); break;
                case Block::Building: config.buildings.push_back(building); break;
                case Block::Resource: config.resources.push_back(resource); break;
                case Block::None: break;
            }
            block = Block::None;
            continue;
        }

        if (block == Block::None)
        {
            if (command == "atlas" && tokens.size() >= 6)
            {
                // atlas <id> <name> "<path>" <cellWidth> <cellHeight>
                TextureAtlasDefinition atlas;
                try
                {
                    atlas.id = RtsDataIntOr(tokens[1]);
                    atlas.name = tokens[2];
                    atlas.path = tokens[3];
                    atlas.cellWidth = RtsDataIntOr(tokens[4]);
                    atlas.cellHeight = RtsDataIntOr(tokens[5]);
                }
                catch (const std::exception&)
                {
                    continue;
                }
                config.atlases.push_back(atlas);
            }
            else if (command == "terrain" && tokens.size() >= 2)
            {
                terrain = TerrainTextureDefinition{};
                terrain.tileType = tokens[1];
                block = Block::Terrain;
            }
            else if (command == "building" && tokens.size() >= 2)
            {
                building = BuildingTextureDefinition{};
                building.buildingType = tokens[1];
                block = Block::Building;
            }
            else if (command == "resource_overlay" && tokens.size() >= 2)
            {
                resourceOverlay = ResourceOverlayTextureDefinition{};
                resourceOverlay.tileType = tokens[1];
                block = Block::ResourceOverlay;
            }
            else if (command == "resource" && tokens.size() >= 2)
            {
                resource = ResourceTextureDefinition{};
                resource.resourceType = tokens[1];
                block = Block::Resource;
            }
            continue;
        }

        if (block == Block::Terrain && command == "variant")
        {
            TerrainVariantDefinition variant;
            variant.texture = ParseRef(tokens);
            variant.weight = IntValue(tokens, "weight", 1);
            terrain.variants.push_back(variant);
        }
        else if (block == Block::ResourceOverlay && command == "variant")
        {
            ResourceOverlayVariantDefinition variant;
            variant.texture = ParseRef(tokens);
            variant.weight = IntValue(tokens, "weight", 1);
            variant.edge = HasFlag(tokens, "edge");
            resourceOverlay.variants.push_back(variant);
        }
        else if (block == Block::Building && command == "sprite")
        {
            building.sprite = ParseRef(tokens);
        }
        else if (block == Block::Building && command == "animation")
        {
            building.animation.enabled = true;
            building.animation.frames = IntValue(tokens, "frames", 1);
            building.animation.frameTime = DoubleValue(tokens, "frame_time", 0.12);
            building.animation.looping = HasFlag(tokens, "loop");
        }
        else if (block == Block::Resource && command == "icon")
        {
            resource.icon = ParseRef(tokens);
        }
    }

    if (config.IsEmpty() && outError != nullptr)
        *outError = "no definitions parsed from " + path;

    return config;
}

std::string FormatTextureConfig(const TextureConfig& config)
{
    std::ostringstream out;

    out << "# Texture bindings for every drawable slot in the game.\n";
    out << "# Addressing: cell <texture> of <atlas>.\n";
    out << "# An atlas is (image, cell size) - a standalone sprite is a one-cell atlas.\n\n";

    out << "# atlas <id> <name> \"<path>\" <cellWidth> <cellHeight>\n";
    std::vector<TextureAtlasDefinition> atlases = config.atlases;
    std::sort(atlases.begin(), atlases.end(),
              [](const TextureAtlasDefinition& a, const TextureAtlasDefinition& b) { return a.id < b.id; });
    for (const auto& atlas : atlases)
    {
        out << "atlas " << atlas.id << ' ' << (atlas.name.empty() ? "unnamed" : atlas.name)
            << " \"" << atlas.path << "\" " << atlas.cellWidth << ' ' << atlas.cellHeight << '\n';
    }
    out << '\n';

    for (const auto& terrain : config.terrain)
    {
        out << "terrain " << terrain.tileType << '\n';
        for (const auto& variant : terrain.variants)
        {
            out << "    variant atlas " << variant.texture.atlasId
                << " texture " << variant.texture.textureId
                << " weight " << variant.weight << '\n';
        }
        out << "end\n\n";
    }

    for (const auto& overlay : config.resourceOverlays)
    {
        out << "resource_overlay " << overlay.tileType << '\n';
        for (const auto& variant : overlay.variants)
        {
            out << "    variant atlas " << variant.texture.atlasId
                << " texture " << variant.texture.textureId
                << " weight " << variant.weight;
            if (variant.edge)
                out << " edge";
            out << '\n';
        }
        out << "end\n\n";
    }

    for (const auto& building : config.buildings)
    {
        out << "building " << building.buildingType << '\n';
        out << "    sprite atlas " << building.sprite.atlasId
            << " texture " << building.sprite.textureId << '\n';
        if (building.animation.enabled)
        {
            out << "    animation frames " << building.animation.frames
                << " frame_time " << FormatDouble(building.animation.frameTime);
            if (building.animation.looping)
                out << " loop";
            out << '\n';
        }
        out << "end\n\n";
    }

    for (const auto& resource : config.resources)
    {
        out << "resource " << resource.resourceType << '\n';
        out << "    icon atlas " << resource.icon.atlasId
            << " texture " << resource.icon.textureId << '\n';
        out << "end\n\n";
    }

    return out.str();
}

bool SaveTextureConfig(const std::string& path, const TextureConfig& config, std::string* outError)
{
    if (outError != nullptr)
        outError->clear();

    std::string error;
    const bool written = AtomicFileTransaction::Write(
        std::filesystem::path(path),
        [&](std::ostream& file)
        {
            file << FormatTextureConfig(config);
            return file.good();
        }, &error);
    if (!written && outError != nullptr)
        *outError = error.empty() ? "atomic write failed: " + path : error;
    return written;
}

bool TextureConfigEquals(const TextureConfig& a, const TextureConfig& b, std::string* outDifference)
{
    auto fail = [outDifference](const std::string& message) {
        if (outDifference != nullptr)
            *outDifference = message;
        return false;
    };

    if (a.atlases.size() != b.atlases.size())
        return fail("atlas count");
    for (size_t i = 0; i < a.atlases.size(); i++)
    {
        const TextureAtlasDefinition* other = b.FindAtlas(a.atlases[i].id);
        if (other == nullptr)
            return fail("missing atlas " + std::to_string(a.atlases[i].id));
        if (other->path != a.atlases[i].path || other->cellWidth != a.atlases[i].cellWidth ||
            other->cellHeight != a.atlases[i].cellHeight || other->name != a.atlases[i].name)
            return fail("atlas " + std::to_string(a.atlases[i].id) + " differs");
    }

    if (a.terrain.size() != b.terrain.size())
        return fail("terrain count");
    for (size_t i = 0; i < a.terrain.size(); i++)
    {
        if (a.terrain[i].tileType != b.terrain[i].tileType)
            return fail("terrain order/name at " + std::to_string(i));
        if (a.terrain[i].variants.size() != b.terrain[i].variants.size())
            return fail("variant count for " + a.terrain[i].tileType);
        for (size_t v = 0; v < a.terrain[i].variants.size(); v++)
        {
            if (!(a.terrain[i].variants[v].texture == b.terrain[i].variants[v].texture) ||
                a.terrain[i].variants[v].weight != b.terrain[i].variants[v].weight)
                return fail("variant " + std::to_string(v) + " of " + a.terrain[i].tileType);
        }
    }

    if (a.resourceOverlays.size() != b.resourceOverlays.size())
        return fail("resource overlay count");
    for (size_t i = 0; i < a.resourceOverlays.size(); i++)
    {
        if (a.resourceOverlays[i].tileType != b.resourceOverlays[i].tileType)
            return fail("resource overlay order/name at " + std::to_string(i));
        if (a.resourceOverlays[i].variants.size() != b.resourceOverlays[i].variants.size())
            return fail("resource overlay variant count for " + a.resourceOverlays[i].tileType);
        for (size_t v = 0; v < a.resourceOverlays[i].variants.size(); v++)
        {
            const auto& left = a.resourceOverlays[i].variants[v];
            const auto& right = b.resourceOverlays[i].variants[v];
            if (!(left.texture == right.texture) || left.weight != right.weight || left.edge != right.edge)
                return fail("resource overlay variant " + std::to_string(v) + " of " + a.resourceOverlays[i].tileType);
        }
    }

    if (a.buildings.size() != b.buildings.size())
        return fail("building count");
    for (size_t i = 0; i < a.buildings.size(); i++)
    {
        if (a.buildings[i].buildingType != b.buildings[i].buildingType)
            return fail("building order/name at " + std::to_string(i));
        if (!(a.buildings[i].sprite == b.buildings[i].sprite))
            return fail("sprite of " + a.buildings[i].buildingType);
        if (!(a.buildings[i].animation == b.buildings[i].animation))
            return fail("animation of " + a.buildings[i].buildingType);
    }

    if (a.resources.size() != b.resources.size())
        return fail("resource count");
    for (size_t i = 0; i < a.resources.size(); i++)
    {
        if (a.resources[i].resourceType != b.resources[i].resourceType)
            return fail("resource order/name at " + std::to_string(i));
        if (!(a.resources[i].icon == b.resources[i].icon))
            return fail("icon of " + a.resources[i].resourceType);
    }

    return true;
}
