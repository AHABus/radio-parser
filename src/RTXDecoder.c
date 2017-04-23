///
/// @file        RTXDecoder.c
/// @brief       AHABus Packet Radio - frame & packet encoding routines
/// @author      Amy Parent
/// @copyright   2017 Amy Parent
///
#include <stdio.h>
#include "RTXDecoder.h"
#include "RTXRS8.h"
#include "logger.h"

uint64_t    receivedBytes       = 0;
uint64_t    lostBytes           = 0;
uint64_t    validFrameBytes     = 0;
uint64_t    invalidFrameBytes   = 0;
uint64_t    correctedBytes      = 0;

static void _clearFrame(uint8_t frame[FRAME_SIZE]) {
    for(uint16_t i = 0; i < FRAME_SIZE; ++i) {
        frame[i] = 0x00;
    }
}

int readCB(uint8_t* byte, RTXCoder* coder) {
    int retVal = coder->readCallback(byte, coder->readData);
    if(retVal > 0) {
        receivedBytes += 1;
    }
    return retVal;
}

static bool _wasteUntilSync(RTXCoder* decoder) {
    int state = 0; // simple state machine. 0 -> waiting, 1 -> 0xaa, 2 -> 0x5a
    uint8_t current = 0x00;
    
    do {
        if(readCB(&current, decoder) == 0) { return false; }
        switch(state) {
            case 0:
                state = current == 0xAA ? 1 : 0;
                break;
            case 1:
                if(current == 0x5a) {
                    PRINT_LOG("frame start detected\n");
                    return true;
                }
                state = 0;
                break;
            default:
                break;
        }
        
    } while(true);
    return false;
}

static int _readFrame(RTXCoder* decoder, uint8_t frame[FRAME_SIZE]) {
    _clearFrame(frame);
    if(!_wasteUntilSync(decoder)) { return false; }
    frame[0] = 0x5A;
    
    int retval = 0;
    
    for(uint16_t i = 1; i < FRAME_SIZE; ++i) {
        retval = readCB(&frame[i], decoder);
        if(retval != 1) { goto error; }
        fprintf(stderr, "\r         ");
        fprintf(stderr, "\rrx: %03hu/256 bytes", i);
    }
    fprintf(stderr, "\r                                       \r");
    return 1;
error:
    fprintf(stderr, "\r                                       \r");
    return retval;
}

static uint8_t _read16(uint16_t* data, uint8_t* frame) {
    *data = (frame[0] << 8) | (frame[1]);
    return 2;
}

static uint8_t _read32(int32_t* data, uint8_t* frame) {
    *data = 0;
    for(int8_t i = 3; i >= 0; --i) {
        *data |= (*(frame++)) << (i*8);
    }
    return 4;
}

static int32_t _extractHeaderFrame(RTXCoder* decoder,
                                    RTXPacketHeader* header,
                                    uint8_t frame[FRAME_SIZE]) {
    uint8_t idx = FRAME_HEADERSIZE;
    
    if(frame[idx++] != PROTOCOL_VERSION) {
        PRINT_ERR("invalid packet protocol version\n");
        return -1;
    }
    
    header->payloadID = frame[idx++];
    idx += _read16(&header->length, &frame[idx]);
    idx += _read32(&header->latitude, &frame[idx]);
    idx += _read32(&header->longitude, &frame[idx]);
    idx += _read16(&header->altitude, &frame[idx]);
    
    header->length -= PACKET_HEADERSIZE;
    
    uint16_t toRead = header->length;
    
    for(; idx < FRAME_DATASIZE && toRead > 0; ++idx) {
        if(!decoder->writeCallback(frame[idx], decoder->writeData)) {
            PRINT_ERR("error fetching incoming byte\n");
            return -1;
        }
        toRead -= 1;
    }
    return toRead;
}

static int32_t _extractDataFrame(RTXCoder* decoder,
                                  int32_t toRead,
                                  uint8_t frame[FRAME_SIZE]) {
    
    for(uint8_t idx = FRAME_HEADERSIZE; idx < FRAME_DATASIZE && toRead > 0; ++idx) {
        if(!decoder->writeCallback(frame[idx], decoder->writeData)) {
            PRINT_ERR("error fetching incoming byte\n");
            return -1;
        }
        toRead -= 1;
    }
    return toRead;
}

static bool _validateFEC(uint8_t frame[FRAME_SIZE]) {
    int8_t corrected = 0;
    if((corrected = decode_rs_8(&frame[1], 0, 0, 0)) >= 0) {
        correctedBytes += corrected;
        return true;
    } else {
        return false;
    }
}

static bool _validateFrame(RTXCoder* decoder, uint8_t frame[FRAME_SIZE]) {
    uint8_t idx = 0;
    if(frame[idx++] != 0x5A) {
        PRINT_ERR("invalid frame start marker\n");
        return false;
    }
    if(frame[idx++] != PROTOCOL_VERSION) {
        PRINT_ERR("invalid frame protocol version\n");
        return false;
    }
    return true;
}

static uint16_t _checkLost(RTXCoder* decoder, uint8_t frame[FRAME_SIZE]) {
    uint16_t sequenceNumber = 0;
    _read16(&sequenceNumber, &frame[2]);
    
    uint32_t expected = (decoder->sequenceNumber + 1) % 65536;
    uint32_t diff = ((65536 + sequenceNumber - expected) % 65536);
    
    decoder->sequenceNumber = sequenceNumber;
    return diff;
}

void fcore_rtxDecodeFrameStream(RTXCoder* decoder, RTXPacketCallback callback) {
    
    uint8_t frame[FRAME_SIZE];
    RTXPacketHeader header;
    int32_t toRead  = 0;
    uint16_t lost   = 0;
    bool valid      = true;
    int state       = 0;    // state:   0: expecting packet header
                            //          1: packet data
    while(true) {
        // Reset for a new packet.
        valid = true;
        int retval = _readFrame(decoder, frame);
        
        if(retval == 0) {
            PRINT_ERR("unable to read frame stream\n");
            return;
        }
        if(retval == -1) {
            PRINT_ERR("incomplete frame detected\n");
        }
        
        if(!_validateFEC(frame)) {
            valid = false;
            PRINT_ERR("too many byte errors\n");
        }
        
        if(!_validateFrame(decoder, frame)) {
            valid = false;
        }
        
        lost = _checkLost(decoder, frame);
        if(valid && lost > 0) {
            PRINT_ERR("lost %hu frames\n", lost);
            invalidFrameBytes += 256 * lost;
            if((256 - FRAME_HEADERSIZE) * lost > toRead) {
                toRead = 0;
                state = 0;
            } else {
                toRead -= (256 - FRAME_HEADERSIZE) * lost;
                state = 1;
            }
        }
        
        switch(state) {
            case 0:
                toRead = _extractHeaderFrame(decoder, &header, frame);
                break;
            case 1:
                toRead = _extractDataFrame(decoder, toRead, frame);
                break;
            default:
                return;
                break;
        }
        
        if(toRead == 0) {
            callback(&header, valid);
            state = 0;
        }
        else if(toRead < 1) {
            callback(&header, false);
            toRead = 0;
            state = 0;
        }
        else {
            state = 1;
        }
        
        if(valid) {
            validFrameBytes += 256;
        } else {
            invalidFrameBytes += 256;
        }
        PRINT_LOG("rx stats: %llu received, " BOLDGREEN "%llu"
                  RESET WHITE "/" BOLDRED "%llu"
                  RESET WHITE " frame bytes (" BOLDYELLOW "%llu fixed"
                  RESET WHITE ")\n",
                  receivedBytes, validFrameBytes, invalidFrameBytes, correctedBytes);
        
        state = toRead == 0 ? 0 : 1;
    }
}
