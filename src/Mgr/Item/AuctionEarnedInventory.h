/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUCTIONEARNEDINVENTORY_H
#define PLAYERBOTS_AUCTIONEARNEDINVENTORY_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Session-local observation only. This ledger cannot authorize live posting:
// durable provenance and transaction reconciliation must be added first.
class AuctionEarnedInventory
{
public:
    struct Identity
    {
        uint32_t Entry;
        int32_t RandomProperty;
        uint32_t SuffixFactor;

        bool operator==(Identity const&) const = default;
    };

    enum class Disposition
    {
        Protected,
        Untradeable,
        Unproven,
        PartialEarned,
        WholeStackCandidate
    };

    static Disposition Classify(uint32_t count, uint32_t earnedCount, bool surplus, bool tradeable)
    {
        if (!surplus)
            return Disposition::Protected;
        if (!tradeable)
            return Disposition::Untradeable;
        if (!count || !earnedCount || earnedCount > count)
            return Disposition::Unproven;
        if (earnedCount != count)
            return Disposition::PartialEarned;
        return Disposition::WholeStackCandidate;
    }

    // StoreNewItem precedes LootItem in the native loot path. New stock is
    // unproven until that matching loot event arrives, once, in the same call.
    void RecordStored(uint64_t guid, Identity identity, uint32_t count, uint32_t addedCount)
    {
        if (!guid || !count || !addedCount)
            return;
        // Long grouped/dead sessions may postpone inventory refresh. Bound
        // unmatched acquisition history rather than grow it without limit.
        if (_lots.size() >= MAX_TRACKED_ITEMS && !_lots.contains(guid))
            return;

        Lot& lot = _lots[guid];
        Observe(lot, identity, count);
        lot.EarnedCount = std::min(lot.EarnedCount, count - std::min(count, addedCount));
        lot.PendingCount = count;
        lot.PendingAddedCount = addedCount;
    }

    void RecordLoot(uint64_t guid, Identity identity, uint32_t count, uint32_t lootedCount)
    {
        auto found = _lots.find(guid);
        if (found == _lots.end())
            return;

        Lot& lot = found->second;
        if (lot.ItemIdentity != identity || !lootedCount || lot.PendingCount != count ||
            lot.PendingAddedCount != lootedCount)
            return;

        lot.EarnedCount = static_cast<uint32_t>(
            std::min<uint64_t>(count, static_cast<uint64_t>(lot.EarnedCount) + std::min(count, lootedCount)));
        lot.PendingAddedCount = 0;
    }

    uint32_t ObserveCount(uint64_t guid, Identity identity, uint32_t count)
    {
        auto found = _lots.find(guid);
        if (found == _lots.end())
            return 0;

        Observe(found->second, identity, count);
        // Acquisition callbacks are synchronous; a later snapshot cannot claim
        // an earlier unmatched store event as newly earned goods.
        found->second.PendingAddedCount = 0;
        return found->second.EarnedCount;
    }

    void Forget(uint64_t guid) { _lots.erase(guid); }

    void Retain(std::unordered_set<uint64_t> const& presentGuids)
    {
        for (auto it = _lots.begin(); it != _lots.end();)
        {
            if (!presentGuids.contains(it->first))
                it = _lots.erase(it);
            else
                ++it;
        }
    }

private:
    static constexpr std::size_t MAX_TRACKED_ITEMS = 256;

    struct Lot
    {
        Identity ItemIdentity{};
        uint32_t Count = 0;
        uint32_t EarnedCount = 0;
        uint32_t PendingCount = 0;
        uint32_t PendingAddedCount = 0;
    };

    static void Observe(Lot& lot, Identity identity, uint32_t count)
    {
        if (lot.ItemIdentity != identity)
            lot = {identity};

        // Charge all observed consumption to earned stock first. Unknown
        // additions and a later count increase never restore provenance.
        if (count < lot.Count)
        {
            uint32_t removedCount = lot.Count - count;
            lot.EarnedCount -= std::min(lot.EarnedCount, removedCount);
        }
        lot.EarnedCount = std::min(lot.EarnedCount, count);
        lot.Count = count;
    }

    std::unordered_map<uint64_t, Lot> _lots;
};

#endif
