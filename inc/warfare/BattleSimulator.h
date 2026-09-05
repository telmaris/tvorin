#ifndef BATTLE_SIMULATOR_H
#define BATTLE_SIMULATOR_H

#include "world/WorldIds.h"

#include <cstdint>
#include <string>
#include <vector>

using BattleSeed = std::uint64_t;

// A battle receives effective values, not live BattleUnit/Player pointers.
// Technology, focus and province modifiers are therefore snapshotted by the
// authority when the battle starts.
struct BattleUnitSnapshot
{
    int instanceId{0};
    std::string unitDefId;
    double effectiveFieldAttack{0.0};
};

struct BattleSideSnapshot
{
    PlayerId ownerId{InvalidPlayerId};
    std::vector<BattleUnitSnapshot> units;
    double defensiveBonus{0.0};
};

struct BattleRules
{
    double baseLossFraction{0.20};
    double casualtyCapFraction{0.85};
    double drawBand{0.10};
    double crushingRatio{3.0};
    double lootFraction{0.10};
    std::uint64_t durationTicks{100};
};

enum class BattleWinner : std::uint8_t
{
    Invalid,
    Draw,
    Attacker,
    Defender
};

struct BattleOutcome
{
    bool valid{false};
    BattleWinner winner{BattleWinner::Invalid};
    double attackerStrength{0.0};
    double defenderStrength{0.0};
    int attackerCasualtyBudget{0};
    int defenderCasualtyBudget{0};
    std::vector<int> attackerLostUnitIds;
    std::vector<int> defenderLostUnitIds;
    double lootValue{0.0};
    bool crushingVictory{false};
    std::uint64_t durationTicks{0};
};

class BattleSimulator
{
public:
    static BattleOutcome Resolve(const BattleSideSnapshot& attacker,
                                 const BattleSideSnapshot& defender,
                                 const BattleRules& rules, BattleSeed seed);
};

struct BattleRulesDiagnostic
{
    std::string path;
    std::size_t line{0};
    std::string message;
};

struct BattleRulesLoadResult
{
    BattleRules rules;
    std::vector<BattleRulesDiagnostic> diagnostics;
    bool IsValid() const { return diagnostics.empty(); }
};

BattleRulesLoadResult LoadBattleRulesFromFile(const std::string& path);
const BattleRules& GetBattleRules();

#endif
