#ifndef STOCKPILE_INDEX_H
#define STOCKPILE_INDEX_H

#include "data/Resource.h"

#include <map>
#include <vector>

class Building;
class Player;
struct ProvinceEconomy;

// How much of one resource type sits in one warehouse.
struct StockpileHolding
{
    int buildingId{0};
    Building* building{nullptr};
    int amount{0};
    int capacity{0};
};

// One resource type aggregated over the whole warehouse network.
struct StockpileTotals
{
    int amount{0};
    int capacity{0};
    // Warehouses actually holding this type, in building-id order.
    std::vector<StockpileHolding> holdings;
};

// The player's warehouses seen as a single stock ledger: how much of each
// resource exists and, by building id, where it physically sits. Every
// consumer of "how much do I have" — top HUD, stockpile panel, build costs,
// AI feasibility checks and the logistics pull — reads this one place, so
// those numbers can never drift apart.
//
// "Warehouse" is deliberately narrower than Building::IsStorageLike(): only
// Headquarters and StorageBuilding pool stock for the whole economy. A
// Barracks' queued unit costs are StorageComponent buffers too, but they belong
// to that one building's own consumption —
// counting them as shared stock made the HUD promise resources that could
// never actually be spent or delivered elsewhere.
//
// Derived state, never stored: every query reads Player::storages live, so
// there is no cache to invalidate and nothing to serialize (same approach as
// ConstructionQueue). All ordering is by building id, never by Building*, so
// results are identical across hosts — see docs/tech_debt.md #6.
class StockpileIndex
{
public:
    // True for the shared-stock hubs (Headquarters, StorageBuilding).
    static bool IsWarehouse(const Building* building);

    // The player's warehouses in building-id order. Buildings still under
    // construction are excluded — they hold nothing and cannot serve.
    static std::vector<Building*> Warehouses(const Player& owner);
    static std::vector<Building*> Warehouses(const ProvinceEconomy& economy);

    static int GetTotal(const Player& owner, ResourceType type);
    static int GetTotal(const ProvinceEconomy& economy, ResourceType type);
    static int GetCapacity(const Player& owner, ResourceType type);
    static int GetCapacity(const ProvinceEconomy& economy, ResourceType type);
    // Warehouses holding at least one unit of `type`, in building-id order.
    static std::vector<StockpileHolding> GetHoldings(const Player& owner, ResourceType type);
    static std::vector<StockpileHolding> GetHoldings(const ProvinceEconomy& economy, ResourceType type);
    // Every type currently held anywhere, for panel rendering.
    static std::map<ResourceType, StockpileTotals> Snapshot(const Player& owner);
    static std::map<ResourceType, StockpileTotals> Snapshot(const ProvinceEconomy& economy);

    // Warehouses that hold `type` AND have a usable road path to `requester`,
    // nearest first (road distance, then building id as a deterministic
    // tie-break). Road connectivity is a hard requirement — a warehouse the
    // requester cannot reach can never serve it, so it is never returned.
    static std::vector<Building*> RankSourcesFor(ResourceType type, Building& requester);

    // Spends `amount` across the warehouse network in building-id order.
    // Returns how much was actually taken (< amount when stock ran out).
    static int Consume(Player& owner, ResourceType type, int amount);
    static int Consume(ProvinceEconomy& economy, ResourceType type, int amount);
    // Puts `amount` back into the network, filling warehouses in building-id
    // order. Returns how much fit; the remainder is dropped (no free capacity).
    static int Deposit(Player& owner, ResourceType type, int amount);
    static int Deposit(ProvinceEconomy& economy, ResourceType type, int amount);
};

#endif
