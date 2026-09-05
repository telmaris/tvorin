#ifndef CONTROL_ICONS_H
#define CONTROL_ICONS_H

#include "raylib.h"

#include <string>

// Runtime loader for the 64x64 keyboard/mouse shortcut icons used by the
// controls reference and tutorial popups.
namespace UiControlIcons
{
    // Semantic slots in the generated royal HUD icon atlas. The atlas retains
    // its high-resolution source cells and is downscaled at draw time, so HUD
    // controls stay sharp across supported window sizes.
    enum class HudIcon
    {
        Build,
        Destroy,
        Road,
        Logistics,
        Resources,
        Roster,
        Decisions,
        Technology,
        Manpower,
        Builders,
        // The global map currently has no authored atlas cell. The icon
        // system renders a neutral compass placeholder for this semantic
        // slot until the final map glyph is available.
        GlobalMap
    };

    // Icons authored specifically for unit-stat tooltips. Stats with a clear
    // product equivalent (sword, shield, bow, horse, etc.) deliberately keep
    // using the shared resource atlas instead of duplicating artwork here.
    enum class MilitaryStatIcon
    {
        Time,
        HitPoints,
        MoveSpeed,
        AttackSpeed,
        AreaTargets,
        AirTargeting
    };

    void Load(const std::string& directory = "assets/ui/controls");
    void Unload();
    bool IsLoaded();

    // Draws one icon into a destination rectangle. Returns false when the
    // requested asset is unavailable, allowing callers to fall back to text.
    bool Draw(const std::string& name, Rectangle destination, Color tint = WHITE);

    // Draws one framed strategic-HUD icon from the generated atlas. Returns
    // false when the atlas is unavailable so the HUD can use its simple
    // procedural fallback instead of becoming unusable.
    bool DrawHud(HudIcon icon, Rectangle destination, bool hovered = false, Color tint = WHITE);

    // Draws only the subject and its dark inset, omitting the atlas cell frame.
    // Used inside the independently framed statistic chips.
    bool DrawHudGlyph(HudIcon icon, Rectangle destination, Color tint = WHITE);

    // Pilot skin for the strategic top HUD. The same neutral 9-slice is used
    // for the outer bar, stat chips, resource cassette and action buttons;
    // the heraldic left cap is a separate top-HUD-only overlay.
    bool DrawPixelHudFrame(Rectangle destination, bool hovered = false,
                           Color tint = WHITE);
    // Draws the new pixel-pilot frame for regular panels and windows.
    bool DrawPixelHudPanelFrame(Rectangle destination, Color tint = WHITE);
    // Draws the new pixel-pilot frame for regular buttons and compact controls.
    bool DrawPixelHudButtonFrame(Rectangle destination, bool hovered = false,
                                 Color tint = WHITE);
    // Keeps the original compact frame for controls embedded in the strategic
    // top HUD; those controls intentionally do not use button_hud.png.
    bool DrawPixelTopHudWidgetFrame(Rectangle destination, bool hovered = false,
                                    Color tint = WHITE);
    // Pilot top-HUD skin. The source artwork uses asymmetric 9-slice borders:
    // 128 px left/right and 74 px top/bottom.
    bool DrawPixelTopHudFrame(Rectangle destination, Color tint = WHITE);
    // Content-safe inset matching the fixed corner modules of the main frame.
    float PixelHudFrameInset(Rectangle destination);
    // Symmetric top-right close control, kept clear of the main frame corners.
    Rectangle PixelHudCloseButtonRect(Rectangle destination);
    // Slimmer companion frame for repeated statistic slots and action icons.
    bool DrawPixelHudWidgetFrame(Rectangle destination, bool hovered = false,
                                 Color tint = WHITE);
    bool DrawPixelTopHudCrest(Rectangle destination, Color tint = WHITE);
    bool DrawPixelHudGlyph(HudIcon icon, Rectangle destination, Color tint = WHITE);
    // Draws a shader-generated halo from the glyph alpha. The crisp glyph is
    // intentionally drawn separately by the caller on top of this pass.
    bool DrawPixelHudGlow(HudIcon icon, Rectangle destination, Color color,
                          float intensity = 1.0f);
    // Draws one of the thirteen portraits from the Barracks recruitment atlas.
    bool DrawUnitPortrait(const std::string& unitDefId, Rectangle destination,
                          Color tint = WHITE);
    // Draws a stat which has no suitable equivalent in the resource atlas.
    bool DrawMilitaryStat(MilitaryStatIcon icon, Rectangle destination,
                          Color tint = WHITE);

    // Draws the cold-castle panel from independent background, rail, corner
    // and fixed-keystone modules. Unique ornaments are never stretched.
    bool DrawRoyalPanel(Rectangle destination, Color tint = WHITE);

    // Draws the reusable left-side status chip with fixed ornamental end caps
    // and a stretchable, detail-free middle section.
    bool DrawRoyalChip(Rectangle destination, Color tint = WHITE);

    // Draws the shared 9-slice graphite button frame. The four ornamental
    // corners remain fixed; only quiet metal rails and the central face are
    // stretched, so menu and in-game controls use one coherent chrome.
    bool DrawRoyalButtonFrame(Rectangle destination, bool hovered = false,
                              Color tint = WHITE);

    // Draws the fixed-aspect lion crest used as the left anchor of the HUD.
    bool DrawRoyalCrest(Rectangle destination, Color tint = WHITE);

    // Draws the shared scalable frame for all large game windows.
    bool DrawRoyalWindowPanel(Rectangle destination, Color tint = WHITE);

    // Returns the fixed rail inset used by DrawRoyalWindowPanel.  Title bars
    // and dividers use this exact value so they never paint over the frame.
    float RoyalWindowPanelInset(Rectangle destination);

    // Draws the quiet steel-and-navy background used by compact resource
    // slots.  The resource icon and value badge are drawn by the caller.
    bool DrawRoyalResourceSlot(Rectangle destination, Color tint = WHITE);

    // Draws the scalable steel-and-navy title plaque. Its fixed end caps and
    // detail-free middle keep the header consistent at every panel width.
    bool DrawRoyalTitleBar(Rectangle destination, Color tint = WHITE);

    // Draws the dedicated close control.  `hovered` selects its cool-blue
    // highlight asset instead of falling back to a text glyph.
    bool DrawPanelCloseButton(Rectangle destination, bool hovered, Color tint = WHITE);
}

#endif
