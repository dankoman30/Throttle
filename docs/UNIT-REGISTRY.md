# Unit Registry

Append-only record of assigned nRF24 addresses per handle+receiver pair. See
the "Multi-unit isolation" item in `docs/OPEN-ITEMS.md` for why this exists
and the reasoning behind the scheme below.

Rules:

- Assign unit numbers **sequentially** (next = highest assigned + 1). Never
  reuse a number, even for a retired/decommissioned unit.
- `0x00` is reserved — it means "still on the firmware placeholder address,
  not yet assigned" (see `PLACEHOLDER_ADDR` in `src/common/nrf24.c`).
- `0xFF` is reserved for a possible future broadcast/test address.
- Address = `0xE7 0xE7 0xE7 0xE7 <unit #>`.
- Add a row here the moment a number is assigned, even before the address is
  actually flashed into hardware, so no two people ever assign the same
  number.

| Unit # | Handle label | Receiver label | Address | Assigned | Status | Notes |
|---|---|---|---|---|---|---|
| — | — | — | — | — | — | No units assigned yet — bench units still run on `PLACEHOLDER_ADDR` (`0xE7E7E7E7E7`). |
