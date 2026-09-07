#ifndef VILLAGE_SUPPLY_RULE_H
#define VILLAGE_SUPPLY_RULE_H

#include "data/Resource.h"

// One population supply stream.  A package covers a data-defined number of
// residents and is requested/consumed at the rule's base cadence. Balance
// modifiers can still tune the resulting package rate per resource.
struct VillageSupplyRuleDefinition
{
    ResourceType resource{ResourceType::Null};
    int packageAmount{1};
    double residentsPerPackage{1.0};
    double intervalSeconds{60.0};
};

#endif
