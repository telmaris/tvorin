#include "data/Resource.h"

#include <gtest/gtest.h>

#include <set>

TEST(ResourceBufferTests, PresentationCatalogContainsEveryActiveTypeExactlyOnce)
{
    std::set<ResourceType> catalog{std::begin(resourceTypes), std::end(resourceTypes)};

    EXPECT_EQ(catalog.size(), std::size(resourceTypes));
    EXPECT_EQ(catalog.size(), 73u);
    EXPECT_LT(ResourcePresentationRank(ResourceType::WOOD),
              ResourcePresentationRank(ResourceType::PLANKS));
    EXPECT_LT(ResourcePresentationRank(ResourceType::PLANKS),
              ResourcePresentationRank(ResourceType::STONE));
    EXPECT_LT(ResourcePresentationRank(ResourceType::BRICKS),
              ResourcePresentationRank(ResourceType::COAL));
    EXPECT_LT(ResourcePresentationRank(ResourceType::TOOLS),
              ResourcePresentationRank(ResourceType::WHEAT));
    EXPECT_LT(ResourcePresentationRank(ResourceType::FOOD_PROVISIONS),
              ResourcePresentationRank(ResourceType::CATTLE));
    EXPECT_LT(ResourcePresentationRank(ResourceType::HORSE),
              ResourcePresentationRank(ResourceType::HEMP));
    EXPECT_LT(ResourcePresentationRank(ResourceType::BRONZE_SWORD),
              ResourcePresentationRank(ResourceType::IRON_SWORD));
    EXPECT_LT(ResourcePresentationRank(ResourceType::IRON_SWORD),
              ResourcePresentationRank(ResourceType::STEEL_SWORD));
}

TEST(ResourceBufferTests, AddResourceRespectsCapacity)
{
    Resource woodA{ResourceType::WOOD};
    Resource woodB{ResourceType::WOOD};
    Resource woodC{ResourceType::WOOD};
    ResourceBuffer buffer{ResourceType::WOOD, 2};

    buffer.AddResource(&woodA);
    buffer.AddResource(&woodB);
    buffer.AddResource(&woodC);

    EXPECT_EQ(buffer.buffer.size(), 2u);
}

TEST(ResourceBufferTests, GetResourceReturnsStoredResourcesLastInFirstOut)
{
    Resource first{ResourceType::WOOD};
    Resource second{ResourceType::WOOD};
    ResourceBuffer buffer{ResourceType::WOOD, 2};
    buffer.AddResource(&first);
    buffer.AddResource(&second);

    auto [hasSecond, secondPtr] = buffer.GetResource();
    auto [hasFirst, firstPtr] = buffer.GetResource();
    auto [hasNone, nonePtr] = buffer.GetResource();

    EXPECT_TRUE(hasSecond);
    EXPECT_EQ(secondPtr, &second);
    EXPECT_TRUE(hasFirst);
    EXPECT_EQ(firstPtr, &first);
    EXPECT_FALSE(hasNone);
    EXPECT_EQ(nonePtr, nullptr);
}

TEST(ResourceBufferTests, SetStoredAmountUsesStaticPoolAndClampsToCapacity)
{
    ResourceBuffer buffer{ResourceType::STONE, 3};

    buffer.SetStoredAmount(5);
    EXPECT_EQ(buffer.buffer.size(), 3u);
    for (auto* resource : buffer.buffer)
    {
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->type, ResourceType::STONE);
    }

    buffer.Clear();
    EXPECT_TRUE(buffer.buffer.empty());
}

TEST(ResourceBufferTests, GameplayResourcesReturnToSharedFixedPool)
{
    ResourcePool pool;
    const std::size_t before = pool.Available();
    Resource* resource = Resource::CreateOwned(ResourceType::WOOD);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(pool.Available(), before - 1);
    EXPECT_TRUE(resource->ownedAllocation);

    Resource::DestroyOwned(resource);
    EXPECT_EQ(pool.Available(), before);
}

TEST(ResourceBufferTests, RepeatedShortLivedBuffersReleaseOwnedResources)
{
    for (int i = 0; i < 10000; ++i)
    {
        ResourceBuffer buffer{ResourceType::WOOD, 4};
        buffer.SetStoredAmount(4);
        ASSERT_EQ(buffer.buffer.size(), 4u);
    }
}

TEST(ResourceBufferTests, CopyingAResourceDoesNotTransferAllocationOwnership)
{
    Resource original{ResourceType::WOOD};
    original.shipmentId = 42;
    original.transportPath = {1, 2};

    Resource detachedCopy = original;
    EXPECT_FALSE(detachedCopy.ownedAllocation);
    EXPECT_EQ(detachedCopy.shipmentId, 0u);
    EXPECT_TRUE(detachedCopy.transportPath.empty());

    Resource assigned{ResourceType::STONE};
    assigned.shipmentId = 7;
    assigned = original;
    EXPECT_FALSE(assigned.ownedAllocation);
    EXPECT_EQ(assigned.shipmentId, 0u);
    EXPECT_TRUE(assigned.transportPath.empty());

    ResourceBuffer buffer{ResourceType::STONE, 1};
    buffer.GenerateResource(ResourceType::STONE);
    ASSERT_EQ(buffer.buffer.size(), 1u);

    Resource copy = *buffer.buffer.front();
    EXPECT_FALSE(copy.ownedAllocation);
    buffer.Clear();
    EXPECT_EQ(copy.type, ResourceType::STONE);
}

TEST(ResourceQuantityTests, AddAndRemoveAreBoundedAndAtomic)
{
    ResourceQuantity quantity{ResourceType::WOOD, 5};

    EXPECT_TRUE(quantity.TryAdd(3));
    EXPECT_EQ(quantity.amount, 3);
    EXPECT_EQ(quantity.AvailableCapacity(), 2);

    EXPECT_FALSE(quantity.TryAdd(3));
    EXPECT_EQ(quantity.amount, 3);
    EXPECT_FALSE(quantity.TryRemove(4));
    EXPECT_EQ(quantity.amount, 3);

    EXPECT_TRUE(quantity.TryRemove(2));
    EXPECT_EQ(quantity.amount, 1);
    EXPECT_FALSE(quantity.TryAdd(-1));
    EXPECT_FALSE(quantity.TryRemove(-1));
    EXPECT_EQ(quantity.amount, 1);
}

TEST(ResourceQuantityTests, NegativeCapacityIsClampedWithoutCreatingAmount)
{
    ResourceQuantity quantity{ResourceType::STONE, -10};

    EXPECT_EQ(quantity.capacity, 0);
    EXPECT_EQ(quantity.amount, 0);
    EXPECT_FALSE(quantity.TryAdd(1));
}
