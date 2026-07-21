# TMHotasLEDSync Wire Protocol

**This file is a mirror.** The canonical copy lives in [`TMHotasLEDSync`'s `WIRE_PROTOCOL.md`](https://github.com/iknowkungfutoo/TMHotasLEDSync/blob/main/WIRE_PROTOCOL.md) - if you're changing tag assignments, packet grammar, or per-aircraft field lists, edit it there first, then copy the changes here. Keeping both in sync is manual (these are three separate repos with no shared build), so check the canonical copy if this one looks stale.

**Status: implemented here, in `TMHotasLEDSync`, and in `dcs2target` - all uncommitted/unreleased as of this writing.** Until a release is actually cut across all three repos together, do not deploy this to end users.

This document is the canonical source of truth for the tag-value wire protocol that replaced the old fixed-position `'u'` (update) packet format. It governs three repositories:

- **[TMHotasLEDSync](https://github.com/iknowkungfutoo/TMHotasLEDSync)** - the receiver. Decodes packets and drives LEDs.
- **[dcs2target](https://github.com/iknowkungfutoo/DCS2Target)** - a sender, for DCS World.
- **BMS2Target** (this repo) - a sender, for Falcon BMS.

Any change to tag assignments, packet grammar, or per-aircraft field lists must be made in the canonical copy first, then implemented consistently across all three repos. See "Change process" at the bottom before adding or modifying anything.

## Why this exists

The previous protocol sent every field at a fixed character offset (e.g. `led_states[10]` always meant F/A-18C's `master_caution_status`). If a sender and receiver ever disagreed about field count or order - a new field inserted mid-packet on one side without a matching change on the other - every field after the mismatch silently decoded as the wrong value. This caused real bugs (F/A-18C wing fold/hook data landing on the wrong LEDs) in `dcs2target`.

The tag-value format below makes every field self-describing: a receiver that doesn't recognize a tag can still skip it correctly (because it knows the length) and leave everything else that follows unaffected. Old and new versions on either end of the connection degrade gracefully instead of corrupting each other's data.

## Packet grammar

```
update_packet := 'u' entry*
entry         := tag length value
tag           := single letter, 'A'-'Z' or 'a'-'z' (never a digit - keeps tags and
                  values visually distinct when eyeballing DEBUG output)
length        := single digit '0'-'9' (how many value characters follow)
value         := exactly `length` digit characters '0'-'9'
```

Only fields that actually changed since the last packet are included - this is a delta protocol, not a full snapshot every frame. On the sender side (this repo), that falls out of the existing `sent_*` "last value actually sent" trackers in `BMS2Target.cpp` (distinct from the raw shared-memory-bit trackers used for console printing) - they reset to `NOT_YET_SENT` whenever a flight starts, so the first update of a flight naturally sends everything once.

**Example:** F-16C reports gear nose down and speed brake at 75%, nothing else changed:
```
uN11B3075
```
(`N` `1` `1` = tag N, length 1, value "1" — gear nose on. `B` `3` `075` = tag B, length 3, value "075" — speed brake 75%.)

## Sender rule (applies to `BMS2Target.cpp`)

Only append a tag entry for a field when its own value actually differs from what was last sent (`append_tag_if_changed()` handles this). The RWR sub-fields (`Q/A/Z/J/E/V`) are all derived from `aux_power_lamp_state` gating several raw shared-memory bits at once - diff the *effective* computed value against its own `sent_*` tracker, not against the raw bit or against the generic `updated` flag (which only means "something happened this tick," not "this specific tag needs resending").

## Version handshake (implemented in this repo)

Separate from the `'u'` update packet - `BMS2Target.cpp`'s `main()` sends one `'v'` packet on connect, so `TMHotasLEDSync` can log which exporter and version it's talking to. Not the tag-value format above (a one-off handshake, not a repeating payload), but the same self-describing principle:

```
version_packet := 'v' exporter_type version_string
exporter_type  := single character identifying which sender this is - 'D' for
                   dcs2target, 'B' for BMS2Target. Reserve a new letter here
                   (documented) before adding another sender.
version_string := remaining bytes - that sender's own bare version number
                   (e.g. "1.0.5"), no descriptive prefix text.
```

The connect message is built as `"vB" + std::string(VERSION)` - the `"B"` here is what identifies this repo as the sender. The `std::cout` banner printed to the console still says the full `"BMS2Target v" + VERSION`; only the bare version goes out over the wire.

**Example:** BMS2Target v1.0.5 connecting sends `vB1.0.5`; `TMHotasLEDSync` prints `Connected to BMS2Target v1.0.5 (TMHotasLEDSync v2.0.0)`.

## Tag scope: per-aircraft, not global

`TMHotasLEDSync`'s `TCPCallback` dispatches by aircraft before any field decoding happens. Because of that, **a tag only needs to be unique within one aircraft's own table below** - it's safe (and expected) for the same letter to mean different things for different aircraft. BMS2Target only ever sends F-16C data, so only the F-16C table below applies to this repo.

## F-16C

| Tag | Meaning | Width |
|---|---|---|
| `N` / `L` / `R` | Gear nose / left / right | 1 |
| `B` | Speed brake position | 3 |
| `W` | Gear warning | 1 |
| `Q` | RWR Search | 1 |
| `A` | RWR Activity | 1 |
| `Z` | RWR A-Power | 1 |
| `J` | RWR Alt Low | 1 |
| `E` | RWR Alt | 1 |
| `V` | RWR System Power | 1 |
| `S` | JFS Run | 1 |
| `G` | Main Gen | 1 |
| `T` | Stby Gen | 1 |
| `C` | FLCS Rly | 1 |
| `U` | EPU Run | 1 |

## Change process

1. **Adding a field:** pick any tag letter not already used in the F-16C table above. Add the row in the canonical copy first, then implement the sender side here and the receiver side (`f-16c_led_utils.tmh` in `TMHotasLEDSync`) to match.
2. **Never reuse a retired tag's letter for a new meaning**, even after removing the old field - old packet captures, logs, or not-yet-updated builds in the wild could still be using the old meaning. Retire it (mark unused) instead of reassigning it.
3. **Migration:** clean break, decided. `TMHotasLEDSync` does not support both the old fixed-position format and this one - all three repos cut over together. No dual-protocol transition period.
