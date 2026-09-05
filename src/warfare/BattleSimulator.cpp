#include "warfare/BattleSimulator.h"

#include "data/RtsDataFile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace
{
    constexpr const char* DefaultBattleRulesPath = "assets/data/battle_rules.rtsdata";

    void AddDiagnostic(BattleRulesLoadResult& result, const std::string& path,
                       std::size_t line, std::string message)
    {
        result.diagnostics.push_back({path, line, std::move(message)});
    }

    bool ParseDouble(const RtsDataLine& tokens, std::size_t index, double& value)
    {
        return index < tokens.size() && ParseRtsDataDouble(tokens[index], value) &&
               std::isfinite(value);
    }

    bool ParseTicks(const RtsDataLine& tokens, std::size_t index, std::uint64_t& value)
    {
        int parsed = 0;
        if (index >= tokens.size() || !ParseRtsDataInt(tokens[index], parsed) || parsed <= 0)
            return false;
        value = static_cast<std::uint64_t>(parsed);
        return true;
    }

    std::uint64_t Mix(std::uint64_t value)
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    bool HasDuplicateOrInvalidIds(const std::vector<BattleUnitSnapshot>& units)
    {
        std::set<int> ids;
        for (const auto& unit : units)
        {
            if (unit.instanceId <= 0 || !std::isfinite(unit.effectiveFieldAttack) ||
                unit.effectiveFieldAttack < 0.0 || !ids.insert(unit.instanceId).second)
                return true;
        }
        return false;
    }

    double Strength(const BattleSideSnapshot& side)
    {
        double result = std::max(0.0, side.defensiveBonus);
        for (const auto& unit : side.units)
            result += unit.effectiveFieldAttack;
        return result;
    }

    int CasualtyBudget(std::size_t unitCount, double fraction, bool allowAnnihilation)
    {
        if (unitCount == 0 || fraction <= 0.0)
            return 0;
        const int count = static_cast<int>(unitCount);
        const int maximum = allowAnnihilation ? count : std::max(0, count - 1);
        const int requested = std::max(1, static_cast<int>(std::ceil(count * fraction)));
        return std::clamp(requested, 0, maximum);
    }

    std::vector<int> SelectLosses(const std::vector<BattleUnitSnapshot>& units,
                                  int budget, BattleSeed seed, std::uint64_t sideSalt)
    {
        struct Candidate
        {
            std::uint64_t key{0};
            int instanceId{0};
        };
        std::vector<Candidate> candidates;
        candidates.reserve(units.size());
        for (const auto& unit : units)
        {
            const std::uint64_t key = Mix(seed ^ sideSalt ^
                                           static_cast<std::uint64_t>(unit.instanceId));
            candidates.push_back({key, unit.instanceId});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& first,
                                                           const Candidate& second)
        {
            if (first.key != second.key)
                return first.key < second.key;
            return first.instanceId < second.instanceId;
        });
        budget = std::clamp(budget, 0, static_cast<int>(candidates.size()));
        std::vector<int> losses;
        losses.reserve(static_cast<std::size_t>(budget));
        for (int index = 0; index < budget; ++index)
            losses.push_back(candidates[static_cast<std::size_t>(index)].instanceId);
        std::sort(losses.begin(), losses.end());
        return losses;
    }
}

BattleOutcome BattleSimulator::Resolve(const BattleSideSnapshot& attacker,
                                       const BattleSideSnapshot& defender,
                                       const BattleRules& rules, BattleSeed seed)
{
    BattleOutcome result;
    if (attacker.ownerId == InvalidPlayerId || defender.ownerId == InvalidPlayerId ||
        attacker.ownerId == defender.ownerId || HasDuplicateOrInvalidIds(attacker.units) ||
        HasDuplicateOrInvalidIds(defender.units) ||
        !std::isfinite(attacker.defensiveBonus) || !std::isfinite(defender.defensiveBonus) ||
        attacker.defensiveBonus < 0.0 || defender.defensiveBonus < 0.0 ||
        !std::isfinite(rules.baseLossFraction) || rules.baseLossFraction < 0.0 ||
        !std::isfinite(rules.casualtyCapFraction) || rules.casualtyCapFraction < 0.0 ||
        rules.casualtyCapFraction >= 1.0 || !std::isfinite(rules.drawBand) ||
        rules.drawBand < 0.0 || rules.drawBand > 1.0 ||
        !std::isfinite(rules.crushingRatio) || rules.crushingRatio < 1.0 ||
        !std::isfinite(rules.lootFraction) || rules.lootFraction < 0.0 ||
        rules.lootFraction > 1.0 || rules.durationTicks == 0)
        return result;

    if (attacker.units.empty() && defender.units.empty())
        return result;

    result.valid = true;
    result.attackerStrength = Strength(attacker);
    result.defenderStrength = Strength(defender);
    result.durationTicks = rules.durationTicks;

    if (result.attackerStrength == 0.0 && result.defenderStrength == 0.0)
    {
        result.winner = BattleWinner::Draw;
    }
    else if (attacker.units.empty() ||
             (result.defenderStrength > result.attackerStrength &&
              result.attackerStrength == 0.0))
    {
        result.winner = BattleWinner::Defender;
    }
    else if (defender.units.empty() ||
             (result.attackerStrength > result.defenderStrength &&
              result.defenderStrength == 0.0))
    {
        result.winner = BattleWinner::Attacker;
    }
    else
    {
        const double maximum = std::max(result.attackerStrength, result.defenderStrength);
        const double difference = std::abs(result.attackerStrength - result.defenderStrength);
        if (maximum == 0.0 || difference / maximum <= rules.drawBand)
            result.winner = BattleWinner::Draw;
        else
            result.winner = result.attackerStrength > result.defenderStrength
                ? BattleWinner::Attacker : BattleWinner::Defender;
    }

    const double cap = std::min(rules.casualtyCapFraction,
                                std::nextafter(1.0, 0.0));
    const double baseLoss = std::min(rules.baseLossFraction, cap);
    const double attackerRatio = result.defenderStrength > 0.0
        ? result.attackerStrength / result.defenderStrength
        : std::numeric_limits<double>::infinity();
    const double defenderRatio = result.attackerStrength > 0.0
        ? result.defenderStrength / result.attackerStrength
        : std::numeric_limits<double>::infinity();
    const bool attackerCrushing = result.winner == BattleWinner::Attacker &&
                                  attackerRatio >= rules.crushingRatio;
    const bool defenderCrushing = result.winner == BattleWinner::Defender &&
                                  defenderRatio >= rules.crushingRatio;
    result.crushingVictory = attackerCrushing || defenderCrushing;

    auto loserFraction = [&](double ratio)
    {
        if (!std::isfinite(ratio))
            return cap;
        return std::min(cap, baseLoss * std::max(1.0, ratio));
    };
    auto winnerFraction = [&](double ratio)
    {
        if (!std::isfinite(ratio) || ratio <= 1.0)
            return baseLoss;
        return std::min(cap, baseLoss / ratio);
    };

    switch (result.winner)
    {
        case BattleWinner::Attacker:
            result.attackerCasualtyBudget = CasualtyBudget(
                attacker.units.size(), winnerFraction(attackerRatio), false);
            result.defenderCasualtyBudget = CasualtyBudget(
                defender.units.size(), loserFraction(attackerRatio), attackerCrushing);
            result.lootValue = result.defenderStrength * rules.lootFraction;
            break;
        case BattleWinner::Defender:
            result.attackerCasualtyBudget = CasualtyBudget(
                attacker.units.size(), loserFraction(defenderRatio), defenderCrushing);
            result.defenderCasualtyBudget = CasualtyBudget(
                defender.units.size(), winnerFraction(defenderRatio), false);
            result.lootValue = result.attackerStrength * rules.lootFraction;
            break;
        case BattleWinner::Draw:
            result.attackerCasualtyBudget = CasualtyBudget(
                attacker.units.size(), baseLoss, false);
            result.defenderCasualtyBudget = CasualtyBudget(
                defender.units.size(), baseLoss, false);
            break;
        case BattleWinner::Invalid:
            result.valid = false;
            break;
    }

    result.attackerLostUnitIds = SelectLosses(
        attacker.units, result.attackerCasualtyBudget, seed, 0xA77ACCEFull);
    result.defenderLostUnitIds = SelectLosses(
        defender.units, result.defenderCasualtyBudget, seed, 0xD3F3AD3Full);
    return result;
}

BattleRulesLoadResult LoadBattleRulesFromFile(const std::string& path)
{
    BattleRulesLoadResult result;
    const RtsDataDocument document = ReadRtsDataDocument(path);
    for (const auto& diagnostic : document.diagnostics)
        result.diagnostics.push_back({diagnostic.path, diagnostic.line, diagnostic.message});
    if (!document.IsValid())
        return result;

    bool inBlock = false;
    bool blockValid = true;
    std::set<std::string> seen;
    for (std::size_t index = 0; index < document.lines.size(); ++index)
    {
        const auto& tokens = document.lines[index];
        const std::size_t line = document.sourceLines[index];
        if (tokens[0] == "battle_rules")
        {
            if (inBlock || tokens.size() != 2 || tokens[1].empty())
            {
                AddDiagnostic(result, path, line, "battle_rules requires one unique ID");
                blockValid = false;
                continue;
            }
            inBlock = true;
            continue;
        }
        if (!inBlock)
        {
            AddDiagnostic(result, path, line, "token outside battle_rules block");
            continue;
        }
        if (tokens[0] == "end")
        {
            if (tokens.size() != 1)
            {
                AddDiagnostic(result, path, line, "end does not accept arguments");
                blockValid = false;
            }
            inBlock = false;
            continue;
        }
        if (tokens.size() != 2 || !seen.insert(tokens[0]).second)
        {
            AddDiagnostic(result, path, line, "field requires one unique value");
            blockValid = false;
            continue;
        }
        double value = 0.0;
        if (tokens[0] == "base_loss_fraction")
        {
            if (!ParseDouble(tokens, 1, value) || value < 0.0 || value >= 1.0) blockValid = false;
            else result.rules.baseLossFraction = value;
        }
        else if (tokens[0] == "casualty_cap_fraction")
        {
            if (!ParseDouble(tokens, 1, value) || value < 0.0 || value >= 1.0) blockValid = false;
            else result.rules.casualtyCapFraction = value;
        }
        else if (tokens[0] == "draw_band")
        {
            if (!ParseDouble(tokens, 1, value) || value < 0.0 || value > 1.0) blockValid = false;
            else result.rules.drawBand = value;
        }
        else if (tokens[0] == "crushing_ratio")
        {
            if (!ParseDouble(tokens, 1, value) || value < 1.0) blockValid = false;
            else result.rules.crushingRatio = value;
        }
        else if (tokens[0] == "loot_fraction")
        {
            if (!ParseDouble(tokens, 1, value) || value < 0.0 || value > 1.0) blockValid = false;
            else result.rules.lootFraction = value;
        }
        else if (tokens[0] == "duration_ticks")
        {
            if (!ParseTicks(tokens, 1, result.rules.durationTicks)) blockValid = false;
        }
        else
        {
            AddDiagnostic(result, path, line, "unknown battle rule field");
            blockValid = false;
        }
        if (!blockValid)
            AddDiagnostic(result, path, line, "invalid battle rule value");
    }
    if (inBlock)
    {
        AddDiagnostic(result, path, document.sourceLines.back(),
                      "battle_rules block is missing end");
        blockValid = false;
    }
    if (!blockValid || result.diagnostics.size() != 0)
        return result;
    return result;
}

const BattleRules& GetBattleRules()
{
    static const BattleRulesLoadResult loaded =
        LoadBattleRulesFromFile(DefaultBattleRulesPath);
    static const BattleRules fallback{};
    return loaded.IsValid() ? loaded.rules : fallback;
}
