#ifndef BALANCE_MODIFIERS_H
#define BALANCE_MODIFIERS_H

#include "economy/BalanceStats.h"
#include "economy/Building.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

enum class BalanceModifierScopeType
{
    GlobalPlayer,
    Building,
    Area
};

struct BalanceModifierScope
{
    BalanceModifierScopeType type{BalanceModifierScopeType::GlobalPlayer};
    std::optional<int> buildingId;
    std::optional<int> positionId;
    std::optional<Vec2i> center;
    int radius{0};
    // Local building ids, tile ids and coordinates are only unique inside a
    // province. A local scope must carry the map identity as well; otherwise
    // an upgrade in one province can affect the same tile/building id in
    // another province owned by the same player.
    std::optional<ProvinceId> provinceId;

    static BalanceModifierScope Global()
    {
        return {};
    }

    static BalanceModifierScope Building(int targetBuildingId)
    {
        BalanceModifierScope scope;
        scope.type = BalanceModifierScopeType::Building;
        scope.buildingId = targetBuildingId;
        return scope;
    }

    static BalanceModifierScope Building(ProvinceId targetProvinceId, int targetBuildingId)
    {
        BalanceModifierScope scope = Building(targetBuildingId);
        scope.provinceId = targetProvinceId;
        return scope;
    }

    static BalanceModifierScope BuildingAtPosition(int targetPositionId)
    {
        BalanceModifierScope scope;
        scope.type = BalanceModifierScopeType::Building;
        scope.positionId = targetPositionId;
        return scope;
    }

    static BalanceModifierScope BuildingAtPosition(ProvinceId targetProvinceId,
                                                   int targetPositionId)
    {
        BalanceModifierScope scope = BuildingAtPosition(targetPositionId);
        scope.provinceId = targetProvinceId;
        return scope;
    }

    static BalanceModifierScope Area(Vec2i areaCenter, int areaRadius)
    {
        BalanceModifierScope scope;
        scope.type = BalanceModifierScopeType::Area;
        scope.center = areaCenter;
        scope.radius = std::max(0, areaRadius);
        return scope;
    }

    static BalanceModifierScope Area(ProvinceId targetProvinceId, Vec2i areaCenter,
                                     int areaRadius)
    {
        BalanceModifierScope scope = Area(areaCenter, areaRadius);
        scope.provinceId = targetProvinceId;
        return scope;
    }
};

struct BalanceModifierContext
{
    BalanceStat stat{BalanceStat::BuildTime};
    BuildingType buildingType{BuildingType::Building};
    ResourceType resourceType{ResourceType::Null};
    std::optional<Vec2i> position;
    std::optional<int> buildingId;
    std::optional<int> positionId;
    // Empty for non-unit modifier queries.
    std::string unitDefId;
    ProvinceId provinceId{InvalidProvinceId};
};

struct BalanceModifier
{
    BalanceStat stat{BalanceStat::BuildTime};
    double additive{0.0};
    double multiplier{1.0};
    BalanceModifierScope scope;
    std::optional<BuildingType> buildingType;
    std::optional<ResourceType> resourceType;
    std::string source;
    // Category-scoped bonus: applies to every resource of this category (e.g. a
    // "+10% Metal production" / "+5% Sword power" modifier). Matched against the
    // category of context.resourceType via ResourceCategoryOf(). Placed last so
    // existing positional BalanceModifier{...} aggregate initializers still work.
    std::optional<ResourceCategory> resourceCategory;
    // Restricts a unit modifier to one definition. Unset applies to every unit.
    std::optional<std::string> unitDefId;

    bool AppliesTo(const BalanceModifierContext& context) const
    {
        if (stat != context.stat)
            return false;
        if (buildingType.has_value() && buildingType.value() != context.buildingType)
            return false;
        if (resourceType.has_value() && resourceType.value() != context.resourceType)
            return false;
        if (resourceCategory.has_value() &&
            (context.resourceType == ResourceType::Null ||
             ResourceCategoryOf(context.resourceType) != resourceCategory.value()))
            return false;
        if (unitDefId.has_value() && unitDefId.value() != context.unitDefId)
            return false;
        return AppliesToScope(context);
    }

    bool AppliesToScope(const BalanceModifierContext& context) const
    {
        if (scope.provinceId.has_value() &&
            scope.provinceId.value() != context.provinceId)
            return false;

        switch (scope.type)
        {
            case BalanceModifierScopeType::GlobalPlayer:
                return true;

            case BalanceModifierScopeType::Building:
                if (scope.buildingId.has_value())
                    return context.buildingId.has_value() && context.buildingId.value() == scope.buildingId.value();
                if (scope.positionId.has_value())
                    return context.positionId.has_value() && context.positionId.value() == scope.positionId.value();
                return false;

            case BalanceModifierScopeType::Area:
            {
                if (!scope.center.has_value() || !context.position.has_value())
                    return false;

                int dx = context.position->x - scope.center->x;
                int dy = context.position->y - scope.center->y;
                return dx * dx + dy * dy <= scope.radius * scope.radius;
            }
        }

        return false;
    }
};

class IBalanceModifierSource
{
public:
    virtual ~IBalanceModifierSource() = default;
    virtual void CollectModifiers(std::vector<BalanceModifier>& out) const = 0;
};

class BalanceModifierSet
{
public:
    void AddModifier(BalanceModifier modifier)
    {
        modifiers.push_back(std::move(modifier));
    }

    void ClearSource(const std::string& source)
    {
        modifiers.erase(
            std::remove_if(modifiers.begin(), modifiers.end(), [&](const BalanceModifier& modifier)
            {
                return modifier.source == source;
            }),
            modifiers.end());
    }

    // Removes all modifiers emitted by a group of sources.
    void ClearSourcePrefix(const std::string& sourcePrefix)
    {
        modifiers.erase(
            std::remove_if(modifiers.begin(), modifiers.end(), [&](const BalanceModifier& modifier)
            {
                return modifier.source.rfind(sourcePrefix, 0) == 0;
            }),
            modifiers.end());
    }

    void Clear()
    {
        modifiers.clear();
    }

    double ModifyDouble(double base, const BalanceModifierContext& context) const
    {
        double additive = 0.0;
        double multiplier = 1.0;
        for (const auto& modifier : modifiers)
        {
            if (!modifier.AppliesTo(context))
                continue;

            additive += modifier.additive;
            multiplier *= modifier.multiplier;
        }

        return std::max(0.0, (base + additive) * multiplier);
    }

    int ModifyInt(int base, const BalanceModifierContext& context, int minimum = 0) const
    {
        return std::max(minimum, static_cast<int>(std::round(ModifyDouble(static_cast<double>(base), context))));
    }

    const std::vector<BalanceModifier>& GetModifiers() const
    {
        return modifiers;
    }

private:
    std::vector<BalanceModifier> modifiers;
};

#endif
