# Playerbot auction selling requirements

Date: 2026-10-02. Status: proposed; planning only.

## Intended result

Autonomous progression bots bring their earned surplus to a suitable auction
house, post it using their own inventory and money, and return to a mailbox to
collect proceeds or expired goods. Decisions should produce believable commerce
without making town travel dominate progression or overwhelming the low-level
economy with generated stock.

## First release

- Opt-in autonomous cohort only; initially four bots, then twenty, then the
  existing eighty-bot cohort if acceptance checks pass. Player-controlled or
  grouped bots do not independently start commerce errands.
- Inventory policy retains upgrades, equipment, quest/guild goods, profession
  reserves, tools, ammunition and consumables. Only proven earned surplus is
  eligible. Template classification alone does not authorize posting.
- Cover ordinary loot and gathered goods first. Factory/starter/reset goods and
  items acquired from trades, mail or AH purchases are ineligible by default.
  Crafting provenance is a later extension unless validated during implementation.
- Select vendor versus auction using conservative demand, net return, deposit
  risk, spending reserves and route cost. An expensive route can make otherwise
  saleable goods better to vendor locally.
- Faction auctioneers and real nearby mailboxes; reachable terrain and ordinary
  interaction distances. Use walking and already available flight routes.
- List and collect through native core validation on the correct thread. Recheck
  current player, item, count, gold, NPC/object, range and pending operation.
- Start with single whole stacks that are completely surplus. Partial-stack
  auctioning needs a separate crash/transaction audit before enabling it.
- Preserve exact inventory and money accounting through rejection, logout,
  death, session rotation and restart. An attempted operation is not a success.
- Collect delivered auction money even with full bags. Retrieve returned items
  only with capacity. Leave other correspondence and COD mail alone; delete only
  confirmed empty processed auction mail.
- Limit listings, trips, expiry retries and cohort participation. Town errands
  yield to combat, grouping and progression safety limits.
- Reduce synthetic filler as earned supply grows. Synthetic buyer spending has
  a cohort-wide cap, separate from existing per-owner caps.
- Existing behavior remains the default when the feature is disabled. Disabling
  new selling still permits safe settlement of auctions previously created.

## Deferred

Neutral-house arbitrage, cross-continent market tours, speculative purchasing,
auction cancellation/undercutting loops, autonomous crafting to meet market
demand, new gathering AI and independent trips by player-controlled bots.

## Evidence required

Verify protected inventory, route failure, full bags, insufficient gold, real
listing deposits, sale and expiry mail, interruption, rejected/ambiguous
operations, restart reconciliation and feature-off behavior. Measure earned
versus filler supply, buyer spending, progression time lost to travel, queue
latency and world-thread cost during a pilot. Rollout requires those results,
not merely a successful build or rising auction counts.
