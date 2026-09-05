#ifndef BOUNDED_HISTORY_H
#define BOUNDED_HISTORY_H

#include <cstddef>
#include <iterator>

namespace BoundedHistory
{
    template <typename Container, typename Predicate>
    std::size_t CountMatching(const Container& records, Predicate predicate)
    {
        std::size_t count = 0;
        for (const auto& [id, value] : records)
        {
            (void)id;
            if (predicate(value))
                ++count;
        }
        return count;
    }

    // Associative lifecycle containers are ordered by monotonic ID, so this
    // deterministically removes the oldest eligible records first while
    // preserving every active record.
    template <typename Container, typename Predicate>
    void TrimOldestMatching(Container& records, std::size_t maximumMatches,
                            Predicate predicate)
    {
        std::size_t matching = CountMatching(records, predicate);
        for (auto it = records.begin(); it != records.end() && matching > maximumMatches;)
        {
            if (predicate(it->second))
            {
                it = records.erase(it);
                --matching;
            }
            else
            {
                ++it;
            }
        }
    }

    template <typename Container>
    void TrimOldest(Container& records, std::size_t maximumSize)
    {
        if (records.size() <= maximumSize)
            return;
        auto endOfRemovedRange = records.begin();
        std::advance(endOfRemovedRange,
                     static_cast<typename Container::difference_type>(
                         records.size() - maximumSize));
        records.erase(records.begin(), endOfRemovedRange);
    }
}

#endif
