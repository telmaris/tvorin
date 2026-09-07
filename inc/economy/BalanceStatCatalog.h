#ifndef BALANCE_STAT_CATALOG_H
#define BALANCE_STAT_CATALOG_H

#include "economy/BalanceStats.h"

#include <optional>
#include <string_view>
#include <vector>

// The single data contract for balance-stat identifiers.  Runtime parsers,
// editors and UI presentation all consume this catalog so a newly added stat
// cannot silently disappear from one of those surfaces.
struct BalanceStatCatalogEntry
{
    BalanceStat value;
    const char* serializedName;
    const char* displayName;
    bool lowerValueIsBetter;
};

const std::vector<BalanceStatCatalogEntry>& GetBalanceStatCatalog();
std::optional<BalanceStat> TryParseBalanceStat(std::string_view serializedName);
const BalanceStatCatalogEntry* FindBalanceStatCatalogEntry(BalanceStat value);

#endif
