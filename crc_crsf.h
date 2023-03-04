#ifndef __CRC_H__
#define __CRC_H__

#include <inttypes.h>

uint8_t crc8(const uint8_t * ptr, uint32_t len);
uint8_t crc8_BA(const uint8_t * ptr, uint32_t len);

#endif
