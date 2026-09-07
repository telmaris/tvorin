#include "economy/Building.h"
#include "economy/Player.h"
#include "core/Log.h"
#include "simulation/MapGenerator.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// ─── StorageComponent ────────────────────────────────────────────────────────

bool StorageComponent::CanAccept(ResourceType type) const
{
    return buffers.contains(type);
}

bool StorageComponent::CanReceive(ResourceType type) const
{
    auto it = buffers.find(type);
    return it != buffers.end() &&
           static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
}

void StorageComponent::AddResource(Resource* res, Building& self)
{
    if (res == nullptr)
        return;

    auto it = buffers.find(res->type);
    if (it == buffers.end() || static_cast<int>(it->second.buffer.size()) >= it->second.bufferSize)
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        return;
    }

    TVORIN_LOG_TRACE(self.tag, "resource added!");
    it->second.AddResource(res);
}

void StorageComponent::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr)
        return;
    buffers[res->type].AddResource(res);
}

Resource StorageComponent::GetResource(ResourceType type)
{
    auto [avail, res] = buffers[type].GetResource();
    if (!avail)
        return Resource{};
    Resource value = *res;
    Resource::DestroyOwned(res);
    return value;
}

int StorageComponent::HandleTransport(ResourceType type, int amount, Building* receiver,
                                       Building& self)
{
    int sent = 0;
    int requested = std::min(amount, GetReceiveCapacity(receiver, type));
    for (int i = 0; i < requested; i++)
    {
        auto [avail, res] = buffers[type].GetResource();
        if (avail)
        {
            if (receiver == nullptr || !receiver->CanReceiveResource(type))
            {
                buffers[type].AddResource(res);
                break;
            }
            // Debug-level: "transport started" is too noisy (spam when retrying without roads)
            if (self.owner->BeginTransport(&self, receiver, res))
                sent++;
            else
            {
                buffers[type].AddResource(res);
                break;
            }
        }
    }
    return sent;
}

// Storage is passive: consumers pull through StockpileIndex. Pushing from
// storage would make interchangeable warehouses bounce resources indefinitely.

std::vector<ResourceBufferView> StorageComponent::GetBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [res, buf] : buffers)
        result.push_back({res, static_cast<int>(buf.buffer.size()), buf.bufferSize, 0});
    return result;
}

bool LocalResourceBufferComponent::CanAccept(ResourceType type) const
{
    return buffers.contains(type);
}

bool LocalResourceBufferComponent::CanReceive(ResourceType type) const
{
    auto it = buffers.find(type);
    return it != buffers.end() &&
           static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
}

void LocalResourceBufferComponent::AddResource(Resource* res, Building& self)
{
    if (res == nullptr)
        return;

    auto it = buffers.find(res->type);
    if (it == buffers.end() || static_cast<int>(it->second.buffer.size()) >= it->second.bufferSize)
    {
        if (res->sourceBuilding != nullptr)
            res->sourceBuilding->ReturnOutgoingResource(res);
        return;
    }

    TVORIN_LOG_TRACE(self.tag, "local resource added!");
    it->second.AddResource(res);
}

void LocalResourceBufferComponent::ReturnOutgoingResource(Resource* res)
{
    if (res != nullptr)
        buffers[res->type].AddResource(res);
}

Resource LocalResourceBufferComponent::GetResource(ResourceType type)
{
    auto [available, resource] = buffers[type].GetResource();
    if (!available)
        return Resource{};
    Resource value = *resource;
    Resource::DestroyOwned(resource);
    return value;
}

std::vector<ResourceBufferView> LocalResourceBufferComponent::GetBufferViews() const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [type, buffer] : buffers)
        result.push_back({type, static_cast<int>(buffer.buffer.size()), buffer.bufferSize, 0});
    return result;
}

