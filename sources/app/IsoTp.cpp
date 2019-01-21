#include <cstring>
#include "os_Task.h"
#include "Can.h"
#include "IsoTp.h"
#include "trace.h"

static const int __attribute__((unused)) g_DebugZones = 0;// ZONE_ERROR | ZONE_WARNING | ZONE_VERBOSE | ZONE_INFO;

using app::ISOTP;

ISOTP::ISOTP(const hal::Can& interface, uint32_t sid, uint32_t did) : mInterface(interface), mSid(sid), mDid(did)
{
    Trace(ZONE_INFO, "Constructor\r\n");
}

size_t ISOTP::send_Message(std::string_view message, std::chrono::milliseconds timeout)
{
    if (message.size() > ISOTP::MAX_ISOTP_MESSAGE_LENGTH) {
        Trace(ZONE_WARNING, "Message in ISOTP has a max length of 4095 Bytes.\r\n");
        return 0;
    }
    if (message.size() <= MAX_ISOTP_PAYLOAD_SIZE) {
        return ISOTP::send_SF(message);
    } else {
        ISOTP::send_FF(message);
        if (ISOTP::receive_FC(timeout)) {
            return ISOTP::send_CF(message);
        }
        return 0;
    }
}

size_t ISOTP::receive_Message(char* buffer, const size_t length, std::chrono::milliseconds timeout)
{
    std::memset(&mCanRxMsg, 0, sizeof(mCanRxMsg));
    std::memset(buffer, 0, sizeof(length));
    bool frame_received = false;
    uint32_t startTime = os::Task::getTickCount();
    uint32_t timeNow = startTime;

    while (timeNow < (startTime + timeout.count())) {
        if (mInterface.receive(mCanRxMsg)) {
            frame_received = true;
            break;
        }
        timeNow = os::Task::getTickCount();
    }
    if (!frame_received) {
        Trace(ZONE_INFO, "Receiving Frame failed probably due timeout.\r\n");
        return 0;
    }
    if ((mCanRxMsg.Data[0] & 0xF0) == FrameTypes::SINGLE_FRAME) {
        mRxMsgLength = mCanRxMsg.Data[0];
        if (length < mRxMsgLength) {
            Trace(ZONE_INFO, "Message is to long for buffer.\r\n");
            return 0;
        }
        if (mRxMsgLength > 7) {
            Trace(ZONE_INFO, "Message is to long for single Frame\r\n");
            return 0;
        }
        std::memcpy(buffer, (mCanRxMsg.Data + 1), mRxMsgLength);
        return receive_SF(std::string_view(reinterpret_cast<const char*>(mCanRxMsg.Data), sizeof(mCanRxMsg.Data)));
        ;
    } else if ((mCanRxMsg.Data[0] >> 4) == FrameTypes::FIRST_FRAME) {
        mRxMsgLength = (mCanRxMsg.Data[0] & 0x0f) | (mCanRxMsg.Data[1]);
        receive_FF(std::string_view(reinterpret_cast<const char*>(mCanRxMsg.Data), sizeof(mCanRxMsg.Data)));
        if (mRxMsgLength > length) {
            Trace(ZONE_INFO, "Message is to long for buffer Overflow.\r\n");
            send_FC(mRxMsgLength, FlowControlStatus::FS_Overflow);
            return 0;
        }
        std::memcpy(buffer, (mCanRxMsg.Data + 2), 6);
        send_FC(mRxMsgLength, FlowControlStatus::FS_Clear_To_Send);
        return receive_CF(buffer, length, timeout);
    } else {
        auto tempFirstByte = mCanRxMsg.Data[0];

        Trace(ZONE_INFO, "Not received a single or first frame because byte 0 is = %d\r\n", tempFirstByte);
        return 0;
    }
}

size_t ISOTP::send_SF(std::string_view message)
{
    if (mSid > 0x7ff) {
        Trace(ZONE_WARNING, "Extended IDs not supported yet.\r\n");
        return 0;
    }
    mCanTxMsg.StdId = 0x7ff & mSid;
    mCanTxMsg.IDE = 0;
    mCanTxMsg.RTR = 0;
    mCanTxMsg.DLC = message.size() + 1;
    std::memset(mCanTxMsg.Data, 0, sizeof(mCanTxMsg.Data));
    mCanTxMsg.Data[0] = (FrameTypes::SINGLE_FRAME << 4) + (message.size() & 0x0f);
    std::memcpy(mCanTxMsg.Data + 1, message.data(), message.size());

    mInterface.send(mCanTxMsg);
    return message.size();
}

void ISOTP::send_FF(std::string_view message)
{
    if (mSid > 0x7ff) {
        Trace(ZONE_WARNING, "Extended IDs not supported yet.\r\n");
        return;
    }
    mCanTxMsg.StdId = 0x7ff & mSid;
    mCanTxMsg.IDE = 0;
    mCanTxMsg.RTR = 0;
    mCanTxMsg.DLC = 8;
    std::memset(mCanTxMsg.Data, 0, sizeof(mCanTxMsg.Data));
    mCanTxMsg.Data[0] = (FrameTypes::FIRST_FRAME << 4) + ((message.size() & 0x00000f00) >> 8);
    mCanTxMsg.Data[1] = message.size() & 0x000000ff;
    std::memcpy(mCanTxMsg.Data + 2, message.data(), 6);
    mInterface.send(mCanTxMsg);
}

void ISOTP::send_FC(const size_t length, const FlowControlStatus& status)
{
    mCanTxMsg.StdId = 0x7ff & mDid;
    mCanTxMsg.IDE = 0;
    mCanTxMsg.RTR = 0;
    mCanTxMsg.DLC = 3;
    std::memset(mCanTxMsg.Data, 0, sizeof(mCanTxMsg.Data));
    mCanTxMsg.Data[0] = (FrameTypes::FLOW_CONTROL << 4) + (status & 0x0f);
    // blocksize
    mCanTxMsg.Data[1] = length / 7;
    // seperation Time of 1 milliseconds
    mCanTxMsg.Data[2] = 0x1;

    mInterface.send(mCanTxMsg);
}

size_t ISOTP::send_CF(std::string_view message)
{
    uint8_t sequenzNumber = 1;
    int index = 6;

    mCanTxMsg.StdId = 0x7ff & mSid;
    mCanTxMsg.IDE = 0;
    mCanTxMsg.RTR = 0;

    while (message.size() >= static_cast<long unsigned int>(index)) {
        if (mSeperationTime.count()) {
            os::ThisTask::sleep(mSeperationTime);
        }
        mCanTxMsg.DLC = std::min<uint8_t>(7, message.size() - index) + 1;
        std::memset(mCanTxMsg.Data, 0, sizeof(mCanTxMsg.Data));
        mCanTxMsg.Data[0] = (FrameTypes::CONSECUTIVE_FRAME << 4) + ((sequenzNumber) & 0x0f);
        sequenzNumber = ++sequenzNumber & 0x0F;
        std::memcpy(mCanTxMsg.Data + 1, message.data() + index, std::min<unsigned int>(7, message.size() - index));
        mInterface.send(mCanTxMsg);
        index += 7;
    }
    return message.size();
}

size_t ISOTP::receive_SF(std::string_view message)
{
    if (message[0] > MAX_ISOTP_PAYLOAD_SIZE) {
        Trace(ZONE_WARNING, "Message is to long for a single frame.\r\n");
        return 0;
    }
    return message.size() - 1;
}

size_t ISOTP::receive_FF(std::string_view message)
{
    if (mRxMsgLength <= MAX_ISOTP_PAYLOAD_SIZE) {
        Trace(ZONE_WARNING, "Message is to short for a first frame.\r\n");
        return 0;
    }
    return 6;
}

size_t ISOTP::receive_CF(char* buffer, size_t length, std::chrono::milliseconds timeout)
{
    std::memset(&mCanRxMsg, 0, sizeof(CanRxMsg));
    size_t sequenzNumberOfConsecutiveFrame = 1;
    size_t index = 6;

    while ((mRxMsgLength >= index)) {
        uint32_t startTime = os::Task::getTickCount();
        uint32_t timeNow = startTime;
        bool consecutive_Frame_received = false;

        while (timeNow < (startTime + timeout.count())) {
            if (mInterface.receive(mCanRxMsg)) {
                consecutive_Frame_received = true;
                if (sequenzNumberOfConsecutiveFrame != (mCanRxMsg.Data[0] & 0x0f)) {
                    return 0;
                }
                std::memcpy(buffer + index, (mCanRxMsg.Data + 1), std::min<uint8_t>(7, mRxMsgLength - index) + 1);
                sequenzNumberOfConsecutiveFrame = ++sequenzNumberOfConsecutiveFrame & 0x0f;
                index += 7;
                break;
            }
            timeNow = os::Task::getTickCount();
        }
        if (!consecutive_Frame_received) {
            return 0;
        }
    }
    return std::strlen(buffer);
}

bool ISOTP::receive_FC(std::chrono::milliseconds timeout)
{
    bool flow_control_received = false;
    uint32_t startTime = os::Task::getTickCount();
    uint32_t timeNow = startTime;

    if (mDid > 0x7ff) {
        Trace(ZONE_WARNING, "Extended IDs not supported yet.\r\n");
        return 0;
    }
    while (timeNow < (startTime + timeout.count())) {
        if (mInterface.receive(mCanRxMsg)) {
            flow_control_received = true;
            break;
        }
        timeNow = os::Task::getTickCount();
    }
    if (!flow_control_received) {
        Trace(ZONE_INFO, "No flow control received.\r\n");
        return false;
    }
    if ((mCanRxMsg.StdId != mDid) && (mCanRxMsg.DLC != 3) &&
        (mCanRxMsg.Data[0] != (FrameTypes::FLOW_CONTROL << 4 & 0xf0)))
    {
        Trace(ZONE_INFO, "False flow control status received or has a different did or frame size is to long.\r\n");
        return false;
    }
    mSeperationTime = std::chrono::milliseconds(mCanRxMsg.Data[2]);
    mBlockSize = mCanRxMsg.Data[1];
    return true;
}
