#ifndef CRC8_H
#define CRC8_H

#include <stdint.h>
#include <stddef.h>

/* CRC-8/MAXIM (Dallas 1-Wire polynomial, 0x31 reflected = 0x8C).
 * Simple bit-banged version - negligible cost on an STM32, and easy
 * to hand-verify against known test vectors before trusting it.
 *
 * Test vector: crc8_compute("123456789", 9) should equal 0xA1
 * for this specific poly/init/reflect combination - verify this
 * on your target before relying on it in the packet pipeline.
 */
static inline uint8_t crc8_compute(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x01) {
                crc = (crc >> 1) ^ 0x8C;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

#endif /* CRC8_H */
