#include "ui/UnitTypeCard.h"

#include "ui/ControlIcons.h"
#include "ui/UiText.h"
#include "ui/UiTheme.h"

#include <algorithm>

bool DrawUnitTypeCard(Rectangle bounds, const UnitTypeCardView& view,
                      bool hovered)
{
    const Color portraitTint = view.enabled ? WHITE : Color{132, 132, 132, 210};
    UiControlIcons::DrawPixelHudWidgetFrame(
        bounds, hovered, view.enabled ? WHITE : Color{126, 128, 132, 220});

    const Rectangle portrait{bounds.x + 9.0f, bounds.y + 9.0f,
                             std::max(1.0f, bounds.width - 18.0f),
                             std::max(1.0f, bounds.height - 18.0f)};
    if (!UiControlIcons::DrawUnitPortrait(view.unitDefId, portrait, portraitTint))
        UiText::DrawFit(view.displayName, portrait, 14,
                        view.enabled ? UiTheme::Parchment : UiTheme::ParchmentDim);

    if (!view.enabled)
        DrawRectangleRec({bounds.x + 4.0f, bounds.y + 4.0f,
                          bounds.width - 8.0f, bounds.height - 8.0f},
                         Fade(BLACK, 0.34f));

    const float badgeRadius = std::clamp(bounds.width * 0.14f, 12.0f, 15.0f);
    const Vector2 badgeCenter{bounds.x + bounds.width - badgeRadius - 5.0f,
                              bounds.y + bounds.height - badgeRadius - 5.0f};
    DrawCircleV(badgeCenter, badgeRadius, Color{20, 27, 35, 238});
    UiText::DrawFit(std::to_string(std::max(0, view.primaryCount)),
                    {badgeCenter.x - badgeRadius, badgeCenter.y - 8.0f,
                     badgeRadius * 2.0f, 16.0f},
                    13, UiTheme::Parchment);

    if (view.secondaryCount > 0)
        UiText::DrawFit("(" + std::to_string(view.secondaryCount) + ")",
                        {bounds.x + 5.0f, bounds.y + 4.0f,
                         bounds.width - 10.0f, 16.0f},
                        12, UiTheme::AmberBright);
    return true;
}
