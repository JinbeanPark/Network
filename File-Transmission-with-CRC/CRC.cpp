/*
Jinbean Park - 805330751
*/

#include <iostream>
#include "CRC.h"

const uint64_t CRC::m_polynomial = 0x42F0E1EBA9EA3693ull;

CRC::CRC() {
    create_crc_table();
}

CRC::~CRC()
{}

void CRC::create_crc_table() {
       
        m_CRC_table[0] = 0;
        uint64_t crc = 0x8000000000000000;
        int i = 1;
        do {
            if ((crc & 0x8000000000000000) != 0) {
                crc <<= 1;
                crc ^= m_polynomial;
            }
            else {
                crc <<= 1;
            }
            for (int j = 0; j < i; j++) {
                m_CRC_table[i + j] = crc ^ m_CRC_table[j];
            }
            i <<= 1;
        } while (i < 256);
}

uint64_t CRC::get_crc_code(uint8_t *stream, int length) {

        uint64_t indexCRC, shift8Bit, rem = 0x0000000000000000;

        for (int i = 0; i < 7; i++) {

            shift8Bit = rem <<= 8;

            indexCRC = m_CRC_table[stream[i] ^ (rem >>= (length - 8))];

            rem = shift8Bit ^ indexCRC;
        }
        return rem;
}