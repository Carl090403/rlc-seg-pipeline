#ifndef CRC32_H
#define CRC32_H
#include <stddef.h>
#include <stdint.h>
/* CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320), table driven. Thread-safe after crc32_init(). */
void crc32_init(void);
uint32_t crc32(const uint8_t *data, size_t len);
#endif
