/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Daniel Koman */
#ifndef THROTTLE_PROTOCOL_H
#define THROTTLE_PROTOCOL_H

#include <stdint.h>

/* ---------------------------------------------------------------------
 * Packet definition (handle -> remote unit)
 * Fixed-size, sent at HANDLE_TX_RATE_HZ over nRF24L01+.
 * Keep this struct packed and identical on both ends of the link.
 * ------------------------------------------------------------------- */

#define PACKET_SYNC_BYTE      0xA5u   /* arbitrary fixed marker, first byte of every packet */

/* Command flag bits (packet.flags).
 * These are the LOGICAL states the receiver acts on ("this function is
 * requested/active"), NOT the raw electrical level of any switch. The handle
 * firmware is responsible for mapping each physical momentary/rocker switch
 * (and its open=on / closed=off wiring) into these bits, so the receiver
 * never has to know how the handle is wired. */
#define CMD_FLAG_KILL         (1u << 0)   /* cut ignition, latch KILLED */
#define CMD_FLAG_START_REQ    (1u << 1)   /* hold-confirmed start request */
#define CMD_FLAG_CRUISE       (1u << 2)   /* throttle is being held at a cruise setpoint */
#define CMD_FLAG_AUX1         (1u << 3)   /* accessory 1 (e.g. lights) - level, on while set */
#define CMD_FLAG_AUX2         (1u << 4)   /* accessory 2 (e.g. smoke)  - level, on while set */
/* bits 5-7 reserved for future use */

/* Cruise control (resolved on the HANDLE; see handle_firmware.c).
 * Cruise freezes the transmitted throttle at the value captured when engaged.
 * It disengages on: a second cruise-button press, kill, or the trigger moving
 * more than CRUISE_DISENGAGE_THROTTLE_DELTA away from the frozen setpoint. */
#define CRUISE_DISENGAGE_THROTTLE_DELTA  10  /* 0-255 units; trigger move beyond this drops cruise */

#pragma pack(push, 1)
typedef struct {
    uint8_t sync;       /* must equal PACKET_SYNC_BYTE */
    uint8_t seq;         /* rolling 0-255 sequence number */
    uint8_t throttle;    /* 0-255, mapped from handle ADC reading */
    uint8_t flags;       /* CMD_FLAG_* bitfield */
    uint8_t crc8;         /* CRC8 over sync..flags (4 bytes) */
} throttle_packet_t;
#pragma pack(pop)

#define PACKET_SIZE           (sizeof(throttle_packet_t))   /* = 5 bytes */
#define PACKET_CRC_LEN         (PACKET_SIZE - 1)              /* bytes covered by CRC */

/* ---------------------------------------------------------------------
 * Timing constants (compile-time, tune on the bench before flight)
 * ------------------------------------------------------------------- */

#define HANDLE_TX_RATE_HZ           80      /* packets/sec sent by handle */
#define HANDLE_TX_PERIOD_MS         (1000 / HANDLE_TX_RATE_HZ)

#define WATCHDOG_RAMP_START_MS      175     /* threshold A: start ramping to idle */
#define WATCHDOG_FULL_IDLE_MS       600     /* threshold B: fully idle, hold, stop ramping */
#define RAMP_TO_IDLE_DURATION_MS    400     /* time to go from current throttle to idle once ramp starts */

#define LINK_RESTORE_STABLE_MS      300     /* signal must be continuously good this long before
                                                throttle is allowed to respond to pilot input again
                                                after a loss-of-signal event */

#define MAX_THROTTLE_STEP_PER_TICK  6       /* rate limit: max change in throttle units per control tick,
                                                applies during normal operation too, not just recovery */

#define IDLE_THROTTLE_VALUE         0       /* 0-255 scale, define what "idle" means on your servo mapping */
#define IDLE_THRESHOLD_FOR_START    15      /* throttle must be <= this value for a start request to be honored */

#define START_HOLD_REQUIRED_MS      600     /* pilot must hold start button this long before it's sent as valid */

#define KILL_DEBOUNCE_MS            30      /* kill line must read "kill" continuously this long before latching.
                                               Kill is wired fail-safe (normally-closed, open/broken = kill), so this
                                               rejects vibration glitches on the line without defeating fail-safe:
                                               a real press or a genuinely severed wire persists and still latches. */

#define INPUT_DEBOUNCE_MS          20      /* cruise + accessory inputs must read stable this long before the
                                               debounced state changes - rejects contact bounce so a single cruise
                                               press can't register as several toggles. (Kill has its own debounce
                                               above; start has its 600 ms hold, which is inherently debounced.) */

/* ---------------------------------------------------------------------
 * Electric start (receiver-only). MANUAL crank: the starter is energized
 * while the pilot holds the (hold-confirmed) start button and released when
 * they let go - the pilot is the "engine caught" sensor, so there is NO tach
 * in the controller (see ADR 0007). These values bound the crank so it can
 * never run away. Compile-time constants, tuned on the bench like the watchdog
 * thresholds - not runtime-adjustable.
 * ------------------------------------------------------------------- */

#define MAX_CRANK_MS               2000    /* hard backstop: a single crank can never exceed this, even if the start
                                               button is held or a fault leaves START_REQ stuck asserted. */
#define CRANK_LOSS_ABORT_MS        175     /* stop cranking if no valid packet arrives for this long - a faster,
                                               dedicated loss-of-signal abort than the MAX_CRANK_MS backstop. */
#define STARTER_COOLDOWN_MS        3000    /* after a crank is FORCE-stopped (backstop or loss), refuse a new crank
                                               this long. A normal button release imposes no cooldown, so re-cranking
                                               a stubborn cold engine is instant. */

#endif /* THROTTLE_PROTOCOL_H */
