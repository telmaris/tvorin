#ifndef CONSTRUCTION_QUEUE_H
#define CONSTRUCTION_QUEUE_H

#include "core/Stat.h"

#include <vector>

class Player;
class Building;
struct ProvinceEconomy;

// Per-player construction manager. A limited pool of builders (held as a
// Stat<int> so tech / focus / national buffs can raise it through
// BalanceStat::BuilderAmount) works a single FIFO build queue: the first
// `builders` buildings still under construction actually progress, while the
// rest wait — shaded on the map — until a builder frees up.
//
// The queue is derived state: every simulation tick Refresh() rebuilds it from
// the player's under-construction buildings (ordered by building id, which is
// also their placement order), so it needs no separate serialization and stays
// deterministic across host and client.
class ConstructionQueue
{
public:
    // Base builder count before balance modifiers.
    Stat<int> builders{BalanceStat::BuilderAmount, 1};

    // Effective builder count after tech / focus / national BuilderAmount buffs.
    int EffectiveBuilders(const Player& player) const;

    // Rebuilds queue order and flags which buildings may progress this tick.
    // Call once per simulation tick, before ticking the buildings.
    void Refresh(Player& player);
    void Refresh(Player& player, ProvinceEconomy& economy);

    // 1-based queue position of a building, or 0 when it is not queued.
    int QueuePosition(int buildingId) const;

    // True when the building currently occupies one of the active builder slots.
    bool IsActive(int buildingId) const;

    // Number of buildings queued (actively building + waiting).
    int QueueLength() const { return static_cast<int>(order.size()); }

    // Number of builders actively working this tick.
    int ActiveCount() const { return activeCount; }

private:
    std::vector<int> order;  // building ids, ascending == enqueue order
    int activeCount{0};
};

#endif
