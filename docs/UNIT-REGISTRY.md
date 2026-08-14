# Unit Registry

Append-only record of assigned nRF24 addresses per handle+receiver pair. See
the "Multi-unit isolation" item in `docs/OPEN-ITEMS.md` for why this exists,
the reasoning behind the scheme below, and
`docs/decisions/0009-per-unit-address-compile-time.md` for why this is a
compile-time constant rather than a hardware switch.

**Building a real unit:** edit `UNIT_NUMBER` in `src/common/unit_config.h`
to the number assigned below (not the tracked default, `0xFF`), build,
flash, then don't commit that local edit back — see the comment at the top
of that file. There is a standing, permanently-unchecked reminder for this
in `docs/OPEN-ITEMS.md` specifically so it doesn't get forgotten build to
build.

Rules:

- Assign unit numbers **sequentially** (next = highest assigned + 1). Never
  reuse a number, even for a retired/decommissioned unit.
- `0x00` is reserved — it means "still on the firmware placeholder address,
  not yet assigned." `src/common/unit_config.h` refuses to even compile if
  `UNIT_NUMBER` is left at `0x00`.
- `0xFF` is reserved for a deliberate unpaired bench/test build — the
  tracked default in `unit_config.h`. Both boards' status LED heartbeat
  renders visibly inverted on a `0xFF` build, as a hardware-visible "this
  is not a real paired unit" cue.
- Address = `0xE7 0xE7 0xE7 0xE7 <unit #>`.
- Add a row here the moment a number is assigned, even before the address is
  actually flashed into hardware, so no two people ever assign the same
  number.

| Unit # | Handle label | Receiver label | Address | Assigned | Status | Notes |
|---|---|---|---|---|---|---|
| — | — | — | — | — | — | No units assigned yet — bench units still run with `UNIT_NUMBER 0xFF` (deliberate test-build marker). |
