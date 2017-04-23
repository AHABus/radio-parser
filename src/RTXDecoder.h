///
/// @file        RTXDecoder.h
/// @brief       AHABus Packet Radio - frame & packet encoding routines
/// @author      Amy Parent
/// @copyright   2017 Amy Parent
///
#pragma once
#include "RTXCommon.h"

typedef void (*RTXPacketCallback)(RTXPacketHeader*, bool);

// Starts reading a stream of incoming frames, and attempts to decode packets
// from it. Packet data is sent to the coder's write function, and the packet
// callback is called every time a full packet has been decoded.
void fcore_rtxDecodeFrameStream(RTXCoder* decoder, RTXPacketCallback callback);
