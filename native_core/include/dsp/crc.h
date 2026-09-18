#pragma once
#include <cstdint>
#include <cstddef>

namespace dsp {

class CRC {
public:
    // CRC-16-CCITT for AX.25 HDLC (polynomial 0x8408 in reflected / 0x1021 normal)
    static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= static_cast<uint16_t>(data[i]);
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0x8408;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc ^ 0xFFFF;
    }

    // CRC-14 for FT8 (polynomial 0x2757)
    static uint16_t crc14_ft8(const uint8_t* bits, size_t num_bits) {
        uint32_t poly = 0x2757;
        uint32_t crc = 0;
        for (size_t i = 0; i < num_bits; ++i) {
            uint32_t msb = (crc >> 13) & 1;
            crc = ((crc << 1) & 0x3FFF) | (bits[i] & 1);
            if (msb) {
                crc ^= poly;
            }
        }
        return static_cast<uint16_t>(crc & 0x3FFF);
    }
};

} // namespace dsp
