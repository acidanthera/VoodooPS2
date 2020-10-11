/*
 * Elan PS2 touchpad integration
 *
 * Mostly contains code ported from Linux
 * https://github.com/torvalds/linux/blob/master/drivers/input/mouse/elantech.c
 *
 * Created by Bartosz Korczyński (@bandysc), Hiep Bao Le (@hieplpvip)
 * Special thanks to Kishor Prins (@kprinssu), EMlyDinEsHMG and whole VoodooInput team
 */

#ifndef _APPLEPS2ELAN_H
#define _APPLEPS2ELAN_H

#include "../VoodooPS2Controller/ApplePS2MouseDevice.h"
#include <IOKit/hidsystem/IOHIPointing.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

#include "VoodooInputMultitouch/VoodooInputEvent.h"
#include "VoodooPS2TrackpadCommon.h"

struct virtual_finger_state {
    TouchCoordinates prev;
    TouchCoordinates now;
    uint8_t pressure;
    uint8_t width;
    bool touch;
    bool button;
    MT2FingerType fingerType;
};

#define kPacketLengthMax 6

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
// FROM LINUX ELANTECH.C

/*
 * Command values for Synaptics style queries
 */
#define ETP_FW_ID_QUERY               0x00
#define ETP_FW_VERSION_QUERY          0x01
#define ETP_CAPABILITIES_QUERY        0x02
#define ETP_SAMPLE_QUERY              0x03
#define ETP_RESOLUTION_QUERY          0x04

/*
 * Command values for register reading or writing
 */
#define ETP_REGISTER_READ             0x10
#define ETP_REGISTER_WRITE            0x11
#define ETP_REGISTER_READWRITE        0x00

/*
 * Hardware version 2 custom PS/2 command value
 */
#define ETP_PS2_CUSTOM_COMMAND        0xf8

/*
 * Times to retry a ps2_command and millisecond delay between tries
 */
#define ETP_PS2_COMMAND_TRIES         3
#define ETP_PS2_COMMAND_DELAY         500

/*
 * Times to try to read back a register and millisecond delay between tries
 */
#define ETP_READ_BACK_TRIES           5
#define ETP_READ_BACK_DELAY           2000

/*
 * Register bitmasks for hardware version 1
 */
#define ETP_R10_ABSOLUTE_MODE         0x04
#define ETP_R11_4_BYTE_MODE           0x02

/*
 * Capability bitmasks
 */
#define ETP_CAP_HAS_ROCKER            0x04

/*
 * One hard to find application note states that X axis range is 0 to 576
 * and Y axis range is 0 to 384 for harware version 1.
 * Edge fuzz might be necessary because of bezel around the touchpad
 */
#define ETP_EDGE_FUZZ_V1              32

#define ETP_XMIN_V1                   (  0 + ETP_EDGE_FUZZ_V1)
#define ETP_XMAX_V1                   (576 - ETP_EDGE_FUZZ_V1)
#define ETP_YMIN_V1                   (  0 + ETP_EDGE_FUZZ_V1)
#define ETP_YMAX_V1                   (384 - ETP_EDGE_FUZZ_V1)

/*
 * The resolution for older v2 hardware doubled.
 * (newer v2's firmware provides command so we can query)
 */
#define ETP_XMIN_V2                   0
#define ETP_XMAX_V2                   1152
#define ETP_YMIN_V2                   0
#define ETP_YMAX_V2                   768

// Preasure min-max
#define ETP_PMIN_V2                   0
#define ETP_PMAX_V2                   255

// Width min-max
#define ETP_WMIN_V2                   0
#define ETP_WMAX_V2                   15

/*
 * v3 hardware has 2 kinds of packet types,
 * v4 hardware has 3.
 */
#define PACKET_UNKNOWN                0x01
#define PACKET_DEBOUNCE               0x02
#define PACKET_V3_HEAD                0x03
#define PACKET_V3_TAIL                0x04
#define PACKET_V4_HEAD                0x05
#define PACKET_V4_MOTION              0x06
#define PACKET_V4_STATUS              0x07
#define PACKET_TRACKPOINT             0x08

/*
 * track up to 5 fingers for v4 hardware
 */
#define ETP_MAX_FINGERS               5

/*
 * weight value for v4 hardware
 */
#define ETP_WEIGHT_VALUE              5

/*
 * Bus information on 3rd byte of query ETP_RESOLUTION_QUERY(0x04)
 */
#define ETP_BUS_PS2_ONLY              0
#define ETP_BUS_SMB_ALERT_ONLY        1
#define ETP_BUS_SMB_HST_NTFY_ONLY     2
#define ETP_BUS_PS2_SMB_ALERT         3
#define ETP_BUS_PS2_SMB_HST_NTFY      4

/*
 * New ICs are either using SMBus Host Notify or just plain PS2.
 *
 * ETP_FW_VERSION_QUERY is:
 * Byte 1:
 *  - bit 0..3: IC BODY
 * Byte 2:
 *  - bit 4: HiddenButton
 *  - bit 5: PS2_SMBUS_NOTIFY
 *  - bit 6: PS2CRCCheck
 */
#define ETP_NEW_IC_SMBUS_HOST_NOTIFY(fw_version)    \
        ((((fw_version) & 0x0f2000) == 0x0f2000) && \
         ((fw_version) & 0x0000ff) > 0)

/*
 * The base position for one finger, v4 hardware
 */
struct finger_pos {
    unsigned int x;
    unsigned int y;
};

struct elantech_device_info {
    unsigned char capabilities[3];
    unsigned char samples[3];
    unsigned char debug;
    unsigned char hw_version;
    unsigned int fw_version;
    unsigned int x_min;
    unsigned int y_min;
    unsigned int x_max;
    unsigned int y_max;
    unsigned int x_res;
    unsigned int y_res;
    unsigned int x_traces;
    unsigned int y_traces;
    unsigned int width;
    unsigned int bus;
    bool paritycheck;
    bool jumpy_cursor;
    bool reports_pressure;
    bool crc_enabled;
    bool set_hw_resolution;
    bool is_buttonpad;
    bool has_trackpoint;
    bool has_middle_button;
};

struct elantech_data {
    unsigned char reg_07;
    unsigned char reg_10;
    unsigned char reg_11;
    unsigned char reg_20;
    unsigned char reg_21;
    unsigned char reg_22;
    unsigned char reg_23;
    unsigned char reg_24;
    unsigned char reg_25;
    unsigned char reg_26;
    unsigned int single_finger_reports;
    struct finger_pos mt[ETP_MAX_FINGERS];
    unsigned char parity[256];
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2Elan Class Declaration
//

class EXPORT ApplePS2Elan : public IOHIPointing {
    typedef IOHIPointing super;
    OSDeclareDefaultStructors(ApplePS2Elan);

private:
    IOService*            voodooInputInstance {nullptr};
    ApplePS2MouseDevice*  _device {nullptr};
    bool                  _interruptHandlerInstalled {false};
    bool                  _powerControlHandlerInstalled {false};
    UInt32                _packetByteCount {0};
    UInt32                _packetLength {0};
    RingBuffer<UInt8, kPacketLengthMax * 32> _ringBuffer {};

    IOCommandGate*        _cmdGate {nullptr};

    VoodooInputEvent inputEvent {};

    // when trackpad has physical button
    UInt32 leftButton = 0;
    UInt32 rightButton = 0;
    UInt32 lastLeftButton = 0;
    UInt32 lastRightButton = 0;

    const float sin30deg = 0.5f;
    const float cos30deg = 0.86602540378f;
    UInt32 lastFingers = 0;

    bool trackpointScrolling {false};

    int heldFingers = 0;
    int headPacketsCount = 0;
    virtual_finger_state virtualFinger[ETP_MAX_FINGERS] {};

    static_assert(ETP_MAX_FINGERS <= kMT2FingerTypeLittleFinger, "Too many fingers for one hand");

    ForceTouchMode _forceTouchMode {FORCE_TOUCH_BUTTON};

    int _scrollresolution {2300};
    int wakedelay {1000};
    int _trackpointMultiplierX {120};
    int _trackpointMultiplierY {120};
    int _trackpointDividerX {120};
    int _trackpointDividerY {120};

    int _mouseResolution {0x3};
    int _mouseSampleRate {200};

    bool _set_hw_resolution {false};

    bool ignoreall {false};
    bool usb_mouse_stops_trackpad {true};

    bool _processusbmouse {true};
    bool _processbluetoothmouse {true};

    uint64_t keytime {0};
    uint64_t maxaftertyping {500000000};

    OSSet *attachedHIDPointerDevices {nullptr};

    IONotifier *usb_hid_publish_notify {nullptr};          // Notification when an USB mouse HID device is connected
    IONotifier *usb_hid_terminate_notify {nullptr};        // Notification when an USB mouse HID device is disconnected

    IONotifier *bluetooth_hid_publish_notify {nullptr};    // Notification when a bluetooth HID device is connected
    IONotifier *bluetooth_hid_terminate_notify {nullptr};  // Notification when a bluetooth HID device is disconnected

    virtual PS2InterruptResult interruptOccurred(UInt8 data);
    virtual void packetReady();
    virtual void setDevicePowerState(UInt32 whatToDo);

    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;

    void setParamPropertiesGated(OSDictionary *dict);
    void injectVersionDependentProperties(OSDictionary *dict);

    void registerHIDPointerNotifications();
    void unregisterHIDPointerNotifications();

    void notificationHIDAttachedHandlerGated(IOService *newService, IONotifier *notifier);
    bool notificationHIDAttachedHandler(void *refCon, IOService *newService, IONotifier *notifier);

    elantech_data etd {};
    elantech_device_info info {};
    int elantechDetect();
    int elantechQueryInfo();
    int elantechSetProperties();
    int elantechSetAbsoluteMode();
    int elantechSetInputParams();
    int elantechSetupPS2();
    int elantechReadReg(unsigned char reg, unsigned char *val);
    int elantechWriteReg(unsigned char reg, unsigned char val);
    int elantechDebounceCheckV2();
    int elantechPacketCheckV1();
    int elantechPacketCheckV2();
    int elantechPacketCheckV3();
    int elantechPacketCheckV4();
    void elantechRescale(unsigned int x, unsigned int y);
    void elantechReportAbsoluteV1();
    void elantechReportAbsoluteV2();
    void elantechReportAbsoluteV3(int packetType);
    void elantechReportAbsoluteV4(int packetType);
    void elantechReportTrackpoint();
    void processPacketStatusV4();
    void processPacketHeadV4();
    void processPacketMotionV4();
    void sendTouchData();
    void resetMouse();
    void setTouchPadEnable(bool enable);

    static MT2FingerType GetBestFingerType(int i);

    template<int I>
    int ps2_command(UInt8* params, unsigned int command);
    template<int I>
    int elantech_ps2_command(unsigned char *param, int command);
    int ps2_sliced_command(UInt8 command);
    template<int I>
    int synaptics_send_cmd(unsigned char c, unsigned char *param);
    template<int I>
    int elantech_send_cmd(unsigned char c, unsigned char *param);
    template<int I>
    int send_cmd(unsigned char c, unsigned char *param);

    bool elantech_is_signature_valid(const unsigned char *param);
    static unsigned int elantech_convert_res(unsigned int val);
    int elantech_get_resolution_v4(unsigned int *x_res, unsigned int *y_res, unsigned int *bus);

public:
    bool init(OSDictionary *properties) override;
    ApplePS2Elan *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    UInt32 deviceType() override;
    UInt32 interfaceID() override;

    IOReturn setParamProperties(OSDictionary* dict) override;
    IOReturn setProperties(OSObject *props) override;

    IOReturn message(UInt32 type, IOService* provider, void* argument) override;
};

#endif /* _ApplePS2Elan_H */
