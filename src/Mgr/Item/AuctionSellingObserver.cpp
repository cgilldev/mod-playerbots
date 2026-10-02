/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AuctionSellingObserver.h"

#include "Bag.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"

AuctionSellingObserver::AuctionSellingObserver(uint32_t initialDelayMilliseconds)
{
    _events.ScheduleEvent(EVENT_REFRESH, Milliseconds(initialDelayMilliseconds));
}

AuctionEarnedInventory::Identity AuctionSellingObserver::GetIdentity(Item const* item)
{
    return {item->GetEntry(), item->GetItemRandomPropertyId(), item->GetItemSuffixFactor()};
}

void AuctionSellingObserver::RecordStored(Item const* item, uint32_t count)
{
    if (item)
        _inventory.RecordStored(item->GetGUID().GetRawValue(), GetIdentity(item), item->GetCount(), count);
}

void AuctionSellingObserver::RecordLoot(Item const* item, uint32_t count)
{
    if (item)
        _inventory.RecordLoot(item->GetGUID().GetRawValue(), GetIdentity(item), item->GetCount(), count);
}

void AuctionSellingObserver::Forget(Item const* item)
{
    if (item)
        _inventory.Forget(item->GetGUID().GetRawValue());
}

void AuctionSellingObserver::Update(PlayerbotAI* botAI, uint32_t elapsedMilliseconds)
{
    _events.Update(elapsedMilliseconds);
    if (_events.ExecuteEvent() != EVENT_REFRESH)
        return;

    _events.ScheduleEvent(EVENT_REFRESH, Milliseconds(REFRESH_MILLISECONDS));
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
        return;

    // Observation never schedules travel or interferes with combat/group work.
    if (bot->GetGroup() || botAI->GetMaster() || !bot->IsAlive() || bot->IsInCombat())
    {
        _candidates.clear();
        return;
    }

    Refresh(botAI);
}

void AuctionSellingObserver::Refresh(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    std::vector<Candidate> candidates;
    std::array<uint32_t, DISPOSITION_COUNT> counts{};
    std::unordered_set<uint64_t> presentGuids;

    auto observe = [&](Item* item)
    {
        if (!item)
            return;

        uint64_t guid = item->GetGUID().GetRawValue();
        presentGuids.insert(guid);
        uint32_t earnedCount = _inventory.ObserveCount(guid, GetIdentity(item), item->GetCount());
        if (!earnedCount)
        {
            ++counts[static_cast<std::size_t>(AuctionEarnedInventory::Disposition::Unproven)];
            return;
        }
        ItemTemplate const* proto = item->GetTemplate();
        std::string qualifier =
            std::to_string(item->GetEntry()) + "," + std::to_string(item->GetItemRandomPropertyId());
        // A private fresh value keeps observation from resetting the normal
        // AI's shared classification cache or changing its vendor decisions.
        ItemUsageValue usageValue(botAI);
        usageValue.Qualify(qualifier);
        ItemUsage usage = usageValue.Calculate();
        bool tradeable = item->CanBeTraded() && !item->IsEquipped() && !item->IsNotEmptyBag() &&
                         !proto->HasFlag(ITEM_FLAG_CONJURED) && !item->GetUInt32Value(ITEM_FIELD_DURATION);
        auto disposition =
            AuctionEarnedInventory::Classify(item->GetCount(), earnedCount, usage == ITEM_USAGE_AH, tradeable);
        ++counts[static_cast<std::size_t>(disposition)];
        if (disposition == AuctionEarnedInventory::Disposition::WholeStackCandidate)
        {
            candidates.push_back({guid, item->GetEntry(), item->GetCount(), item->GetItemRandomPropertyId(),
                                  item->GetItemSuffixFactor(),
                                  static_cast<uint64_t>(proto->SellPrice) * item->GetCount()});
        }
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        observe(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
        Bag* bag = bagItem ? bagItem->ToBag() : nullptr;
        if (!bag)
            continue;

        for (uint8 slot = 0; slot < bag->GetBagSize(); ++slot)
            observe(bag->GetItemByPos(slot));
    }

    _inventory.Retain(presentGuids);
    if (!_reported || counts != _lastCounts || candidates != _candidates)
    {
        LOG_INFO("playerbots",
                 "Auction selling dry run: bot={} protected={} untradeable={} unproven={} "
                 "partial-earned={} whole-stack-candidates={} (inventory eligibility only; no posting)",
                 bot->GetGUID().ToString(), counts[0], counts[1], counts[2], counts[3], counts[4]);
        for (Candidate const& candidate : candidates)
        {
            LOG_INFO("playerbots",
                     "Auction selling dry run candidate: bot={} item-guid={} entry={} count={} "
                     "random-property={} vendor-value-copper={}",
                     bot->GetGUID().ToString(), candidate.ItemGuid, candidate.Entry, candidate.Count,
                     candidate.RandomProperty, candidate.VendorValue);
        }
    }
    _reported = true;
    _lastCounts = counts;
    _candidates = std::move(candidates);
}
