#include <stdint.h>

class CRC {

private:

    uint64_t m_CRC_table[256];
    static const uint64_t m_polynomial; 

    void create_crc_table();

public:
    CRC();
    ~CRC();
    uint64_t get_crc_code(uint8_t *stream, int length);

};