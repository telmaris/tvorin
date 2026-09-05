#include "scenes/SceneUtils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>

std::string SanitizeSaveName(std::string name)
{
    if (name.empty() || name == "Default textbox text")
        return "default";

    for (auto& c : name)
    {
        bool valid = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
        if (!valid)
            c = '_';
    }
    return name;
}

std::string ReadSaveDisplayName(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return path.stem().string();

    std::string tag;
    int version = 0;
    file >> tag >> version;
    if (tag == "RTS_SAVE")
    {
        std::string worldTag;
        std::string worldName;
        file >> worldTag >> std::quoted(worldName);
        if (worldTag == "WORLD" && !worldName.empty())
            return worldName;
    }

    return path.stem().string();
}

bool SaveExists(const std::string& saveName)
{
    return std::filesystem::exists(std::filesystem::path("saves") / (SanitizeSaveName(saveName) + ".save"));
}

void PopulateSaveButtons(VBox& saveButtons, const std::function<void(std::string)>& onSavePressed,
                         const std::filesystem::path& root)
{
    namespace fs = std::filesystem;
    saveButtons.ClearChildren();

    if (!fs::exists(root))
    {
        fs::create_directories(root);
        return;
    }

    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".save")
            continue;

        const std::string displayName = ReadSaveDisplayName(entry.path());
        const std::string saveName = entry.path().stem().string();

        auto button = std::make_shared<UiButton>();
        button->ChangeText(displayName);
        button->func = [onSavePressed, saveName]()
        {
            onSavePressed(saveName);
        };

        saveButtons.AddChild(button);
    }
}

std::string MapSizeName(MapSizePreset preset)
{
    switch (preset)
    {
        case MapSizePreset::S: return "S 201x201";
        case MapSizePreset::M: return "M 301x301";
        case MapSizePreset::L: return "L 401x401";
        case MapSizePreset::XL: return "XL 501x501";
        default: return "S 201x201";
    }
}

int SliderToInt(float value, int minValue, int maxValue)
{
    return minValue + static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * (maxValue - minValue)));
}

void ApplyDebugLocalMapPreset(MapParameters& params)
{
    params.sizePreset = MapSizePreset::S;
    params.sizeX = MapGenerator::SizeFromPreset(MapSizePreset::S);
    params.sizeY = params.sizeX;
    params.resourceDensity = 0.65f;
    params.resourceFieldSize = 0.45f;
    params.resourceRichness = 120;
}
