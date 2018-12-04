#include <string_view>
#include "Can.h"

namespace app
{
class ISOTP
{
    // max length sizes
    static constexpr const uint16_t MAX_ISOTP_MESSAGE_LENGTH = 4095;
    static constexpr const uint8_t MAX_ISOTP_PAYLOAD_SIZE = 7;

    // PCI Types
    /*
       static constexpr const uint8_t PCI_Single_Frame = 0x00;
       static constexpr const uint8_t PCI_First_Frame = 0x01;
       static constexpr const uint8_t PCI_Consecutive_Frame = 0x02;
       static constexpr const uint8_t PCI_Flow_Control = 0x03;
     */

    enum FrameTypes {
        SINGLE_FRAME = 0x00,
        FIRST_FRAME = 0x01,
        CONSECUTIVE_FRAME = 0x02,
        FLOW_CONTROL = 0x03
    };

    // Flow Control Status
    static constexpr const uint8_t FS_Clear_To_Send = 0x00;
    static constexpr const uint8_t FS_Wait = 0x01;
    static constexpr const uint8_t FS_Overflow = 0x02;

    const hal::Can& mInterface;

    CanTxMsg mCanTxMsg;
    CanRxMsg mCanRxMsg;

    void send_SF(uint32_t sid, std::string_view message);
    void send_FF(uint32_t sid, std::string_view message);
    void send_CF(uint32_t sid, std::string_view message);
    void send_FC(uint32_t sid, std::string_view message);
    void receive_SF(std::string_view message);
    void receive_FF(uint32_t did);
    void receive_CF(uint32_t did);
    void delay_ST(uint8_t separationTime);

public:
    ISOTP(const hal::Can& interface);

    void send_Message(uint32_t sid, std::string_view message);
    void receive_Message(void);
};

struct PCI {
    uint8_t frameType;
    uint16_t dl;
    uint8_t seq_number;
    uint8_t flowControlStatus;
    uint8_t blockSize;
    uint8_t minSeparationTime;
};
}
