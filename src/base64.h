#pragma once

#include <stddef.h>

char *base64_encode(const unsigned char *data, size_t data_length,
                    size_t *output_length);

char *base64_decode(const unsigned char *data, size_t data_length,
                    size_t *output_length);
