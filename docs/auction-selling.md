# Earned auction commerce

Persistent progression bots can take an occasional town errand to auction earned
surplus, or visit a mailbox to collect auction proceeds and returned goods. New
selling is opt-in. Ordinary bot use/vendor decisions remain the fallback.

## Installation and configuration

Build the matching `persistent-bots-ah-20261002` core and playerbots branches.
The core supplies inventory-count and transaction hooks, cached player settings,
and optional auction role flags. Playerbots has no include/link dependency on
mod-ah-bot-plus. When using that companion, deploy its matching branch and apply
`2026_10_02_00_earned_bot_budget.sql` to the characters database before enabling
its buyer; a missing budget table blocks purchases of identified earned stock.

The core's `EnablePlayerSettings` must be enabled. The ledger uses the existing
`character_settings` table with source `playerbot_commerce_v1`, not a new bot DB.
Options are read during initialization:

```ini
AiPlayerbot.AuctionSelling.Enabled = 0
AiPlayerbot.AuctionSelling.DryRun = 0
AiPlayerbot.AuctionSelling.DryRunBotGuids = ""
AiPlayerbot.AuctionSelling.MaxListings = 5
AiPlayerbot.AuctionSelling.BatchSize = 3
AiPlayerbot.AuctionSelling.TravelSeconds = 600
AiPlayerbot.AuctionSelling.CooldownSeconds = 3600
AiPlayerbot.AuctionSelling.ReserveCopper = 1000
```

Set Enabled to 1 for selling. DryRun=1 overrides new posting and retains
inventory observations. The historical DryRunBotGuids name selects participants
for both modes: use four actual cohort character GUIDs for a pilot. Empty means
all progression-cohort bots. Existing journals continue settlement if selling is
disabled or participation changes; these settings do not credit old inventory.

## Goods and money

Only matching native StoreNewItem/LootItem events from creature or gameobject
loot prove earned stock, including gathering through those paths. Starting
inventory, factory/reset goods, purchases, crafting, quest rewards, item-container
loot, trades and arbitrary mail do not acquire provenance. Unknown additions,
consumption, changed variants and uncertain transfers reduce proof. The bounded
ledger holds up to 256 item GUIDs per bot. Group-roll paths without matching
callbacks are conservatively unproven.

Fresh ItemUsageValue decisions protect equipment upgrades, quest/profession
reserves, tools, food and ammunition. Each actual instance must also pass native
trade restrictions. Only a whole, fully earned surplus stack is listed: native
partial/multiple-stack listing paths have different transaction boundaries and
are deliberately excluded. No fixed item-ID allowlist is used.

Pricing uses median human/ordinary market asks, then observed nonsynthetic sale
prices when available. Item variants and faction houses have separate quotes.
Earned bot asks and generated filler asks are excluded. Recent sale samples are
bounded and expire with the process. Without a quote, price is five times vendor
value; quotes are bounded to two through twenty times vendor value. Zero-vendor
items have no fallback and go through their existing bot policy.

The bot compares a conservative 50% expected sale return against vending,
auction cut, failure deposit loss, estimated route time and taxi fare. It reserves
repair/training costs plus the configured copper buffer. Total deposits must fit
10% of cash remaining after those reserves. Twelve-hour auctions allow one
returned-item relist, discounted 10%; after two attempts normal vending takes
precedence. Quantity, protected uses, affordability and restrictions are checked
again immediately before native posting. Attempts are credited only after the
native auction-add hook confirms the same item GUID.

## Travel and interruption

Planning runs on the world thread; triggers read cached status. Eligible bots
must be alive, ungrouped, autonomous, outside combat/BGs/dungeons, and not paused
at the progression ceiling. Mail settlement remains possible for paused bots.
At most two errands per faction run concurrently. The bot chooses a friendly
faction auctioneer or a nearby physical mailbox on its current map. Neutral
auctioneers are excluded unless the core combines faction auction houses.

A round-trip estimate must fit the travel budget and the remaining cached
progression/rotation session, with a 30-second margin. Known same-map taxi paths may
be chosen when faster and affordable; native flight-master interaction checks
all nodes again and charges normal fares. The feature never grants flight nodes.
Ground movement uses the existing navigation system with teleport recovery
explicitly disabled. Invalid destination heights fail closed. Death, joining a
group, receiving a master, stuck movement, or exceeding the trip budget abandons
the errand and releases reservations. Combat pauses execution while the trip
clock continues. Login rotation discards movement intent and replans later from
native inventory/auction state. Other progression movement resumes after an
errand; this release does not create a separate return-to-origin trip.

Only the selected batch is reserved from automatic vendor selling. Severe bag
pressure releases it to the normal vendor path. An errand posts at most three
stacks by default and observes the five-active-listing cap. Failed queued work
expires after 30 seconds; generation tokens reject stale operations after timeout
or relogin. Finished/abandoned errands have a persisted wall-clock cooldown.

## Mail and persistence

Listings, item ownership, deposits and auction settlement use native core
handlers on the world thread. The earned ledger joins the same character DB
transaction as native inventory saves/listings/attachment collection. Core
player-settings cache is updated alongside it, preventing a later full save from
overwriting metadata. Native auction and mail state remain authoritative; the
journal never spawns items, invents gold credits or recreates an auction.

Mailbox errands handle only delivered, non-COD auction mail. Money collection
works independently of free bag slots; attachments use native capacity checks.
Each visit performs at most three collections. Returned lots preserve their
attempt count, including conservative native merges. Unallocatable multistack
returns are collected normally but lose resale proof. Empty successful/expired
settlement mail reconciles a crash after native collection committed. Pending
sale messages and ordinary/player/guild/COD mail are left alone. Empty mail is
allowed to expire through normal mail maintenance.

Restart restoration marks known earned auctions before companion buyer cycles.
Native IDs deduplicate outstanding listings; a stale journal does not authorize a
replacement item. Disabling selling releases unposted reservations and keeps
physical mailbox settlement for outstanding auctions. Preserve the journal and
native mail when rolling back; do not remove auctions or refund their deposits
manually.

## Companion economy

The AH filler module distinguishes generated stock, earned stock and human
stock using optional core auction flags. Earned listings occupy filler targets,
category/quality deficits and duplicate limits; human listings remain outside
those filler limits. Existing progression metadata rules still apply to filler.

The synthetic buyer has an additional shared rolling-hour cap across all houses
and earned sellers: `AuctionHouseBot.Buyer.EarnedBots.MaxCopperPerHour = 10000`
and `MaxCommitmentsPerHour = 20`. Every bid/buyout commitment joins its native
transaction in `mod_ahbot_earned_bot_commitment`; restarts cannot reset the cap.
Commitments conservatively count bid increases at their full submitted amount.
Seven-day history is pruned during budget refresh. Existing per-seller policy
continues to apply independently. Synthetic sales do not train market quotes.
`AuctionHouseBot.EarnedBots.ReduceFiller = 1` controls filler coordination.

## Validation and rollout

Focused C++ tests exercise unknown stock, replayed callbacks, consumption,
variant mismatches, bounded history, restart serialization, returned-item attempts,
corrupt state, price bounds, deposit loss and batch spending reserves. Source
validation includes module codestyle, changed-range formatting and compiler
checks against this installation's native flags. Full build and pilot results
are recorded separately with the deployment artifacts.

Begin with four participants, then twenty and the full cohort after reviewing
confirmed listings, money/return collection, route failures, deposit expenditure,
synthetic commitments and progression. A natural twelve-hour auction plus one
relist takes over 24 hours to observe end-to-end expiration in production.
Any duplicate credit, protected-item sale, unsafe repeated travel or budget
breach should stop expansion. Auction commerce inventory/candidate logs are
eligibility snapshots; listing logs explicitly distinguish confirmed native
auctions from attempts. A bounded pilot does not establish long-term economic
balance or progression impact.
