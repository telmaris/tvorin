#ifndef WORLD_IDS_H
#define WORLD_IDS_H

#include <cstdint>

// IDs crossing campaign aggregates are deliberately distinct from local tile
// and building IDs. They are serialized as fixed-width integers and never
// encode one another into an existing positional field.
using PlayerId = std::int32_t;
using ProvinceId = std::uint32_t;
using ProvinceConnectionId = std::uint32_t;
using WorldJourneyId = std::uint64_t;
using BattleId = std::uint64_t;
using WorldEventInstanceId = std::uint64_t;

constexpr PlayerId InvalidPlayerId = static_cast<PlayerId>(-1);
constexpr ProvinceId InvalidProvinceId = 0;
constexpr ProvinceConnectionId InvalidProvinceConnectionId = 0;
constexpr WorldJourneyId InvalidWorldJourneyId = 0;
constexpr BattleId InvalidBattleId = 0;
constexpr WorldEventInstanceId InvalidWorldEventInstanceId = 0;

#endif
