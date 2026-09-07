#ifndef SCENE_UTILS_H
#define SCENE_UTILS_H

#include "ui/Gui.h"
#include "simulation/MapGenerator.h"

#include <filesystem>
#include <functional>
#include <string>

// Small helpers shared by save-file, new-game, and multiplayer scenes.

// Converts user-provided save names into filesystem-safe filenames.
std::string SanitizeSaveName(std::string name);

// Reads world display name from a save file header.
std::string ReadSaveDisplayName(const std::filesystem::path& path);

// Returns true when a save file with this sanitized name exists.
bool SaveExists(const std::string& saveName);

// Rebuilds save-list buttons from files in the saves directory.
void PopulateSaveButtons(VBox& saveButtons, const std::function<void(std::string)>& onSavePressed,
                         const std::filesystem::path& root = "saves");

// Human-readable label for a map size preset.
std::string MapSizeName(MapSizePreset preset);

// Maps a normalized [0,1] slider value onto an integer range.
int SliderToInt(float value, int minValue, int maxValue);

// Applies only the local-map shortcuts used by debug sessions. Campaign-wide
// global-map parameters remain owned by the corresponding setup screen.
void ApplyDebugLocalMapPreset(MapParameters& params);

#endif
