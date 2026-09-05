#ifndef RESOURCE_H
#define RESOURCE_H

#include "core/Types.h"
#include "simulation/Transport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// raylib exposes GOLD as a color macro; resources need the plain enum name.
#ifdef GOLD
#undef GOLD
#endif

// ─── Resource Taxonomy ─────────────────────────────────────────────────────
// Resources fall into two classes, each with different transport/storage rules:
//
// 1. CONCRETE — individual items produced by buildings and transported over roads
//    as Transportable resource objects. Stored in StorageComponent buffers.
//    Types: WOOD, PLANKS, BREAD, MEAT, TOOLS, all ore/metal, armor, weapons (SWORD, BOW, etc),
//    and FOOD_PROVISIONS (consumed by Village -> manpower, see PopulationComponent).
//    Transport: BeginTransport(source, receiver, resource)
//
// 2. STRATEGIC — global aggregates tracked in StrategicResourcePool on Player
//    (no per-building instance, never transported). Types: Manpower, Workers.

enum class ResourceType : uint8_t
{
    Null = 255,

    COPPER_ORE = 0,
    COPPER = 1,
    IRON_ORE = 2,
    IRON = 3,
    SILVER_ORE = 4,
    SILVER = 5,
    GOLD_ORE = 6,
    GOLD = 7,

    WOOD = 8,
    PLANKS = 9,

    LEATHER = 10,
    COAL = 11,
    STONE = 12,

    WHEAT = 13,
    FLOUR = 14,
    BREAD = 15,
    MEAT = 16,
    WATER = 17,
    BEER = 18,
    
    COINS = 19,
    PAPER = 20,
    
    TOOLS = 21,
    FOOD_PROVISIONS = 22,

    // Retired product slot kept so old saves retain their numeric layout.
    ReservedCopperSwordSlot = 24,
    IRON_SWORD = 25,
    STEEL_SWORD = 26,
    BOW = 27,
    ARROWS = 28,
    HORSE = 29,

    // Equipment categories for the supply/division system (material progression:
    // stone → copper → bronze → iron → steel). See Equipment.h for the taxonomy.
    BRONZE_SWORD = 30,
    SPEAR = 31,
    CROSSBOW = 32,
    BOLTS = 33,
    WOODEN_SHIELD = 34,
    IRON_SHIELD = 35,
    LEATHER_ARMOR = 36,
    IRON_ARMOR = 37,

    // Resource & world expansion (see docs/resource_world_design.md).
    // Raw deposits:
    TIN_ORE = 38,
    SAND = 39,
    SULFUR = 40,
    SALTPETER = 41,
    // Smelted / processed:
    TIN = 42,
    BRONZE = 43,
    COKE = 44,
    STEEL = 45,
    GLASS = 46,
    GUNPOWDER = 47,

    // Firearms (Phase 3 — steampunk chemistry consumer):
    MUSKET = 48,
    CARTRIDGE = 49,

    // Active medieval economy expansion. Values are appended deliberately:
    // saves serialize ResourceType numerically, so retired prototype resources
    // above must keep their historical ids even when no longer generated.
    CLAY = 50,
    CATTLE = 51,
    RAW_HIDE = 52,
    TALLOW = 53,
    CLOTHES = 54,
    POTTERY = 55,
    HOUSEHOLD_GOODS = 56,
    SOAP = 57,
    INK = 58,
    BOOKS = 59,
    COPPERWARE = 60,
    URBAN_GOODS = 61,
    HEMP = 62,
    FIBRE = 63,
    ROPE = 64,
    COPPER_VESSEL = 65,
    COPPER_PIPE = 66,
    MECHANICAL_PARTS = 67,
    HEAVY_BOW = 68,
    // Retired product slot kept so old saves retain their numeric layout.
    ReservedWeaponSlot = 69,
    HEAVY_ARMOR = 70,
    BRICKS = 71,
    CLOTH = 72,
    BALLISTA = 73,
    BATTERING_RAM = 74,
    CATAPULT = 75

};

// Stable value object shared by authoritative strategic transfers and their
// serialized command/journey payloads. Resource types are ordered by their
// numeric enum value at validation boundaries.
struct ResourceAmount
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

// Resource types supported by the data-driven resource catalog, in their
// player-facing presentation order.  Keep this separate from the numeric
// ResourceType ids: ids are serialized in saves and multiplayer snapshots,
// while this order is safe to tune for coherent storage/UI panels.
//
// The order is grouped by material family, then by processing stage within a
// family. This makes a full warehouse read as distinct thematic blocks rather
// than one long production-chain timeline.
constexpr ResourceType resourceTypes[] =
{
    // Timber
    ResourceType::WOOD,
    ResourceType::PLANKS,

    // Stone, clay and sand products
    ResourceType::STONE,
    ResourceType::CLAY,
    ResourceType::SAND,
    ResourceType::BRICKS,
    ResourceType::POTTERY,
    ResourceType::GLASS,

    // Fuel and chemical inputs
    ResourceType::COAL,
    ResourceType::SULFUR,
    ResourceType::SALTPETER,
    ResourceType::COKE,
    ResourceType::GUNPOWDER,

    // Metals and metalworking
    ResourceType::COPPER_ORE,
    ResourceType::COPPER,
    ResourceType::COPPERWARE,
    ResourceType::COPPER_VESSEL,
    ResourceType::COPPER_PIPE,
    ResourceType::TIN_ORE,
    ResourceType::TIN,
    ResourceType::BRONZE,
    ResourceType::IRON_ORE,
    ResourceType::IRON,
    ResourceType::STEEL,
    ResourceType::SILVER_ORE,
    ResourceType::SILVER,
    ResourceType::GOLD_ORE,
    ResourceType::GOLD,
    ResourceType::TOOLS,
    ResourceType::MECHANICAL_PARTS,

    // Food and consumables
    ResourceType::WHEAT,
    ResourceType::FLOUR,
    ResourceType::BREAD,
    ResourceType::WATER,
    ResourceType::BEER,
    ResourceType::MEAT,
    ResourceType::FOOD_PROVISIONS,

    // Animals and their by-products
    ResourceType::CATTLE,
    ResourceType::RAW_HIDE,
    ResourceType::LEATHER,
    ResourceType::TALLOW,
    ResourceType::HORSE,

    // Plant fibres and textiles
    ResourceType::HEMP,
    ResourceType::FIBRE,
    ResourceType::ROPE,
    ResourceType::CLOTH,
    ResourceType::CLOTHES,

    // Settlement, knowledge and trade goods
    ResourceType::PAPER,
    ResourceType::INK,
    ResourceType::BOOKS,
    ResourceType::SOAP,
    ResourceType::HOUSEHOLD_GOODS,
    ResourceType::URBAN_GOODS,
    ResourceType::COINS,

    // Weapons, ammunition, protection and siege equipment
    ResourceType::SPEAR,
    ResourceType::BRONZE_SWORD,
    ResourceType::IRON_SWORD,
    ResourceType::STEEL_SWORD,
    ResourceType::BOW,
    ResourceType::ARROWS,
    ResourceType::HEAVY_BOW,
    ResourceType::CROSSBOW,
    ResourceType::BOLTS,
    ResourceType::MUSKET,
    ResourceType::CARTRIDGE,
    ResourceType::WOODEN_SHIELD,
    ResourceType::IRON_SHIELD,
    ResourceType::LEATHER_ARMOR,
    ResourceType::IRON_ARMOR,
    ResourceType::HEAVY_ARMOR,
    ResourceType::BALLISTA,
    ResourceType::BATTERING_RAM,
    ResourceType::CATAPULT
};

// Sort key for UI lists.  Unknown/retired values intentionally appear after
// the catalog rather than disappearing from a loaded legacy save.
inline int ResourcePresentationRank(ResourceType type)
{
    for (int index = 0; index < static_cast<int>(std::size(resourceTypes)); ++index)
        if (resourceTypes[index] == type)
            return index;
    return static_cast<int>(std::size(resourceTypes)) + static_cast<int>(type);
}

// Converts resource type to a readable debug label.
inline std::string rt2s(ResourceType s)
{
    switch (s)
    {
        case ResourceType::Null: return "NULL";
        case ResourceType::COPPER_ORE: return "COPPER_ORE";
        case ResourceType::COPPER: return "COPPER";
        case ResourceType::WOOD:  return "WOOD";
        case ResourceType::IRON_ORE: return "IRON_ORE";
        case ResourceType::SILVER_ORE: return "SILVER_ORE";
        case ResourceType::SILVER: return "SILVER";
        case ResourceType::GOLD_ORE: return "GOLD_ORE";
        case ResourceType::GOLD: return "GOLD";
        case ResourceType::COAL: return "COAL";
        case ResourceType::STONE: return "STONE";
        case ResourceType::IRON: return "IRON";
        case ResourceType::PLANKS: return "PLANKS";
        case ResourceType::LEATHER: return "LEATHER";
        case ResourceType::MEAT: return "MEAT";
        case ResourceType::WHEAT: return "WHEAT";
        case ResourceType::BREAD: return "BREAD";
        case ResourceType::FLOUR: return "FLOUR";
        case ResourceType::WATER: return "WATER";
        case ResourceType::BEER: return "BEER";
        case ResourceType::COINS: return "COINS";
        case ResourceType::FOOD_PROVISIONS: return "FOOD_PROVISIONS";
        case ResourceType::PAPER: return "PAPER";
        case ResourceType::TOOLS: return "TOOLS";
        case ResourceType::IRON_SWORD: return "IRON_SWORD";
        case ResourceType::STEEL_SWORD: return "STEEL_SWORD";
        case ResourceType::BOW: return "BOW";
        case ResourceType::ARROWS: return "ARROWS";
        case ResourceType::HORSE: return "HORSE";
        case ResourceType::BRONZE_SWORD: return "BRONZE_SWORD";
        case ResourceType::SPEAR: return "SPEAR";
        case ResourceType::CROSSBOW: return "CROSSBOW";
        case ResourceType::BOLTS: return "BOLTS";
        case ResourceType::WOODEN_SHIELD: return "WOODEN_SHIELD";
        case ResourceType::IRON_SHIELD: return "IRON_SHIELD";
        case ResourceType::LEATHER_ARMOR: return "LEATHER_ARMOR";
        case ResourceType::IRON_ARMOR: return "IRON_ARMOR";
        case ResourceType::TIN_ORE: return "TIN_ORE";
        case ResourceType::SAND: return "SAND";
        case ResourceType::SULFUR: return "SULFUR";
        case ResourceType::SALTPETER: return "SALTPETER";
        case ResourceType::TIN: return "TIN";
        case ResourceType::BRONZE: return "BRONZE";
        case ResourceType::COKE: return "COKE";
        case ResourceType::STEEL: return "STEEL";
        case ResourceType::GLASS: return "GLASS";
        case ResourceType::GUNPOWDER: return "GUNPOWDER";
        case ResourceType::MUSKET: return "MUSKET";
        case ResourceType::CARTRIDGE: return "CARTRIDGE";
        case ResourceType::CLAY: return "CLAY";
        case ResourceType::CATTLE: return "CATTLE";
        case ResourceType::RAW_HIDE: return "RAW_HIDE";
        case ResourceType::TALLOW: return "TALLOW";
        case ResourceType::CLOTHES: return "CLOTHES";
        case ResourceType::POTTERY: return "POTTERY";
        case ResourceType::HOUSEHOLD_GOODS: return "HOUSEHOLD_GOODS";
        case ResourceType::SOAP: return "SOAP";
        case ResourceType::INK: return "INK";
        case ResourceType::BOOKS: return "BOOKS";
        case ResourceType::COPPERWARE: return "COPPERWARE";
        case ResourceType::URBAN_GOODS: return "URBAN_GOODS";
        case ResourceType::HEMP: return "HEMP";
        case ResourceType::FIBRE: return "FIBRE";
        case ResourceType::ROPE: return "ROPE";
        case ResourceType::COPPER_VESSEL: return "COPPER_VESSEL";
        case ResourceType::COPPER_PIPE: return "COPPER_PIPE";
        case ResourceType::MECHANICAL_PARTS: return "MECHANICAL_PARTS";
        case ResourceType::HEAVY_BOW: return "HEAVY_BOW";
        case ResourceType::HEAVY_ARMOR: return "HEAVY_ARMOR";
        case ResourceType::BRICKS: return "BRICKS";
        case ResourceType::CLOTH: return "CLOTH";
        case ResourceType::BALLISTA: return "BALLISTA";
        case ResourceType::BATTERING_RAM: return "BATTERING_RAM";
        case ResourceType::CATAPULT: return "CATAPULT";
        case ResourceType::ReservedCopperSwordSlot: return "ReservedCopperSwordSlot";
        case ResourceType::ReservedWeaponSlot: return "ReservedWeaponSlot";

        default: return "Unknown";
    }
}

// Canonical data/config parser. Every named ResourceType, including retained
// legacy slots, is resolved through the same table used by debug labels. An
// unknown name returns false instead of silently becoming ResourceType::Null.
inline bool TryParseResourceType(std::string_view value, ResourceType& out)
{
    if (value == "NULL")
    {
        out = ResourceType::Null;
        return true;
    }

    constexpr ResourceType legacyTypes[] = {
        ResourceType::ReservedCopperSwordSlot,
        ResourceType::ReservedWeaponSlot,
    };
    for (ResourceType type : legacyTypes)
    {
        if (value == rt2s(type))
        {
            out = type;
            return true;
        }
    }

    for (ResourceType type : resourceTypes)
    {
        if (value == rt2s(type))
        {
            out = type;
            return true;
        }
    }
    return false;
}

// Player-facing resource name. Keep rt2s() as the stable, all-caps debug and
// serialization label; UI should use this helper instead.
inline std::string ResourceDisplayName(ResourceType type)
{
    std::string name = rt2s(type);
    bool capitalizeNext = true;
    for (char& character : name)
    {
        if (character == '_')
        {
            character = ' ';
            capitalizeNext = true;
            continue;
        }

        unsigned char value = static_cast<unsigned char>(character);
        character = static_cast<char>(capitalizeNext ? std::toupper(value) : std::tolower(value));
        capitalizeNext = false;
    }
    return name;
}

// ─── Resource categories / tags ───────────────────────────────────────────────
// Every ResourceType belongs to exactly one broad category. Categories let
// buildings and bonuses reason about *classes* of goods instead of hard-coding
// individual resource ids: a "+10% Metal production" bonus lifts every metal, a
// "+5% Sword power" bonus lifts every sword tier, a supply hub can pack "the best
// available Sword" without naming each sword resource one by one.
//
// This is the authoritative economic tag layer. The finer combat role of a
// weapon (slot, quality) still lives in Equipment.h's EquipmentCategory; the two
// are kept consistent by ResourceCategoryOf() deriving weapon categories from the
// equipment profile (see Resource.cpp).
enum class ResourceCategory : uint8_t
{
    None = 0,

    // Economy / production chains
    Ore,             // raw mined ore deposits (COPPER_ORE, IRON_ORE, TIN_ORE, …)
    Mineral,         // quarried/mined non-metal solids (COAL, STONE, SAND, SULFUR, …)
    Metal,           // refined metals (COPPER, IRON, BRONZE, STEEL, …)
    Timber,          // WOOD, PLANKS
    Textile,         // LEATHER
    Foodstuff,       // WHEAT, FLOUR, BREAD, MEAT, WATER, BEER
    Chemical,        // processed industrial goods (GLASS, GUNPOWDER, COKE)
    Tool,            // TOOLS
    Paper,           // PAPER
    Currency,        // COINS
    Mount,           // HORSE
    Livestock,       // CATTLE
    CraftedGood,     // pottery, clothes, books, copper goods, mechanisms
    SettlementSupply,// HOUSEHOLD_GOODS, URBAN_GOODS

    // Military logistics (abstract package units carried to the front)
    MilitarySupply,  // FOOD_PROVISIONS

    // Equipment (mirrors Equipment.h EquipmentCategory so gear is tagged too)
    Sword,
    Spear,
    Bow,
    Crossbow,
    Firearm,
    Ammunition,      // ARROWS, BOLTS, CARTRIDGE
    Shield,
    Armor,

    Count
};

// Authoritative category of a resource type. Weapon/armor categories are derived
// from the equipment profile so the two taxonomies never drift. Defined in
// Resource.cpp.
ResourceCategory ResourceCategoryOf(ResourceType type);

// Human-readable label for a category (debug / UI / data files).
const char* ResourceCategoryLabel(ResourceCategory category);

// True when the category is a weapon/armor/ammo class (i.e. an equipment tag).
bool IsEquipmentCategory(ResourceCategory category);

// True when the category is a primary weapon (Sword/Spear/Bow/Crossbow/Firearm).
bool IsWeaponCategory(ResourceCategory category);

// Transportable resource instance. Instances come from one process-wide,
// fixed-capacity pool; gameplay never allocates a Resource on the heap.
struct Resource : Transportable
{
    Resource() = default;
    Resource(ResourceType rtype) : type(rtype), category(ResourceCategoryOf(rtype)) {}
    Resource(const Resource& other)
        // A copied cargo value is not the same shipment. Do not duplicate
        // transport endpoints, path state, or the network-owned ShipmentId.
        : Transportable(), tag(other.tag), type(other.type), category(other.category)
    {
        ownedAllocation = false;
    }
    Resource& operator=(const Resource& other)
    {
        if (this == &other)
            return *this;
        // Assignment into a value object must also detach any previous
        // shipment identity instead of aliasing the source's in-flight cargo.
        static_cast<Transportable&>(*this) = Transportable{};
        tag = other.tag;
        type = other.type;
        category = other.category;
        ownedAllocation = false;
        return *this;
    }
    ~Resource() = default;
    static Resource* CreateOwned(ResourceType type);
    static void DestroyOwned(Resource* resource);
    std::string_view tag{"[Resource]"};
    ResourceType type{ResourceType::Null};
    // Broad economic/combat tag of this resource, derived from `type`.
    ResourceCategory category{ResourceCategory::None};
    // Kept under the historical name to avoid a flag-day API break. True now
    // means "checked out from the static pool", not heap ownership.
    bool ownedAllocation{false};
};

// Value-semantic quantity contract used by the WP-11 migration. It is kept
// independent from the legacy pointer buffer until storage and shipment
// callers have been migrated together. Failed operations are atomic.
struct ResourceQuantity
{
    ResourceType type{ResourceType::Null};
    int capacity{0};
    int amount{0};

    ResourceQuantity() = default;
    ResourceQuantity(ResourceType resourceType, int maxCapacity) noexcept
        : type(resourceType), capacity(std::max(0, maxCapacity))
    {
    }

    int AvailableCapacity() const noexcept
    {
        return capacity - amount;
    }

    bool TryAdd(int units) noexcept
    {
        if (units < 0 || amount < 0 || amount > capacity || units > capacity - amount)
            return false;
        amount += units;
        return true;
    }

    bool TryRemove(int units) noexcept
    {
        if (units < 0 || amount < 0 || amount > capacity || units > amount)
            return false;
        amount -= units;
        return true;
    }
};

// Single-resource-type FIFO/LIFO buffer used by buildings.
class ResourceBuffer
{
    public:
        ResourceBuffer(ResourceType t, int size) : type(t), bufferSize(size) {}
        ResourceBuffer() = default;
        ~ResourceBuffer();
        ResourceBuffer(const ResourceBuffer& other);
        ResourceBuffer& operator=(const ResourceBuffer& other);
        ResourceBuffer(ResourceBuffer&& other) noexcept;
        ResourceBuffer& operator=(ResourceBuffer&& other) noexcept;

        int bufferSize{0};
        ResourceType type{ResourceType::Null};

        // Adds a resource pointer when there is free capacity.
        void AddResource(Resource* res);
        // Removes and returns one resource pointer when available.
        std::pair<bool, Resource*> GetResource();
        
        // Allocates one resource instance and stores it in this buffer.
        void GenerateResource(ResourceType type);
        // Releases one stored owned resource instance.
        void FreeResource();
        // Releases all stored owned resource instances.
        void Clear();
        // Replaces stored amount with freshly generated owned resources.
        void SetStoredAmount(int amount);

        std::vector<Resource*> buffer;
};

// Facade over the process-wide static resource free-list. All ResourcePool
// instances address the same fixed storage; constructing a world or province
// never creates another multi-megabyte pool.
class ResourcePool
{
public:
    ResourcePool() = default;

    Resource* GetResource(ResourceType);
    void FreeResource(Resource*);
    std::size_t Available() const noexcept;
    static constexpr std::size_t Capacity = 1'000'000;
};

// Stored buffers reuse checked-out objects; no per-resource construction or
// destruction takes place during production, transport or loading.

#endif
