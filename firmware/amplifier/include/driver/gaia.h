#ifndef GAIA_H
#define GAIA_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

constexpr uint8_t GAIA_SOF_BYTE         = 0xFF;
constexpr uint8_t GAIA_PROTOCOL_VERSION = 0x01;
constexpr uint16_t GAIA_VENDOR_QUALCOMM = 0x000A;
constexpr uint8_t GAIA_FLAG_CHECKSUM    = 0x01;

enum GaiaHeaderOffset : size_t {
    OFFSET_SOF         = 0,
    OFFSET_VERSION     = 1,
    OFFSET_FLAGS       = 2,
    OFFSET_LENGTH      = 3,
    OFFSET_VENDOR_MSB  = 4,
    OFFSET_VENDOR_LSB  = 5,
    OFFSET_COMMAND_MSB = 6,
    OFFSET_COMMAND_LSB = 7,
    GAIA_HEADER_SIZE   = 8
};

enum GaiaCommandId : uint16_t {
    CMD_CHANGE_VOLUME             = 0x0201,
    CMD_GET_VOLUME                = 0x0281,
    CMD_DEVICE_RESET              = 0x0202,
    CMD_PLAY_TONE                 = 0x0209,
    CMD_SET_LED_CONTROL           = 0x0207,
    CMD_GET_LED_CONTROL           = 0x0287,
    CMD_FACTORY_DEFAULT_RESET     = 0x0104,
    CMD_GET_API_VERSION           = 0x0300,
    CMD_GET_APPLICATION_VERSION   = 0x0304,
    CMD_GET_CURRENT_RSSI          = 0x0301,
    CMD_GET_CURRENT_BATTERY_LEVEL = 0x0302,
    CMD_SET_EQ_CONTROL            = 0x0214,
    CMD_GET_EQ_CONTROL            = 0x0294,
    CMD_GET_BASS_BOOST_CONTROL    = 0x0295,
    CMD_SET_BASS_BOOST_CONTROL    = 0x0215,
    CMD_GET_3D_ENHANCEMENT_CONTROL = 0x0296,
    CMD_SET_3D_ENHANCEMENT_CONTROL = 0x0216,
    CMD_GET_USER_EQ_CONTROL       = 0x02A0,
    CMD_SET_USER_EQ_CONTROL       = 0x0220,
    CMD_GET_EQ_PARAMETER          = 0x029A,
    CMD_SET_EQ_PARAMETER          = 0x021A,
    CMD_RETRIEVE_PS_KEY           = 0x0710,
    CMD_STORE_PS_KEY              = 0x0712,
    CMD_DELETE_PDL                = 0x0750,
    CMD_GET_DEVICE_NAME           = 0x018F,
    CMD_SET_DEVICE_NAME           = 0x010F,
    CMD_GET_BOOT_MODE             = 0x0282,
    CMD_GET_MODULE_ID             = 0x0303,
    CMD_GET_PEER_ADDRESS          = 0x030A,
    CMD_GET_CODEC                 = 0x02C0,
    CMD_SET_CODEC                 = 0x0240,
    CMD_GET_POWER_STATE           = 0x0284,
    CMD_SET_POWER_STATE           = 0x0204,
    CMD_GET_PIO_STATE             = 0x0306,
    CMD_SET_PIO_CONTROL           = 0x0203,
    CMD_GET_AUDIO_SOURCE          = 0x029E,
    CMD_SET_AUDIO_SOURCE          = 0x021E,
    CMD_ENTER_BLUETOOTH_PAIRING_MODE = 0x021D,
    CMD_GET_AUDIO_PROMPT_LANGUAGE = 0x0292,
    CMD_CHANGE_AUDIO_PROMPT_LANGUAGE = 0x020B,
    CMD_GET_SPEECH_RECOGNITION_CONTROL = 0x028C,
    CMD_SET_SPEECH_RECOGNITION_CONTROL = 0x020C,
    CMD_FIND_MY_REMOTE            = 0x022B,
    CMD_GET_TWS_AUDIO_ROUTING     = 0x02A4,
    CMD_SET_TWS_AUDIO_ROUTING     = 0x0224,
    CMD_GET_TWS_VOLUME            = 0x02A5,
    CMD_SET_TWS_VOLUME            = 0x0225,
    CMD_AV_REMOTE_CONTROL         = 0x021F,
    CMD_REGISTER_NOTIFICATION     = 0x4001,
    CMD_CANCEL_NOTIFICATION       = 0x4002,
    CMD_EVENT_NOTIFICATION        = 0x4003,
    CMD_GET_DATA_ENDPOINT_MODE    = 0x02AE,
    CMD_SET_DATA_ENDPOINT_MODE    = 0x022E
};

enum AvrcpOpCode : uint8_t {
    AVRCP_PLAY        = 0x44,
    AVRCP_PAUSE       = 0x46,
    AVRCP_STOP        = 0x45,
    AVRCP_MUTE        = 0x43,
    AVRCP_FORWARD     = 0x4B,
    AVRCP_BACKWARD    = 0x4C,
    AVRCP_VOLUME_UP   = 0x41,
    AVRCP_VOLUME_DOWN = 0x42
};

struct GaiaPacket {
    uint8_t version;
    bool enableChecksum;
    uint16_t vendorId;
    uint16_t commandId;
    std::vector<uint8_t> payload;
};

// Helper string decoders
const char* getGaiaStatusString(uint8_t status);
const char* getFilterName(uint16_t ordinal);
const char* getTwsSpeakerName(uint8_t speaker);
const char* getTwsChannelName(uint8_t channel);
const char* getCodecName(uint8_t codec);
const char* getAudioSourceName(uint8_t src);
const char* getGaiaEventName(uint8_t eventId);

// Public API
void gaiaInit();
void gaiaTick(uint32_t now);
bool gaiaIsConnected();
int8_t gaiaGetLastRssi();
void gaiaDisconnect();
void gaiaResetConnectionState();
uint8_t gaiaGetFailureCount();
void gaiaSendCommand(uint16_t commandId, const uint8_t* payload = nullptr, uint8_t payloadLen = 0);
void gaiaSendAvrcpControl(AvrcpOpCode opCode);
void gaiaProcessConsoleCommand(std::string command);

#endif // GAIA_H
