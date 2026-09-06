#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "ui/ControlIcons.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "economy/BalanceStatDisplay.h"
#include "core/PersistenceLimits.h"
#include "warfare/UnitDefinition.h"
#include "world/Expedition.h"
#include "world/ProvinceDefinition.h"
#include "world/WorldEventDefinition.h"
#include "world/ColonizationDefinition.h"
#include "world/ColonizationService.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>

namespace
{
    constexpr float GlobalFogMaskScale = 0.5f;
    constexpr float GlobalMapNodeHitRadius = 52.0f;
    constexpr std::size_t CampaignJournalMaxVisibleEffects = 3;

    bool IsActiveJourneyStatus(WorldJourneyStatus status)
    {
        return status == WorldJourneyStatus::Planned ||
               status == WorldJourneyStatus::InTransit ||
               status == WorldJourneyStatus::AwaitingUnload;
    }

    float ScoutJourneyProgress(const JourneyStatusView& journey,
                               std::uint64_t currentTick)
    {
        if (journey.status == WorldJourneyStatus::Succeeded)
            return 1.0f;
        if (!IsActiveJourneyStatus(journey.status) || journey.totalLegs == 0)
            return 0.0f;

        const auto* definition = FindExpeditionDefinition("scout");
        const std::uint64_t nominalLegDuration = definition == nullptr
            ? 1u
            : std::max<std::uint64_t>(
                1u, (definition->durationTicks + journey.totalLegs - 1) /
                    journey.totalLegs);
        const std::uint64_t legStart = journey.currentLeg == 0
            ? journey.startTick
            : journey.etaTick > nominalLegDuration
                ? journey.etaTick - nominalLegDuration : journey.startTick;
        const std::uint64_t legDuration = std::max<std::uint64_t>(
            1u, journey.etaTick > legStart ? journey.etaTick - legStart : 1u);
        const float legProgress = std::clamp(
            static_cast<float>(currentTick > legStart ? currentTick - legStart : 0u) /
                static_cast<float>(legDuration),
            0.0f, 1.0f);
        return std::clamp((static_cast<float>(journey.currentLeg) + legProgress) /
                              static_cast<float>(journey.totalLegs),
                          0.0f, 1.0f);
    }

    float JourneyProgress(const JourneyStatusView& journey, std::uint64_t currentTick)
    {
        if (journey.status == WorldJourneyStatus::Succeeded)
            return 1.0f;
        if (!IsActiveJourneyStatus(journey.status) || journey.etaTick <= journey.startTick)
            return 0.0f;
        const std::uint64_t totalTicks = journey.etaTick - journey.startTick;
        const std::uint64_t elapsedTicks = currentTick > journey.startTick
            ? std::min(totalTicks, currentTick - journey.startTick) : 0;
        return std::clamp(static_cast<float>(elapsedTicks) /
                              static_cast<float>(totalTicks), 0.0f, 1.0f);
    }

    const char* JourneyOperationLabel(WorldJourneyKind kind)
    {
        switch (kind)
        {
            case WorldJourneyKind::Scout: return "Scouting expedition";
            case WorldJourneyKind::Trade: return "Trade expedition";
            case WorldJourneyKind::Attack: return "Army on the march";
            case WorldJourneyKind::Colonization: return "Colonists traveling";
            case WorldJourneyKind::ResourceTransfer: return "Resource convoy";
            case WorldJourneyKind::ArmyTransfer: return "Army transfer";
            case WorldJourneyKind::Unknown: break;
        }
        return "Operation in progress";
    }

    Vector2 NodeScreenPosition(const GlobalMapNodeView& node, Vector2 origin, float scale)
    {
        return {origin.x + static_cast<float>(node.layoutPosition.x) * scale,
                origin.y + static_cast<float>(node.layoutPosition.y) * scale};
    }

    const GlobalMapNodeView* FindNode(const GlobalMapView& view, ProvinceId id)
    {
        for (const auto& node : view.nodes)
            if (node.id == id)
                return &node;
        return nullptr;
    }

    Color NodeColor(ProvinceKnowledgeLevel knowledge)
    {
        switch (knowledge)
        {
            case ProvinceKnowledgeLevel::Owned: return Color{236, 186, 74, 255};
            case ProvinceKnowledgeLevel::Scouted: return Color{112, 205, 224, 255};
            case ProvinceKnowledgeLevel::ReachableUnknown: return Color{155, 162, 178, 255};
            case ProvinceKnowledgeLevel::Hidden: break;
        }
        return Color{82, 92, 108, 255};
    }

    float SegmentDistanceSquared(Vector2 point, Vector2 first, Vector2 second)
    {
        const Vector2 direction{second.x - first.x, second.y - first.y};
        const Vector2 offset{point.x - first.x, point.y - first.y};
        const float lengthSquared = direction.x * direction.x + direction.y * direction.y;
        if (lengthSquared <= 0.0001f)
            return offset.x * offset.x + offset.y * offset.y;
        const float projection = std::clamp(
            (offset.x * direction.x + offset.y * direction.y) / lengthSquared,
            0.0f, 1.0f);
        const Vector2 closest{first.x + direction.x * projection,
                              first.y + direction.y * projection};
        const float dx = point.x - closest.x;
        const float dy = point.y - closest.y;
        return dx * dx + dy * dy;
    }

    const char* ProvinceKindLabel(std::optional<ProvinceKind> kind)
    {
        if (!kind.has_value())
            return "Unknown";
        switch (*kind)
        {
            case ProvinceKind::Buildable: return "Buildable";
            case ProvinceKind::NeutralSettlement: return "Neutral city";
            case ProvinceKind::BanditCamp: return "Bandit camp";
            case ProvinceKind::TreasureSite: return "Event site";
        }
        return "Unknown";
    }

    const TaskGroupView* FindTaskGroupView(const GameSnapshot& snapshot, TaskGroupId id)
    {
        for (const auto& group : snapshot.taskGroups)
            if (group.id == id)
                return &group;
        return nullptr;
    }

    std::vector<ResourceType> TransferResourceTypes()
    {
        return {ResourceType::WOOD, ResourceType::STONE, ResourceType::COAL,
                ResourceType::IRON_ORE, ResourceType::PLANKS, ResourceType::IRON,
                ResourceType::FOOD_PROVISIONS};
    }

    std::string TaskGroupStatusLabel(TaskGroupStatus status)
    {
        switch (status)
        {
            case TaskGroupStatus::Empty: return "Empty";
            case TaskGroupStatus::Reserve: return "Reserve";
            case TaskGroupStatus::Garrison: return "Garrison";
            case TaskGroupStatus::Journey: return "Journey";
            case TaskGroupStatus::Battle: return "Battle";
            case TaskGroupStatus::Mixed: return "Mixed";
        }
        return "Unknown";
    }

    std::string AppliedWorldEventEffectLabel(const AppliedWorldEventEffect& effect)
    {
        switch (effect.kind)
        {
            case AppliedWorldEventEffectKind::ResourceDelta:
                return (effect.amount >= 0 ? "+" : "") + std::to_string(effect.amount) +
                       " " + rt2s(effect.resourceType);
            case AppliedWorldEventEffectKind::TimedModifier:
            {
                std::string value = "Modifier: " + std::string(BalanceStatLabel(effect.stat));
                if (effect.additive != 0.0)
                {
                    value += " ";
                    if (effect.additive > 0.0)
                        value += "+";
                    value += FormatOneDecimal(effect.additive);
                }
                if (effect.multiplier != 1.0)
                    value += " x" + FormatOneDecimal(effect.multiplier);
                return value + " for " + FormatDurationTicks(effect.durationTicks);
            }
            case AppliedWorldEventEffectKind::TradeScore:
            {
                std::string value = "Trade score ";
                if (effect.amount >= 0)
                    value += "+";
                return value + std::to_string(effect.amount);
            }
            case AppliedWorldEventEffectKind::BuildingLoss:
                return "Buildings lost: " + std::to_string(effect.amount);
            case AppliedWorldEventEffectKind::UnitLoss:
                return "Units lost: " + std::to_string(effect.amount);
            case AppliedWorldEventEffectKind::RaidStarted:
                return "Raid started - enemy strength: " + std::to_string(effect.amount);
        }
        return "Applied effect";
    }

    tvorin::ui::TextureHandle LoadGlobalMapTexture(const char* path,
                                                   TextureFilter filter)
    {
        if (path == nullptr || !FileExists(path))
            return {};
        tvorin::ui::TextureHandle texture{::LoadTexture(path)};
        if (texture)
            SetTextureFilter(texture.Get(), filter);
        return texture;
    }

    std::size_t ProvinceTextureIndex(const GlobalMapNodeView& node)
    {
        // Unknown nodes use the dedicated question-mark badge even though
        // their concrete kind and owner remain deliberately absent from the
        // presentation view.
        if (node.knowledge < ProvinceKnowledgeLevel::Scouted ||
            !node.visibleKind.has_value())
            return 5;
        if (*node.visibleKind == ProvinceKind::Buildable)
            return node.visibleOwner.has_value() ? 1 : 0;
        switch (*node.visibleKind)
        {
            case ProvinceKind::NeutralSettlement: return 2;
            case ProvinceKind::BanditCamp: return 3;
            case ProvinceKind::TreasureSite: return 4;
            case ProvinceKind::Buildable: break;
        }
        return 5;
    }

    std::size_t ParchmentTextureIndex(int tileX, int tileY, std::size_t textureCount)
    {
        const std::uint32_t x = static_cast<std::uint32_t>(tileX);
        const std::uint32_t y = static_cast<std::uint32_t>(tileY);
        std::uint32_t hash = x * 0x9E3779B9u + y * 0x85EBCA6Bu + 0xC2B2AE35u;
        hash ^= hash >> 16;
        hash *= 0x7FEB352Du;
        hash ^= hash >> 15;
        return textureCount == 0 ? 0 : hash % textureCount;
    }
}

GlobalMapPanelWidget::GlobalMapPanelWidget()
{
    static constexpr std::array<const char*, 4> parchmentPaths{
        "assets/ui/global_map/global_map_parchment_main_v5.png",
        "assets/ui/global_map/global_map_parchment_aux_v2.png",
        "assets/ui/global_map/global_map_parchment_aux_v3.png",
        "assets/ui/global_map/global_map_parchment_aux_v4.png"};
    for (std::size_t index = 0; index < parchmentPaths.size(); ++index)
        globalMapBackgrounds[index] = LoadGlobalMapTexture(parchmentPaths[index],
                                                            TEXTURE_FILTER_BILINEAR);

    static constexpr std::array<const char*, 6> provincePaths{
        "assets/ui/global_map/province_buildable.png",
        "assets/ui/global_map/province_colonized.png",
        "assets/ui/global_map/province_settlement.png",
        "assets/ui/global_map/province_bandit_camp.png",
        "assets/ui/global_map/province_treasure.png",
        "assets/ui/global_map/province_unknown.png"};
    static constexpr std::array<const char*, 6> provinceHoverPaths{
        "assets/ui/global_map/province_buildable_hover.png",
        "assets/ui/global_map/province_colonized_hover.png",
        "assets/ui/global_map/province_settlement_hover.png",
        "assets/ui/global_map/province_bandit_camp_hover.png",
        "assets/ui/global_map/province_treasure_hover.png",
        "assets/ui/global_map/province_unknown_hover.png"};
    for (std::size_t index = 0; index < provincePaths.size(); ++index)
    {
        provinceTextures[index] = LoadGlobalMapTexture(provincePaths[index],
                                                        TEXTURE_FILTER_BILINEAR);
        provinceHoverTextures[index] = LoadGlobalMapTexture(provinceHoverPaths[index],
                                                             TEXTURE_FILTER_BILINEAR);
    }

    globalMapFogRevealTexture = LoadGlobalMapTexture("assets/light/radial_light_mask_1024.png",
                                                      TEXTURE_FILTER_BILINEAR);
}

GlobalMapPanelWidget::~GlobalMapPanelWidget()
{
    globalMapShaders.Shutdown();
    if (IsWindowReady())
        globalMapFogMask.Reset();
    else
        globalMapFogMask.Forget();
}

std::optional<ProvinceId> GlobalMapNodeHitTester::HitTest(const GlobalMapView& view,
                                                          Vector2 point, Vector2 origin,
                                                          float scale, float radius)
{
    if (scale <= 0.0f || radius <= 0.0f)
        return std::nullopt;
    const float radiusSquared = radius * radius;
    for (const auto& node : view.nodes)
    {
        const Vector2 screen = NodeScreenPosition(node, origin, scale);
        const float dx = point.x - screen.x;
        const float dy = point.y - screen.y;
        if (dx * dx + dy * dy <= radiusSquared)
            return node.id;
    }
    return std::nullopt;
}

std::optional<ProvinceConnectionId> GlobalMapEdgeHitTester::HitTest(
    const GlobalMapView& view, Vector2 point, Vector2 origin, float scale, float tolerance)
{
    if (scale <= 0.0f || tolerance <= 0.0f)
        return std::nullopt;
    const float threshold = tolerance * tolerance;
    std::optional<ProvinceConnectionId> best;
    float bestDistance = std::numeric_limits<float>::max();
    for (const auto& edge : view.edges)
    {
        if (!edge.canInspect)
            continue;
        const auto* from = FindNode(view, edge.from);
        const auto* to = FindNode(view, edge.to);
        if (from == nullptr || to == nullptr)
            continue;
        const float distance = SegmentDistanceSquared(
            point, NodeScreenPosition(*from, origin, scale),
            NodeScreenPosition(*to, origin, scale));
        if (distance > threshold)
            continue;
        if (!best.has_value() || distance < bestDistance ||
            (std::abs(distance - bestDistance) < 0.001f && edge.connectionId < *best))
        {
            best = edge.connectionId;
            bestDistance = distance;
        }
    }
    return best;
}

void GlobalMapPanelWidget::UpdateSize(Vec2i windowSize)
{
    ChangePosition(0, 0);
    ChangeSize(windowSize.x, windowSize.y);
}

Rectangle GlobalMapPanelWidget::CanvasRect() const
{
    // The strategic map is the complete screen, not a panel placed inside a
    // parchment frame. This makes the black fog a world layer rather than an
    // inset with visible UI margins around it.
    return {0.0f, 0.0f, std::max(1.0f, static_cast<float>(size.x)),
            std::max(1.0f, static_cast<float>(size.y))};
}

float GlobalMapPanelWidget::MapScale() const
{
    // The global plan deliberately starts at a 1:1 background scale.  The
    // lower bound prevents zooming far enough out to turn the node icons into
    // unreadable dots or to reveal the complete campaign in one view.
    return mapZoom;
}

Vector2 GlobalMapPanelWidget::MapOrigin() const
{
    const Rectangle canvas = CanvasRect();
    if (scene == nullptr || scene->latestSnapshot.globalMapView.nodes.empty())
        return {canvas.x + canvas.width * 0.5f + mapPanOffset.x,
                canvas.y + canvas.height * 0.5f + mapPanOffset.y};
    const auto& nodes = scene->latestSnapshot.globalMapView.nodes;
    int minX = nodes.front().layoutPosition.x;
    int maxX = minX;
    int minY = nodes.front().layoutPosition.y;
    int maxY = minY;
    for (const auto& node : nodes)
    {
        minX = std::min(minX, node.layoutPosition.x);
        maxX = std::max(maxX, node.layoutPosition.x);
        minY = std::min(minY, node.layoutPosition.y);
        maxY = std::max(maxY, node.layoutPosition.y);
    }
    const float scale = MapScale();
    const float mapCenterX = (minX + maxX) * 0.5f * scale;
    const float mapCenterY = (minY + maxY) * 0.5f * scale;
    return {canvas.x + canvas.width * 0.5f - mapCenterX + mapPanOffset.x,
            canvas.y + canvas.height * 0.5f - mapCenterY + mapPanOffset.y};
}

namespace
{
    void DrawGlobalMapButton(Rectangle bounds, const char* label, bool hovered, Color tint)
    {
        if (!UiControlIcons::DrawPixelHudWidgetFrame(bounds, hovered, tint))
        {
            DrawRectangleRec(bounds, hovered ? UiTheme::SurfaceHover : UiTheme::Inset);
            DrawRectangleLinesEx(bounds, 1.0f, tint);
        }
        UiText::DrawFit(label,
                        Rectangle{bounds.x + 12.0f, bounds.y + 7.0f,
                                  bounds.width - 24.0f, bounds.height - 14.0f},
                        15, UiTheme::Parchment);
    }
}

void GlobalMapPanelWidget::DrawParchmentBackground(Rectangle bounds, Vector2 anchor,
                                                   float textureScale) const
{
    const auto textureIt = std::find_if(globalMapBackgrounds.begin(), globalMapBackgrounds.end(),
        [](const auto& texture) { return texture.IsValid(); });
    if (textureIt == globalMapBackgrounds.end())
    {
        DrawRectangleRec(bounds, Color{118, 82, 39, 255});
        return;
    }

    const float tileSize = static_cast<float>(textureIt->Get().width) * textureScale;
    const int firstX = static_cast<int>(std::floor((bounds.x - anchor.x) / tileSize));
    const int lastX = static_cast<int>(std::floor((bounds.x + bounds.width - anchor.x) / tileSize));
    const int firstY = static_cast<int>(std::floor((bounds.y - anchor.y) / tileSize));
    const int lastY = static_cast<int>(std::floor((bounds.y + bounds.height - anchor.y) / tileSize));
    for (int tileY = firstY; tileY <= lastY; ++tileY)
    {
        for (int tileX = firstX; tileX <= lastX; ++tileX)
        {
            const auto& texture = globalMapBackgrounds[ParchmentTextureIndex(
                tileX, tileY, globalMapBackgrounds.size())];
            if (!texture.IsValid())
                continue;
            const Texture2D& image = texture.Get();
            // The default/global minimum zoom is 1:1. Only the deliberately
            // narrow close-up range scales the paper with the nodes, keeping
            // their geography fixed on the physical parchment plan.
            DrawTexturePro(image,
                           {0.0f, 0.0f, static_cast<float>(image.width),
                            static_cast<float>(image.height)},
                           {anchor.x + tileX * tileSize, anchor.y + tileY * tileSize,
                            tileSize, tileSize},
                           {0.0f, 0.0f}, 0.0f, WHITE);
        }
    }
}

std::vector<GlobalMapPanelWidget::ProvinceAction>
GlobalMapPanelWidget::AvailableProvinceActions(const GlobalMapNodeView& selected) const
{
    std::vector<ProvinceAction> actions;
    if (selected.canScout)
        actions.push_back(ProvinceAction::Scout);
    if (selected.canTrade)
        actions.push_back(ProvinceAction::Trade);
    if (selected.canAttack)
        actions.push_back(ProvinceAction::Attack);
    if (selected.canColonize)
        actions.push_back(ProvinceAction::Colonize);
    if (scene != nullptr && scene->game != nullptr &&
        selected.visibleOwner.has_value() &&
        *selected.visibleOwner == scene->game->GetLocalPlayerId() &&
        selected.id != scene->game->GetLocalActiveProvinceId())
    {
        actions.push_back(ProvinceAction::ResourceTransfer);
        actions.push_back(ProvinceAction::ArmyTransfer);
    }
    return actions;
}

std::vector<int> GlobalMapPanelWidget::AvailableScoutIds(ProvinceId sourceProvinceId) const
{
    std::vector<int> result;
    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr || sourceProvinceId == InvalidProvinceId)
        return result;
    for (const auto& [instanceId, unit] : player->roster.units)
    {
        const auto* definition = FindUnitDefinition(unit.unitDefId);
        if (definition == nullptr || definition->role != UnitRole::Scout)
            continue;
        const bool currentlyAvailable =
            UnitAssignmentService::IsAvailableFromReserve(unit, sourceProvinceId);
        if (currentlyAvailable)
            result.push_back(instanceId);
        if (result.size() >= PersistenceLimits::MaxExpeditionUnits)
            break;
    }
    return result;
}

const JourneyStatusView* GlobalMapPanelWidget::FindLatestScoutJourney(
    ProvinceId targetProvinceId, bool activeOnly) const
{
    if (scene == nullptr || scene->game == nullptr)
        return nullptr;
    const PlayerId playerId = scene->game->GetLocalPlayerId();
    const JourneyStatusView* result = nullptr;
    for (const auto& journey : scene->latestSnapshot.journeyStatuses)
    {
        if (journey.kind != WorldJourneyKind::Scout || journey.ownerId != playerId ||
            journey.targetProvinceId != targetProvinceId ||
            (activeOnly && !IsActiveJourneyStatus(journey.status)))
            continue;
        if (result == nullptr || journey.journeyId > result->journeyId)
            result = &journey;
    }
    return result;
}

const JourneyStatusView* GlobalMapPanelWidget::FindLatestActiveJourney(
    ProvinceId targetProvinceId) const
{
    if (scene == nullptr || scene->game == nullptr)
        return nullptr;
    const PlayerId playerId = scene->game->GetLocalPlayerId();
    const JourneyStatusView* result = nullptr;
    for (const auto& journey : scene->latestSnapshot.journeyStatuses)
    {
        if (journey.ownerId != playerId || journey.targetProvinceId != targetProvinceId ||
            !IsActiveJourneyStatus(journey.status))
            continue;
        if (result == nullptr || journey.journeyId > result->journeyId)
            result = &journey;
    }
    return result;
}

bool GlobalMapPanelWidget::IsNodeAnchorVisible(const GlobalMapNodeView& node,
                                               Vector2 origin, float scale) const
{
    return CheckCollisionPointRec(NodeScreenPosition(node, origin, scale), CanvasRect());
}

Rectangle GlobalMapPanelWidget::ProvinceTooltipRect(const GlobalMapNodeView& selected,
                                                    Vector2 origin, float scale) const
{
    const std::size_t actionCount = AvailableProvinceActions(selected).size();
    const bool scoutSetup = scoutSetupProvinceId == selected.id;
    const bool scoutActive = FindLatestScoutJourney(selected.id, true) != nullptr ||
                             scoutPendingProvinceId == selected.id;
    const bool journeyActive = FindLatestActiveJourney(selected.id) != nullptr;
    const bool colonizationActive = scene != nullptr && scene->game != nullptr &&
        scene->game->IsColonizationInProgress(selected.id);
    const bool battleActive = scene != nullptr && std::any_of(
        scene->latestSnapshot.battleStatuses.begin(),
        scene->latestSnapshot.battleStatuses.end(),
        [&selected](const BattleStatusView& battle)
        {
            return battle.targetProvinceId == selected.id &&
                (battle.status == BattleLifecycleStatus::InTransit ||
                 battle.status == BattleLifecycleStatus::Active);
        });
    const bool operationActive = scoutActive || journeyActive || colonizationActive || battleActive;
    const bool buildable = selected.visibleKind.has_value() &&
                           *selected.visibleKind == ProvinceKind::Buildable;
    const float width = scoutSetup ? 310.0f : buildable ? 340.0f : 268.0f;
    const float height = scoutSetup ? 250.0f
        : operationActive ? 188.0f
        : buildable
            ? 250.0f + static_cast<float>(std::max<std::size_t>(1, actionCount)) * 42.0f
            : 98.0f + static_cast<float>(std::max<std::size_t>(1, actionCount)) * 42.0f;
    const Vector2 center = NodeScreenPosition(selected, origin, scale);
    const float nodeClearance = 66.0f * scale;
    // No viewport clamping: this is a world-anchored card. It follows the
    // selected node exactly and leaves the screen together with that node.
    return {center.x - width * 0.5f, center.y - height - nodeClearance,
            width, height};
}

Rectangle GlobalMapPanelWidget::ProvinceActionRect(Rectangle tooltip, std::size_t actionIndex) const
{
    const float actionTop = tooltip.width >= 320.0f ? 220.0f : 88.0f;
    return {tooltip.x + 14.0f, tooltip.y + actionTop + static_cast<float>(actionIndex) * 42.0f,
            tooltip.width - 28.0f, 34.0f};
}

Rectangle GlobalMapPanelWidget::ScoutCountButtonRect(Rectangle tooltip, bool increment) const
{
    return {increment ? tooltip.x + tooltip.width - 54.0f : tooltip.x + 14.0f,
            tooltip.y + 116.0f, 40.0f, 32.0f};
}

Rectangle GlobalMapPanelWidget::ScoutConfirmButtonRect(Rectangle tooltip) const
{
    return {tooltip.x + 14.0f, tooltip.y + tooltip.height - 42.0f,
            tooltip.width - 118.0f, 32.0f};
}

Rectangle GlobalMapPanelWidget::ScoutCancelButtonRect(Rectangle tooltip) const
{
    return {tooltip.x + tooltip.width - 96.0f, tooltip.y + tooltip.height - 42.0f,
            82.0f, 32.0f};
}

Rectangle GlobalMapPanelWidget::OperationDialogRect() const
{
    const bool wide = operationDialog == OperationDialogKind::Attack ||
                      operationDialog == OperationDialogKind::ArmyTransfer;
    const float width = operationDialog == OperationDialogKind::ResourceTransfer
        ? std::min(620.0f, std::max(360.0f, static_cast<float>(size.x) - 40.0f))
        : wide
            ? std::min(920.0f, std::max(360.0f, static_cast<float>(size.x) - 40.0f))
            : std::min(540.0f, std::max(360.0f, static_cast<float>(size.x) - 40.0f));
    const float height = operationDialog == OperationDialogKind::ResourceTransfer
        ? std::min(500.0f, std::max(360.0f, static_cast<float>(size.y) - 40.0f))
        : wide
            ? std::min(680.0f, std::max(420.0f, static_cast<float>(size.y) - 40.0f))
            : 268.0f;
    return Rectangle{(static_cast<float>(size.x) - width) * 0.5f,
                     (static_cast<float>(size.y) - height) * 0.5f, width, height};
}

Rectangle GlobalMapPanelWidget::OperationCloseButtonRect() const
{
    const Rectangle panel = OperationDialogRect();
    return Rectangle{panel.x + panel.width - 156.0f, panel.y + panel.height - 52.0f,
                     138.0f, 36.0f};
}

Rectangle GlobalMapPanelWidget::OperationConfirmButtonRect() const
{
    const Rectangle panel = OperationDialogRect();
    return Rectangle{panel.x + 18.0f, panel.y + panel.height - 52.0f, 190.0f, 36.0f};
}

void GlobalMapPanelWidget::EnsureFogResources(Rectangle canvas)
{
    // The reveal mask is intentionally half resolution, matching the local
    // map's fog strategy. Bilinear upscale preserves the soft authored edge
    // while cutting mask fill-rate and storage by 75%.
    const int width = std::max(1, static_cast<int>(std::ceil(
        canvas.width * GlobalFogMaskScale)));
    const int height = std::max(1, static_cast<int>(std::ceil(
        canvas.height * GlobalFogMaskScale)));
    if (!globalMapShaders.IsAvailable(ShaderId::RadialLight))
        globalMapShaders.LoadFragment(ShaderId::RadialLight, "assets/shaders/radial_light.fs");
    if (!globalMapShaders.IsAvailable(ShaderId::FogOfWar))
        globalMapShaders.LoadFragment(ShaderId::FogOfWar, "assets/shaders/fog_of_war.fs");

    if (globalMapFogMask.IsValid() && fogMaskSize.x == width && fogMaskSize.y == height)
        return;

    globalMapFogMask.Reset();
    fogMaskSize = {};
    if (!IsWindowReady())
        return;

    globalMapFogMask = tvorin::ui::RenderTextureHandle{LoadRenderTexture(width, height)};
    if (!globalMapFogMask.IsValid())
        return;
    SetTextureFilter(globalMapFogMask.Get().texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(globalMapFogMask.Get().texture, TEXTURE_WRAP_CLAMP);
    fogMaskSize = {width, height};
}

void GlobalMapPanelWidget::DrawFogOfWar(const GlobalMapView& view, Vector2 origin,
                                        float scale, Rectangle canvas)
{
    EnsureFogResources(canvas);
    const Shader* fogShader = globalMapShaders.Find(ShaderId::FogOfWar);
    if (!globalMapFogMask.IsValid() || fogShader == nullptr)
        return;

    BeginTextureMode(globalMapFogMask.Get());
    ClearBackground(BLACK);

    const Shader* radialShader = globalMapShaders.Find(ShaderId::RadialLight);
    const bool useSoftMask = radialShader != nullptr && globalMapFogRevealTexture.IsValid();
    if (useSoftMask)
    {
        const float animationTime = static_cast<float>(GetTime());
        const float animationAmount = 0.85f;
        const int maskOnly = 1;
        const int animationTimeLocation = globalMapShaders.GetLocation(
            ShaderId::RadialLight, "animationTime");
        const int animationAmountLocation = globalMapShaders.GetLocation(
            ShaderId::RadialLight, "animationAmount");
        const int maskOnlyLocation = globalMapShaders.GetLocation(ShaderId::RadialLight,
                                                                    "maskOnly");
        BeginShaderMode(*radialShader);
        if (animationTimeLocation >= 0)
            SetShaderValue(*radialShader, animationTimeLocation, &animationTime,
                           SHADER_UNIFORM_FLOAT);
        if (animationAmountLocation >= 0)
            SetShaderValue(*radialShader, animationAmountLocation, &animationAmount,
                           SHADER_UNIFORM_FLOAT);
        if (maskOnlyLocation >= 0)
            SetShaderValue(*radialShader, maskOnlyLocation, &maskOnly, SHADER_UNIFORM_INT);
    }

    const float revealRadius = 236.0f * scale;
    const auto drawReveal = [&](Vector2 screenCenter, float radius)
    {
        const Vector2 center{
            (screenCenter.x - canvas.x) * GlobalFogMaskScale,
            (screenCenter.y - canvas.y) * GlobalFogMaskScale};
        radius *= GlobalFogMaskScale;
        if (useSoftMask)
        {
            const Texture2D& maskTexture = globalMapFogRevealTexture.Get();
            DrawTexturePro(maskTexture,
                           {0.0f, 0.0f, static_cast<float>(maskTexture.width),
                            static_cast<float>(maskTexture.height)},
                           {center.x - radius, center.y - radius, radius * 2.0f,
                            radius * 2.0f},
                           {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else
            DrawCircleV(center, radius, WHITE);
    };

    // The mask is a union of soft revealers. It intentionally includes only
    // known nodes and their discovered routes, so a successful scout extends
    // the revealed area through newly reachable neighbours without exposing
    // hidden information as isolated compass-circle cut-outs.
    for (const auto& node : view.nodes)
        if (node.knowledge != ProvinceKnowledgeLevel::Hidden)
            drawReveal(NodeScreenPosition(node, origin, scale), revealRadius);

    for (const auto& edge : view.edges)
    {
        const auto* from = FindNode(view, edge.from);
        const auto* to = FindNode(view, edge.to);
        if (from == nullptr || to == nullptr ||
            from->knowledge == ProvinceKnowledgeLevel::Hidden ||
            to->knowledge == ProvinceKnowledgeLevel::Hidden)
            continue;

        const Vector2 first = NodeScreenPosition(*from, origin, scale);
        const Vector2 second = NodeScreenPosition(*to, origin, scale);
        const float dx = second.x - first.x;
        const float dy = second.y - first.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const int segments = std::max(1, static_cast<int>(std::ceil(
            distance / (revealRadius * 0.55f))));
        for (int segment = 1; segment < segments; ++segment)
        {
            const float progress = static_cast<float>(segment) /
                                   static_cast<float>(segments);
            drawReveal({first.x + dx * progress, first.y + dy * progress},
                       revealRadius * 0.92f);
        }
    }

    if (useSoftMask)
        EndShaderMode();
    EndTextureMode();

    BeginShaderMode(*fogShader);
    const Texture2D& maskTexture = globalMapFogMask.Get().texture;
    DrawTexturePro(maskTexture,
                   {0.0f, 0.0f, static_cast<float>(maskTexture.width),
                    -static_cast<float>(maskTexture.height)},
                   canvas, {0.0f, 0.0f}, 0.0f, WHITE);
    EndShaderMode();
}

void GlobalMapPanelWidget::DrawCanvas(const GlobalMapView& view, Vector2 origin,
                                      float scale)
{
    const Rectangle canvas = CanvasRect();
    const auto drawEdges = [&]()
    {
        for (const auto& edge : view.edges)
        {
            const auto* from = FindNode(view, edge.from);
            const auto* to = FindNode(view, edge.to);
            if (from == nullptr || to == nullptr)
                continue;
            DrawLineEx(NodeScreenPosition(*from, origin, scale),
                       NodeScreenPosition(*to, origin, scale), 4.0f,
                       edge.connectionId == selectedConnectionId
                           ? UiTheme::Gold
                           : (edge.canInspect ? Color{70, 46, 27, 245}
                                              : Color{64, 43, 27, 220}));
        }
    };

    const auto drawNodes = [&]()
    {
        const auto hovered = GlobalMapNodeHitTester::HitTest(
            view, GetMousePosition(), origin, scale,
            GlobalMapNodeHitRadius * scale);
        for (const auto& node : view.nodes)
        {
            const Vector2 center = NodeScreenPosition(node, origin, scale);
            const bool selected = node.id == selectedProvinceId;
            const bool isHovered = hovered.has_value() && *hovered == node.id;
            const std::size_t textureIndex = ProvinceTextureIndex(node);
            // Selection is deliberately a hover state too: the authored
            // hover texture remains visible after the pointer leaves a node.
            const auto& textures = (selected || isHovered)
                ? provinceHoverTextures : provinceTextures;
            const Texture2D& texture = textures[textureIndex].Get();
            const float baseIconSize = selected ? 104.0f : (isHovered ? 94.0f : 86.0f);
            const float iconSize = baseIconSize * scale;
            if (selected || isHovered)
            {
                const Color ring = selected ? UiTheme::Gold : UiTheme::Cyan;
                DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y),
                                iconSize * 0.60f, ring);
                DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y),
                                iconSize * 0.66f, Color{ring.r, ring.g, ring.b, 180});
            }
            if (textures[textureIndex].IsValid())
            {
                DrawTexturePro(texture,
                               {0.0f, 0.0f, static_cast<float>(texture.width),
                                static_cast<float>(texture.height)},
                               {center.x - iconSize * 0.5f, center.y - iconSize * 0.5f,
                                iconSize, iconSize},
                               {0.0f, 0.0f}, 0.0f, WHITE);
            }
            else
            {
                DrawCircleV(center, iconSize * 0.29f, NodeColor(node.knowledge));
                DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y),
                                iconSize * 0.31f,
                                selected ? UiTheme::Gold : Color{10, 20, 31, 255});
            }
            const std::string label = node.knowledge == ProvinceKnowledgeLevel::ReachableUnknown
                ? "?" : (node.knowledge == ProvinceKnowledgeLevel::Hidden
                    ? "" : std::to_string(node.id));
            if (!label.empty())
                UiText::Draw(label, center.x - UiText::Measure(label, 14) * 0.5f,
                             center.y - 7.0f, 14, Color{8, 16, 25, 255});
        }
    };

    BeginScissorMode(static_cast<int>(canvas.x), static_cast<int>(canvas.y),
                     static_cast<int>(canvas.width), static_cast<int>(canvas.height));
    DrawParchmentBackground(canvas, origin, scale);
    drawEdges();
    // Every node remains in the graph layer. The visibility mask below is
    // presentation-only and prevents hidden nodes from leaking through.
    drawNodes();
    EndScissorMode();

    if (view.fogOfWarEnabled)
        DrawFogOfWar(view, origin, scale, canvas);
}

void GlobalMapPanelWidget::DrawProvinceTooltip(const GlobalMapNodeView* selected, Vector2 origin,
                                               float scale) const
{
    if (selected == nullptr || !IsNodeAnchorVisible(*selected, origin, scale))
        return;
    const Rectangle panel = ProvinceTooltipRect(*selected, origin, scale);
    UiControlIcons::DrawPixelHudPanelFrame(panel);
    UiText::Draw("Province " + std::to_string(selected->id),
                 panel.x + 12.0f, panel.y + 10.0f, 19, UiTheme::Parchment);
    UiText::Draw("Type: " + std::string(ProvinceKindLabel(selected->visibleKind)),
                 panel.x + 12.0f, panel.y + 40.0f, 15, Color{185, 198, 211, 255});
    UiText::Draw("Owner: " + (selected->visibleOwner.has_value()
                                  ? std::to_string(*selected->visibleOwner) : "Unknown"),
                 panel.x + 12.0f, panel.y + 62.0f, 15, Color{185, 198, 211, 255});

    const JourneyStatusView* activeJourney = FindLatestActiveJourney(selected->id);
    const auto colonization = scene != nullptr && scene->game != nullptr
        ? scene->game->GetColonizationProgress(selected->id)
        : std::optional<ColonizationProgressView>{};
    const BattleStatusView* activeBattle = nullptr;
    if (scene != nullptr)
        for (const auto& battle : scene->latestSnapshot.battleStatuses)
            if (battle.targetProvinceId == selected->id &&
                (battle.status == BattleLifecycleStatus::InTransit ||
                 battle.status == BattleLifecycleStatus::Active) &&
                (activeBattle == nullptr || battle.battleId > activeBattle->battleId))
                activeBattle = &battle;
    const bool scoutPending = scoutPendingProvinceId == selected->id;
    if (colonization.has_value() || activeJourney != nullptr || activeBattle != nullptr ||
        scoutPending)
    {
        float progress = 0.0f;
        std::uint64_t remainingTicks = 0;
        std::string operationLabel = "Dispatching scouts";
        std::string detail = "Preparing the operation...";
        if (colonization.has_value())
        {
            progress = colonization->progress;
            remainingTicks = colonization->remainingTicks;
            operationLabel = colonization->phase == ColonizationPhase::Establishing
                ? "Establishing settlement" : "Colonists traveling";
            detail = colonization->phase == ColonizationPhase::Establishing
                ? "The settlement is being established."
                : "The colonists are traveling to the province.";
        }
        else if (activeBattle != nullptr &&
                 activeBattle->status == BattleLifecycleStatus::Active)
        {
            const std::uint64_t duration = activeBattle->endTick > activeBattle->startTick
                ? activeBattle->endTick - activeBattle->startTick : 0;
            const std::uint64_t elapsed = scene->latestSnapshot.simulationTick >
                    activeBattle->startTick
                ? std::min(duration, scene->latestSnapshot.simulationTick -
                                      activeBattle->startTick) : 0;
            progress = duration == 0 ? 0.0f
                : static_cast<float>(elapsed) / static_cast<float>(duration);
            remainingTicks = activeBattle->remainingTicks;
            operationLabel = activeBattle->isRaid ? "Raid in progress" : "Battle in progress";
            detail = activeBattle->isRaid
                ? "Raiders are attacking this province."
                : "Forces are engaged in this province.";
        }
        else if (activeJourney != nullptr)
        {
            progress = activeJourney->kind == WorldJourneyKind::Scout
                ? ScoutJourneyProgress(*activeJourney, scene->latestSnapshot.simulationTick)
                : JourneyProgress(*activeJourney, scene->latestSnapshot.simulationTick);
            remainingTicks = activeJourney->remainingTicks;
            operationLabel = JourneyOperationLabel(activeJourney->kind);
            detail = activeJourney->status == WorldJourneyStatus::AwaitingUnload
                ? "Waiting for cargo to be unloaded."
                : "The operation is traveling along its planned route.";
        }

        UiText::Draw(operationLabel, panel.x + 12.0f, panel.y + 92.0f,
                     15, UiTheme::Cyan);
        const Rectangle progressBounds{panel.x + 14.0f, panel.y + 119.0f,
                                       panel.width - 28.0f, 22.0f};
        DrawRectangleRec(progressBounds, Color{7, 15, 23, 235});
        DrawRectangleRec({progressBounds.x + 2.0f, progressBounds.y + 2.0f,
                          (progressBounds.width - 4.0f) * std::clamp(progress, 0.0f, 1.0f),
                          progressBounds.height - 4.0f}, UiTheme::Cyan);
        DrawRectangleLinesEx(progressBounds, 1.0f, UiTheme::Iron);
        const std::string progressLabel = scoutPending && activeJourney == nullptr
            ? "Dispatching scouts..."
            : std::to_string(static_cast<int>(std::round(progress * 100.0f))) +
                "%  |  ETA " + FormatOneDecimal(remainingTicks / 100.0) + " s";
        UiText::DrawFit(progressLabel,
                        {progressBounds.x + 6.0f, progressBounds.y + 2.0f,
                         progressBounds.width - 12.0f, progressBounds.height - 4.0f},
                        13, UiTheme::Parchment);
        UiText::DrawFit(detail,
                        {panel.x + 12.0f, panel.y + 151.0f,
                         panel.width - 24.0f, 20.0f},
                        13, UiTheme::ParchmentDim);
        return;
    }

    float provinceInfoY = panel.y + 88.0f;
    if (!selected->displayName.empty())
    {
        UiText::DrawFit(selected->displayName,
                        Rectangle{panel.x + 12.0f, provinceInfoY,
                                  panel.width - 24.0f, 18.0f},
                        14, UiTheme::AmberBright);
        provinceInfoY += 20.0f;
    }
    if (selected->visibleKind.has_value() && *selected->visibleKind == ProvinceKind::Buildable)
    {
        if (!selected->traitIds.empty())
        {
            std::string traits = "Traits: ";
            for (std::size_t i = 0; i < selected->traitIds.size() && i < 3; ++i)
                traits += (i == 0 ? "" : ", ") + selected->traitIds[i];
            if (selected->traitIds.size() > 3)
                traits += " +" + std::to_string(selected->traitIds.size() - 3);
            UiText::DrawFit(traits,
                            Rectangle{panel.x + 12.0f, provinceInfoY,
                                      panel.width - 24.0f, 18.0f},
                            13, UiTheme::ParchmentDim);
            provinceInfoY += 20.0f;
        }
        if (!selected->naturalResourceTypes.empty())
        {
            std::string resources = "Resources: ";
            for (std::size_t i = 0; i < selected->naturalResourceTypes.size() && i < 6; ++i)
                resources += (i == 0 ? "" : ", ") +
                    ResourceDisplayName(selected->naturalResourceTypes[i]);
            if (selected->naturalResourceTypes.size() > 6)
                resources += " +" + std::to_string(selected->naturalResourceTypes.size() - 6);
            UiText::DrawFit(resources,
                            Rectangle{panel.x + 12.0f, provinceInfoY,
                                      panel.width - 24.0f, 18.0f},
                            13, UiTheme::Parchment);
            provinceInfoY += 20.0f;
        }

        const ProvinceId sourceProvinceId = scene != nullptr && scene->game != nullptr
            ? scene->game->GetLocalActiveProvinceId() : InvalidProvinceId;
        Player* player = GuiLocalPlayer(scene);
        const ProvinceEconomy* sourceEconomy = player != nullptr
            ? player->GetProvinceEconomy(sourceProvinceId) : nullptr;
        const auto* definition = FindColonizationDefinition("frontier_settlement");
        if (selected->canColonize && player != nullptr && sourceEconomy != nullptr &&
            definition != nullptr)
        {
            const ColonizationQuote quote = BuildColonizationQuote(
                scene->game->GetGlobalMap(), *player, *sourceEconomy,
                sourceProvinceId, selected->id, *definition,
                scene->game->IsColonizationInProgress(selected->id),
                scene->game->GetColonizationOperationCount(),
                PersistenceLimits::MaxColonizationOperations);
            std::string costText = "Cost: ";
            for (std::size_t i = 0; i < quote.costs.size(); ++i)
            {
                if (i > 0)
                    costText += ", ";
                const auto& cost = quote.costs[i];
                costText += ResourceDisplayName(cost.type) + " x" + std::to_string(cost.amount);
            }
            UiText::DrawFit(costText,
                            Rectangle{panel.x + 12.0f, provinceInfoY,
                                      panel.width - 24.0f, 18.0f},
                            13, quote.allowed ? UiTheme::SageBright : UiTheme::RustBright);
            provinceInfoY += 20.0f;
            if (quote.allowed)
            {
                UiText::DrawFit("Travel " + FormatOneDecimal(quote.travel.totalDurationTicks / 100.0) +
                                    " s  |  Settle " +
                                    FormatOneDecimal(quote.settlementDurationTicks / 100.0) +
                                    " s  |  Total " +
                                    FormatOneDecimal(quote.totalDurationTicks / 100.0) + " s",
                                Rectangle{panel.x + 12.0f, provinceInfoY,
                                          panel.width - 24.0f, 18.0f},
                                13, UiTheme::Cyan);
            }
            else
                UiText::DrawFit(quote.reason,
                                Rectangle{panel.x + 12.0f, provinceInfoY,
                                          panel.width - 24.0f, 18.0f},
                                12, UiTheme::RustBright);
        }
    }

    if (scoutSetupProvinceId == selected->id)
    {
        const ProvinceId sourceProvinceId = scene != nullptr && scene->game != nullptr
            ? scene->game->GetLocalActiveProvinceId() : InvalidProvinceId;
        const auto available = AvailableScoutIds(sourceProvinceId);
        const int assigned = std::clamp(selectedScoutCount, 0,
                                        static_cast<int>(available.size()));
        UiText::Draw("Assign scouts", panel.x + 12.0f, panel.y + 92.0f,
                     15, UiTheme::Cyan);
        const Rectangle minus = ScoutCountButtonRect(panel, false);
        const Rectangle plus = ScoutCountButtonRect(panel, true);
        DrawGlobalMapButton(minus, "-", CheckCollisionPointRec(GetMousePosition(), minus),
                            UiTheme::Cyan);
        DrawGlobalMapButton(plus, "+", CheckCollisionPointRec(GetMousePosition(), plus),
                            UiTheme::Cyan);
        UiText::DrawFit(std::to_string(assigned) + " / " +
                            std::to_string(available.size()) + " available",
                        Rectangle{minus.x + minus.width + 6.0f, minus.y,
                                  plus.x - minus.x - minus.width - 12.0f, minus.height},
                        15, assigned > 0 ? UiTheme::Parchment : UiTheme::Iron);

        int routeRiskBasisPoints = 0;
        for (const auto& [id, definition] : GetWorldEventCatalog())
            if (definition.trigger == WorldEventTriggerDomain::Route)
                routeRiskBasisPoints = std::max(routeRiskBasisPoints,
                                                definition.chanceBasisPoints);
        const auto* expedition = FindExpeditionDefinition("scout");
        const double durationSeconds = expedition == nullptr
            ? 0.0 : expedition->durationTicks / 100.0;
        UiText::Draw("Duration: " + FormatOneDecimal(durationSeconds) + " s",
                     panel.x + 14.0f, panel.y + 158.0f, 14,
                     Color{185, 198, 211, 255});
        UiText::Draw("Random incident risk: up to " +
                         FormatOneDecimal(routeRiskBasisPoints / 100.0) + "% / leg",
                     panel.x + 14.0f, panel.y + 180.0f, 14,
                     Color{255, 166, 119, 255});
        const Rectangle confirm = ScoutConfirmButtonRect(panel);
        const Rectangle cancel = ScoutCancelButtonRect(panel);
        DrawGlobalMapButton(confirm, assigned > 0 ? "Confirm mission" : "No scouts available",
                            assigned > 0 && CheckCollisionPointRec(GetMousePosition(), confirm),
                            assigned > 0 ? UiTheme::Sage : UiTheme::Iron);
        DrawGlobalMapButton(cancel, "Back", CheckCollisionPointRec(GetMousePosition(), cancel),
                            UiTheme::Cyan);
        return;
    }

    const auto actions = AvailableProvinceActions(*selected);
    if (actions.empty())
    {
        UiText::Draw("No operation available", panel.x + 12.0f, panel.y + 98.0f, 14,
                     UiTheme::Iron);
        return;
    }
    for (std::size_t index = 0; index < actions.size(); ++index)
    {
        const char* label = actions[index] == ProvinceAction::Scout ? "Scout province"
            : actions[index] == ProvinceAction::Trade ? "Trade"
            : actions[index] == ProvinceAction::Attack ? "Attack"
            : actions[index] == ProvinceAction::Colonize ? "Colonize"
            : actions[index] == ProvinceAction::ResourceTransfer
                ? "Transport resources" : "Transfer army";
        const Color tint = actions[index] == ProvinceAction::Attack
            ? Color{168, 89, 78, 255}
            : actions[index] == ProvinceAction::Scout ? UiTheme::Cyan
            : actions[index] == ProvinceAction::ResourceTransfer ? UiTheme::AmberBright
            : actions[index] == ProvinceAction::ArmyTransfer ? UiTheme::Gold
            : UiTheme::Sage;
        const Rectangle button = ProvinceActionRect(panel, index);
        DrawGlobalMapButton(button, label, CheckCollisionPointRec(GetMousePosition(), button), tint);
    }
}

void GlobalMapPanelWidget::DrawRouteTooltip(const GlobalMapView& view,
                                           const ProvinceEdgeView* selectedEdge,
                                           Vector2 origin, float scale) const
{
    if (selectedEdge == nullptr)
        return;
    const Rectangle panel = RouteTooltipRect(view, *selectedEdge, origin, scale);
    if (panel.width <= 0.0f || panel.height <= 0.0f)
        return;
    UiControlIcons::DrawPixelHudPanelFrame(panel);
    UiText::Draw("Route " + std::to_string(selectedEdge->connectionId),
                 panel.x + 12.0f, panel.y + 10.0f, 19, UiTheme::Parchment);
    UiText::Draw("Province " + std::to_string(selectedEdge->from) + " <-> " +
                     std::to_string(selectedEdge->to),
                 panel.x + 12.0f, panel.y + 40.0f, 14, Color{185, 198, 211, 255});
    UiText::Draw("Length: " + std::to_string(selectedEdge->lengthUnits) + " units",
                 panel.x + 12.0f, panel.y + 62.0f, 14, UiTheme::Parchment);
    UiText::Draw("Level: " + std::to_string(selectedEdge->level) +
                     (selectedEdge->nextLevel > selectedEdge->level
                          ? " / " + std::to_string(selectedEdge->nextLevel)
                          : " (max)"),
                 panel.x + 12.0f, panel.y + 82.0f, 14, UiTheme::Cyan);
    const double routeSpeedBonus =
        (1.0 - selectedEdge->routeTimeBasisPoints / 10000.0) * 100.0;
    UiText::Draw("Route speed: " + FormatOneDecimal(routeSpeedBonus) + "%",
                 panel.x + 12.0f, panel.y + 102.0f, 14, UiTheme::Parchment);
    UiText::Draw("Incident risk: -" +
                     FormatOneDecimal(selectedEdge->incidentReductionBasisPoints / 100.0) + " pp",
                 panel.x + 12.0f, panel.y + 122.0f, 14, UiTheme::ParchmentDim);

    if (selectedEdge->canUpgrade && selectedEdge->upgradeRemainingTicks > 0)
    {
        const std::uint64_t total = std::max<std::uint64_t>(1,
            selectedEdge->nextUpgradeDurationTicks);
        const double progress = std::clamp(
            1.0 - static_cast<double>(selectedEdge->upgradeRemainingTicks) /
                static_cast<double>(total), 0.0, 1.0);
        UiText::Draw("Next level: " + std::to_string(selectedEdge->nextLevel) +
                         "  " + std::to_string(static_cast<int>(std::round(progress * 100.0))) +
                         "% | ETA " + FormatOneDecimal(selectedEdge->upgradeRemainingTicks / 100.0) + " s",
                     panel.x + 12.0f, panel.y + 146.0f, 14, UiTheme::AmberBright);
        const Rectangle bar{panel.x + 12.0f, panel.y + 168.0f,
                            panel.width - 24.0f, 12.0f};
        DrawRectangleRec(bar, UiTheme::Ink);
        DrawRectangleRec({bar.x, bar.y, bar.width * static_cast<float>(progress), bar.height},
                         UiTheme::Sage);
        DrawRectangleLinesEx(bar, 1.0f, UiTheme::Iron);
        return;
    }

    float actionY = panel.y + 146.0f;
    if (selectedEdge->canUpgrade)
    {
        UiText::Draw("Next: level " + std::to_string(selectedEdge->nextLevel) +
                         "  " + FormatOneDecimal(selectedEdge->nextUpgradeDurationTicks / 100.0) + " s",
                     panel.x + 12.0f, actionY, 13, UiTheme::ParchmentDim);
        actionY += 20.0f;
        for (const auto& cost : selectedEdge->nextUpgradeCost)
        {
            UiText::Draw(ResourceDisplayName(cost.type) + " x" + std::to_string(cost.amount),
                         panel.x + 12.0f, actionY, 13, UiTheme::Parchment);
            actionY += 18.0f;
        }
        const Rectangle button{panel.x + 14.0f, panel.y + panel.height - 48.0f,
                              panel.width - 28.0f, 34.0f};
        DrawGlobalMapButton(button, "Upgrade route",
                            CheckCollisionPointRec(GetMousePosition(), button), UiTheme::Sage);
    }
    else
        UiText::Draw("Inspect only", panel.x + 12.0f, actionY, 14, UiTheme::Iron);
}

Rectangle GlobalMapPanelWidget::RouteTooltipRect(const GlobalMapView& view,
                                                 const ProvinceEdgeView& selectedEdge,
                                                 Vector2 origin, float scale) const
{
    const auto* from = FindNode(view, selectedEdge.from);
    const auto* to = FindNode(view, selectedEdge.to);
    if (from == nullptr || to == nullptr)
        return {};
    const Rectangle canvas = CanvasRect();
    const Vector2 first = NodeScreenPosition(*from, origin, scale);
    const Vector2 second = NodeScreenPosition(*to, origin, scale);
    const float width = 320.0f;
    const float height = selectedEdge.canUpgrade
        ? 216.0f + static_cast<float>(selectedEdge.nextUpgradeCost.size()) * 20.0f
        : 142.0f;
    const Vector2 center{(first.x + second.x) * 0.5f, (first.y + second.y) * 0.5f};
    return {std::clamp(center.x - width * 0.5f, canvas.x + 8.0f,
                       canvas.x + canvas.width - width - 8.0f),
            std::clamp(center.y - height - 20.0f, canvas.y + 8.0f,
                       canvas.y + canvas.height - height - 8.0f),
            width, height};
}

void GlobalMapPanelWidget::DrawOperationDialog(const GlobalMapView& view)
{
    if (operationDialog == OperationDialogKind::None)
        return;
    const Rectangle panel = OperationDialogRect();
    DrawRectangle(0, 0, size.x, size.y, Color{4, 9, 17, 105});
    UiControlIcons::DrawPixelHudPanelFrame(panel);

    const char* title = operationDialog == OperationDialogKind::Trade ? "Trade operation"
        : operationDialog == OperationDialogKind::Attack ? "Attack operation"
        : operationDialog == OperationDialogKind::Colonize ? "Colonization operation"
        : operationDialog == OperationDialogKind::ResourceTransfer
            ? "Transport resources" : "Transfer army";
    UiText::Draw(title, panel.x + 18.0f, panel.y + 16.0f, 22, UiTheme::Parchment);
    UiText::Draw("Origin province: " + std::to_string(operationOriginProvinceId),
                 panel.x + 18.0f, panel.y + 54.0f, 16, UiTheme::Cyan);
    const auto* target = FindNode(view, operationTargetProvinceId);
    UiText::Draw("Target province: " + std::to_string(operationTargetProvinceId),
                 panel.x + 18.0f, panel.y + 78.0f, 16, UiTheme::Cyan);
    if (target == nullptr)
    {
        UiText::DrawFit("The target is no longer present in the current authoritative view.",
                        Rectangle{panel.x + 18.0f, panel.y + 116.0f,
                                  panel.width - 36.0f, 42.0f}, 16, UiTheme::Iron);
    }
    else if (operationDialog == OperationDialogKind::Trade)
    {
        UiText::DrawFit("Coin and barter use the same revision-checked trade journey. Open a quote before submitting an order.",
                        Rectangle{panel.x + 18.0f, panel.y + 116.0f,
                                  panel.width - 36.0f, 48.0f}, 16, UiTheme::ParchmentDim);
    }
    else if (operationDialog == OperationDialogKind::Attack ||
             operationDialog == OperationDialogKind::ArmyTransfer)
    {
        Player* player = GuiLocalPlayer(scene);
        std::vector<const TaskGroupView*> groups;
        if (player != nullptr)
            for (const auto& group : scene->latestSnapshot.taskGroups)
                if (group.stationProvinceId == operationOriginProvinceId && group.editable &&
                    (group.status == TaskGroupStatus::Reserve || group.status == TaskGroupStatus::Empty))
                    groups.push_back(&group);
        std::sort(groups.begin(), groups.end(),
                  [](const TaskGroupView* lhs, const TaskGroupView* rhs)
                  { return lhs->id < rhs->id; });

        UiText::Draw("Ready task groups", panel.x + 18.0f, panel.y + 116.0f, 16,
                     UiTheme::AmberBright);
        const float groupTop = panel.y + 142.0f;
        const float groupHeight = 48.0f;
        const float groupViewportHeight = operationDialog == OperationDialogKind::Attack
            ? 196.0f : 150.0f;
        BeginScissorMode(static_cast<int>(panel.x + 18.0f), static_cast<int>(groupTop),
                         static_cast<int>(panel.width - 36.0f),
                         static_cast<int>(groupViewportHeight));
        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            const TaskGroupView& group = *groups[index];
            const float y = groupTop + static_cast<float>(index) * groupHeight;
            const bool selected = std::find(selectedOperationTaskGroups.begin(),
                                             selectedOperationTaskGroups.end(), group.id) !=
                                  selectedOperationTaskGroups.end();
            DrawGlobalMapButton({panel.x + 18.0f, y, panel.width - 36.0f, 40.0f},
                                ("Group " + std::to_string(group.id) + "  " +
                                 std::to_string(group.total) + " units  " +
                                 TaskGroupStatusLabel(group.status)).c_str(),
                                selected, selected ? UiTheme::Gold : UiTheme::Cyan);
        }
        EndScissorMode();
        if (groups.empty())
            UiText::Draw("No ready task group in the source province.", panel.x + 18.0f,
                         groupTop + 12.0f, 15, UiTheme::Iron);

        if (operationDialog == OperationDialogKind::Attack)
        {
            const float loadoutY = panel.y + 356.0f;
            UiText::Draw("Draft loadout (presentation only)", panel.x + 18.0f, loadoutY,
                         15, UiTheme::ParchmentDim);
            const auto drawDraft = [&](const char* label, int amount, float y)
            {
                UiText::Draw(label, panel.x + 18.0f, y + 5.0f, 14, UiTheme::Parchment);
                DrawRectangleRec({panel.x + 170.0f, y + 7.0f, panel.width - 260.0f, 12.0f},
                                 UiTheme::Ink);
                DrawRectangleRec({panel.x + 170.0f, y + 7.0f,
                                  std::min(1.0f, amount / 32.0f) * (panel.width - 260.0f),
                                  12.0f}, UiTheme::AmberBright);
                UiText::Draw(std::to_string(amount), panel.x + panel.width - 78.0f,
                             y + 3.0f, 14, UiTheme::AmberBright);
            };
            drawDraft("Food provisions", attackFoodDraft, loadoutY + 24.0f);
            drawDraft("Iron swords", attackSwordDraft, loadoutY + 48.0f);
            UiText::DrawFit("Draft loadout is not consumed by the current combat backend.",
                            {panel.x + 18.0f, loadoutY + 76.0f, panel.width - 36.0f, 20.0f},
                            13, UiTheme::Iron);
        }
        else
        {
            const float targetY = panel.y + 310.0f;
            UiText::Draw("Destination Barracks", panel.x + 18.0f, targetY, 15,
                         UiTheme::AmberBright);
            const auto* targetProvince = scene->game->GetGlobalMap().FindBuildableProvince(
                operationTargetProvinceId);
            int row = 0;
            if (targetProvince != nullptr && targetProvince->GetSimulation() != nullptr)
                for (Building* building : targetProvince->GetSimulation()->GetEconomy().dataTracker.buildings)
                    if (building != nullptr && building->buildingType == BuildingType::Barracks &&
                        !building->IsUnderConstruction())
                    {
                        const bool selected = destinationBarracksBuildingId == building->id;
                        DrawGlobalMapButton(
                            {panel.x + 18.0f + static_cast<float>(row) * 116.0f,
                             targetY + 22.0f, 106.0f, 30.0f},
                            ("#" + std::to_string(building->id)).c_str(), selected,
                            selected ? UiTheme::Gold : UiTheme::Cyan);
                        ++row;
                    }
            if (row == 0)
                UiText::Draw("No completed Barracks in target province.", panel.x + 18.0f,
                             targetY + 24.0f, 14, UiTheme::RustBright);
        }
    }
    else if (operationDialog == OperationDialogKind::ResourceTransfer)
    {
        Player* player = GuiLocalPlayer(scene);
        const auto* source = scene->game->GetGlobalMap().FindBuildableProvince(
            operationOriginProvinceId);
        const ProvinceEconomy* sourceEconomy = player != nullptr && source != nullptr &&
            source->GetSimulation() != nullptr ? &source->GetSimulation()->GetEconomy() : nullptr;
        UiText::Draw("Cargo", panel.x + 18.0f, panel.y + 116.0f, 16, UiTheme::AmberBright);
        const auto resources = TransferResourceTypes();
        for (std::size_t index = 0; index < resources.size(); ++index)
        {
            const ResourceType type = resources[index];
            const int available = sourceEconomy == nullptr ? 0 :
                StockpileIndex::GetTotal(*sourceEconomy, type);
            const int amount = resourceTransferDraft[type];
            const float y = panel.y + 142.0f + static_cast<float>(index) * 32.0f;
            GuiPanel::DrawResourceIcon(type, {panel.x + 18.0f, y + 1.0f, 26.0f, 26.0f});
            UiText::DrawFit(ResourceDisplayName(type), {panel.x + 52.0f, y + 4.0f, 150.0f, 22.0f},
                            14, UiTheme::Parchment);
            UiText::Draw("available " + std::to_string(available), panel.x + 210.0f, y + 5.0f,
                         13, UiTheme::ParchmentDim);
            DrawGlobalMapButton({panel.x + panel.width - 190.0f, y, 28.0f, 28.0f}, "-",
                                false, UiTheme::Cyan);
            UiText::Draw(std::to_string(amount), panel.x + panel.width - 152.0f, y + 5.0f,
                         14, amount > 0 ? UiTheme::SageBright : UiTheme::Iron);
            DrawGlobalMapButton({panel.x + panel.width - 112.0f, y, 28.0f, 28.0f}, "+",
                                false, UiTheme::Cyan);
            DrawGlobalMapButton({panel.x + panel.width - 76.0f, y, 58.0f, 28.0f}, "Max",
                                false, UiTheme::AmberBright);
        }
        UiText::DrawFit("Cargo is consumed atomically only after authority validation.",
                        {panel.x + 18.0f, panel.y + panel.height - 92.0f,
                         panel.width - 36.0f, 20.0f}, 13, UiTheme::ParchmentDim);
    }
    else
    {
        UiText::DrawFit(operationOriginProvinceId == InvalidProvinceId
                            ? "No active owned origin is available."
                            : "The map and starting base are created only after the authority accepts this request.",
                        Rectangle{panel.x + 18.0f, panel.y + 116.0f,
                                  panel.width - 36.0f, 48.0f}, 16,
                        operationOriginProvinceId == InvalidProvinceId ? UiTheme::Iron
                                                                        : UiTheme::ParchmentDim);
    }

    const bool hasSelectedGroups = !selectedOperationTaskGroups.empty();
    const bool hasCargo = std::any_of(resourceTransferDraft.begin(), resourceTransferDraft.end(),
                                      [](const auto& entry) { return entry.second > 0; });
    const bool hasTransferTarget = operationDialog != OperationDialogKind::ArmyTransfer ||
                                   destinationBarracksBuildingId > 0;
    const bool canConfirm = operationOriginProvinceId != InvalidProvinceId &&
        ((operationDialog == OperationDialogKind::Colonize) ||
         (operationDialog == OperationDialogKind::Attack && hasSelectedGroups) ||
         (operationDialog == OperationDialogKind::ResourceTransfer && hasCargo) ||
         (operationDialog == OperationDialogKind::ArmyTransfer && hasSelectedGroups &&
          hasTransferTarget));
    if (canConfirm)
        DrawGlobalMapButton(OperationConfirmButtonRect(), "Confirm",
                            CheckCollisionPointRec(GetMousePosition(), OperationConfirmButtonRect()),
                            UiTheme::Sage);
    else if (operationDialog != OperationDialogKind::Trade)
        DrawGlobalMapButton(OperationConfirmButtonRect(), "Unavailable", false, UiTheme::Iron);
    DrawGlobalMapButton(OperationCloseButtonRect(), "Close",
                        CheckCollisionPointRec(GetMousePosition(), OperationCloseButtonRect()),
                        UiTheme::Cyan);
}

void GlobalMapPanelWidget::HandleInput(const GlobalMapView& view, Vector2 origin,
                                       float scale, const GlobalMapNodeView* selected,
                                       const ProvinceEdgeView* selectedEdge)
{
    if (scene == nullptr || scene->game == nullptr)
        return;
    const Vector2 mouse = GetMousePosition();
    if (operationDialog != OperationDialogKind::None)
    {
        if (CheckCollisionPointRec(mouse, OperationCloseButtonRect()))
        {
            operationDialog = OperationDialogKind::None;
            return;
        }
        const auto sourceGroups = [&]()
        {
            std::vector<const TaskGroupView*> groups;
            for (const auto& group : scene->latestSnapshot.taskGroups)
                if (group.stationProvinceId == operationOriginProvinceId && group.editable &&
                    (group.status == TaskGroupStatus::Reserve || group.status == TaskGroupStatus::Empty))
                    groups.push_back(&group);
            std::sort(groups.begin(), groups.end(),
                      [](const TaskGroupView* lhs, const TaskGroupView* rhs)
                      { return lhs->id < rhs->id; });
            return groups;
        };
        if (operationDialog == OperationDialogKind::Attack ||
            operationDialog == OperationDialogKind::ArmyTransfer)
        {
            const auto groups = sourceGroups();
            const float groupTop = OperationDialogRect().y + 142.0f;
            for (std::size_t index = 0; index < groups.size(); ++index)
            {
                const Rectangle row{OperationDialogRect().x + 18.0f,
                                    groupTop + static_cast<float>(index) * 48.0f,
                                    OperationDialogRect().width - 36.0f, 40.0f};
                if (!CheckCollisionPointRec(mouse, row))
                    continue;
                const TaskGroupId id = groups[index]->id;
                const auto existing = std::find(selectedOperationTaskGroups.begin(),
                                                 selectedOperationTaskGroups.end(), id);
                if (existing == selectedOperationTaskGroups.end())
                    selectedOperationTaskGroups.push_back(id);
                else
                    selectedOperationTaskGroups.erase(existing);
                std::sort(selectedOperationTaskGroups.begin(), selectedOperationTaskGroups.end());
                return;
            }
            if (operationDialog == OperationDialogKind::ArmyTransfer)
            {
                const auto* target = scene->game->GetGlobalMap().FindBuildableProvince(
                    operationTargetProvinceId);
                int row = 0;
                if (target != nullptr && target->GetSimulation() != nullptr)
                    for (Building* building : target->GetSimulation()->GetEconomy().dataTracker.buildings)
                        if (building != nullptr && building->buildingType == BuildingType::Barracks &&
                            !building->IsUnderConstruction())
                        {
                            const Rectangle button{OperationDialogRect().x + 18.0f +
                                                       static_cast<float>(row) * 116.0f,
                                                   OperationDialogRect().y + 332.0f,
                                                   106.0f, 30.0f};
                            if (CheckCollisionPointRec(mouse, button))
                            {
                                destinationBarracksBuildingId = building->id;
                                return;
                            }
                            ++row;
                        }
            }
        }
        if (operationDialog == OperationDialogKind::ResourceTransfer)
        {
            const auto* source = scene->game->GetGlobalMap().FindBuildableProvince(
                operationOriginProvinceId);
            const ProvinceEconomy* sourceEconomy = source != nullptr &&
                source->GetSimulation() != nullptr ? &source->GetSimulation()->GetEconomy() : nullptr;
            const auto resources = TransferResourceTypes();
            for (std::size_t index = 0; index < resources.size(); ++index)
            {
                const ResourceType type = resources[index];
                const int available = sourceEconomy == nullptr ? 0 :
                    StockpileIndex::GetTotal(*sourceEconomy, type);
                const float y = OperationDialogRect().y + 142.0f +
                                static_cast<float>(index) * 32.0f;
                const Rectangle minus{OperationDialogRect().x + OperationDialogRect().width - 190.0f,
                                      y, 28.0f, 28.0f};
                const Rectangle plus{OperationDialogRect().x + OperationDialogRect().width - 112.0f,
                                     y, 28.0f, 28.0f};
                const Rectangle max{OperationDialogRect().x + OperationDialogRect().width - 76.0f,
                                    y, 58.0f, 28.0f};
                if (CheckCollisionPointRec(mouse, minus))
                    resourceTransferDraft[type] = std::max(0, resourceTransferDraft[type] - 1);
                else if (CheckCollisionPointRec(mouse, plus))
                    resourceTransferDraft[type] = std::min(available,
                                                            resourceTransferDraft[type] + 1);
                else if (CheckCollisionPointRec(mouse, max))
                    resourceTransferDraft[type] = available;
                else
                    continue;
                return;
            }
        }
        if (operationDialog == OperationDialogKind::Colonize &&
            operationOriginProvinceId != InvalidProvinceId &&
            CheckCollisionPointRec(mouse, OperationConfirmButtonRect()))
        {
            scene->SubmitLocalCommand(GameCommand::ColonizeProvince(
                scene->game->GetLocalPlayerId(), operationOriginProvinceId,
                operationTargetProvinceId));
            operationDialog = OperationDialogKind::None;
        }
        else if (operationDialog == OperationDialogKind::Attack &&
                 !selectedOperationTaskGroups.empty() &&
                 CheckCollisionPointRec(mouse, OperationConfirmButtonRect()))
        {
            scene->SubmitLocalCommand(GameCommand::StartProvinceAttackWithTaskGroups(
                scene->game->GetLocalPlayerId(), operationOriginProvinceId,
                operationTargetProvinceId, selectedOperationTaskGroups));
            operationDialog = OperationDialogKind::None;
        }
        else if (operationDialog == OperationDialogKind::ResourceTransfer &&
                 CheckCollisionPointRec(mouse, OperationConfirmButtonRect()))
        {
            std::vector<ResourceAmount> cargo;
            for (const auto& [type, amount] : resourceTransferDraft)
                if (amount > 0)
                    cargo.push_back({type, amount});
            if (!cargo.empty())
                scene->SubmitLocalCommand(GameCommand::StartResourceTransfer(
                    scene->game->GetLocalPlayerId(), operationOriginProvinceId,
                    operationTargetProvinceId, std::move(cargo)));
            operationDialog = OperationDialogKind::None;
        }
        else if (operationDialog == OperationDialogKind::ArmyTransfer &&
                 !selectedOperationTaskGroups.empty() && destinationBarracksBuildingId > 0 &&
                 CheckCollisionPointRec(mouse, OperationConfirmButtonRect()))
        {
            scene->SubmitLocalCommand(GameCommand::StartArmyTransfer(
                scene->game->GetLocalPlayerId(), operationOriginProvinceId,
                operationTargetProvinceId, selectedOperationTaskGroups,
                destinationBarracksBuildingId));
            operationDialog = OperationDialogKind::None;
        }
        return;
    }

    const Rectangle fullPanel{18.0f, 18.0f, static_cast<float>(size.x) - 36.0f,
                              static_cast<float>(size.y) - 36.0f};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(fullPanel)))
    {
        scene->controller->ChangeSystem("default");
        return;
    }
    if (selected != nullptr && IsNodeAnchorVisible(*selected, origin, scale))
    {
        const ProvinceId originProvinceId = scene->game->GetLocalActiveProvinceId();
        const Rectangle tooltip = ProvinceTooltipRect(*selected, origin, scale);
        const bool operationStatusVisible =
            FindLatestActiveJourney(selected->id) != nullptr ||
            scoutPendingProvinceId == selected->id ||
            scene->game->IsColonizationInProgress(selected->id) ||
            std::any_of(scene->latestSnapshot.battleStatuses.begin(),
                        scene->latestSnapshot.battleStatuses.end(),
                        [&selected](const BattleStatusView& battle)
                        {
                            return battle.targetProvinceId == selected->id &&
                                (battle.status == BattleLifecycleStatus::InTransit ||
                                 battle.status == BattleLifecycleStatus::Active);
                        });
        if (operationStatusVisible && CheckCollisionPointRec(mouse, tooltip))
        {
            // The status card owns clicks inside itself, but an active scout
            // mission is not modal. Clicks elsewhere on the map must still be
            // allowed to select another province.
            return;
        }
        if (scoutSetupProvinceId == selected->id)
        {
            const auto available = AvailableScoutIds(originProvinceId);
            const int maximum = static_cast<int>(available.size());
            if (CheckCollisionPointRec(mouse, ScoutCountButtonRect(tooltip, false)))
            {
                selectedScoutCount = std::max(1, selectedScoutCount - 1);
                return;
            }
            if (CheckCollisionPointRec(mouse, ScoutCountButtonRect(tooltip, true)))
            {
                selectedScoutCount = std::min(maximum, selectedScoutCount + 1);
                return;
            }
            if (CheckCollisionPointRec(mouse, ScoutCancelButtonRect(tooltip)))
            {
                scoutSetupProvinceId = InvalidProvinceId;
                return;
            }
            if (maximum > 0 && CheckCollisionPointRec(mouse, ScoutConfirmButtonRect(tooltip)))
            {
                selectedScoutCount = std::clamp(selectedScoutCount, 1, maximum);
                std::vector<int> assigned(available.begin(),
                                          available.begin() + selectedScoutCount);
                scene->SubmitLocalCommand(GameCommand::StartScoutExpedition(
                    scene->game->GetLocalPlayerId(), originProvinceId,
                    selected->id, std::move(assigned)));
                scoutSetupProvinceId = InvalidProvinceId;
                scoutPendingProvinceId = selected->id;
                scoutPendingUntil = GetTime() + 2.0;
                return;
            }
            return;
        }
        const auto actions = AvailableProvinceActions(*selected);
        for (std::size_t index = 0; index < actions.size(); ++index)
        {
            if (!CheckCollisionPointRec(mouse, ProvinceActionRect(tooltip, index)))
                continue;
            switch (actions[index])
            {
                case ProvinceAction::Scout:
                    scoutSetupProvinceId = selected->id;
                    selectedScoutCount = 1;
                    return;
                case ProvinceAction::Trade:
                    operationDialog = OperationDialogKind::Trade;
                    break;
                case ProvinceAction::Attack:
                    operationDialog = OperationDialogKind::Attack;
                    break;
                case ProvinceAction::Colonize:
                    operationDialog = OperationDialogKind::Colonize;
                    break;
                case ProvinceAction::ResourceTransfer:
                    operationDialog = OperationDialogKind::ResourceTransfer;
                    break;
                case ProvinceAction::ArmyTransfer:
                    operationDialog = OperationDialogKind::ArmyTransfer;
                    break;
            }
            operationTargetProvinceId = selected->id;
            operationOriginProvinceId = originProvinceId;
            selectedOperationTaskGroups.clear();
            resourceTransferDraft.clear();
            destinationBarracksBuildingId = 0;
            attackFoodDraft = 0;
            attackSwordDraft = 0;
            if (operationDialog == OperationDialogKind::Attack)
            {
                attackFoodDraft = 1;
                attackSwordDraft = 1;
            }
            if (operationDialog == OperationDialogKind::ArmyTransfer)
            {
                const auto* targetProvince = scene->game->GetGlobalMap().FindBuildableProvince(
                    operationTargetProvinceId);
                if (targetProvince != nullptr && targetProvince->GetSimulation() != nullptr)
                    for (Building* targetBuilding :
                         targetProvince->GetSimulation()->GetEconomy().dataTracker.buildings)
                        if (targetBuilding != nullptr &&
                            targetBuilding->buildingType == BuildingType::Barracks &&
                            !targetBuilding->IsUnderConstruction() &&
                            (destinationBarracksBuildingId == 0 ||
                             targetBuilding->id < destinationBarracksBuildingId))
                            destinationBarracksBuildingId = targetBuilding->id;
            }
            return;
        }
    }
    if (selectedEdge != nullptr && selectedEdge->canUpgrade &&
        selectedEdge->upgradeRemainingTicks == 0)
    {
        const Rectangle routeTooltip = RouteTooltipRect(view, *selectedEdge, origin, scale);
        const Rectangle routeButton{routeTooltip.x + 14.0f,
                                    routeTooltip.y + routeTooltip.height - 48.0f,
                                    routeTooltip.width - 28.0f, 34.0f};
        if (CheckCollisionPointRec(mouse, routeButton))
        {
            ProvinceId sourceProvinceId = InvalidProvinceId;
            const auto* fromProvince = scene->game->GetGlobalMap().FindBuildableProvince(
                selectedEdge->from);
            const auto* toProvince = scene->game->GetGlobalMap().FindBuildableProvince(
                selectedEdge->to);
            if (fromProvince != nullptr &&
                fromProvince->GetOwnerId() == scene->game->GetLocalPlayerId())
                sourceProvinceId = fromProvince->GetId();
            else if (toProvince != nullptr &&
                     toProvince->GetOwnerId() == scene->game->GetLocalPlayerId())
                sourceProvinceId = toProvince->GetId();
            if (sourceProvinceId != InvalidProvinceId)
                scene->SubmitLocalCommand(GameCommand::UpgradeProvinceConnection(
                    scene->game->GetLocalPlayerId(), sourceProvinceId,
                    selectedEdge->connectionId));
            return;
        }
    }

    const auto hit = GlobalMapNodeHitTester::HitTest(
        view, mouse, origin, scale, GlobalMapNodeHitRadius * scale);
    if (hit.has_value())
    {
        const auto* hitNode = FindNode(view, *hit);
        if (view.fogOfWarEnabled && hitNode != nullptr &&
            hitNode->knowledge == ProvinceKnowledgeLevel::Hidden)
            return;
        if (*hit != selectedProvinceId)
        {
            operationDialog = OperationDialogKind::None;
            scoutSetupProvinceId = InvalidProvinceId;
            scoutPendingProvinceId = InvalidProvinceId;
        }
        selectedProvinceId = *hit;
        selectedConnectionId = InvalidProvinceConnectionId;
        return;
    }
    const auto edgeHit = GlobalMapEdgeHitTester::HitTest(view, mouse, origin, scale, 12.0f);
    selectedConnectionId = edgeHit.value_or(InvalidProvinceConnectionId);
    if (edgeHit.has_value())
    {
        selectedProvinceId = InvalidProvinceId;
        operationDialog = OperationDialogKind::None;
        scoutSetupProvinceId = InvalidProvinceId;
        scoutPendingProvinceId = InvalidProvinceId;
    }
}

void GlobalMapPanelWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr)
        return;
    if (panning && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        const Vector2 mouse = GetMousePosition();
        mapPanOffset.x += mouse.x - lastPanMouse.x;
        mapPanOffset.y += mouse.y - lastPanMouse.y;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        panning = false;

    const GlobalMapView& view = scene->latestSnapshot.globalMapView;
    const Vector2 origin = MapOrigin();
    const float scale = std::max(0.05f, MapScale());

    if (scoutPendingProvinceId != InvalidProvinceId &&
        (FindLatestScoutJourney(scoutPendingProvinceId, true) != nullptr ||
         GetTime() >= scoutPendingUntil))
        scoutPendingProvinceId = InvalidProvinceId;
    if (scoutSetupProvinceId != InvalidProvinceId && scene->game != nullptr)
    {
        const int available = static_cast<int>(AvailableScoutIds(
            scene->game->GetLocalActiveProvinceId()).size());
        selectedScoutCount = available > 0
            ? std::clamp(selectedScoutCount, 1, available) : 0;
    }

    const Rectangle fullPanel{18.0f, 18.0f, static_cast<float>(size.x) - 36.0f,
                              static_cast<float>(size.y) - 36.0f};
    DrawCloseButton(fullPanel);

    DrawCanvas(view, origin, scale);
}

void GlobalMapPanelWidget::DrawOverlay(double dt)
{
    (void)dt;
    if (scene == nullptr)
        return;

    const GlobalMapView& view = scene->latestSnapshot.globalMapView;
    const Vector2 origin = MapOrigin();
    const float scale = std::max(0.05f, MapScale());
    const auto* selected = FindNode(view, selectedProvinceId);
    DrawProvinceTooltip(selected, origin, scale);

    const ProvinceEdgeView* selectedEdge = nullptr;
    for (const auto& edge : view.edges)
        if (edge.connectionId == selectedConnectionId)
        {
            selectedEdge = &edge;
            break;
        }
    DrawRouteTooltip(view, selectedEdge, origin, scale);
    DrawOperationDialog(view);
}

void GlobalMapPanelWidget::AdjustMapZoom(Vec2i point, float wheel)
{
    if (wheel == 0.0f)
        return;
    const Rectangle canvas = CanvasRect();
    const Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (!CheckCollisionPointRec(mouse, canvas))
        return;
    const float oldZoom = mapZoom;
    const float newZoom = std::clamp(mapZoom + wheel * 0.08f, 1.0f, 1.28f);
    if (std::abs(newZoom - oldZoom) < 0.001f)
        return;

    const Vector2 canvasCenter{canvas.x + canvas.width * 0.5f,
                               canvas.y + canvas.height * 0.5f};
    const Vector2 local{mouse.x - canvasCenter.x, mouse.y - canvasCenter.y};
    mapPanOffset.x = local.x - (local.x - mapPanOffset.x) * (newZoom / oldZoom);
    mapPanOffset.y = local.y - (local.y - mapPanOffset.y) * (newZoom / oldZoom);
    mapZoom = newZoom;
}

void GlobalMapPanelWidget::BeginPanning()
{
    if (operationDialog != OperationDialogKind::None ||
        !CheckCollisionPointRec(GetMousePosition(), CanvasRect()))
        return;
    const Vector2 mouse = GetMousePosition();
    panning = true;
    lastPanMouse = {mouse.x, mouse.y};
}

void GlobalMapPanelWidget::EndPanning()
{
    panning = false;
}

void GlobalMapPanelWidget::HandleLeftClick()
{
    if (scene == nullptr)
        return;
    if (scene->campaignSidebar.CapturesPointer(GetMousePosition()))
        return;
    const GlobalMapView& view = scene->latestSnapshot.globalMapView;
    const Vector2 origin = MapOrigin();
    const float scale = std::max(0.05f, MapScale());
    const auto* selected = FindNode(view, selectedProvinceId);
    const ProvinceEdgeView* selectedEdge = nullptr;
    for (const auto& edge : view.edges)
        if (edge.connectionId == selectedConnectionId)
        {
            selectedEdge = &edge;
            break;
        }
    HandleInput(view, origin, scale, selected, selectedEdge);
}

void OwnedProvinceListWidget::UpdateSize(Vec2i windowSize)
{
    const int belowStrategicHud = static_cast<int>(
        std::clamp(windowSize.y * 0.114f, 106.0f, 130.0f)) + 16;
    ChangePosition(18, belowStrategicHud);
    const int width = static_cast<int>(std::clamp(windowSize.x * 0.20f, 300.0f, 380.0f));
    const int availableForList = std::max(160, windowSize.y - belowStrategicHud - 62 - 8 - 18);
    ChangeSize(width, std::min(static_cast<int>(windowSize.y * 0.38f), availableForList));
}

bool OwnedProvinceListWidget::CapturesPointer(Vector2 point) const
{
    if (scene == nullptr || scene->game == nullptr)
        return false;
    const PlayerId localPlayerId = scene->game->GetLocalPlayerId();
    const std::size_t ownedCount = static_cast<std::size_t>(std::count_if(
        scene->latestSnapshot.globalMapView.nodes.begin(),
        scene->latestSnapshot.globalMapView.nodes.end(),
        [localPlayerId](const GlobalMapNodeView& node)
        {
            return node.visibleOwner == localPlayerId;
        }));
    if (ownedCount < 2)
        return false;
    const float height = expanded ? static_cast<float>(size.y) : 38.0f;
    return CheckCollisionPointRec(point,
        {static_cast<float>(pos.x), static_cast<float>(pos.y),
         static_cast<float>(size.x), height});
}

void OwnedProvinceListWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr)
        return;

    const GlobalMapView& view = scene->latestSnapshot.globalMapView;
    std::vector<const GlobalMapNodeView*> owned;
    for (const auto& node : view.nodes)
        if (node.visibleOwner == scene->game->GetLocalPlayerId())
            owned.push_back(&node);
    if (owned.size() < 2)
        return;

    const Rectangle toggle{static_cast<float>(pos.x), static_cast<float>(pos.y),
                           static_cast<float>(size.x), 38.0f};
    const bool toggleHovered = CheckCollisionPointRec(GetMousePosition(), toggle);
    UiControlIcons::DrawPixelHudWidgetFrame(toggle, toggleHovered);
    UiText::DrawFit(expanded ? "Owned provinces  ^" : "Owned provinces  v",
                    Rectangle{toggle.x + 10.0f, toggle.y + 7.0f,
                              toggle.width - 20.0f, toggle.height - 12.0f},
                    16, UiTheme::Parchment);
    if (toggleHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        expanded = !expanded;
        if (!expanded)
            scrollOffset = 0.0f;
    }
    if (!expanded)
    {
        if (journal != nullptr)
            journal->ChangePosition(pos.x, pos.y + 46);
        return;
    }

    const float rowHeight = 38.0f;
    const float viewportHeight = std::max(48.0f, static_cast<float>(size.y) - 44.0f);
    const float contentHeight = rowHeight * static_cast<float>(owned.size()) + 4.0f * owned.size();
    maxScrollOffset = std::max(0.0f, contentHeight - viewportHeight);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);
    const Rectangle panel{toggle.x, toggle.y, toggle.width, toggle.height + viewportHeight};
    if (journal != nullptr)
        journal->ChangePosition(pos.x, static_cast<int>(panel.y + panel.height + 8.0f));
    UiControlIcons::DrawPixelHudPanelFrame(panel);
    const Rectangle viewport{panel.x + 6.0f, panel.y + 42.0f,
                             panel.width - 12.0f, viewportHeight - 4.0f};
    if (CheckCollisionPointRec(GetMousePosition(), viewport))
        scrollOffset = std::clamp(scrollOffset - InputManager::GetMouseWheelMove() * rowHeight,
                                  0.0f, maxScrollOffset);
    BeginScissorMode(static_cast<int>(viewport.x), static_cast<int>(viewport.y),
                     static_cast<int>(viewport.width), static_cast<int>(viewport.height));
    for (std::size_t index = 0; index < owned.size(); ++index)
    {
        const auto* node = owned[index];
        const Rectangle row{viewport.x, viewport.y - scrollOffset +
                                (rowHeight + 4.0f) * static_cast<float>(index),
                            panel.width - 12.0f, rowHeight - 4.0f};
        const bool active = node->id == scene->game->GetLocalActiveProvinceId();
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
        UiControlIcons::DrawPixelHudWidgetFrame(row, hovered || active,
                                                 active ? UiTheme::Gold : WHITE);
        std::string label = node->displayName.empty()
            ? "Province " + std::to_string(node->id) : node->displayName;
        WorldEventInstanceId newestEventId = InvalidWorldEventInstanceId;
        for (const auto& event : scene->latestSnapshot.eventNotifications)
            if (event.provinceId == node->id || event.secondaryProvinceId == node->id)
                newestEventId = std::max(newestEventId, event.instanceId);
        auto acknowledged = alertAcknowledgedEventIds.find(node->id);
        if (acknowledged == alertAcknowledgedEventIds.end())
            acknowledged = alertAcknowledgedEventIds.emplace(node->id, newestEventId).first;
        const bool hasNewEvent = newestEventId > acknowledged->second;
        bool hasActiveDefenseAlert = false;
        bool hasActiveBattle = false;
        for (const auto& battle : scene->latestSnapshot.battleStatuses)
            if (battle.targetProvinceId == node->id &&
                (battle.status == BattleLifecycleStatus::InTransit ||
                 battle.status == BattleLifecycleStatus::Active))
            {
                hasActiveBattle = true;
                break;
            }
        for (const auto& defense : scene->latestSnapshot.provinceDefenses)
            if (defense.provinceId == node->id &&
                defense.supplyStatus != GarrisonSupplyStatus::Supplied)
            {
                hasActiveDefenseAlert = true;
                break;
            }
        const bool hasAlert = hasNewEvent || hasActiveBattle || hasActiveDefenseAlert;
        UiText::DrawFit(label, Rectangle{row.x + 8.0f, row.y + 5.0f,
                                         row.width - (hasAlert ? 40.0f : 16.0f), row.height - 10.0f},
                        15, active ? UiTheme::AmberBright : UiTheme::Parchment);
        if (hasAlert)
            UiText::Draw("!", row.x + row.width - 25.0f, row.y + 5.0f, 18,
                         UiTheme::AmberBright);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            acknowledged->second = newestEventId;
            scene->game->SetLocalActiveProvince(node->id);
        }
    }
    EndScissorMode();
}

void CampaignStatusWidget::UpdateSize(Vec2i windowSize)
{
    const int belowStrategicHud = static_cast<int>(
        std::clamp(windowSize.y * 0.114f, 106.0f, 130.0f)) + 16;
    ChangePosition(18, belowStrategicHud + 46);
    ChangeSize(static_cast<int>(std::clamp(windowSize.x * 0.20f, 300.0f, 380.0f)),
               std::max(180, static_cast<int>(std::min(360.0f, windowSize.y * 0.42f))));
}

bool CampaignStatusWidget::CapturesPointer(Vector2 point) const
{
    if (scene == nullptr)
        return false;
    const auto& snapshot = scene->latestSnapshot;
    const bool hasContent = !snapshot.eventNotifications.empty() ||
        !snapshot.battleStatuses.empty() || !snapshot.battleReports.empty() ||
        !snapshot.journeyStatuses.empty() || !snapshot.provinceDefenses.empty();
    if (!hasContent)
        return false;
    const float renderedHeight = journalExpanded ? static_cast<float>(size.y) : 62.0f;
    return CheckCollisionPointRec(point,
        {static_cast<float>(pos.x), static_cast<float>(pos.y),
         static_cast<float>(size.x), renderedHeight});
}

void CampaignStatusWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr ||
        (scene->latestSnapshot.eventNotifications.empty() &&
         scene->latestSnapshot.battleStatuses.empty() &&
         scene->latestSnapshot.battleReports.empty() &&
         scene->latestSnapshot.journeyStatuses.empty() &&
         scene->latestSnapshot.provinceDefenses.empty()))
        return;

    const auto& snapshot = scene->latestSnapshot;
    if (!snapshot.eventNotifications.empty() &&
        snapshot.eventNotifications.back().instanceId != newestSeenEventId)
    {
        newestSeenEventId = snapshot.eventNotifications.back().instanceId;
        headlineVisibleUntil = GetTime() + 6.0;
    }

    const float renderedHeight = journalExpanded ? static_cast<float>(size.y) : 62.0f;
    const Rectangle panel{static_cast<float>(pos.x), static_cast<float>(pos.y),
                          static_cast<float>(size.x), renderedHeight};
    const Rectangle header{panel.x, panel.y, panel.width, 62.0f};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), header);
    const bool highlighted = GetTime() < headlineVisibleUntil;
    UiControlIcons::DrawPixelHudPanelFrame(panel,
                                           highlighted ? UiTheme::Gold : WHITE);
    UiText::Draw("Campaign journal  " +
                     std::string(journalExpanded ? "^" : "v") + "  (" +
                     std::to_string(snapshot.eventNotifications.size()) + ")",
                 header.x + 12.0f, header.y + 8.0f, 16,
                 hovered || highlighted ? UiTheme::AmberBright : UiTheme::Parchment);
    const std::string headline = snapshot.eventNotifications.empty()
        ? "No campaign reports yet"
        : snapshot.eventNotifications.back().title.empty()
            ? snapshot.eventNotifications.back().definitionId
            : snapshot.eventNotifications.back().title;
    UiText::DrawFit(headline,
                    Rectangle{header.x + 12.0f, header.y + 33.0f,
                              header.width - 24.0f, 19.0f},
                    14, highlighted ? UiTheme::AmberBright : UiTheme::ParchmentDim);
    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        journalExpanded = !journalExpanded;
        if (!journalExpanded)
            scrollOffset = 0.0f;
    }
    if (!journalExpanded)
        return;

    const float viewportTop = panel.y + 70.0f;
    const float viewportHeight = std::max(24.0f, panel.height - 78.0f);
    constexpr float HeaderHeight = 34.0f;
    constexpr float EffectHeight = 22.0f;
    const auto eventHeight = [this](const WorldEventNotificationView& event)
    {
        if (!expandedEventIds.contains(event.instanceId))
            return HeaderHeight;
        const std::size_t visibleEffects = std::min(CampaignJournalMaxVisibleEffects,
                                                    event.appliedEffects.size());
        const float effectsHeight = event.appliedEffects.empty()
            ? EffectHeight
            : EffectHeight * static_cast<float>(visibleEffects) +
                (visibleEffects < event.appliedEffects.size() ? EffectHeight : 0.0f);
        return HeaderHeight + 28.0f + effectsHeight + 8.0f;
    };
    float contentHeight = 8.0f;
    for (auto it = snapshot.eventNotifications.rbegin();
         it != snapshot.eventNotifications.rend(); ++it)
        contentHeight += eventHeight(*it) + 4.0f;
    if (!snapshot.journeyStatuses.empty() || !snapshot.battleStatuses.empty() ||
        !snapshot.battleReports.empty())
        contentHeight += 28.0f;
    maxScrollOffset = std::max(0.0f, contentHeight - viewportHeight);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);
    const Rectangle viewport{panel.x + 8.0f, viewportTop, panel.width - 16.0f, viewportHeight};
    if (CheckCollisionPointRec(GetMousePosition(), viewport))
        scrollOffset = std::clamp(scrollOffset - InputManager::GetMouseWheelMove() * HeaderHeight,
                                  0.0f, maxScrollOffset);
    BeginScissorMode(static_cast<int>(viewport.x), static_cast<int>(viewport.y),
                     static_cast<int>(viewport.width), static_cast<int>(viewport.height));
    float y = viewportTop + 4.0f - scrollOffset;
    for (auto eventIt = snapshot.eventNotifications.rbegin();
         eventIt != snapshot.eventNotifications.rend(); ++eventIt)
    {
        const auto& event = *eventIt;
        const std::string title = event.title.empty() ? event.definitionId : event.title;
        const bool expandedEvent = expandedEventIds.contains(event.instanceId);
        const Rectangle eventHeader{viewport.x + 2.0f, y, viewport.width - 4.0f,
                                    HeaderHeight - 2.0f};
        const bool headerHovered = CheckCollisionPointRec(GetMousePosition(), viewport) &&
                                   CheckCollisionPointRec(GetMousePosition(), eventHeader);
        UiControlIcons::DrawPixelHudWidgetFrame(eventHeader, headerHovered || expandedEvent,
                                                 expandedEvent ? UiTheme::Bronze : WHITE);
        UiText::DrawFit(std::string(expandedEvent ? "^ " : "v ") +
                            FormatSimulationTimestamp(event.startTick) + " " + title,
                        Rectangle{eventHeader.x + 7.0f, eventHeader.y + 7.0f,
                                  eventHeader.width - 14.0f, 18.0f},
                        14, UiTheme::AmberBright);
        if (headerHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (expandedEvent)
                expandedEventIds.erase(event.instanceId);
            else
                expandedEventIds.insert(event.instanceId);
        }
        y += HeaderHeight;
        if (!expandedEvent)
        {
            y += 4.0f;
            continue;
        }
        UiText::DrawFit(event.description.empty() ? "No details" : event.description,
                        Rectangle{viewport.x + 10.0f, y, viewport.width - 18.0f, 18.0f},
                        12, Color{185, 198, 211, 255});
        y += 28.0f;
        if (event.appliedEffects.empty())
        {
            UiText::DrawFit("No effective change",
                            Rectangle{viewport.x + 10.0f, y, viewport.width - 18.0f, 18.0f},
                            11, UiTheme::ParchmentDim);
            y += EffectHeight;
        }
        else
        {
            const std::size_t visibleEffects = std::min(
                CampaignJournalMaxVisibleEffects, event.appliedEffects.size());
            for (std::size_t effectIndex = 0; effectIndex < visibleEffects; ++effectIndex)
            {
                const AppliedWorldEventEffect& effect = event.appliedEffects[effectIndex];
                float textX = viewport.x + 10.0f;
                if (effect.kind == AppliedWorldEventEffectKind::ResourceDelta &&
                    effect.resourceType != ResourceType::Null)
                {
                    GuiPanel::DrawResourceIcon(effect.resourceType,
                        {textX, y + 1.0f, 18.0f, 18.0f});
                    textX += 24.0f;
                }
                UiText::DrawFit(AppliedWorldEventEffectLabel(effect),
                                Rectangle{textX, y + 2.0f,
                                          viewport.x + viewport.width - textX - 8.0f, 18.0f},
                                11, UiTheme::Cyan);
                y += EffectHeight;
            }
            if (visibleEffects < event.appliedEffects.size())
            {
                UiText::DrawFit("... +" + std::to_string(event.appliedEffects.size() - visibleEffects) +
                                    " more",
                                Rectangle{viewport.x + 10.0f, y, viewport.width - 18.0f, 18.0f},
                                11, UiTheme::ParchmentDim);
                y += EffectHeight;
            }
        }
        y += 12.0f;
    }

    std::string operationSummary;
    Color operationColor = UiTheme::Cyan;
    const auto activeBattle = std::find_if(snapshot.battleStatuses.rbegin(),
        snapshot.battleStatuses.rend(), [](const BattleStatusView& battle)
        {
            return battle.status == BattleLifecycleStatus::InTransit ||
                   battle.status == BattleLifecycleStatus::Active;
        });
    if (activeBattle != snapshot.battleStatuses.rend())
        operationSummary = std::string(activeBattle->isRaid ? "RAID" : "Battle") +
            " in province " + std::to_string(activeBattle->targetProvinceId) +
            " | ETA " + FormatDurationTicks(activeBattle->remainingTicks);
    else
    {
        const auto activeJourney = std::find_if(snapshot.journeyStatuses.rbegin(),
            snapshot.journeyStatuses.rend(), [](const JourneyStatusView& journey)
            {
                return IsActiveJourneyStatus(journey.status);
            });
        if (activeJourney != snapshot.journeyStatuses.rend())
            operationSummary = "Active journey " +
                std::to_string(activeJourney->originProvinceId) + " -> " +
                std::to_string(activeJourney->targetProvinceId) +
                " | ETA " + FormatDurationTicks(activeJourney->remainingTicks);
        else if (!snapshot.battleReports.empty())
        {
            const BattleReportView& report = snapshot.battleReports.back();
            operationSummary = std::string(report.raid ? "Raid" : "Battle") +
                " resolved in province " + std::to_string(report.targetProvinceId) +
                " | " + ToString(report.winner);
            operationColor = report.winner == BattleWinner::Defender
                ? UiTheme::SageBright : UiTheme::RustBright;
        }
    }
    if (!operationSummary.empty() && y + 20.0f < viewportTop + viewportHeight)
        UiText::DrawFit(operationSummary,
                        {viewport.x + 4.0f, y, viewport.width - 8.0f, 20.0f},
                        15, operationColor);
    EndScissorMode();
}

void CampaignSidebarWidget::UpdateSize(Vec2i newWindowSize)
{
    windowSize = newWindowSize;
    ChangePosition(0, 0);
    ChangeSize(newWindowSize.x, newWindowSize.y);
    if (provinces != nullptr)
        provinces->UpdateSize(newWindowSize);
    if (journal != nullptr)
        journal->UpdateSize(newWindowSize);
}

void CampaignSidebarWidget::Update(double dt)
{
    if (provinces == nullptr || journal == nullptr)
        return;
    journal->ChangePosition(provinces->pos.x, provinces->pos.y + 46);
    provinces->Update(dt);

    const int bottomInset = 18;
    const int remaining = std::max(62, windowSize.y - journal->pos.y - bottomInset);
    journal->ChangeSize(provinces->size.x, std::min(journal->size.y, remaining));
    journal->Update(dt);
}

bool CampaignSidebarWidget::CapturesPointer(Vector2 point) const
{
    return (provinces != nullptr && provinces->CapturesPointer(point)) ||
           (journal != nullptr && journal->CapturesPointer(point));
}

GlobalMapGuiSystem::GlobalMapGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = dynamic_cast<GameScene*>(owner->scene);
    panel.scene = scene;
    // Match the decision-tree interaction model: the GuiController owns
    // click, drag and wheel dispatch, while the widget owns its local map
    // transform and presentation.
    WireCommonSystemActions(*this, cameraMovement);
}

bool GlobalMapGuiSystem::CanActivate()
{
    return HasCompletedBarracks(scene);
}

void GlobalMapGuiSystem::UpdateUiWidgets(Vec2i size)
{
    panel.UpdateSize(size);
}

void GlobalMapGuiSystem::Update(double dt)
{
    (void)dt;
    owner->AddUiWidget(&panel);
}

void GlobalMapGuiSystem::EscPressed()
{
    panel.EndPanning();
    owner->ChangeSystem("default");
}

void GlobalMapGuiSystem::BuildPressed()
{
    panel.EndPanning();
    owner->ChangeSystem("build");
}

void GlobalMapGuiSystem::RoadBuildPressed()
{
    panel.EndPanning();
    owner->ChangeSystem("road_build");
}

void GlobalMapGuiSystem::DestroyPressed()
{
    panel.EndPanning();
    owner->ChangeSystem("destroy");
}

void GlobalMapGuiSystem::StockpilePressed()
{
    panel.EndPanning();
    owner->ChangeSystem("stockpile");
}

void GlobalMapGuiSystem::StatsPressed()
{
    panel.EndPanning();
    owner->ChangeSystem("stats");
}

void GlobalMapGuiSystem::FocusPressed()
{
    panel.EndPanning();
    owner->ChangeSystem("focus");
}

void GlobalMapGuiSystem::TechPressed()
{
    panel.EndPanning();
    owner->ChangeSystem("tech");
}

void GlobalMapGuiSystem::RosterPressed()
{
    if (!HasCompletedBarracks(scene))
        return;
    panel.EndPanning();
    owner->ChangeSystem("roster");
}

void GlobalMapGuiSystem::LmbPressed()
{
    panel.HandleLeftClick();
}

void GlobalMapGuiSystem::LmbReleased()
{
}

void GlobalMapGuiSystem::RmbPressed()
{
    panel.BeginPanning();
}

void GlobalMapGuiSystem::RmbReleased()
{
    panel.EndPanning();
}

void GlobalMapGuiSystem::Scroll()
{
    const Vector2 mouse = GetMousePosition();
    if (scene != nullptr && scene->campaignSidebar.CapturesPointer(mouse))
        return;
    panel.AdjustMapZoom({static_cast<int>(mouse.x), static_cast<int>(mouse.y)},
                        InputManager::GetMouseWheelMove());
}
