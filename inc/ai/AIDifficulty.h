#ifndef AI_DIFFICULTY_H
#define AI_DIFFICULTY_H

#include <array>
#include <cstddef>
#include <string_view>

// Difficulty changes the AI's action cadence, not its starting state or the
// quality of its decisions. Every level uses the same deterministic utility
// model and the same initial economy as a human player.
enum class AIDifficulty : int
{
    Primitive = 0,
    Easy = 1,
    Normal = 2,
    Hard = 3
};

struct AIDifficultyProfile
{
    std::string_view name;
    double actionIntervalSeconds;
};

namespace AIDifficultyProfiles
{
    inline constexpr std::array Profiles{
        AIDifficultyProfile{"Primitive", 10.0},
        AIDifficultyProfile{"Easy", 6.0},
        AIDifficultyProfile{"Normal", 3.0},
        AIDifficultyProfile{"Hard", 1.0},
    };
}

inline constexpr const AIDifficultyProfile& GetAIDifficultyProfile(AIDifficulty difficulty)
{
    return AIDifficultyProfiles::Profiles[static_cast<int>(difficulty)];
}

inline constexpr const AIDifficultyProfile& GetAIDifficultyProfile(int difficulty)
{
    if (difficulty < static_cast<int>(AIDifficulty::Primitive))
        difficulty = static_cast<int>(AIDifficulty::Primitive);
    if (difficulty > static_cast<int>(AIDifficulty::Hard))
        difficulty = static_cast<int>(AIDifficulty::Hard);
    return AIDifficultyProfiles::Profiles[static_cast<std::size_t>(difficulty)];
}

#endif
