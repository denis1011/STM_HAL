#include <cstring>
#include "Can.h"
#include "IsoTp.h"
#include "trace.h"

/*
 #define MAX_PAYLOAD_LENGTH 8
 #define MAX_MESSAGE_LENGTH 4095
 */

static const int __attribute__((unused)) g_DebugZones = ZONE_ERROR | ZONE_WARNING | ZONE_VERBOSE | ZONE_INFO;

using app::ISOTP;

ISOTP::ISOTP(const hal::Can& interface) : mInterface(interface)
{
    Trace(ZONE_INFO, "Constructor");
}

void ISOTP::send_Message(uint32_t sid, std::string_view message)
{
    if (message.size() > ISOTP::MAX_ISOTP_MESSAGE_LENGTH) {
        Trace(ZONE_WARNING, "Message in ISOTP has a max length of 4095 Bytes");
        return;
    }
    if (message.size() < MAX_ISOTP_PAYLOAD_SIZE) {
        Trace(ZONE_INFO, "Send Single Frame");
        ISOTP::send_SF(sid, message);
        return;
    } else if (message.size() > 8) {
        Trace(ZONE_INFO, "Send First Frame");
        return;
    }
}

void ISOTP::send_SF(uint32_t sid, std::string_view message)
{
    if (message.size() > MAX_ISOTP_PAYLOAD_SIZE) {
        return;
    }

    if (sid > 0x7ff) {
        Trace(ZONE_WARNING, "Extended IDs not supported yet\r\n");
        return;
    }
    mCanTxMsg.StdId = 0x7ff & sid;
    mCanTxMsg.IDE = 0;
    mCanTxMsg.RTR = 0;
    mCanTxMsg.DLC = message.size() + 1;
    std::memset(mCanTxMsg.Data, 0, sizeof(mCanTxMsg.Data));
    std::memcpy(mCanTxMsg.Data + 1, message.data(), message.size());
    mCanTxMsg.Data[0] = (FrameTypes::SINGLE_FRAME << 4) + (message.size() & 0x0f);

    mInterface.send(mCanTxMsg);
}

void ISOTP::send_FF(uint32_t sid, std::string_view message)
{
    if (message.size() < MAX_ISOTP_PAYLOAD_SIZE + 1) {
        Trace(ZONE_WARNING, "Message should be send as Single Frame");
        return;
    }
}

void ISOTP::receive_Message(void)
{
    std::memset(&mCanRxMsg, 0, sizeof(mCanRxMsg));

    auto receivedFrame = mInterface.receive(mCanRxMsg);
    Trace(ZONE_INFO, "received %d\r\n", receivedFrame);

    if ((mCanRxMsg.Data[0] & 0xF0) == FrameTypes::SINGLE_FRAME) {
        receive_SF(std::string_view(reinterpret_cast<const char*>(mCanRxMsg.Data), sizeof(mCanRxMsg.Data)));
    }
}

void ISOTP::receive_SF(std::string_view message)
{
    if (message.size() > MAX_ISOTP_PAYLOAD_SIZE) {
        Trace(ZONE_WARNING, "Message is to long for a single Frame");
        return;
    }
    Trace(ZONE_INFO, message.data());
}
