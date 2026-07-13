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
 *  @file sensirion_streaming_shdlc.h
 */
#ifndef SENSIRION_STREAMING_SHDLC_H
#define SENSIRION_STREAMING_SHDLC_H

#include "sensirion_common.h"
#include "sensirion_shdlc.h"
#include "sensirion_streaming.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * sensirion_shdlc_begin_stream() - Initialize buffer and add the first three
 *                                  fixed-use data bytes to it.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param buffer   The pointer to big enough buffer to hold the payload that
 *                 needs to be sent (+ 4 bytes for an possible protocol header).
 * @param command     Command to be written into the stream.
 * @param address     Address to be written into the stream.
 * @param data_length Specifies the size of the parameters.
 */
void sensirion_shdlc_begin_stream(sensirion_streaming_state* stream,
                                  uint8_t* buffer, uint8_t command,
                                  uint8_t address, uint8_t data_length);

/**
 * sensirion_shdlc_write_request() - Transmit the SHDLC request.
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @return         NO_ERROR on success, an error code otherwise.
 */
int16_t sensirion_shdlc_write_request(sensirion_streaming_state* stream);

/**
 * sensirion_shdlc_read_response() - Receive data from the slave.
 *
 * @note The header and data must be discarded on failure
 *
 * @param stream Data structure that holds the state while data is
 *                     received or transmitted.
 * @param expected_data_length Expected data amount to receive.
 * @param header               Memory where the SHDLC header containing the
 *                             sender address, command, state and data_length
 *                             is stored.
 * @param max_timeout_ms       timeout in milliseconds. This is the maximum time
 * the request is allowed to take.
 *
 * @return            NO_ERROR on success, an error code otherwise
 */
int16_t sensirion_shdlc_read_response(sensirion_streaming_state* stream,
                                      uint8_t expected_data_length,
                                      struct sensirion_shdlc_rx_header* header,
                                      uint32_t max_timeout_ms);

#ifdef __cplusplus
}
#endif

#endif  // SENSIRION_STREAMING_SHDLC_H