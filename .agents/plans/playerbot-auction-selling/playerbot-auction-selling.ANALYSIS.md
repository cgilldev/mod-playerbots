# Playerbot auction selling investigation

Inspected October 2, 2026. This is a feasibility/design investigation, not a
feature implementation. Paths below are relative to the playerbots module
unless prefixed with core/. No auction-selling behavior was enabled.

## Conclusion

Feasible as an opt-in playerbots feature, with a small companion integration in
mod-ah-bot-plus. The core already supplies auction listing, deposits, settlement,
returned items, and mail collection. Most new work concerns decisions, travel,
inventory preservation, and a reliable auction/mail lifecycle. A core rewrite
does not appear necessary, subject to validating bot acknowledgment handling.

## Existing pieces and actual gaps

| Component | Evidence | Implication |
| --- | --- | --- |
| Inventory classification | src/Ai/Base/Value/ItemUsageValue.cpp | Already recognizes equipment, quest items, supplies, profession reserves, guild tasks, disenchanting, vendor goods, and ITEM_USAGE_AH. Auction-worthy is currently a broad classification, not a demand/profit prediction. |
| Vendor selling | src/Ai/Base/Actions/SellAction.cpp, SellVendorItemsVisitor::Visit | Currently vendors both ITEM_USAGE_VENDOR and ITEM_USAGE_AH. Auction candidates need an opt-in reservation policy and an emergency-bag-space fallback. |
| Old auction implementation | src/Ai/Base/Actions/LootAction.cpp, StoreLootAction::AuctionItem | Entire implementation is inside a block comment, uses obsolete APIs, creates new items and sets zero deposit. It is not an active listing feature and should not be revived unchanged. |
| Auctioneer travel | src/Ai/Base/Actions/ChooseTravelTargetAction.cpp | Can randomly target city NPCs including auctioneers. This is sightseeing, not an inventory-driven selling trip. Mailbox target selection is commented out. |
| Auctioneer selector | src/Mgr/Travel/TravelMgr.h, SelectAuctioneerByMap | Declaration found without implementation or callers in the inspected checkout. Not a usable selector to switch on. |
| Current RPG behavior | src/Ai/World/Rpg/NewRpgInfo.h and Strategy/NewRpgStrategy.cpp | Active state variants cover questing, grinding, wandering, resting and flights, not auction/mail errands. New functionality must be wired into this active behavior or a cooperating strategy. |
| Nearby mail interaction | src/Ai/Base/Actions/MailAction.cpp | Has mailbox lookup and take-money/take-item operations, but the generic processor is command-oriented and does not provide a durable autonomous errand. It also checks bag space before collecting money. |
| Autonomous mail checker | src/Ai/Base/Actions/CheckMailAction.cpp | Handles player/guild-task correspondence, interpreting sender as a player GUID. It does not check auction mail type or collect proceeds; processed mail is deleted. The automatic check-mail trigger is commented out in the inspected strategy. Protect auction mail explicitly before adding autonomous collection; this is a latent integration risk, not evidence that current bots delete auction proceeds. |
| Native auction entry point | core/src/server/game/Handlers/AuctionHouseHandler.cpp, HandleAuctionSellItem | Validates auctioneer interaction, actual item GUID/count, tradeability, legal duration, prices and deposit funds. Use normal player-owned inventory rather than creating replacement items. |
| Thread ownership | core/src/server/game/Server/Protocol/Opcodes.cpp | Listing and mail-taking opcodes are PROCESS_THREADUNSAFE. Bot AI runs on map threads; queue these operations through PlayerbotWorldThreadProcessor and resolve current GUIDs on execution. |
| Lifecycle hooks | core/src/server/game/Scripting/ScriptDefines/AuctionHouseScript.h | Existing add/success/expire/remove hooks can record bot listing outcomes and update market summaries. |

## Proposed behavior

1. Cache a surplus inventory plan after meaningful inventory changes, with a
   bounded refresh interval. Keep equipment upgrades, quest/guild items, tools,
   food, ammunition, and profession reserves. Revalidate the actual item instance
   when posting; a template-level classification is not sufficient.
2. Choose keep/use, vendor, disenchant, or auction based on expected demand,
   vendor proceeds, sale prices, cut, expected lost deposits and travel cost.
   Reserve money for repair/training and listing deposits. Never fabricate stock
   or grant money simply to finance an errand.
3. Schedule a town trip only when surplus value or collectable mail justifies it.
   Combine repair, junk sales, mail collection, and auction posting. Grouped bots
   must respect their real player's activity; town trips should not disrupt an
   active group or encounter. Spread trips over time rather than sending the
   whole cohort to the city together.
4. Choose a friendly, reachable auctioneer using route cost, level-appropriate
   safety, actual house fees and expected demand. Prefer the faction market for
   initial implementation. Neutral-house speculation and cross-continent trips
   are later work, not a nearest-coordinate heuristic.
5. Travel using existing movement/flight machinery; interact within the normal
   range. No routine teleport-to-auctioneer or remote mailbox collection. The
   observed bad-height stuck destinations need correction before reliable long
   shopping trips can be promised. Recovery must validate destination terrain.
6. Post a small batch of real surplus inventory using native listing validation
   and a confirmed result. Re-resolve item/auctioneer GUIDs, counts, gold, range,
   and eligibility after queued operations. Do not label an attempted action as
   success. Prevent duplicate pending operations across retries and logouts.
7. Return to questing. Visit a mailbox when auction money or returned goods have
   actually arrived. Collect money without requiring spare bag slots, retrieve
   returned items only with capacity, distinguish auction mail from player/COD
   correspondence, and delete only empty successfully processed mail.
8. Record sale/expiry outcomes. Relist a bounded number of times, adjust price
   conservatively, then vendor or retain stale stock instead of paying deposits
   indefinitely. Persist counters and retry state across session rotation/restart.

## Market data and integration

Follow-up native transaction inspection: the single-whole-stack listing branch
saves auction, inventory and gold in one transaction. The partial/multi-stack
branch can delete source stacks in separate transactions before saving the final
auction. The detailed plan therefore limits initial posting to single whole
surplus stacks and requires a separate audit before partial-stack posting.

Build per-house price/supply summaries in the world thread and serve cached
snapshots to bot decisions. Do not scan the auction map or run synchronous SQL
for every bot tick. Prefer robust unit prices and realized sales over blindly
undercutting one listing; stack counts and random-property equipment matter.

The synthetic seller currently counts only configured auction characters such as
Jirt. Ordinary cohort characters would be distinct sellers, and the existing
buyer excludes configured auction characters but can consider cohort listings.
Its per-seller budgets therefore become much larger in aggregate across 80 bots.
Add a cohort-wide buyer budget/demand cap and explicit bot/human identity handling
to avoid manufacturing apparent liquidity through a buy/relist loop. Never buy
one's own listings. Preserve the existing delay, bidder and price safeguards.

Track human listings, earned cohort listings and generated filler separately.
Reduce filler in categories adequately supplied by earned goods, rather than
posting the full synthetic target on top of cohort supply and drowning it out.
This requires cooperation with mod-ah-bot-plus, not a shared list of item IDs.

## Suggested implementation phases

1. **Inventory and pricing policy:** opt-in surplus reservations, vendor fallback,
   cached valuations, deposit budgeting and per-bot listing limits.
2. **Faction-town errands:** inventory-driven target selection, reachable
   auctioneer/mailbox visits, native listing/collection, small batches and
   interruption/resume behavior. Begin with a few autonomous cohort bots.
3. **Persistent lifecycle:** sale/expiry accounting, bounded relisting, mail
   delivery handling, restart/session rotation and queue failure recovery.
4. **Economy cooperation:** earned-supply-aware filler, aggregate buyer budgets,
   market feedback and selected neutral-house decisions.

This is a substantial module feature, not a configuration change. Existing
infrastructure reduces the transaction work; pathfinding, interruptions, mail
semantics and economic decisions account for most of the integration effort.

## Acceptance evidence needed for implementation

- A bot sells only actual surplus and retains equipment, quest items and reserves.
- Vendor-vs-auction decisions include fees/deposits and preserve basic spending.
- The bot physically reaches a valid friendly auctioneer/mailbox and interacts
  in range; unsafe/unreachable routes are abandoned with a bounded retry.
- Listing transfers the exact item/count once and charges the normal deposit.
- Success, insufficient money, lost item, logout and rejected posting are
  distinguished and cannot duplicate stock or endlessly retry.
- Sold proceeds and expired goods are collected after delivery, including when
  money can be taken but bags are full; unrelated/COD mail is left alone.
- Restart and ordinary 30–90 minute bot rotation preserve useful state.
- Groups, combat and progression safety stops override optional selling errands.
- Measured world-thread work stays bounded with the 80-bot cohort; no per-tick
  database/market scans or mass synchronized city travel.
- Generated and earned supply remain distinguishable; bot-buyer spending does
  not grow unchecked merely because sellers have different character GUIDs.
