/* ---------------------------------------------------------------------
 * handle_firmware.c
 *
 * Runs on the STM32 mounted in the trigger handle.
 * Responsibilities:
 *   - Read trigger position (ADC), kill switch, start button
 *   - Debounce buttons, require hold-to-start
 *   - Build a throttle_packet_t and transmit over nRF24L01+ at a fixed rate
 *
 * NOTE: HAL/RF24 calls below are named to match typical STM32 HAL +
 * RF24 (TMRh20) library conventions, but are stubbed/commented -
 * wire these to your actual peripheral init once board/pins are chosen.
 * ------------------------------------------------------------------- */

#include <stdint.h>
#include <stdbool.h>
#include "throttle_protocol.h"
#include "crc8.h"
#include "battery_monitor.h"

/* --- Hardware handles (fill in during board bring-up) --- */
// extern ADC_HandleTypeDef hadc1;         /* trigger position input */
// extern RF24 radio;                       /* nRF24L01+ instance (RF24 library) */
// GPIO defines for kill switch input, start button input

/* --- Local state --- */
static uint8_t   g_seq = 0;
static bool      g_start_button_pressed_last = false;
static uint32_t  g_start_hold_start_ms = 0;
static bool      g_start_hold_confirmed = false;

/* Kill is LATCHED on the handle: once the kill switch is seen active even
 * once, we keep commanding kill on every packet from then on. A brief press
 * therefore produces a sustained stream of kill packets rather than a few,
 * so a run of dropped packets (no radio ack in this design) can't swallow it.
 * This mirrors the receiver's sticky STATE_KILLED. Clearing it requires a
 * deliberate re-arm - a power cycle here, which matches the "physical re-arm
 * only" rule in PROJECT_DESIGN.md. The mechanical kill line is the backup for
 * when the radio itself is dead. */
static bool      g_kill_latched = false;

/* Cruise control state (handle-side; see apply_cruise). */
static bool      g_cruise_engaged = false;
static uint8_t   g_cruise_setpoint = 0;
static bool      g_cruise_button_last = false;

/* Local battery monitor state (this pack only - no telemetry from receiver). */
static uint32_t  g_batt_last_poll_ms = 0;
static bool      g_batt_low = false;

/* Handle pack profile. Example: 2S Li-ion (~8.4V full / 6.0V empty), scaled
 * back up from whatever divider feeds the ADC. Measure and set for real. */
static const battery_profile_t HANDLE_BATT = {
    .full_mv = 8400, .empty_mv = 6000, .low_mv = 6600, .led_count = 4
};

/* Replace with your actual millisecond tick source (e.g. HAL_GetTick()) */
static uint32_t millis(void) {
    // return HAL_GetTick();
    return 0; /* placeholder */
}

/* Read raw ADC and map to 0-255 throttle scale.
 * Apply a light low-pass filter here so noisy ADC readings don't
 * translate into a jittery servo on the receiving end. */
static uint8_t read_throttle_position(void) {
    // uint32_t raw = HAL_ADC_GetValue(&hadc1); // e.g. 0-4095 for 12-bit ADC
    uint32_t raw = 0; /* placeholder */

    static uint32_t filtered = 0;
    const uint32_t FILTER_SHIFT = 2; /* simple exponential moving average, tune as needed */
    filtered = filtered - (filtered >> FILTER_SHIFT) + raw;
    uint32_t smoothed = filtered >> FILTER_SHIFT;

    /* map 0-4095 -> 0-255; adjust min/max to your actual trigger travel
     * (measure real ADC min/max at full release / full pull, don't assume rails) */
    uint8_t mapped = (uint8_t)((smoothed * 255) / 4095);
    return mapped;
}

static bool read_kill_switch(void) {
    // return HAL_GPIO_ReadPin(KILL_SW_GPIO_Port, KILL_SW_Pin) == GPIO_PIN_SET;
    return false; /* placeholder */
}

static bool read_start_button_raw(void) {
    // return HAL_GPIO_ReadPin(START_BTN_GPIO_Port, START_BTN_Pin) == GPIO_PIN_SET;
    return false; /* placeholder */
}

/* --- Cruise + accessory inputs ---
 * Each of these returns the LOGICAL "active/requested" state, hiding the
 * electrical polarity. Momentary buttons here are wired open=off / closed=on
 * (per the start/kill switches); the accessory rocker/momentary switches use
 * open=on / closed=off, so read_aux* inverts. Put the ONLY polarity decision
 * for each input right here so re-wiring is a one-line change. */
static bool read_cruise_button_raw(void) {
    // return HAL_GPIO_ReadPin(CRUISE_BTN_GPIO_Port, CRUISE_BTN_Pin) == GPIO_PIN_SET;
    return false; /* placeholder */
}

static bool read_aux1_switch(void) { /* e.g. lights: open=on -> invert */
    // return HAL_GPIO_ReadPin(AUX1_GPIO_Port, AUX1_Pin) == GPIO_PIN_RESET;
    return false; /* placeholder */
}

static bool read_aux2_switch(void) { /* e.g. smoke: open=on -> invert */
    // return HAL_GPIO_ReadPin(AUX2_GPIO_Port, AUX2_Pin) == GPIO_PIN_RESET;
    return false; /* placeholder */
}

/* Cruise control, resolved entirely on the handle.
 *   - Rising edge of the cruise button toggles cruise on/off.
 *   - Engaging captures the current trigger position as the setpoint; while
 *     engaged we transmit that frozen value instead of the live trigger, so
 *     the pilot can release the trigger and the throttle holds.
 *   - Cruise drops on: kill (always), a second button press, or the trigger
 *     moving more than CRUISE_DISENGAGE_THROTTLE_DELTA from the setpoint in
 *     either direction (pilot overrides by pulling more or backing off).
 * Returns the throttle value that should actually be transmitted this packet.
 * Kill must be evaluated (g_kill_latched set) BEFORE calling this. */
static uint8_t apply_cruise(uint8_t live_throttle) {
    bool btn = read_cruise_button_raw();
    bool rising = btn && !g_cruise_button_last;
    g_cruise_button_last = btn;

    if (g_kill_latched) {
        g_cruise_engaged = false;           /* kill always disengages cruise */
    } else if (rising) {
        if (!g_cruise_engaged) {
            /* only engage if we're actually above idle - holding "idle" is
             * pointless and could surprise the pilot */
            if (live_throttle > IDLE_THRESHOLD_FOR_START) {
                g_cruise_engaged = true;
                g_cruise_setpoint = live_throttle;
            }
        } else {
            g_cruise_engaged = false;       /* second press = toggle off */
        }
    }

    if (g_cruise_engaged) {
        int16_t delta = (int16_t)live_throttle - (int16_t)g_cruise_setpoint;
        if (delta < 0) delta = -delta;
        if (delta > CRUISE_DISENGAGE_THROTTLE_DELTA) {
            g_cruise_engaged = false;       /* trigger moved -> pilot overrides */
        }
    }

    return g_cruise_engaged ? g_cruise_setpoint : live_throttle;
}

/* Requires the start button to be held continuously for
 * START_HOLD_REQUIRED_MS before it's considered a valid request.
 * This guards against a brief bump or RF glitch being read as start intent
 * further down the chain (receiver still re-validates independently). */
static bool start_request_confirmed(void) {
    bool pressed = read_start_button_raw();

    if (pressed && !g_start_button_pressed_last) {
        /* rising edge - begin hold timer */
        g_start_hold_start_ms = millis();
        g_start_hold_confirmed = false;
    }

    if (pressed && !g_start_hold_confirmed) {
        if ((millis() - g_start_hold_start_ms) >= START_HOLD_REQUIRED_MS) {
            g_start_hold_confirmed = true;
        }
    }

    if (!pressed) {
        g_start_hold_confirmed = false;
    }

    g_start_button_pressed_last = pressed;
    return g_start_hold_confirmed;
}

static void build_and_send_packet(void) {
    throttle_packet_t pkt;
    pkt.sync = PACKET_SYNC_BYTE;
    pkt.seq = g_seq++;

    /* Evaluate kill FIRST so cruise sees it, then let cruise decide what
     * throttle value actually goes on the wire (frozen setpoint vs live). */
    if (read_kill_switch()) {
        g_kill_latched = true;   /* sticky: never un-latches without a re-arm */
    }
    pkt.throttle = apply_cruise(read_throttle_position());

    pkt.flags = 0;
    if (g_kill_latched) {
        /* Once killed, suppress start AND cruise so a latched kill can never
         * share a packet with them. Cruise is already force-disengaged above. */
        pkt.flags |= CMD_FLAG_KILL;
    } else if (g_cruise_engaged) {
        pkt.flags |= CMD_FLAG_CRUISE;
    } else if (start_request_confirmed()) {
        pkt.flags |= CMD_FLAG_START_REQ;
    }

    /* Accessories are independent level flags (lights/smoke) - they ride
     * alongside whatever the primary command is, including kill. */
    if (read_aux1_switch()) pkt.flags |= CMD_FLAG_AUX1;
    if (read_aux2_switch()) pkt.flags |= CMD_FLAG_AUX2;

    pkt.crc8 = crc8_compute((const uint8_t *)&pkt, PACKET_CRC_LEN);

    // radio.write(&pkt, PACKET_SIZE);  /* RF24 library call */
}

/* --- Local battery monitor (transmitter side) ---
 * Reads THIS unit's pack only. Drives a 3/4-LED bar that lights the moment
 * the handle powers on, and beeps a piezo when the pack is low. No return
 * telemetry: the handle never sees the receiver's battery. */
static uint16_t read_battery_mv(void) {
    // uint32_t raw = HAL_ADC_GetValue(&hadc_batt);   // dedicated battery ADC channel
    // return (uint16_t)(raw * ADC_MV_PER_LSB * DIVIDER_RATIO);
    return HANDLE_BATT.full_mv; /* placeholder: pretend full */
}

static void set_battery_leds(uint8_t lit) {
    (void)lit; /* drive the bar-graph LED GPIOs: light 'lit' of HANDLE_BATT.led_count */
}

static void set_buzzer(bool on) {
    (void)on;  /* drive the piezo GPIO (or start/stop a PWM tone) */
}

static void battery_tick(void) {
    uint32_t now = millis();
    if ((now - g_batt_last_poll_ms) >= BATTERY_POLL_MS) {
        g_batt_last_poll_ms = now;
        battery_status_t st = battery_eval(read_battery_mv(), &HANDLE_BATT);
        set_battery_leds(st.leds_lit);
        g_batt_low = st.low;
    }
    /* buzzer cadence is time-based, refresh it every loop (non-blocking) */
    set_buzzer(battery_buzzer_on(g_batt_low, now));
}

int handle_firmware_main(void) {
    /* --- Init section (fill in) ---
     * HAL_Init();
     * SystemClock_Config();
     * MX_ADC1_Init();
     * MX_GPIO_Init();
     * radio.begin();
     * radio.setPALevel(RF24_PA_HIGH);
     * radio.setDataRate(RF24_250KBPS);   // favor range/reliability over throughput here
     * radio.setChannel(...);              // pick a clear channel, consider scanning on boot
     * radio.setAutoAck(false);            // we don't need ack for this use case; sending fast
     *                                      // and relying on sequence numbers + watchdog is simpler
     *                                      // and avoids ack-related latency. Reconsider if you want
     *                                      // confirmed delivery for kill specifically.
     * radio.stopListening();              // handle only transmits
     */

    uint32_t last_tx = millis();

    while (1) {
        uint32_t now = millis();
        if ((now - last_tx) >= HANDLE_TX_PERIOD_MS) {
            last_tx = now;
            build_and_send_packet();
        }
        battery_tick(); /* self-paced via BATTERY_POLL_MS; non-blocking */
        /* keep loop tight - avoid blocking delays here, timing precision matters */
    }
}
