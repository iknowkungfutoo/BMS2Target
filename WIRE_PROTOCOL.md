# TMHotasLEDSync Wire Protocol

**This file is a mirror.** The canonical copy lives in [`TMHotasLEDSync`'s `WIRE_PROTOCOL.md`](https://github.com/iknowkungfutoo/TMHotasLEDSync/blob/main/WIRE_PROTOCOL.md) - if you're changing tag assignments, packet grammar, or per-aircraft field lists, edit it there first, then copy the changes here. Keeping both in sync is manual (these are three separate repos with no shared build), so check the canonical copy if this one looks stale.

**Status: implemented and released here, in `TMHotasLEDSync`, and in `dcs2target`.**

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

## Heartbeat / connection-loss detection

Because the `'u'` update packet is delta-only (see above), silence alone is ambiguous - "nothing changed" and "the sender is gone" look identical to a receiver that only watches for `'u'` packets. The heartbeat closes that gap:

```
heartbeat_packet := 'h'
```

A bare single character, no payload. `BMS2Target.cpp`'s polling loop sends one roughly every second (`HEARTBEAT_INTERVAL_MS` = 1000), independent of whether a `'u'` delta also fires that tick - it's sent purely to prove "I'm still here", any time `target_connected` is true, whether or not a flight is currently in progress.

`TMHotasLEDSync` treats receipt of **any** packet - `'q'`, `'r'`, `'v'`, `'m'`, `'u'`, or `'h'` - as proof of life and resets an inactivity counter; if none arrive for about 5 seconds it goes dark and forgets the current aircraft. This covers both an ungraceful crash of Falcon BMS or this app (no time to send `'q'`) and a user switching from this exporter to dcs2target (or back) without restarting `TMHotasLEDSync`.

The watchdog only arms after the first packet `TMHotasLEDSync` has seen since it started - sitting idle waiting for a sim to launch is not a lost connection (there was never one to lose), so no "connection lost" message or `lights_out()` fires until at least one sender has connected at least once. It's also disarmed by a `'q'` (graceful sim exit) and re-arms automatically on the next packet - the silence between one sim session ending and the next one starting is expected, not a fault, the same as the pre-first-connection case.

## Reset packets (implemented everywhere, previously undocumented here)

Two single-character, payload-less packets exist alongside the `'u'`/`'v'` formats above - both were already implemented in `TMHotasLEDSync` and (for `'q'`) already sent by `dcs2target` before this repo adopted either; this section just catches the mirror up to reality.

```
reset_packet     := 'r'   -- full reset: TMHotasLEDSync's reset_leds(), a ~2s
                            blocking LED self-test sweep. Sent by BMS2Target
                            at normal end-of-flight (IsEndFlight) and at quit.
                            Expensive - don't send it from a path that could
                            fire repeatedly (e.g. reconnect churn).
quit_packet      := 'q'   -- lights-out: TMHotasLEDSync's lights_out(), a
                            cheap immediate all-off with no self-test sweep.
                            Sent by BMS2Target when Falcon BMS itself has
                            gone away (IntellivibeData::IsExitGame), so the
                            LEDs don't stay stuck showing stale state.
```

`'r'` and `'q'` both bypass the `sent_*` diffing entirely - they're unconditional commands, not tag-value entries.

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
