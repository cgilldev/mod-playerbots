# Earned auction selling development

The first implementation is an inventory observer. It does not post auctions,
reserve goods, change vendor selling, travel, collect mail or evaluate profitability.
It is disabled by default and applies only to the persistent progression cohort.

## Optional inventory dry run

On a server built with this change, select a few cohort character GUIDs:

```ini
AiPlayerbot.AuctionSelling.DryRun = 1
AiPlayerbot.AuctionSelling.DryRunBotGuids = 101,102,103,104
```

The GUIDs above are examples; replace them with actual cohort members. An empty
selection observes the whole cohort. Configuration is read at initialization.
No production configuration is changed by adding this source feature.

The observer records matching native StoreNewItem/LootItem events for creature
and gameobject loot. It does not infer earned origin from inventory ownership.
Each bot's first inventory scan is staggered across one minute; subsequent scans
occur once per minute. Scans run only while alive, outside combat, ungrouped and
without a master. Existing use/keep classifications are evaluated only for
observed earned stock, using a private value rather than resetting the normal
AI's classification cache.

Changed snapshots report counts for protected earned goods, untradeable earned
goods, unproven stock, partly earned stacks and whole-stack candidates. Candidate
lines include item identity, quantity and immediate vendor value in copper.
These are inventory candidates, not recommended prices or predicted sales.

## Conservative boundaries

- Provenance is session-local and resets on logout/restart or disabling observation.
- Existing stock, factory/reset items, purchases, crafting, quest rewards, item
  container loot and mail/trade arrivals do not gain earned provenance.
- Group-roll and other acquisition paths without the inspected matching native
  store/loot callbacks are not credited yet.
- Untrusted additions cannot increase earned quantity. Observed consumption
  reduces earned quantity first. Transfers discard the affected GUID's provenance.
- A loot acquisition spread across several destinations credits at most the
  final returned stack. Stack splitting/merging does not migrate provenance in
  this first implementation; missing GUID allocations are discarded.
- Unmatched store history is bounded to 256 GUIDs per observer. Unproven stock
  skips expensive item-use classification. Candidate logs appear only when the
  sampled snapshot changes.

This deliberately undercounts some earned goods. It is also an observation
ledger, not a complete audit of every possible inventory mutation between scans.
Durable allocation tracking and comprehensive mutation reconciliation are
required before it can authorize posting.

## Validation

Run from the module root:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Werror tests/AuctionEarnedInventoryTests.cpp \
    -o /tmp/playerbot-auction-earned-tests
/tmp/playerbot-auction-earned-tests
python3 apps/codestyle/codestyle-cpp.py
```

The focused tests cover unproven additions, matching and duplicate loot callbacks,
consumption, variants, transfers, missing GUIDs, whole-stack eligibility, quantity
overflow boundaries and bounded history. They do not replace a module/server build
or runtime integration checks.

## Remaining implementation

1. Persistent earned-lot allocations and restart reconciliation.
2. Cached market data, net-return/deposit policy and spending reserves.
3. Earned-supply-aware filler and an aggregate synthetic buyer budget.
4. Native world-thread listing and mail operations with confirmed outcomes.
5. NewRpg faction-town errands, interruption and safe route recovery.
6. A four-bot live pilot exercising sale, expiry, relisting and mail delivery.

The full design and requirements are captured under
`.agents/plans/playerbot-auction-selling/` on the source branch.
