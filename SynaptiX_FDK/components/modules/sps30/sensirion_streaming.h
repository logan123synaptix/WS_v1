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
 *  @file sensirion_streaming.h
 */
#ifndef SENSIRION_STREAMING_H
#define SENSIRION_STREAMING_H

#include "sensirion_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief This data structure holds the state while data is read/written
 *        from/to a sensor.
 */
typedef struct sensirion_streaming_state_tag {
    uint8_t* data;    //< Pointer to the data the stream will send to the sensor
    uint16_t offset;  //< Number of valid bytes in the buffer
    uint8_t checksum;       //< Checksum or crc depending on used protocol
    int16_t stream_status;  //< status of the last stream (read/write) operation
    union {
        int16_t (*read)(uint16_t length, uint8_t* data);
        int16_t (*write)(uint16_t length, const uint8_t* data);
    } stream;  //< io operation
} sensirion_streaming_state;

/**
 * sensirion_add_uint8_t_argument() - Add a uint8_t to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data     uint8_t to be written into the buffer.
 */
void sensirion_add_uint8_t_argument(sensirion_streaming_state* stream,
                                    uint8_t data);

/**
 * sensirion_add_bool_argument() - Add a bool to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data     bool to be written into the stream-buffer.
 */
void sensirion_add_bool_argument(sensirion_streaming_state* stream, bool data);

/**
 * sensirion_add_uint32_t_argument() - Add a uint32_t to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data     uint32_t to be written into the stream-buffer.
 */
void sensirion_add_uint32_t_argument(sensirion_streaming_state* stream,
                                     uint32_t data);

/**
 * sensirion_add_int32_t_argument() - Add a int32_t to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data     int32_t to be written into the stream-buffer.
 */
void sensirion_add_int32_t_argument(sensirion_streaming_state* stream,
                                    int32_t data);

/**
 * sensirion_add_uint16_t_argument() - Add a uint16_t to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data     uint16_t to be written into the stream-buffer.
 */
void sensirion_add_uint16_t_argument(sensirion_streaming_state* stream,
                                     uint16_t data);

/**
 * sensirion_add_int16_t_argument() - Add a int16_t to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data     int16_t to be written into the stream-buffer.
 */
void sensirion_add_int16_t_argument(sensirion_streaming_state* stream,
                                    int16_t data);

/**
 * sensirion_add_float_argument() - Add a float to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data     float to be written into the stream-buffer.
 */
void sensirion_add_float_argument(sensirion_streaming_state* stream,
                                  float data);

/**
 * sensirion_add_bytes_argument() - Add a byte array to the stream.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param data        Pointer to data to be written into the stream-buffer.
 * @param data_length Number of bytes to be written into the stream-buffer.
 */
void sensirion_add_bytes_argument(sensirion_streaming_state* stream,
                                  const uint8_t* data, uint16_t data_length);

#ifdef __cplusplus
}
#endif

#endif  // SENSIRION_STREAMING_H