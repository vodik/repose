#include "base64.h"
#include <stdlib.h>
#include <stdint.h>

static const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
                                     "ghijklmnopqrstuvwxyz0123456789+/";

static uint8_t decoding_table[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x3F,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1E,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00,
};

char *base64_encode(const unsigned char *data, size_t data_length,
                    size_t *output_length)
{
    const size_t length = 4 * ((data_length + 2) / 3);
    char *encoded_data = malloc(length + 1), *p = encoded_data;
    if (encoded_data == NULL)
        return NULL;

    for (size_t i = 0; i < data_length; i += 3) {
        const uint8_t lookahead[2] = {
            i + 1 < data_length,
            i + 2 < data_length
        };

        const uint8_t octets[3] = {
            data[i],
            lookahead[0] ? data[i + 1] : 0,
            lookahead[1] ? data[i + 2] : 0
        };

        const uint32_t bitpattern = (octets[0] << 16) + (octets[1] << 8) + octets[2];
        *p++ = encoding_table[(bitpattern >> 18) & 0x3F];
        *p++ = encoding_table[(bitpattern >> 12) & 0x3F];
        *p++ = lookahead[0] ? encoding_table[(bitpattern >> 6) & 0x3F] : '=';
        *p++ = lookahead[1] ? encoding_table[bitpattern & 0x3F] : '=';
    }

    *p = '\0';
    if (output_length)
        *output_length = length;
    return encoded_data;
}

char *base64_decode(const unsigned char *data, size_t data_length,
                    size_t *output_length)
{
    const size_t length = 3 * ((data_length + 2) / 4);
    char *decoded_data = malloc(length + 1), *p = decoded_data;
    if (decoded_data == NULL)
        return NULL;

    for (size_t i = 0; i < data_length; i += 4) {
        const uint8_t lookahead[3] = {
            i + 1 < data_length,
            i + 2 < data_length,
            i + 3 < data_length
        };

        const uint8_t octets[4] = {
            decoding_table[data[i]],
            lookahead[0] ? decoding_table[data[i + 1]] : 0,
            lookahead[1] ? decoding_table[data[i + 2]] : 0,
            lookahead[1] ? decoding_table[data[i + 3]] : 0
        };

        *p++ = (octets[0] << 2) + ((octets[1] & 0x30) >> 4);
        *p++ = ((octets[1] & 0xf) << 4) + ((octets[2] & 0x3c) >> 2);
        *p++ = ((octets[2] & 0x3) << 6) + octets[3];
    }

    *p = '\0';
    if (output_length)
        *output_length = length;
    return decoded_data;
}
