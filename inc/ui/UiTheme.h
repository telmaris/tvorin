#ifndef UI_THEME_H
#define UI_THEME_H

#include "raylib.h"

// Shared medieval color palette for GUI chrome (panels, tooltips, buttons,
// HUD, cards). Centralized so the whole interface stays visually consistent
// and can be retuned from one place instead of scattered Color literals.
namespace UiTheme
{
    // Neutral castle metals, darkest to lightest. Blue is deliberately not a
    // base surface; it is reserved for small interactive accents.
    // Historic names are kept for source compatibility with existing widgets.
    constexpr Color Ink    {8, 9, 11, 255};    // near-black background
    constexpr Color Bark   {17, 18, 21, 255};  // charcoal panel body
    constexpr Color Oak    {29, 31, 35, 255};  // graphite title bars/cards
    constexpr Color Timber {53, 56, 61, 255};  // raised neutral surface

    // Shared UI surfaces. Use these rather than local brown literals so the
    // menu, in-game widgets and large strategic panels belong to one system.
    constexpr Color Panel        {17, 18, 21, 245};
    constexpr Color Surface      {31, 33, 38, 245};
    constexpr Color SurfaceHover {44, 48, 55, 250};
    constexpr Color Inset        {9, 10, 12, 238};
    constexpr Color InsetHover   {23, 25, 29, 245};
    constexpr Color PanelBackdrop {12, 14, 18, 245};
    constexpr Color TooltipBackdrop {10, 13, 18, 248};
    constexpr Color TreeCanvasBackdrop {7, 10, 14, 245};

    // Metal accents.
    constexpr Color Bronze {180, 181, 178, 255}; // steel borders, dividers
    constexpr Color Iron   {103, 107, 113, 255}; // muted / disabled borders
    constexpr Color Gold   {218, 202, 145, 255}; // restrained royal highlights
    constexpr Color Cyan   {96, 174, 190, 255}; // the restrained blue accent
    constexpr Color SteelHover {210, 212, 214, 255};

    // Text.
    constexpr Color Parchment      {238, 234, 218, 255}; // primary text (replaces RAYWHITE)
    constexpr Color ParchmentDim   {195, 195, 188, 255}; // muted / secondary text
    constexpr Color ParchmentFaint {139, 141, 143, 255}; // disabled / locked text

    // Status.
    constexpr Color Sage {150, 180, 98, 255};  // positive / affordable / ok
    constexpr Color Rust {198, 94, 68, 255};   // negative / missing / locked / danger

    // Brighter status variants for small text/labels on dark backgrounds.
    constexpr Color SageBright  {168, 214, 128, 255}; // affordable cost, positive stat delta
    constexpr Color RustBright  {214, 110, 90, 255};  // unaffordable cost, negative stat delta
    constexpr Color AmberBright {224, 188, 120, 255}; // informational highlight (replaces cool blue accents)

    // Pre-alpha'd fills/borders for specific chrome states.
    constexpr Color SelectedFill   {38, 56, 61, 245};
    constexpr Color SelectedBorder {132, 211, 180, 220};
    constexpr Color DangerFill     {104, 43, 54, 255};
    constexpr Color DangerBorder   {208, 110, 124, 255};
    constexpr Color Oxblood        {82, 24, 34, 255};
    constexpr Color OxbloodHover   {118, 36, 49, 255};
}

#endif
