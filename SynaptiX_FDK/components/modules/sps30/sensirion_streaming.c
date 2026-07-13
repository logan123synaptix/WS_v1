/*
 * Copyright (c) 2024, Sensirion AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Sensirion AG nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 *  @file sensirion_streaming.c
 */

#include "sensirion_streaming.h"

#define SHDLC_MOSI_ADDR_POS 0
#define SHDLC_MOSI_CMD_POS 1
#define SHDLC_MOSI_LEN_POS 2

void sensirion_add_uint8_t_argument(sensirion_streaming_state* stream,
                                    uint8_t data) {
    stream->data[stream->offset++] = data;
}

void sensirion_add_bool_argument(sensirion_streaming_state* stream, bool data) {
    sensirion_add_uint8_t_argument(stream, (uint8_t)data);
}

void sensirion_add_uint32_t_argument(sensirion_streaming_state* stream,
                                     uint32_t data) {
    sensirion_common_uint32_t_to_bytes(data, &stream->data[stream->offset]);
    stream->offset += sizeof(data);
}

void sensirion_add_int32_t_argument(sensirion_streaming_state* stream,
                                    int32_t data) {
    sensirion_common_int32_t_to_bytes(data, &stream->data[stream->offset]);
    stream->offset += sizeof(data);
}

void sensirion_add_uint16_t_argument(sensirion_streaming_state* stream,
                                     uint16_t data) {
    sensirion_common_uint16_t_to_bytes(data, &stream->data[stream->offset]);
    stream->offset += sizeof(data);
}

void sensirion_add_int16_t_argument(sensirion_streaming_state* stream,
                                    int16_t data) {
    sensirion_common_int16_t_to_bytes(data, &stream->data[stream->offset]);
    stream->offset += sizeof(data);
}

void sensirion_add_float_argument(sensirion_streaming_state* stream,
                                  float data) {
    sensirion_common_float_to_bytes(data, &stream->data[stream->offset]);
    stream->offset += sizeof(data);
}

void sensirion_add_bytes_argument(sensirion_streaming_state* stream,
                                  const uint8_t* data, uint16_t data_length) {
    sensirion_common_copy_bytes(data, &stream->data[stream->offset],
                                data_length);
    stream->offset += data_length;
}