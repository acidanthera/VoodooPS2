/*
 * Copyright (c) 2002 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.2 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef _APPLEPS2ELAN_H
#define _APPLEPS2ELAN_H

#include "../VoodooPS2Controller/ApplePS2MouseDevice.h"
#include "LegacyIOHIPointing.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#include <IOKit/IOCommandGate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#pragma clang diagnostic pop

#include "VoodooInputMultitouch/VoodooInputEvent.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// SimpleAverage Class Declaration
//

template <class T, int N>
class SimpleAverage
{
private:
    T m_buffer[N];
    int m_count;
    int m_sum;
    int m_index;
    
public:
    inline SimpleAverage() { reset(); }
    T filter(T data)
    {
        // add new entry to sum
        m_sum += data;
        // if full buffer, then we are overwriting, so subtract old from sum
        if (m_count == N)
            m_sum -= m_buffer[m_index];
        // new entry into buffer
        m_buffer[m_index] = data;
        // move index to next position with wrap around
        if (++m_index >= N)
            m_index = 0;
        // keep count moving until buffer is full
        if (m_count < N)
            ++m_count;
        // return average of current items
        return m_sum / m_count;
    }
    inline void reset()
    {
        m_count = 0;
        m_sum = 0;
        m_index = 0;
    }
    inline int count() const { return m_count; }
    inline int sum() const { return m_sum; }
    T oldest() const
    {
        // undefined if nothing in here, return zero
        if (m_count == 0)
            return 0;
        // if it is not full, oldest is at index 0
        // if full, it is right where the next one goes
        if (m_count < N)
            return m_buffer[0];
        else
            return m_buffer[m_index];
    }
    T newest() const
    {
        // undefined if nothing in here, return zero
        if (m_count == 0)
            return 0;
        // newest is index - 1, with wrap
        int index = m_index;
        if (--index < 0)
            index = m_count-1;
        return m_buffer[index];
    }
    T average() const
    {
        if (m_count == 0)
            return 0;
        return m_sum / m_count;
    }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// DecayingAverage Class Declaration
//

template <class T, class TT, int N1, int N2, int D>
class DecayingAverage
{
private:
    T m_last;
    bool m_lastvalid;
    
public:
    inline DecayingAverage() { reset(); }
    T filter(T data, int fingers)
    {
        TT result = data;
        TT last = m_last;
        if (m_lastvalid)
            result = (result * N1) / D + (last * N2) / D;
        m_lastvalid = true;
        m_last = (T)result;
        return m_last;
    }
    inline void reset()
    {
        m_lastvalid = false;
    }
};

template <class T, class TT, int N1, int N2, int D>
class UndecayAverage
{
private:
    T m_last;
    bool m_lastvalid;
    
public:
    inline UndecayAverage() { reset(); }
    T filter(T data)
    {
        TT result = data;
        TT last = m_last;
        if (m_lastvalid)
            result = (result * D) / N1 - (last * N2) / N1;
        m_lastvalid = true;
        m_last = (T)data;
        return (T)result;
    }
    inline void reset()
    {
        m_lastvalid = false;
    }
};

struct synaptics_hw_state {
    int x;
    int y;
    int z;
    int w;
    int virtualFingerIndex;
};

struct virtual_finger_state {
    SimpleAverage<int, 5> x_avg;
    SimpleAverage<int, 5> y_avg;
    uint8_t pressure;
    uint8_t width;
    bool touch;
    bool button;
	MT2FingerType fingerType;
};

struct virtual_finger_state_2 {
    TouchCoordinates prev;
    TouchCoordinates now;
    bool touch;
    bool button;
    MT2FingerType fingerType;
};

typedef enum {
    FORCE_TOUCH_DISABLED = 0,
    FORCE_TOUCH_BUTTON = 1,
    FORCE_TOUCH_THRESHOLD = 2,
    FORCE_TOUCH_VALUE = 3,
    FORCE_TOUCH_CUSTOM = 4
} ForceTouchMode;

#define SYNAPTICS_MAX_FINGERS 5

#define kPacketLength 6
#define kPacketLengthMax 6

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
// FROM LINUX ELANTECH.C


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

// Preasure min-max
#define ETP_PMIN_V2            0
#define ETP_PMAX_V2            255

// Width min-max
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
    struct finger_pos mt[ETP_MAX_FINGERS];
    unsigned char parity[256];
};



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2Elan Class Declaration
//

class EXPORT ApplePS2Elan : public IOHIPointing
{
    typedef IOHIPointing super;
	OSDeclareDefaultStructors(ApplePS2Elan);

private:
    IOService* voodooInputInstance {nullptr};
    ApplePS2MouseDevice* _device {nullptr};
	bool                _interruptHandlerInstalled {false};
    bool                _powerControlHandlerInstalled {false};
	RingBuffer<UInt8, kPacketLength * 32> _ringBuffer {};
	UInt32              _packetByteCount {0};
    UInt8               _lastdata {0};
    
	IOCommandGate*      _cmdGate {nullptr};
    IOACPIPlatformDevice* _provider {nullptr};
    
	VoodooInputEvent inputEvent {};
    
    int heldFingers = 0;
    int headPacketsCount = 0;
    virtual_finger_state_2 virtualFinger[ETP_MAX_FINGERS] {};
    
    int _scrollresolution {2300};
    int wakedelay {1000};
    
	static_assert(SYNAPTICS_MAX_FINGERS <= kMT2FingerTypeLittleFinger, "Too many fingers for one hand");

    bool ignoreall {false};
    bool usb_mouse_stops_trackpad {true};
    
    int _processusbmouse {true};
    int _processbluetoothmouse {true};

    OSSet* attachedHIDPointerDevices {nullptr};
    
    IONotifier* usb_hid_publish_notify {nullptr};     // Notification when an USB mouse HID device is connected
    IONotifier* usb_hid_terminate_notify {nullptr}; // Notification when an USB mouse HID device is disconnected
    
    IONotifier* bluetooth_hid_publish_notify {nullptr}; // Notification when a bluetooth HID device is connected
    IONotifier* bluetooth_hid_terminate_notify {nullptr}; // Notification when a bluetooth HID device is disconnected
    
	virtual PS2InterruptResult interruptOccurred(UInt8 data);
    virtual void packetReady();
    virtual void   setDevicePowerState(UInt32 whatToDo);
    
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    
    void setParamPropertiesGated(OSDictionary* dict);
    void injectVersionDependentProperties(OSDictionary* dict);

    void registerHIDPointerNotifications();
    void unregisterHIDPointerNotifications();
    
    void notificationHIDAttachedHandlerGated(IOService * newService, IONotifier * notifier);
    bool notificationHIDAttachedHandler(void * refCon, IOService * newService, IONotifier * notifier);
    
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
    void Elantech_Touchpad_enable(bool enable );
    
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
    
    template<int I>
    int send_cmd(unsigned char c, unsigned char *param);
    
    bool changed = true;
    
protected:
    inline void dispatchRelativePointerEventX(int dx, int dy, UInt32 buttonState, uint64_t now)
        { dispatchRelativePointerEvent(dx, dy, buttonState, *(AbsoluteTime*)&now); }
    inline void dispatchScrollWheelEventX(short deltaAxis1, short deltaAxis2, short deltaAxis3, uint64_t now)
        { dispatchScrollWheelEvent(deltaAxis1, deltaAxis2, deltaAxis3, *(AbsoluteTime*)&now); }
    
public:
    bool init( OSDictionary * properties ) override;
    ApplePS2Elan * probe( IOService * provider,
                                               SInt32 *    score ) override;
    bool start( IOService * provider ) override;
    void stop( IOService * provider ) override;
    
    UInt32 deviceType() override;
    UInt32 interfaceID() override;

	IOReturn setParamProperties(OSDictionary * dict) override;
	IOReturn setProperties(OSObject *props) override;
    
    IOReturn message(UInt32 type, IOService* provider, void* argument) override;
};

#endif /* _ApplePS2Elan_H */
