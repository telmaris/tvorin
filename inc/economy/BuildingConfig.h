#ifndef BUILDING_CONFIG_H
#define BUILDING_CONFIG_H

#include "economy/BalanceModifiers.h"
#include "economy/Building.h"

struct ResourceBufferDefinition
{
    ResourceType type{ResourceType::Null};
    int capacity{0};
    int initialAmount{0};
};

struct ProductionDefinition
{
    double cycleTime{0.0};
    std::vector<ResourceAmountDefinition> inputs;
    std::vector<ResourceAmountDefinition> outputs;
    std::vector<ResourceBufferDefinition> inputBuffers;
    std::vector<ResourceBufferDefinition> outputBuffers;
    int workerCapacity{5};
};

struct TerrainProductionDefinition
{
    TileType tileType{TileType::GRASS};
    ProductionDefinition production;
};

struct ProductionRecipeDefinition
{
    std::string name;
    ProductionDefinition production;
    // A building may exist before all of its production methods do.  These
    // gates keep late products data-driven instead of requiring a second
    // building type for every historical refinement.
    std::vector<std::string> requiredTechnologies;
    std::vector<std::string> requiredFocuses;
};

struct RoadDefinition
{
    int upgradeLevel{1};
    int maxCapacity{5};
    double speedModifier{1.0};
};

// One upgrade tier's cost + effect, generic across any future upgradeable
// building type (today only Road registers UpgradeComponent, see
// Road::Road). `modifiers` are stored with a placeholder Global() scope —
// Player::ApplyUpgradeLevelModifiers rewrites the scope to this specific
// building instance (BalanceModifierScope::BuildingAtPosition) when a level
// is reached, so the same data works for any building without change.
struct BuildingUpgradeLevelDefinition
{
    int level{0};
    std::vector<ResourceAmountDefinition> cost;
    double buildTime{0.0};
    std::vector<BalanceModifier> modifiers;
    // Optional absolute settlement stats for this level. They are kept out
    // of BalanceModifierSet deliberately: a Town/City can temporarily fall
    // back to a lower settlement tier when its local supplies run out.
    std::optional<int> populationCap;
    std::optional<double> manpowerRate;
};

struct BuildingDefinition;

// Shared validation/lookup for UI previews and authoritative commands. A
// building's maxLevel is only a hint; the exact next record must exist and be
// well-formed before an upgrade can be presented or started.
bool IsValidUpgradeLevelDefinition(const BuildingUpgradeLevelDefinition& definition);
const BuildingUpgradeLevelDefinition* FindUpgradeLevelDefinition(
    const BuildingDefinition& definition, int level);

struct VillageDefinition
{
    double manpowerRate{5.0};
    int populationCap{1000};
    double upkeepInterval{60.0};
    double foodPackageUpkeep{1.0};
};

// TD(etap-6): Headquarters HP/defense + conquest-spoils parameters.
struct HqDefinition
{
    double maxHp{500.0};
    double hardDefense{0.0};
    double thornsDamage{0.0};
    double thornsInterval{3.0};
    double captureStockFraction{0.4};
    double conquestRampDuration{600.0};
};

// TD(etap-7): DefenseTower combat/ammo/crew parameters. One class handles
// every tower tier (data-driven, like BattleUnit/UnitDefinition) — a second
// tier just needs its own BuildingType + BuildingDefinition entry, not a new
// C++ class. Ammo buffer CAPACITY is data-driven via the existing generic
// `storageBuffers` mechanism (a normal "storage <ammoResource> <cap> <init>"
// line, applied by the existing ApplyStorageDefinition) — not duplicated here.
struct TowerDefinition
{
    double damage{5.0};
    double range{6.0};
    double attackSpeed{1.0};
    ResourceType ammoResource{ResourceType::ARROWS};
    int ammoPerShot{1};
    int workerCapacity{2};
};

// Soft, data-driven production district used by the AI placement scorer.
// Terrain and build validity remain hard constraints; this category only
// makes related buildings prefer the same neighborhood when space permits.
enum class BuildingPlacementCategory
{
    None,
    Core,
    Wood,
    Metal,
    Food,
    Military,
    Knowledge,
    Construction,
    Infrastructure
};

struct BuildingDefinition
{
    BuildingType type{BuildingType::Building};
    std::string name;
    std::string tag;
    std::string texturePath;
    std::string buildCostText{"Cost TBD"};
    std::vector<ResourceAmountDefinition> buildCosts;
    Vec2i footprint{1, 1};
    int textureId{0};
    double buildTime{0.0};
    double transportTime{0.0};
    ProductionDefinition production;
    std::vector<TerrainProductionDefinition> terrainProductions;
    std::vector<ResourceBufferDefinition> storageBuffers;
    RoadDefinition road;
    VillageDefinition village;
    std::vector<std::string> requiredTechnologies;
    std::vector<std::string> requiredFocuses;
    std::vector<ProductionRecipeDefinition> recipes;
    HqDefinition hq;
    TowerDefinition tower;
    // Appended last: MakeDefaultDefinitions() below uses positional aggregate
    // init for every entry, none of which set this — a trailing field is
    // safe to add without touching those (defaults to empty).
    std::vector<BuildingUpgradeLevelDefinition> upgradeLevels;
    BuildingPlacementCategory placementCategory{BuildingPlacementCategory::None};
    // Loading/unloading cadence at the source edge. Kept separate from road
    // traversal time so logistics bonuses may tune either independently.
    double dispatchDelay{0.3};
};

// Returns all configured building definitions.
const std::vector<BuildingDefinition>& GetBuildingDefinitions();

// Returns the largest configured footprint overhang used by render culling.
int GetMaximumBuildingFootprintOverhang();

// Loads building definitions from a specific data file.
std::vector<BuildingDefinition> LoadBuildingDefinitionsFromFile(const std::string& path);

// Returns the definition matching one building type.
const BuildingDefinition& GetBuildingDefinition(BuildingType type);

// Returns building types shown in the standard build panel.
const std::vector<BuildingType>& GetBuildableBuildingTypes();

// Returns building types shown in the road build panel.
const std::vector<BuildingType>& GetBuildableRoadTypes();

// Finds a terrain-specific production definition for a building.
const TerrainProductionDefinition* FindTerrainProductionDefinition(BuildingType type, TileType tileType);

// Applies common definition fields to a building instance.
void ApplyBuildingDefinition(Building& building, const BuildingDefinition& definition);

// Applies recipe and buffer data to a building's production/worker components.
void ApplyProductionDefinition(Building& building, const ProductionDefinition& definition);
void ApplyProductionRecipes(Building& building, const BuildingDefinition& definition);

// Applies storage buffer data to a building's storage component.
void ApplyStorageDefinition(Building& building, const BuildingDefinition& definition);

// Applies HQ HP/defense/conquest data to a building's HqComponent.
void ApplyHqDefinition(Building& building, const BuildingDefinition& definition);

// Applies tower combat/ammo/crew data to a building's TowerCombatComponent
// (+ ammo buffer on its StorageComponent, + crew capacity on its WorkerComponent).
void ApplyTowerDefinition(Building& building, const BuildingDefinition& definition);

#endif
