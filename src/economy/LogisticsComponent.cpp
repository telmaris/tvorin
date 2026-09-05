#include "economy/Building.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "simulation/MapGenerator.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── LogisticsComponent ──────────────────────────────────────────────────────

bool LogisticsComponent::HasSupplier(ResourceType type) const
{
    auto it = suppliers.find(type);
    if (it == suppliers.end())
        return false;
    for (auto* s : it->second)
        if (s != nullptr) return true;
    return false;
}

bool LogisticsComponent::HasReceiver(ResourceType type) const
{
    return receivers.contains(type) && receivers.at(type) != nullptr;
}

bool LogisticsComponent::IsRestrictedToDirectSuppliers(ResourceType type) const
{
    auto it = suppliers.find(type);
    if (it == suppliers.end())
        return false;

    // SetSupplier evicts every warehouse supplier the moment a non-warehouse
    // one is wired in, and TileMap::ConnectReceiver is how the player does
    // that by hand. So "a direct producer and no warehouse" is the signature
    // of an explicit routing decision — honour it instead of quietly topping
    // the consumer up from the depot network behind the player's back.
    bool hasDirect = false;
    for (auto* supplier : it->second)
    {
        if (supplier == nullptr)
            continue;
        if (StockpileIndex::IsWarehouse(supplier))
            return false;
        hasDirect = true;
    }
    return hasDirect;
}

bool LogisticsComponent::AcceptsSupplierFor(ResourceType type, const Building* supplier) const
{
    auto it = suppliers.find(type);
    if (it == suppliers.end() || it->second.empty())
        return true;
    return std::find(it->second.begin(), it->second.end(), supplier) != it->second.end();
}

void LogisticsComponent::SetSupplier(ResourceType type, Building* supplier, Building& self)
{
    if (supplier == nullptr)
        return;

    auto& sups = suppliers[type];
    bool changed = false;

    if (!supplier->IsStorageLike())
    {
        size_t before = sups.size();
        sups.erase(std::remove_if(sups.begin(), sups.end(),
            [](Building* e){ return e != nullptr && e->IsStorageLike(); }), sups.end());
        changed = sups.size() != before;
    }

    if (std::find(sups.begin(), sups.end(), supplier) == sups.end())
    {
        sups.push_back(supplier);
        changed = true;
    }

    if (changed)
        pendingRequests[type] = CountIncomingResources(&self, type);
}

void LogisticsComponent::SetReceiver(ResourceType type, Building* receiver, Building& self,
                                     ProductionComponent& prod)
{
    auto it = receivers.find(type);
    Building* prev = (it != receivers.end()) ? it->second : nullptr;
    if (prev != nullptr && prev != receiver)
    {
        prev->RemoveSupplier(type, &self);
        if (prev->owner != nullptr && prev->provinceEconomy != nullptr &&
            prev->CanAcceptResource(type) && !prev->HasSupplier(type))
        {
            Building* storage = prev->provinceEconomy->tilemap->FindDefaultStorage(prev, prev->owner);
            if (storage != nullptr && storage != prev)
                prev->SetSupplier(type, storage);
        }
    }

    receivers[type] = receiver;
    auto alt = altReceivers.find(type);
    if (alt != altReceivers.end() && alt->second == receiver)
        altReceivers.erase(alt);
    if (receiver != nullptr)
        receiver->SetSupplier(type, &self);
}

void LogisticsComponent::SetAltReceiver(ResourceType type, Building* receiver, Building& self)
{
    if (receiver == nullptr)
        return;

    auto primary = receivers.find(type);
    if (primary != receivers.end() && primary->second == receiver)
        return;

    auto prev = altReceivers.find(type);
    if (prev != altReceivers.end() && prev->second != nullptr && prev->second != receiver)
        prev->second->RemoveSupplier(type, &self);

    altReceivers[type] = receiver;
    // An alternative receiver is a fallback destination for the producer;
    // symmetrically, it must be a fallback supplier for the consumer. Keep an
    // existing warehouse supplier instead of letting the direct producer
    // evict it — otherwise a Barracks wired as Inn's alternative starves every
    // time the Inn output buffer is between production cycles, even though HQ
    // still has a package available.
    auto* receiverLogistics = receiver->GetComponent<LogisticsComponent>();
    if (receiverLogistics != nullptr)
    {
        auto& suppliersForType = receiverLogistics->suppliers[type];
        if (std::find(suppliersForType.begin(), suppliersForType.end(), &self) == suppliersForType.end())
            suppliersForType.push_back(&self);
        receiverLogistics->pendingRequests[type] = CountIncomingResources(receiver, type);
    }
}

void LogisticsComponent::RemoveSupplier(ResourceType type, Building* supplier)
{
    auto it = suppliers.find(type);
    if (it == suppliers.end())
        return;

    auto& sups = it->second;
    sups.erase(std::remove(sups.begin(), sups.end(), supplier), sups.end());
    if (sups.empty())
        suppliers.erase(it);

    pendingRequests.erase(type);
}

void LogisticsComponent::RemoveReceiver(ResourceType type, Building* receiver, Building& self,
                                         ProductionComponent&)
{
    auto it = receivers.find(type);
    if (it != receivers.end() && it->second == receiver)
    {
        receivers.erase(it);
        auto alt = altReceivers.find(type);
        if (alt != altReceivers.end() && alt->second != nullptr && alt->second != receiver)
        {
            receivers[type] = alt->second;
            altReceivers.erase(alt);
        }
        return;
    }

    auto alt = altReceivers.find(type);
    if (alt != altReceivers.end() && alt->second == receiver)
        altReceivers.erase(alt);
}

int LogisticsComponent::RequestResource(ResourceType type, int amount, Building& self)
{
    if (amount <= 0)
        return 0;

    int sent = 0;
    std::vector<Building*> tried;
    auto pullFrom = [&](Building* source)
    {
        if (source == nullptr || source == &self)
            return;
        int missing = amount - sent;
        if (missing <= 0)
            return;
        // A wired supplier can also show up in the warehouse ranking below;
        // asking it twice in one tick is pure waste (each attempt walks the
        // road network) and would let one source appear to serve more than it
        // has.
        if (std::find(tried.begin(), tried.end(), source) != tried.end())
            return;
        tried.push_back(source);
        sent += source->HandleTransport(type, missing, &self);
    };

    // Explicitly wired suppliers first — a direct producer link is a player
    // decision and must win over generic warehouse stock.
    auto wired = suppliers.find(type);
    if (wired != suppliers.end())
        for (auto* supplier : wired->second)
            pullFrom(supplier);

    // Then the rest of the warehouse network, nearest connected first. Without
    // this, stock sitting in a warehouse the consumer happens not to be wired
    // to is unreachable and the consumer starves beside a full depot — which
    // is exactly what the removed ambient storage push used to paper over
    // (see StorageComponent.cpp). RankSourcesFor only returns warehouses that
    // hold the type and have a road path here, so an unreachable depot is
    // never even attempted.
    if (sent < amount && !IsRestrictedToDirectSuppliers(type))
        for (Building* warehouse : StockpileIndex::RankSourcesFor(type, self))
            pullFrom(warehouse);

    if (sent < amount)
        requestBlocked = true;

    pendingRequests[type] += sent;
    return sent;
}

void LogisticsComponent::MaintainRequests(Building& self, ProductionComponent& prod)
{
    requestBlocked = false;
    for (auto& [res, buf] : prod.inputBuffers)
    {
        int stored  = static_cast<int>(buf.buffer.size());
        int pending = CountIncomingResources(&self, res);
        pendingRequests[res] = pending;
        int missing = buf.bufferSize - stored - pending;
        if (missing > 0)
            RequestResource(res, missing, self);
    }
}

void LogisticsComponent::DispatchOutputs(Building& self, ProductionComponent& prod)
{
    for (auto& [res, amount] : prod.products)
    {
        auto recvIt = receivers.find(res);
        Building* recv = recvIt != receivers.end() ? recvIt->second : nullptr;
        if (recv == nullptr && self.owner != nullptr && self.provinceEconomy != nullptr)
        {
            Building* storage = self.provinceEconomy->tilemap->FindDefaultStorage(&self, self.owner);
            if (storage != nullptr && storage->CanAcceptResource(res))
            {
                receivers[res] = storage;
                recv = storage;
            }
        }

        std::vector<Building*> targets;
        auto isStorageHub = [](const Building* target)
        {
            return target != nullptr &&
                (target->buildingType == BuildingType::Headquarters ||
                 target->buildingType == BuildingType::StorageBuilding);
        };
        // Direct consumers go first; HQ/storage is overflow. Otherwise the
        // hub can reserve every freshly produced item before an alternative
        // thematic receiver gets a chance to consume it.
        if (recv != nullptr && !isStorageHub(recv))
            targets.push_back(recv);
        auto alt = altReceivers.find(res);
        if (alt != altReceivers.end() && alt->second != nullptr && alt->second != recv)
            targets.push_back(alt->second);
        if (recv != nullptr && isStorageHub(recv))
            targets.push_back(recv);
        if (self.owner != nullptr && self.provinceEconomy != nullptr)
        {
            Building* storage = self.provinceEconomy->tilemap->FindDefaultStorage(&self, self.owner);
            if (storage != nullptr && storage != recv && storage->CanAcceptResource(res) &&
                std::find(targets.begin(), targets.end(), storage) == targets.end())
                targets.push_back(storage);
        }

        for (auto* target : targets)
        {
            int freeCapacity = GetReceiveCapacity(target, res);
            while (freeCapacity > 0)
            {
                auto [avail, r] = prod.outputBuffers[res].GetResource();
                if (!avail)
                    break;

                // Debug-level: "transport started" is too noisy (spam when retrying without roads)
                if (!self.owner->BeginTransport(&self, target, r))
                {
                    prod.outputBuffers[res].AddResource(r);
                    break;
                }
                freeCapacity--;
            }
        }
    }
}

int LogisticsComponent::HandleTransportFrom(ResourceType type, int amount, Building* receiver,
                                              Building& self, ProductionComponent& prod)
{
    int sent = 0;
    int requested = std::min(amount, GetReceiveCapacity(receiver, type));
    for (int i = 0; i < requested; i++)
    {
        auto [avail, res] = prod.outputBuffers[type].GetResource();
        if (avail)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(type))
            {
                prod.outputBuffers[type].AddResource(res);
                break;
            }
            // Debug-level: "transport started" is too noisy (spam when retrying without roads)
            if (self.owner->BeginTransport(&self, receiver, res))
                sent++;
            else
            {
                prod.outputBuffers[type].AddResource(res);
                break;
            }
        }
    }
    return sent;
}

bool LogisticsComponent::IsConnectedToRoadNetwork(Building& self) const
{
    if (self.owner == nullptr || self.provinceEconomy == nullptr ||
        self.provinceEconomy->roadNetwork == nullptr)
        return false;

    for (Building* storage : self.provinceEconomy->storages)
    {
        if (storage == nullptr || storage == &self)
            continue;
        if (!self.provinceEconomy->roadNetwork->CalculatePath(&self, storage).empty())
            return true;
    }
    return false;
}

std::vector<BuildingConnectionView> LogisticsComponent::GetSupplierViews(
    const ProductionComponent& prod) const
{
    std::vector<BuildingConnectionView> result;
    for (const auto& [res, amount] : prod.ingredients)
    {
        auto it = suppliers.find(res);
        if (it == suppliers.end() || it->second.empty())
        {
            result.push_back({res, nullptr});
            continue;
        }
        for (auto* sup : it->second)
            result.push_back({res, sup});
    }
    return result;
}

std::vector<BuildingConnectionView> LogisticsComponent::GetReceiverViews(
    const ProductionComponent& prod) const
{
    std::vector<BuildingConnectionView> result;
    for (const auto& [res, amount] : prod.products)
    {
        auto it = receivers.find(res);
        result.push_back({res, it != receivers.end() ? it->second : nullptr, false});
        auto alt = altReceivers.find(res);
        if (alt != altReceivers.end() && alt->second != nullptr &&
            (it == receivers.end() || alt->second != it->second))
            result.push_back({res, alt->second, true});
    }
    return result;
}

