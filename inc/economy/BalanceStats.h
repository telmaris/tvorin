#ifndef BALANCE_STATS_H
#define BALANCE_STATS_H

// Stable identifiers for gameplay parameters affected by technologies and bonuses.
// Stable balance identifiers retained by the peaceful pilot and the future
// province layer. Province-defense values are intentionally latent until the
// global-map defense rules are introduced.
enum class BalanceStat
{
    BuildTime,
    BuildCost,
    ProductionCycleTime,
    ProductionOutputAmount,
    WorkerCapacity,
    TransportTime,
    RoadCapacity,
    RoadSpeed,
    ManpowerRate,
    PopulationCap,
    BuilderAmount,     // number of concurrent construction builders a player commands

    // BattleUnit stats. Filterable per unit definition via
    // BalanceModifier::unitDefId (see BalanceModifiers.h).
    UnitHp,
    UnitFieldAttack,
    UnitSiegePower,
    UnitArmor,
    UnitMoveSpeed,
    UnitAttackSpeed,
    // Recruiting a unit has a non-zero time and manpower cost.
    UnitRecruitTime,
    UnitRecruitManpowerCost,

    ProvinceFortification,
    ProvinceDefense,
    ProvinceCounterattack,
    // Future strategic parameter retained without an active conquest system.
    ConquestSpoilsFraction,
    ProvinceDefensePower,
    ProvinceDefenseCoverage,
    ProvinceDefenseReadiness,
    ProvinceDefenseSupplyUse,

    // Time between reserving one resource at a source building and releasing
    // it onto the first road tile. Appended to preserve existing enum values.
    TransportDispatchDelay,

    // Relative cadence at which a Village consumes one upkeep package.
    // Resolve with a ResourceType context so FOOD_PROVISIONS,
    // HOUSEHOLD_GOODS and URBAN_GOODS can be balanced independently.
    // Lower is better: effective interval = base interval / consumption.
    VillageSupplyConsumption,

    // Stage 4 append-only province, route and army contracts. These names are
    // intentionally distinct: one gameplay result must never be modified by
    // two legacy aliases that happen to describe the same concept.
    RouteTravelSpeed,
    RouteIncidentChance,
    TradeExchangeRate,
    TradeScoreGain,
    BattleAttack,
    BattleCasualtyRate,
    BattleDuration,
    GarrisonCapacity,
    GarrisonFoodUpkeep,
    RaidBuildingDestructionChance,
    RaidStockLossFraction,
    ProvinceEventChance,
    ProvinceEventWeight,
    ProvinceEventDuration,
    ColonizationDuration
};

#endif
