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
    pkt.throttle = read_throttle_position();

    pkt.flags = 0;
    if (read_kill_switch()) {
        g_kill_latched = true;   /* sticky: never un-latches without a re-arm */
    }
    if (g_kill_latched) {
        pkt.flags |= CMD_FLAG_KILL;
        /* Once killed, suppress any start request so a latched kill and a
         * start can never be commanded in the same packet. */
    } else if (start_request_confirmed()) {
        pkt.flags |= CMD_FLAG_START_REQ;
    }

    pkt.crc8 = crc8_compute((const uint8_t *)&pkt, PACKET_CRC_LEN);

    // radio.write(&pkt, PACKET_SIZE);  /* RF24 library call */
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
        /* keep loop tight - avoid blocking delays here, timing precision matters */
    }
}
