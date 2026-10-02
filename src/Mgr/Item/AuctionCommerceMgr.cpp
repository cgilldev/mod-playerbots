/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#include "AuctionCommerceMgr.h"

#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

#include "AuctionCommercePolicy.h"
#include "AuctionHouseMgr.h"
#include "AuctionHouseScript.h"
#include "BudgetValues.h"
#include "CharacterCache.h"
#include "CharacterDatabase.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "ItemUsageValue.h"
#include "Mail.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"

class CommerceOperation : public PlayerbotOperation
{
public:
    CommerceOperation(ObjectGuid guid, uint64_t generation, uint8 mode)
        : _guid(guid), _generation(generation), _mode(mode)
    {
    }
    ObjectGuid GetBotGuid() const override { return _guid; }
    std::string GetName() const override { return "Auction commerce"; }
    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(_guid);
        if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
            return false;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return false;
        AuctionSellingObserver* observer = botAI->GetAuctionSellingObserver();
        if (!observer || observer->GetGeneration() != _generation)
            return false;
        observer->SetPending(false);
        if (!bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() || botAI->GetMaster() || bot->IsBeingTeleported())
            return false;
        if (_mode == 2)
            AuctionCommerceMgr::Fly(botAI, observer);
        else if (_mode == 1)
            AuctionCommerceMgr::Interact(botAI, observer);
        else
            AuctionCommerceMgr::Plan(botAI, observer);
        return true;
    }

private:
    ObjectGuid _guid;
    uint64_t _generation;
    uint8 _mode;
};

class CommerceMarket
{
public:
    using Key = std::tuple<uint32, uint32, int32, uint32>;
    static bool CanStart(Player* bot)
    {
        static std::map<ObjectGuid, uint32> trips;
        uint32 count = 0;
        for (auto it = trips.begin(); it != trips.end();)
        {
            Player* other = ObjectAccessor::FindPlayer(it->first);
            PlayerbotAI* otherAI = other ? GET_PLAYERBOT_AI(other) : nullptr;
            auto observer = otherAI ? otherAI->GetAuctionSellingObserver() : nullptr;
            if (!observer || !observer->HasErrand())
                it = trips.erase(it);
            else
            {
                if (it->second == bot->GetTeamId())
                    ++count;
                ++it;
            }
        }
        if (count >= 2)
            return false;
        trips[bot->GetGUID()] = bot->GetTeamId();
        return true;
    }
    static void RestoreMarker(AuctionEntry* auction)
    {
        if (!auction || auction->earnedBotStock)
            return;
        static std::map<ObjectGuid, std::unordered_map<uint64_t, uint32>> ownedAuctions;
        auto cache = ownedAuctions.find(auction->owner);
        if (cache == ownedAuctions.end())
        {
            std::unordered_map<uint64_t, uint32> ids;
            auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_SETTINGS);
            stmt->SetData(0, static_cast<uint32>(auction->owner.GetRawValue()));
            if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            {
                do
                {
                    Field* fields = result->Fetch();
                    if (fields[0].Get<std::string>() != AuctionSellingObserver::SETTINGS_SOURCE)
                        continue;
                    auto settings = PlayerSettingsStore::ParseSettingsData(fields[1].Get<std::string>());
                    if (settings.size() < 3 || settings[0].value != 1 || settings[2].value > 256 ||
                        settings.size() != 3 + static_cast<size_t>(settings[2].value) * 9)
                        continue;
                    std::vector<uint32_t> words;
                    for (auto const& setting : settings)
                        words.push_back(setting.value);
                    AuctionEarnedInventory ledger;
                    ledger.Load(words);
                    for (size_t offset = 3; offset < words.size(); offset += 9)
                    {
                        uint64_t guid = words[offset] | (static_cast<uint64_t>(words[offset + 1]) << 32);
                        if (uint32 id = ledger.GetAuctionId(guid))
                            ids.emplace(guid, id);
                    }
                } while (result->NextRow());
            }
            cache = ownedAuctions.emplace(auction->owner, std::move(ids)).first;
        }
        auto lot = cache->second.find(auction->item_guid.GetRawValue());
        if (lot != cache->second.end() && lot->second == auction->Id)
            auction->earnedBotStock = true;
    }
    static void RestoreMarkers()
    {
        std::unordered_set<AuctionHouseObject*> visited;
        for (uint32 faction : {12u, 29u, 120u})
            if (auto house = sAuctionMgr->GetAuctionsMap(faction); house && visited.insert(house).second)
                for (auto const& [id, auction] : house->GetAuctions())
                    RestoreMarker(auction);
    }
    static std::map<Key, std::vector<uint64_t>>& Sales()
    {
        static std::map<Key, std::vector<uint64_t>> sales;
        return sales;
    }
    static void RecordSale(AuctionEntry* auction)
    {
        if (!auction || auction->syntheticBuyer || !auction->bid || !auction->itemCount)
            return;
        if (Item* item = sAuctionMgr->GetAItem(auction->item_guid))
        {
            if (Sales().size() >= 4096)
                Sales().erase(Sales().begin());
            auto& samples = Sales()[{static_cast<uint32>(auction->GetHouseId()), item->GetEntry(),
                                     item->GetItemRandomPropertyId(), item->GetItemSuffixFactor()}];
            if (samples.size() >= 16)
                samples.erase(samples.begin());
            samples.push_back(auction->bid / auction->itemCount);
        }
    }
    static std::vector<AuctionSellingObserver::Destination> const& Targets()
    {
        static std::vector<AuctionSellingObserver::Destination> targets;
        if (!targets.empty())
            return targets;
        for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
        {
            CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(data.id);
            if (proto && ((data.npcflag ? data.npcflag : proto->npcflag) & UNIT_NPC_FLAG_AUCTIONEER))
                targets.push_back({data.mapid, data.id, spawn, data.posX, data.posY, data.posZ, false});
        }
        for (auto const& [spawn, data] : sObjectMgr->GetAllGOData())
        {
            GameObjectTemplate const* proto = sObjectMgr->GetGameObjectTemplate(data.id);
            if (proto && proto->type == GAMEOBJECT_TYPE_MAILBOX)
                targets.push_back({data.mapid, data.id, spawn, data.posX, data.posY, data.posZ, true});
        }
        return targets;
    }
    static std::map<Key, uint64_t> const& Quotes()
    {
        static std::map<Key, uint64_t> quotes;
        static uint32 built = 0;
        if (built && GetMSTimeDiffToNow(built) < 60000)
            return quotes;
        built = getMSTime();
        RestoreMarkers();
        quotes.clear();
        std::map<Key, std::vector<uint64_t>> asks;
        std::unordered_set<AuctionHouseObject*> visited;
        for (uint32 faction : {12u, 29u, 120u})
        {
            AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(faction);
            if (!house || !visited.insert(house).second)
                continue;
            for (auto const& [id, auction] : house->GetAuctions())
            {
                if (!auction || !auction->buyout || !auction->itemCount || auction->earnedBotStock ||
                    auction->syntheticStock)
                    continue;
                Item* item = sAuctionMgr->GetAItem(auction->item_guid);
                if (item)
                    asks[{static_cast<uint32>(auction->GetHouseId()), item->GetEntry(), item->GetItemRandomPropertyId(),
                          item->GetItemSuffixFactor()}]
                        .push_back(auction->buyout / auction->itemCount);
            }
        }
        for (auto& [key, values] : asks)
        {
            std::sort(values.begin(), values.end());
            quotes[key] = values[values.size() / 2];
        }
        for (auto const& [key, samples] : Sales())
        {
            auto values = samples;
            std::sort(values.begin(), values.end());
            if (!values.empty())
                quotes[key] = values[values.size() / 2];
        }
        return quotes;
    }
    static bool HasActive(uint32 id, ObjectGuid owner, ObjectGuid itemGuid)
    {
        for (uint32 faction : {12u, 29u, 120u})
            if (AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(faction))
                if (auto auction = house->GetAuction(id);
                    auction && auction->owner == owner && auction->item_guid == itemGuid)
                    return true;
        return false;
    }
    static bool WorthListing(PlayerbotAI* botAI, AuctionSellingObserver* observer, Item* item,
                             AuctionHouseEntry const* house, uint64_t& price, uint64_t& benefit, uint32& deposit)
    {
        if (!item || !observer->CanPost(item) || !item->CanBeTraded() || item->IsNotEmptyBag() || item->IsEquipped() ||
            item->GetTemplate()->HasFlag(ITEM_FLAG_CONJURED) || item->GetUInt32Value(ITEM_FIELD_DURATION) ||
            observer->GetAttempts(item->GetGUID().GetRawValue()) >= 2)
            return false;
        ItemUsageValue usage(botAI);
        usage.Qualify(std::to_string(item->GetEntry()) + "," + std::to_string(item->GetItemRandomPropertyId()));
        if (usage.Calculate() != ITEM_USAGE_AH)
            return false;
        auto const& quotes = Quotes();
        auto quote = quotes.find(
            {house->houseId, item->GetEntry(), item->GetItemRandomPropertyId(), item->GetItemSuffixFactor()});
        uint64 vendor = static_cast<uint64>(item->GetTemplate()->SellPrice) * item->GetCount();
        uint64 market = quote == quotes.end() ? 0 : quote->second * item->GetCount();
        price = AuctionCommercePolicy::Price(vendor, market, observer->GetAttempts(item->GetGUID().GetRawValue()));
        if (!price)
            return false;
        AuctionEntry temporary{};
        temporary.bid = static_cast<uint32>(price);
        temporary.auctionHouseEntry = house;
        deposit = sAuctionMgr->GetAuctionDeposit(house, 12 * HOUR, item, item->GetCount());
        benefit = AuctionCommercePolicy::ExpectedBenefit(price, temporary.GetAuctionCut(), deposit, vendor);
        return benefit >= std::max<uint64>(5, vendor / 5);
    }
};

bool AuctionCommerceMgr::QueuePlan(PlayerbotAI* botAI, uint64_t generation)
{
    return PlayerbotWorldThreadProcessor::instance().QueueOperation(
        std::make_unique<CommerceOperation>(botAI->GetBot()->GetGUID(), generation, false));
}
bool AuctionCommerceMgr::QueueInteraction(PlayerbotAI* botAI, uint64_t generation)
{
    return PlayerbotWorldThreadProcessor::instance().QueueOperation(
        std::make_unique<CommerceOperation>(botAI->GetBot()->GetGUID(), generation, true));
}

bool AuctionCommerceMgr::QueueFlight(PlayerbotAI* botAI, uint64_t generation)
{
    return PlayerbotWorldThreadProcessor::instance().QueueOperation(
        std::make_unique<CommerceOperation>(botAI->GetBot()->GetGUID(), generation, 2));
}

void AuctionCommerceMgr::Fly(PlayerbotAI* botAI, AuctionSellingObserver* observer)
{
    Player* bot = botAI->GetBot();
    if (!observer->HasErrand() || !observer->NeedsFlight())
        return;
    auto const& target = observer->GetMovementTarget();
    auto range = bot->GetMap()->GetCreatureBySpawnIdStore().equal_range(target.Spawn);
    for (auto it = range.first; it != range.second; ++it)
    {
        Creature* master = bot->GetNPCIfCanInteractWith(it->second->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER);
        if (!master)
            continue;
        for (uint32 node : observer->GetFlightNodes())
            if (!bot->m_taxi.IsTaximaskNodeKnown(node))
            {
                observer->Finish(botAI);
                return;
            }
        botAI->RemoveShapeshift();
        if (bot->IsMounted())
            bot->Dismount();
        if (bot->ActivateTaxiPathTo(observer->GetFlightNodes(), master, 0))
            observer->MarkFlightTaken();
        else
            observer->Finish(botAI);
        return;
    }
}

void AuctionCommerceMgr::Plan(PlayerbotAI* botAI, AuctionSellingObserver* observer)
{
    Player* bot = botAI->GetBot();
    if (!sWorld->getBoolConfig(CONFIG_PLAYER_SETTINGS_ENABLED) || bot->InBattleground() || bot->GetMap()->IsDungeon())
        return;
    if (bot->IsInFlight())
        return;
    bool needsMail = false;
    std::vector<uint32_t> words;
    for (PlayerSetting const& setting : bot->GetPlayerSettings(AuctionSellingObserver::SETTINGS_SOURCE))
        words.push_back(setting.value);
    if (words.size() >= 3 && words[0] == 1 && words.size() == 3 + static_cast<size_t>(words[2]) * 9)
        for (size_t offset = 3; offset < words.size(); offset += 9)
            if (words[offset + 8] && !CommerceMarket::HasActive(
                                         words[offset + 8], bot->GetGUID(),
                                         ObjectGuid(words[offset] | (static_cast<uint64_t>(words[offset + 1]) << 32))))
                needsMail = true;
    for (Mail const* mail : bot->GetMails())
        if (mail && mail->messageType == MAIL_AUCTION && !mail->COD && mail->state != MAIL_STATE_DELETED &&
            mail->deliver_time <= GameTime::GetGameTime().count() && (mail->money || !mail->items.empty()))
            needsMail = true;
    if (!needsMail && (!observer->IsParticipant(bot) || !sPlayerbotAIConfig.auctionSellingEnabled ||
                       sPlayerbotAIConfig.auctionSellingDryRun || observer->GetCandidates().empty() ||
                       sRandomPlayerbotMgr.IsProgressionPausedBot(bot)))
        return;
    uint32 sessionRemaining = sRandomPlayerbotMgr.GetProgressionSessionRemaining(bot);
    if (sessionRemaining <= 30)
        return;
    uint32 travelBudget = std::min(sPlayerbotAIConfig.auctionSellingTravelSeconds, sessionRemaining - 30);
    float bestScore = std::numeric_limits<float>::max();
    AuctionSellingObserver::Destination best{};
    std::vector<uint32> bestFlight;
    std::vector<uint64_t> bestReserved;
    auto master = sTravelMgr.GetNearestFlightMasterInfo(bot);
    for (auto const& target : CommerceMarket::Targets())
    {
        if (target.Mailbox != needsMail || target.Map != bot->GetMapId() || !std::isfinite(target.Z) ||
            target.Z < -1000.0f)
            continue;
        float distance = bot->GetExactDist(target.X, target.Y, target.Z);
        float routeSeconds = distance * 2.0f / std::max(1.0f, bot->GetSpeed(MOVE_RUN));
        std::vector<uint32> flight;
        uint64 fare = 0;
        if (master && bot->GetExactDist(master->pos) < 500.0f && distance > 500.0f)
        {
            uint32 end = sObjectMgr->GetNearestTaxiNode(target.X, target.Y, target.Z, target.Map, bot->GetTeamId());
            auto path = sTravelNodeMap.FindTaxiPath(master->taxiNodeId, end);
            float flightSeconds = bot->GetExactDist(master->pos) * 2.0f / std::max(1.0f, bot->GetSpeed(MOVE_RUN));
            bool known = path.size() >= 2;
            TaxiNodesEntry const* previous = nullptr;
            for (uint32 nodeId : path)
            {
                auto node = sTaxiNodesStore.LookupEntry(nodeId);
                if (!node || node->map_id != target.Map || !bot->m_taxi.IsTaximaskNodeKnown(nodeId))
                {
                    known = false;
                    break;
                }
                if (previous)
                {
                    float dx = node->x - previous->x, dy = node->y - previous->y, dz = node->z - previous->z;
                    flightSeconds += std::sqrt(dx * dx + dy * dy + dz * dz) / 24.0f;
                    uint32 pathId = 0, cost = 0;
                    sObjectMgr->GetTaxiPath(previous->ID, nodeId, pathId, cost);
                    fare += cost;
                }
                previous = node;
            }
            if (known && previous)
            {
                float dx = previous->x - target.X, dy = previous->y - target.Y, dz = previous->z - target.Z;
                flightSeconds +=
                    std::sqrt(dx * dx + dy * dy + dz * dz) * 2.0f / std::max(1.0f, bot->GetSpeed(MOVE_RUN));
                if (flightSeconds < routeSeconds &&
                    bot->GetMoney() > fare * 2 + sPlayerbotAIConfig.auctionSellingReserveCopper)
                {
                    flight = std::move(path);
                    routeSeconds = flightSeconds;
                }
                else
                    fare = 0;
            }
            else
                fare = 0;
        }
        if (routeSeconds * 2 > travelBudget)
            continue;
        uint64 totalBenefit = 0;
        std::vector<uint64_t> reserved;
        if (!needsMail)
        {
            CreatureTemplate const* npc = sObjectMgr->GetCreatureTemplate(target.Entry);
            FactionTemplateEntry const* faction = npc ? sFactionTemplateStore.LookupEntry(npc->faction) : nullptr;
            if (!faction || !faction->IsFriendlyTo(*bot->GetFactionTemplateEntry()))
                continue;
            AuctionHouseEntry const* house = sAuctionMgr->GetAuctionHouseEntryFromFactionTemplate(npc->faction);
            if (!house || (house->houseId == 7 && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION)))
                continue;
            uint64 deposits = 0;
            uint64 reserve = sPlayerbotAIConfig.auctionSellingReserveCopper + fare * 2 +
                             static_cast<uint64>(botAI->GetAiObjectContext()->GetValue<uint32>("repair cost")->Get()) +
                             botAI->GetAiObjectContext()->GetValue<uint32>("train cost")->Get();
            uint32 owned = 0;
            auto nativeHouse = sAuctionMgr->GetAuctionsMap(npc->faction);
            if (!nativeHouse)
                continue;
            for (auto const& [id, auction] : nativeHouse->GetAuctions())
                if (auction && auction->owner == bot->GetGUID())
                    ++owned;
            auto candidates = observer->GetCandidates();
            std::sort(candidates.begin(), candidates.end(),
                      [](auto const& left, auto const& right) { return left.VendorValue > right.VendorValue; });
            for (auto const& candidate : candidates)
            {
                if (reserved.size() >= sPlayerbotAIConfig.auctionSellingBatchSize ||
                    owned + reserved.size() >= sPlayerbotAIConfig.auctionSellingMaxListings)
                    break;
                Item* item = bot->GetItemByGuid(ObjectGuid(candidate.ItemGuid));
                uint64 price = 0, benefit = 0;
                uint32 deposit = 0;
                if (CommerceMarket::WorthListing(botAI, observer, item, house, price, benefit, deposit) &&
                    AuctionCommercePolicy::CanDeposit(bot->GetMoney(), reserve, deposits, deposit))
                {
                    deposits += deposit;
                    totalBenefit += benefit;
                    reserved.push_back(candidate.ItemGuid);
                }
            }
            if (totalBenefit < static_cast<uint64>(routeSeconds * bot->GetLevel() / 30.0f) + 5 + fare * 2)
                continue;
        }
        float score = routeSeconds / (needsMail ? 1.0f : std::sqrt(static_cast<float>(totalBenefit)));
        if (score < bestScore)
        {
            best = target;
            bestFlight = std::move(flight);
            bestReserved = std::move(reserved);
            bestScore = score;
        }
    }
    if (bestScore == std::numeric_limits<float>::max())
        return;
    float height = bot->GetMap()->GetHeight(bot->GetPhaseMask(), best.X, best.Y, best.Z);
    if (!std::isfinite(height) || std::abs(height - best.Z) > 10.0f)
        return;
    if (!CommerceMarket::CanStart(bot))
        return;
    observer->AcceptDestination(best);
    observer->Reserve(std::move(bestReserved));
    if (!bestFlight.empty() && master)
        observer->SetFlight({master->pos.GetMapId(), master->templateEntry, master->dbGuid, master->pos.GetPositionX(),
                             master->pos.GetPositionY(), master->pos.GetPositionZ(), false},
                            std::move(bestFlight));
    botAI->rpgInfo.data = NewRpgInfo::Commerce{};
    botAI->rpgInfo.startT = getMSTime();
    LOG_INFO("playerbots", "Auction commerce: bot={} destination={} spawn={} purpose={}", bot->GetGUID().ToString(),
             best.Entry, best.Spawn, needsMail ? "settlement" : "selling");
}

void AuctionCommerceMgr::Interact(PlayerbotAI* botAI, AuctionSellingObserver* observer)
{
    Player* bot = botAI->GetBot();
    auto const& target = observer->GetDestination();
    if (!observer->HasErrand() || bot->GetMapId() != target.Map)
        return;
    if (target.Mailbox)
    {
        auto range = bot->GetMap()->GetGameObjectBySpawnIdStore().equal_range(target.Spawn);
        GameObject* mailbox = nullptr;
        for (auto it = range.first; it != range.second; ++it)
            if (bot->GetGameObjectIfCanInteractWith(it->second->GetGUID(), GAMEOBJECT_TYPE_MAILBOX))
                mailbox = it->second;
        if (!mailbox)
            return;
        WorldPacket list;
        list << mailbox->GetGUID();
        bot->GetSession()->HandleGetMailList(list);
        uint32 operations = 0;
        std::vector<uint32> ids;
        for (Mail const* mail : bot->GetMails())
            if (mail && mail->messageType == MAIL_AUCTION && !mail->COD && mail->state != MAIL_STATE_DELETED &&
                mail->deliver_time <= GameTime::GetGameTime().count())
                ids.push_back(mail->messageID);
        for (uint32 id : ids)
        {
            Mail* mail = bot->GetMail(id);
            if (mail->money && operations < 3)

            {
                ++operations;
                uint32 before = mail->money;
                WorldPacket take;
                take << mailbox->GetGUID() << id;
                bot->GetSession()->HandleMailTakeMoney(take);
                LOG_INFO("playerbots", "Auction commerce money: bot={} mail={} confirmed={} copper={}",
                         bot->GetGUID().ToString(), id, mail->money == 0, before);
            }
            // An empty native settlement mail also reconciles a crash after
            // money/item collection committed but before the journal save.
            if (!mail->money && mail->items.empty())
            {
                uint32 entry = 0, unused = 0, response = 0, auctionId = 0;
                char separator = 0;
                std::istringstream subject(mail->subject);
                if (subject >> entry >> separator >> unused >> separator >> response >> separator >> auctionId)
                    if (response == AUCTION_SUCCESSFUL || response == AUCTION_EXPIRED || response == AUCTION_CANCELED)
                        observer->SettleAuction(auctionId);
            }
            auto attachments = mail->items;
            for (auto const& attachment : attachments)
            {
                if (operations >= 3)
                    break;
                Item* item = bot->GetMItem(attachment.item_guid);
                if (!item)
                    continue;
                ++operations;
                uint64 guid = item->GetGUID().GetRawValue();
                observer->SetReturning(guid, item->GetCount());
                WorldPacket take;
                take << mailbox->GetGUID() << id << attachment.item_guid;
                bot->GetSession()->HandleMailTakeItem(take);
                observer->SetReturning(0, 0);
                bool stillAttached =
                    std::any_of(mail->items.begin(), mail->items.end(),
                                [&](MailItemInfo const& info) { return info.item_guid == attachment.item_guid; });
                if (!stillAttached && observer->GetAuctionId(guid))
                    observer->Settle(guid);  // Unallocatable multi-stack returns stay unproven.
            }
            // Native deletion rejects non-empty mail. Pending-sale mail is
            // deliberately left alone; empty settlement mail can expire normally.
        }
    }
    else if (observer->IsParticipant(bot) && sPlayerbotAIConfig.auctionSellingEnabled &&
             !sPlayerbotAIConfig.auctionSellingDryRun && !sRandomPlayerbotMgr.IsProgressionPausedBot(bot))
    {
        auto range = bot->GetMap()->GetCreatureBySpawnIdStore().equal_range(target.Spawn);
        Creature* auctioneer = nullptr;
        for (auto it = range.first; it != range.second; ++it)
            if (bot->GetNPCIfCanInteractWith(it->second->GetGUID(), UNIT_NPC_FLAG_AUCTIONEER))
                auctioneer = it->second;
        if (!auctioneer)
            return;
        auto house = sAuctionMgr->GetAuctionHouseEntryFromFactionTemplate(auctioneer->GetFaction());
        uint32 owned = 0;
        auto nativeHouse = sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
        if (!house || !nativeHouse)
            return;
        for (auto const& [id, auction] : nativeHouse->GetAuctions())
            if (auction && auction->owner == bot->GetGUID())
                ++owned;
        uint64 deposits = 0;
        uint64 reserve = sPlayerbotAIConfig.auctionSellingReserveCopper +
                         static_cast<uint64>(botAI->GetAiObjectContext()->GetValue<uint32>("repair cost")->Get()) +
                         botAI->GetAiObjectContext()->GetValue<uint32>("train cost")->Get();
        uint32 posted = 0;
        auto candidates = observer->GetCandidates();
        for (auto const& candidate : candidates)
        {
            if (posted >= sPlayerbotAIConfig.auctionSellingBatchSize ||
                owned >= sPlayerbotAIConfig.auctionSellingMaxListings)
                break;
            Item* item = bot->GetItemByGuid(ObjectGuid(candidate.ItemGuid));
            uint64 price = 0, benefit = 0;
            uint32 deposit = 0;
            if (!observer->IsReserved(candidate.ItemGuid) || !item || item->GetCount() != candidate.Count || !house ||
                !CommerceMarket::WorthListing(botAI, observer, item, house, price, benefit, deposit) ||
                !AuctionCommercePolicy::CanDeposit(bot->GetMoney(), reserve, deposits, deposit))
                continue;
            observer->SetPosting(candidate.ItemGuid);
            WorldPacket packet;
            packet << auctioneer->GetGUID() << uint32(1) << item->GetGUID() << item->GetCount()
                   << uint32(price * 80 / 100) << uint32(price) << uint32(12 * 60);
            bot->GetSession()->HandleAuctionSellItem(packet);
            observer->SetPosting(0);
            bool confirmed = observer->GetAuctionId(candidate.ItemGuid) != 0;
            LOG_INFO("playerbots", "Auction commerce listing: bot={} item={} confirmed={} auction={}",
                     bot->GetGUID().ToString(), candidate.Entry, confirmed, observer->GetAuctionId(candidate.ItemGuid));
            if (confirmed)
            {
                ++posted;
                ++owned;
                deposits += deposit;
            }
        }
    }
    observer->Finish(botAI);
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    bot->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
}

class PlayerbotCommerceAuctionScript : public AuctionHouseScript
{
public:
    PlayerbotCommerceAuctionScript()
        : AuctionHouseScript("PlayerbotCommerceAuctionScript",
                             {AUCTIONHOUSEHOOK_ON_AUCTION_ADD, AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_UPDATE,
                              AUCTIONHOUSEHOOK_ON_AUCTION_SUCCESSFUL})
    {
    }
    void OnBeforeAuctionHouseMgrUpdate() override { CommerceMarket::RestoreMarkers(); }
    void OnAuctionSuccessful(AuctionHouseObject* /*house*/, AuctionEntry* auction) override
    {
        CommerceMarket::RecordSale(auction);
    }
    void OnAuctionAdd(AuctionHouseObject* /*house*/, AuctionEntry* auction) override
    {
        if (!auction)
            return;
        if (Player* bot = ObjectAccessor::FindPlayer(auction->owner))
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                if (auto observer = botAI->GetAuctionSellingObserver())
                    observer->OnAuctionAdded(auction);
        // Loaded auctions pass through this hook before any companion buyer cycle.
        CommerceMarket::RestoreMarker(auction);
    }
};
void AddPlayerbotCommerceScripts() { new PlayerbotCommerceAuctionScript(); }
