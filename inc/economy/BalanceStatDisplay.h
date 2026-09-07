#ifndef BALANCE_STAT_DISPLAY_H
#define BALANCE_STAT_DISPLAY_H

// Presentation rules for balance stats and modifiers: what a stat is called,
// which direction counts as an improvement, and whether a given modifier is a
// bonus or a penalty.
//
// Centralized so every UI presents bonuses and penalties consistently.

#include "economy/BalanceModifiers.h"
#include "economy/BalanceStats.h"

#include <string>

// Human-readable stat name, e.g. "Production cycle time".
const char* BalanceStatLabel(BalanceStat stat);

// True when a SMALLER number is the improvement. Build timers, costs and
// worker headcount all read this way: the design wants as few people tied up
// in buildings as possible, so raising WorkerCapacity is a nerf.
bool LowerValueIsBetter(BalanceStat stat);

// For stats where a shrinking duration is better, the natural phrasing flips to
// a rate: "Build time x0.9" reads as "Build speed +11%".
const char* ImprovedRateLabel(BalanceStat stat);

// Whether the modifier helps the player, accounting for LowerValueIsBetter.
// Drives the green/red split in tooltips and the bonus summary.
bool IsPositiveModifier(const BalanceModifier& modifier);

// Display name for a modifier's building filter.
const char* BalanceBuildingLabel(BuildingType type);

#endif
