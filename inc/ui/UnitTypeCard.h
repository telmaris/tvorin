#ifndef UNIT_TYPE_CARD_H
#define UNIT_TYPE_CARD_H

#include "raylib.h"

#include <string>

struct UnitTypeCardView
{
    std::string unitDefId;
    std::string displayName;
    int primaryCount{0};
    int secondaryCount{0};
    bool enabled{true};
};

// Shared presentation for Barracks and roster unit-type cards. Counts are
// supplied by the caller from its authoritative/presentation view; this
// helper deliberately performs no gameplay lookup or mutation.
bool DrawUnitTypeCard(Rectangle bounds, const UnitTypeCardView& view,
                      bool hovered);

#endif
