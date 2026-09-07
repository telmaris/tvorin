#include "scenes/Scenes.h"
#include "ui/ControlIcons.h"
#include "ui/KeyBindings.h"

#include <algorithm>
#include <cmath>
#include <sstream>

ControlsScene::ControlsScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.44f, 0.88f});
    backButton.ChangeSizeAnchor(Vec2f{0.12f, 0.055f});
    backButton.func = std::bind(&ControlsScene::OnBackPressed, this);

    Vec2i windowSize{GetScreenWidth(), GetScreenHeight()};
    backButton.UpdateSize(windowSize);
}

namespace
{
    float controlsDensity = 1.0f;
    float controlsColumnWidth = 360.0f;

    const char* ControlIconForWord(const std::string& word)
    {
        if (word == "Q") return "key_q";
        if (word == "R") return "key_r";
        if (word == "D") return "key_d";
        if (word == "E") return "key_e";
        if (word == "S") return "key_s";
        if (word == "F") return "key_f";
        if (word == "T") return "key_t";
        if (word == "U") return "key_u";
        if (word == "G") return "key_g";
        if (word == "L") return "key_l";
        if (word == "M") return "key_m";
        if (word == "Space") return "key_space";
        if (word == "ESC") return "key_escape";
        if (word == "LMB") return "mouse_lmb";
        if (word == "RMB") return "mouse_rmb";
        if (word == "MMB") return "mouse_mmb";
        if (word == "Scroll") return "mouse_wheel";
        return nullptr;
    }

    void DrawControlLabel(const std::string& label, float x, float y, float maxWidth)
    {
        const int labelFont = std::max(14, static_cast<int>(std::round(16.0f * controlsDensity)));
        if (!UiControlIcons::IsLoaded())
        {
            UiText::Draw(label, x, y, labelFont, Color{255, 220, 120, 255});
            return;
        }

        std::istringstream tokens(label);
        std::string token;
        float cursor = x;
        bool first = true;
        while (tokens >> token)
        {
            if (!first)
                cursor += 4.0f;

            const char* iconName = ControlIconForWord(token);
            if (iconName != nullptr)
            {
                const float iconSize = std::max(17.0f, 22.0f * controlsDensity);
                if (cursor + iconSize <= x + maxWidth &&
                    !UiControlIcons::Draw(iconName, {cursor, y + 1.0f, iconSize, iconSize}))
                {
                    UiText::Draw(token, cursor, y + 2.0f, labelFont, Color{255, 220, 120, 255});
                }
                cursor += iconSize;
            }
            else
            {
                UiText::Draw(token, cursor, y + 2.0f, labelFont, Color{255, 220, 120, 255});
                cursor += static_cast<float>(UiText::Measure(token, labelFont));
            }
            first = false;
        }
    }

    void DrawControlsSection(float x, float& y, const char* header,
                             const std::vector<std::pair<std::string, std::string>>& rows)
    {
        {
            UiFontRoleScope displayRole{UiFontRole::Display};
            UiText::Draw(header, x, y,
                         std::max(17, static_cast<int>(std::round(22.0f * controlsDensity))),
                         Color{210, 220, 240, 255});
        }
        y += 30.0f * controlsDensity;
        for (const auto& [key, desc] : rows)
        {
            float keyW = 130.0f;
            DrawControlLabel(key, x + 8.0f, y, keyW - 8.0f);
            const int descriptionFont = std::max(
                14, static_cast<int>(std::round(18.0f * controlsDensity)));
            UiText::DrawFit(desc,
                            {x + keyW, y,
                             std::max(80.0f, controlsColumnWidth - keyW - 10.0f),
                             22.0f * controlsDensity},
                            descriptionFont,
                            Color{190, 200, 216, 255});
            y += 24.0f * controlsDensity;
        }
        y += 10.0f * controlsDensity;
    }
}

void ControlsScene::Update(double dt)
{
    ProcessGuiInput(dt);
    BeginDrawing();
    ClearBackground(BLACK);

    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());

    float panelW = sw * 0.90f;
    float panelH = sh * 0.82f;
    float panelX = (sw - panelW) * 0.5f;
    float panelY = sh * 0.07f;

    Rectangle panel{panelX, panelY, panelW, panelH};
    controlsDensity = std::clamp((panelH - 90.0f) / 700.0f, 0.72f, 1.0f);
    if (!UiControlIcons::DrawPixelHudPanelFrame(panel))
    {
        DrawRectangleRounded(panel, 0.03f, 8, Color{18, 22, 30, 242});
        DrawRectangleRoundedLines(panel, 0.03f, 8, 1.0f, Color{90, 106, 130, 255});
    }

    {
        UiFontRoleScope displayRole{UiFontRole::Display};
        UiText::Draw("Controls", panelX + 24.0f, panelY + 16.0f, 30, RAYWHITE);
    }
    DrawLineEx({panelX + 24.0f, panelY + 54.0f}, {panelX + panelW - 24.0f, panelY + 54.0f}, 1.0f, Color{70, 84, 106, 200});

    const float columnWidth = (panelW - 64.0f) / 3.0f;
    controlsColumnWidth = columnWidth;
    float col1X = panelX + 32.0f;
    float col2X = col1X + columnWidth;
    float col3X = col2X + columnWidth;
    float startY = panelY + 68.0f;
    const KeyBindingMap& bindings = GetDefaultKeyBindings();
    auto key = [&](GameAction action)
    {
        return GetBindingDisplayName(bindings, action);
    };

    float y1 = startY;
    DrawControlsSection(col1X, y1, "Camera & navigation", {
        {key(GameAction::CenterCameraOnHeadquarters), "Center on Headquarters"},
        {"RMB drag",    "Pan camera in map/build modes"},
        {"MMB drag",    "Pan camera in every map mode"},
        {"Scroll",      "Zoom map or active tree"},
        {key(GameAction::ToggleLogisticsOverlay), "Toggle logistics load overlay"},
        {"ESC",         "Close panel, cancel mode, or open menu"},
    });

    DrawControlsSection(col1X, y1, "Modes & panels", {
        {key(GameAction::EnterBuildMode), "Build browser / exit build mode"},
        {key(GameAction::EnterRoadMode), "Road construction mode"},
        {key(GameAction::EnterDestroyMode), "Demolition mode"},
        {key(GameAction::EnterUpgradeMode), "Mass upgrade mode"},
        {key(GameAction::OpenStockpilePanel), "Global stockpile overview"},
        {key(GameAction::OpenStatsPanel), "Statistics and economy"},
        {key(GameAction::OpenFocusTree), "Strategic decisions"},
        {key(GameAction::OpenResearchPanel), "Technology tree (needs University)"},
        {key(GameAction::OpenRosterPanel), "Unit roster"},
    });

    DrawControlsSection(col1X, y1, "Map selection & logistics", {
        {"LMB",         "Select building on map"},
        {"LMB (build)", "Open an existing building panel directly"},
        {"RMB click",   "Set selected building's receiver"},
        {"Ctrl+RMB",    "Set an alternative receiver"},
        {key(GameAction::OpenGlobalMap), "Open/close global map; inspect known provinces, tracks, and scouting"},
    });

    float y2 = startY;
    DrawControlsSection(col2X, y2, "Building & roads", {
        {"LMB on card",  "Select building to place"},
        {"LMB on map",   "Place selected building"},
        {"RMB drag",     "Move camera without leaving build mode"},
        {"LMB drag",     "Paint a straight road segment"},
        {"Pause drag",   "Allow a road direction change"},
        {"Scroll",       "Scroll hovered panel; otherwise zoom"},
        {"ESC / Q",      "Exit building placement"},
        {"ESC / R",      "Exit road placement"},
    });

    DrawControlsSection(col2X, y2, "Upgrade & demolition", {
        {"G",            "Enter or leave mass upgrade mode"},
        {"LMB",          "Upgrade one building or road"},
        {"LMB drag",     "Upgrade every crossed road/building once"},
        {"RMB",          "Cancel upgrade gesture and leave mode"},
        {"D",            "Enter or leave demolition mode"},
        {"LMB (destroy)","Demolish hovered owned building"},
    });

    DrawControlsSection(col2X, y2, "Research & decisions", {
        {"LMB node",     "Research technology / choose decision"},
        {"RMB drag",     "Pan technology / decisions view"},
        {"Scroll",       "Zoom tree view"},
        {"Ctrl+Scroll",  "Pan embedded research tree"},
    });

    float y3 = startY;
    DrawControlsSection(col3X, y3, "Debug & rendering", {
        {key(GameAction::ToggleNightPreview), "Toggle night preview"},
        {key(GameAction::ToggleDayNightCycle), "Toggle day/night cycle"},
        {key(GameAction::ToggleDynamicLights), "Toggle dynamic lights"},
        {key(GameAction::CycleRendererDebugView), "Cycle renderer debug view"},
        {"Ctrl+" + key(GameAction::CaptureFinalFrame), "Capture the final rendered frame"},
        {key(GameAction::GrantDebugResources), "Grant HQ resources (debug)"},
        {key(GameAction::SpawnDebugRaid), "Spawn raid on active province (debug)"},
        {key(GameAction::AdvanceTutorialStep), "Advance tutorial step"},
    });

    DrawControlsSection(col3X, y3, "Menus, lobby & text", {
        {"LMB",          "Activate buttons / fields"},
        {"Enter",        "Confirm / send lobby chat"},
        {"Up / Down",    "Navigate dropdown options"},
        {"Scroll",       "Scroll long dropdown lists"},
        {"ESC",          "Close dropdown or unfocus text"},
        {"Backspace",    "Delete text"},
        {"Ctrl+C",       "Copy the whole focused field"},
        {"Ctrl+V",       "Paste at the end of focused field"},
    });

    UiText::Draw("Tip: hover over widgets in the New Game screen for detailed descriptions.",
                 panelX + 24.0f, panelY + panelH - 32.0f, 16, Color{150, 162, 180, 220});

    backButton.Update(dt);

    render.PresentFrame();
}

void ControlsScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void ControlsScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
        backButton.UpdateSize(ptr->windowSize);
}
