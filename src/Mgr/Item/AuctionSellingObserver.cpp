/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AuctionSellingObserver.h"

#include <atomic>

#include "AuctionCommerceMgr.h"
#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "CharacterDatabase.h"
#include "GameTime.h"
#include "ItemUsageValue.h"
#include "Mail.h"
#include "Playerbots.h"

AuctionSellingObserver::AuctionSellingObserver(uint32_t initialDelayMilliseconds)
{
    _generation = NextGeneration();
    _events.ScheduleEvent(EVENT_REFRESH, Milliseconds(initialDelayMilliseconds));
}

uint64_t AuctionSellingObserver::NextGeneration()
{
    static std::atomic<uint64_t> nextGeneration{0};
    return ++nextGeneration;
}

void AuctionSellingObserver::SetPending(bool pending)
{
    _pending = pending;
    _events.CancelEvent(EVENT_OPERATION_TIMEOUT);
    if (pending)
        _events.ScheduleEvent(EVENT_OPERATION_TIMEOUT, Seconds(30));
}

bool AuctionSellingObserver::IsParticipant(Player* bot)
{
    if (!bot || !sRandomPlayerbotMgr.IsProgressionCohortBot(bot))
        return false;
    auto const& selected = sPlayerbotAIConfig.auctionSellingDryRunBotGuids;
    return selected.empty() || selected.contains(static_cast<uint32>(bot->GetGUID().GetRawValue()));
}

void AuctionSellingObserver::Load(Player* bot)
{
    std::vector<uint32_t> words;
    for (PlayerSetting const& setting : bot->GetPlayerSettings(SETTINGS_SOURCE))
        words.push_back(setting.value);
    _nextTrip = _inventory.Load(words);
}

void AuctionSellingObserver::Save(Player* bot, CharacterDatabaseTransaction trans)
{
    PlayerSettingVector settings;
    for (uint32_t word : _inventory.Serialize(_nextTrip))
        settings.emplace_back(word);
    bot->ReplacePlayerSettings(SETTINGS_SOURCE, settings);
    trans->Append(PlayerSettingsStore::PrepareReplaceStatement(static_cast<uint32>(bot->GetGUID().GetRawValue()),
                                                               SETTINGS_SOURCE, settings));
}

void AuctionSellingObserver::ObserveCount(Item const* item, uint32_t previousCount, uint32_t count)
{
    if (item)
    {
        _inventory.ObserveNativeCount(item->GetGUID().GetRawValue(), GetIdentity(item), previousCount, count);
        if (!count)
            _inventory.Forget(item->GetGUID().GetRawValue());
    }
}

void AuctionSellingObserver::OnAuctionAdded(AuctionEntry* auction)
{
    if (_postingGuid && auction->item_guid.GetRawValue() == _postingGuid &&
        _inventory.MarkAuction(_postingGuid, auction->Id))
        auction->earnedBotStock = true;
}

bool AuctionSellingObserver::IsReserved(uint64_t guid) const
{
    if (!sPlayerbotAIConfig.auctionSellingEnabled || sPlayerbotAIConfig.auctionSellingDryRun ||
        (!_hasErrand && !_pending))
        return false;
    return std::find(_reserved.begin(), _reserved.end(), guid) != _reserved.end();
}

bool AuctionSellingObserver::CanPost(Item const* item)
{
    if (!item || _inventory.GetAttempts(item->GetGUID().GetRawValue()) >= 2)
        return false;
    uint32 count = _inventory.ObserveCount(item->GetGUID().GetRawValue(), GetIdentity(item), item->GetCount());
    return count && count == item->GetCount();
}

void AuctionSellingObserver::AcceptDestination(Destination destination)
{
    _flightNodes.clear();
    _flightTaken = false;
    _destination = destination;
    _hasErrand = true;
    _pending = false;
    _tripMilliseconds = 0;
}

void AuctionSellingObserver::Finish(PlayerbotAI* botAI)
{
    _hasErrand = false;
    _reserved.clear();
    SetPending(false);
    _postingGuid = 0;
    _returningGuid = 0;
    _nextTrip = static_cast<uint32>(GameTime::GetGameTime().count()) + sPlayerbotAIConfig.auctionSellingCooldownSeconds;
    botAI->rpgInfo.ChangeToIdle();
}

void AuctionSellingObserver::RecordIncoming(Item const* item)
{
    if (!item)
        return;
    if (_returningGuid && _inventory.RestoreReturn(_returningGuid, item->GetGUID().GetRawValue(), GetIdentity(item),
                                                   item->GetCount(), _returningCount))
        return;
    Forget(item);
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
    if (_hasErrand)
    {
        _tripMilliseconds =
            std::min<uint64_t>(static_cast<uint64_t>(_tripMilliseconds) + elapsedMilliseconds,
                               static_cast<uint64_t>(sPlayerbotAIConfig.auctionSellingTravelSeconds) * 1000);
        if (_tripMilliseconds >= sPlayerbotAIConfig.auctionSellingTravelSeconds * 1000 || botAI->GetBot()->GetGroup() ||
            botAI->GetMaster() || !botAI->GetBot()->IsAlive())
            Finish(botAI);
    }
    _events.Update(elapsedMilliseconds);
    uint32 event = _events.ExecuteEvent();
    if (event == EVENT_OPERATION_TIMEOUT)
    {
        _generation = NextGeneration();  // An old queued request can no longer touch this login.
        SetPending(false);
        if (_hasErrand)
            Finish(botAI);
        return;
    }
    if (event != EVENT_REFRESH)
        return;

    _events.ScheduleEvent(EVENT_REFRESH, Milliseconds(REFRESH_MILLISECONDS));
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
        return;

    // Refresh and errand planning yield to combat and group work.
    if (bot->GetGroup() || botAI->GetMaster() || !bot->IsAlive() || bot->IsInCombat())
    {
        if (!_hasErrand)
            _candidates.clear();
        return;
    }

    Refresh(botAI);
    if (!_hasErrand && !_pending && GameTime::GetGameTime().count() >= _nextTrip &&
        ((sPlayerbotAIConfig.auctionSellingEnabled && IsParticipant(bot)) || _inventory.HasAuctions() || _mailPending))
    {
        SetPending(AuctionCommerceMgr::QueuePlan(botAI, _generation));
    }
}

void AuctionSellingObserver::Refresh(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    _mailPending = std::any_of(bot->GetMails().begin(), bot->GetMails().end(),
                               [](Mail const* mail)
                               {
                                   return mail && mail->messageType == MAIL_AUCTION && !mail->COD &&
                                          mail->state != MAIL_STATE_DELETED &&
                                          mail->deliver_time <= GameTime::GetGameTime().count() &&
                                          (mail->money || !mail->items.empty());
                               });
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
        if (disposition == AuctionEarnedInventory::Disposition::WholeStackCandidate && _inventory.GetAttempts(guid) < 2)
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
                 "Auction commerce inventory: bot={} protected={} untradeable={} unproven={} "
                 "partial-earned={} whole-stack-candidates={} (inventory eligibility snapshot)",
                 bot->GetGUID().ToString(), counts[0], counts[1], counts[2], counts[3], counts[4]);
        for (Candidate const& candidate : candidates)
        {
            LOG_INFO("playerbots",
                     "Auction commerce candidate: bot={} item-guid={} entry={} count={} "
                     "random-property={} vendor-value-copper={}",
                     bot->GetGUID().ToString(), candidate.ItemGuid, candidate.Entry, candidate.Count,
                     candidate.RandomProperty, candidate.VendorValue);
        }
    }
    _reported = true;
    _lastCounts = counts;
    _candidates = std::move(candidates);
}
