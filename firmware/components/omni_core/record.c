#include "omni/record.h"

bool omni_rec_header(const uint8_t *buf, size_t n, uint16_t *version, uint16_t *size)
{
    if (n < OMNI_REC_HDR) return false;
    uint16_t sz = (uint16_t)(buf[2] | buf[3] << 8);
    if (sz != n || sz > OMNI_REC_MAX) return false;
    *version = (uint16_t)(buf[0] | buf[1] << 8);
    *size = sz;
    return true;
}
