//
//  VoodooPS2Elan.hpp
//  VoodooPS2Trackpad
//
//  Created by Bartek on 2020-02-20.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#ifndef _VOODOOPS2ELAN_HPP
#define _VOODOOPS2ELAN_HPP

#include "LegacyIOService.h"
#include <IOKit/IOLib.h>
#include "../VoodooPS2Controller/ApplePS2MouseDevice.h"
#include "LegacyIOHIPointing.h"
#include "VoodooInputMultitouch/VoodooInputEvent.h"

/*
 * Command values for Synaptics style queries
 */
#define ETP_FW_ID_QUERY            0x00
#define ETP_FW_VERSION_QUERY        0x01
#define ETP_CAPABILITIES_QUERY        0x02
#define ETP_SAMPLE_QUERY        0x03
#define ETP_RESOLUTION_QUERY        0x04

/*
 * Command values for register reading or writing
 */
#define ETP_REGISTER_READ        0x10
#define ETP_REGISTER_WRITE        0x11
#define ETP_REGISTER_READWRITE        0x00

/*
 * Hardware version 2 custom PS/2 command value
 */
#define ETP_PS2_CUSTOM_COMMAND        0xf8

/*
 * Times to retry a ps2_command and millisecond delay between tries
 */
#define ETP_PS2_COMMAND_TRIES        3
#define ETP_PS2_COMMAND_DELAY        500

/*
 * Times to try to read back a register and millisecond delay between tries
 */
#define ETP_READ_BACK_TRIES        5
#define ETP_READ_BACK_DELAY        2000

/*
 * Register bitmasks for hardware version 1
 */
#define ETP_R10_ABSOLUTE_MODE        0x04
#define ETP_R11_4_BYTE_MODE        0x02

/*
 * Capability bitmasks
 */
#define ETP_CAP_HAS_ROCKER        0x04

/*
 * One hard to find application note states that X axis range is 0 to 576
 * and Y axis range is 0 to 384 for harware version 1.
 * Edge fuzz might be necessary because of bezel around the touchpad
 */
#define ETP_EDGE_FUZZ_V1        32

#define ETP_XMIN_V1            (  0 + ETP_EDGE_FUZZ_V1)
#define ETP_XMAX_V1            (576 - ETP_EDGE_FUZZ_V1)
#define ETP_YMIN_V1            (  0 + ETP_EDGE_FUZZ_V1)
#define ETP_YMAX_V1            (384 - ETP_EDGE_FUZZ_V1)

/*
 * The resolution for older v2 hardware doubled.
 * (newer v2's firmware provides command so we can query)
 */
#define ETP_XMIN_V2            0
#define ETP_XMAX_V2            1152
#define ETP_YMIN_V2            0
#define ETP_YMAX_V2            768

#define ETP_PMIN_V2            0
#define ETP_PMAX_V2            255
#define ETP_WMIN_V2            0
#define ETP_WMAX_V2            15

/*
 * v3 hardware has 2 kinds of packet types,
 * v4 hardware has 3.
 */
#define PACKET_UNKNOWN            0x01
#define PACKET_DEBOUNCE            0x02
#define PACKET_V3_HEAD            0x03
#define PACKET_V3_TAIL            0x04
#define PACKET_V4_HEAD            0x05
#define PACKET_V4_MOTION        0x06
#define PACKET_V4_STATUS        0x07
#define PACKET_TRACKPOINT        0x08

/*
 * track up to 5 fingers for v4 hardware
 */
#define ETP_MAX_FINGERS            5

/*
 * weight value for v4 hardware
 */
#define ETP_WEIGHT_VALUE        5

/*
 * Bus information on 3rd byte of query ETP_RESOLUTION_QUERY(0x04)
 */
#define ETP_BUS_PS2_ONLY        0
#define ETP_BUS_SMB_ALERT_ONLY        1
#define ETP_BUS_SMB_HST_NTFY_ONLY    2
#define ETP_BUS_PS2_SMB_ALERT        3
#define ETP_BUS_PS2_SMB_HST_NTFY    4

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
    bool has_trackpoint;
    bool has_middle_button;
};

struct elantech_data {
    struct input_dev *tp_dev;    /* Relative device for trackpoint */
    char tp_phys[32];
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
    unsigned int y_max;
    unsigned int width;
    struct finger_pos mt[ETP_MAX_FINGERS];
    unsigned char parity[256];
    struct elantech_device_info info;
};

#define kPacketLength 6
#define kPacketLengthMax 6

class EXPORT ApplePS2Elan : public IOHIPointing
{
    typedef IOHIPointing super;
    OSDeclareDefaultStructors(ApplePS2Elan);
    
    bool init(OSDictionary * properties) override;
    ApplePS2Elan* probe(IOService* provider, SInt32* score) override;
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    
private:
        IOService* voodooInputInstance;
        ApplePS2MouseDevice* _device;
    
    elantech_data etd {};
    elantech_device_info info {};
    int elantechDetect();
    void resetMouse();
    int elantech_query_info();
    int elantech_set_properties();
    int elantech_setup_ps2();
    int elantech_set_absolute_mode();
    int elantech_write_reg(unsigned char reg, unsigned char val);
    int elantech_read_reg(unsigned char reg, unsigned char *val);
    int elantech_set_input_params();
    int elantech_packet_check_v4();
    void elantechReportAbsoluteV4(int packetType);
    void processPacketStatusV4();
    void processPacketHeadV4();
    void processPacketMotionV4();
    void elantechInputSyncV4();
    void setMouseEnable(bool enable);
    void setMouseSampleRate(UInt8 sampleRate);
    void setMouseResolution(UInt8 resolution);
    void Elantech_Touchpad_enable(bool enable );
    void injectVersionDependentProperties(OSDictionary *config);

    template<int I>
    int ps2_command(UInt8* params, unsigned int command);
    template<int I>
    int elantech_ps2_command(unsigned char *param, int command);
    int ps2_sliced_command(UInt8 command);
    template<int I>
    int synaptics_send_cmd(unsigned char c, unsigned char *param);
    template<int I>
    int elantech_send_cmd(unsigned char c, unsigned char *param);
    bool elantech_is_signature_valid(const unsigned char *param);
    int elantech_get_resolution_v4(unsigned int *x_res, unsigned int *y_res, unsigned int *bus);
    
    VoodooInputEvent inputEvent {};
    
    RingBuffer<UInt8, kPacketLengthMax * 32> ringBuffer {};
    int packetByteCount {};
    
    PS2InterruptResult interruptOccurred(UInt8 data) ;

    unsigned long extracted();
    
    void packetReady();
    
    template<int I>
    int send_cmd(unsigned char c, unsigned char *param);
};

#endif /* VoodooPS2Elan_hpp */