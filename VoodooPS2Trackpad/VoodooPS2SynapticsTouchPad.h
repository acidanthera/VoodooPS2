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

#ifndef _APPLEPS2SYNAPTICSTOUCHPAD_H
#define _APPLEPS2SYNAPTICSTOUCHPAD_H

#include "../VoodooPS2Controller/ApplePS2MouseDevice.h"
#include <IOKit/IOCommandGate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "VoodooInputMultitouch/VoodooInputEvent.h"
#include "VoodooPS2TrackpadCommon.h"

struct synaptics_hw_state {
    int x;
    int y;
    int z;
    int w;
    int virtualFingerIndex;
};

/*
 Если touch = false, то палец игнорируется.
 Соответствие физических и виртуальных пальцев - динамическое.
 transducers заполняются каждый раз с номером виртуального пальца столько, сколько надо.
 Будут ли при этом отжиматься отпущенные пальцы?
 */
struct synaptics_virtual_finger_state {
    SimpleAverage<int, 5> x_avg;
    SimpleAverage<int, 5> y_avg;
    uint8_t pressure;
    uint8_t width;
    bool touch;
    bool button;
	MT2FingerType fingerType;
};

#pragma pack(push)
#pragma pack(1)

#define SYNAPTICS_IDENTIFY_QUERY        0x00
struct synaptics_identify_trackpad {
    uint8_t minor_ver;
    uint8_t synaptics_const;
    uint8_t major_ver : 4;
    uint8_t model_code : 4;  // Unused field
};
static_assert(sizeof(synaptics_identify_trackpad) == 3, "Invalid Identity packet size");

#define SYNA_MODEL_QUERY                0x01
struct synaptics_model {
    uint8_t guest_present: 1;
    uint8_t more_extended_caps: 1;
    uint16_t model_number: 14;
    uint8_t mode_byte;
};
static_assert(sizeof(synaptics_model) == 3, "Invalid Model packet size");

#define SYNA_CAPABILITIES_QUERY         0x02
struct synaptics_capabilities {
    // Byte 0
    uint8_t _reserved0: 2;
    uint8_t middle_btn: 1;
    uint8_t _reserved1: 1;
    uint8_t extended_queries: 3;
    uint8_t has_extended_queries: 1;
    // Byte 1
    uint8_t model_sub_num;
    // Byte 2
    uint8_t palm_detect: 1;
    uint8_t multi_finger: 1;
    uint8_t ballistics: 1;
    uint8_t sleep: 1;
    uint8_t four_buttons: 1; // Currently unsupported
    uint8_t extended_w_supported: 1;
    uint8_t low_power: 1;
    uint8_t passthrough: 1;
};
static_assert(sizeof(synaptics_capabilities) == 3, "Invalid Capabilities packet size");

#define SYNA_SCALE_QUERY                0x08
struct synaptics_scale {
    uint8_t xupmm;
    uint8_t reserved;
    uint8_t yupmm;
};
static_assert(sizeof(synaptics_scale) == 3, "Invalid Scale packet size");

#define SYNA_EXTENDED_ID_QUERY          0x09
struct synaptics_extended_id {
    // Byte 0
    uint8_t vert_scroll_zone: 1;
    uint8_t horiz_scroll_zone: 1;
    uint8_t extended_w_supported: 1;
    uint8_t vertical_wheel: 1;
    uint8_t transparent_passthru: 1;
    uint8_t peak_detection: 1;
    uint8_t has_leds: 1;
    uint8_t reserved0: 1;
    // Byte 1
    uint8_t reserved1: 2;
    uint8_t info_sensor: 2;
    uint8_t extended_btns: 4;
    // Byte 2
    uint8_t product_id;
};
static_assert(sizeof(synaptics_extended_id) == 3, "Invalid Extended ID packet size");

#define SYNA_CONT_CAPS_QUERY            0x0C
struct synaptics_cont_capabilities {
    // Byte 0
    uint8_t adj_button_threshold: 1;
    uint8_t reports_max: 1;
    uint8_t is_clearpad: 1;
    uint8_t advanced_gestures: 1;
    uint8_t one_btn_clickpad: 1;
    uint8_t multifinger_mode: 2;
    uint8_t covered_pad_gesture: 1;
    // Byte 1
    uint8_t two_btn_clickpad: 1;
    uint8_t deluxe_leds: 1;
    uint8_t no_abs_pos_filter: 1;
    uint8_t reports_v: 1;
    uint8_t uniform_clickpad: 1;
    uint8_t reports_min: 1;
    uint8_t intertouch: 1;
    uint8_t reserved: 1;
    // Byte 2
    uint8_t intertouch_addr;
};
static_assert(sizeof(synaptics_cont_capabilities) == 3, "Invalid continued capabilities packet size");

#define SYNA_LOGIC_MAX_QUERY            0x0D
#define SYNA_LOGIC_MIN_QUERY            0x0F
#define SYNA_LOGIC_X(x)             ((x.x_high << 5) | (x.x_low << 1))
#define SYNA_LOGIC_Y(x)             (x.y << 1)
struct synaptics_logic_min_max {
    uint8_t x_high;
    uint8_t x_low: 4;
    uint16_t y: 12;
};
static_assert(sizeof(synaptics_logic_min_max) == 3, "Invalid logic packet size");

#define SYNA_SECUREPAD_QUERY    0x10
struct synaptics_securepad_id {
    uint8_t trackstick_btns: 1;
    uint8_t is_securepad: 1;
    uint8_t unused: 6;
    uint8_t securepad_width;
    uint8_t securepad_height;
};
static_assert(sizeof(synaptics_securepad_id) == 3, "Invalid securepad packet size");

#define SYNA_MODE_ABSOLUTE  0x80
#define SYNA_MODE_HIGH_RATE 0x40
#define SYNA_MODE_SLEEP     0x08
#define SYNA_MODE_EXT_W     0x04
#define SYNA_MODE_W_MODE    0x01

// W Values for packet types
#define SYNA_W_EXTENDED     0x02
#define SYNA_W_PASSTHRU     0x03

#pragma pack(pop)

#define SYNAPTICS_MAX_EXT_BTNS 8
#define SYNAPTICS_MAX_FINGERS 5

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2SynapticsTouchPad Class Declaration
//

#define XMIN 0
#define XMAX 6143
#define YMIN 0
#define YMAX 6143
#define XMIN_NOMINAL 1472
#define XMAX_NOMINAL 5472
#define YMIN_NOMINAL 1408
#define YMAX_NOMINAL 4448

#define ABS_POS_BITS 13
#define X_MAX_POSITIVE 8176
#define Y_MAX_POSITIVE 8176


#define kPacketLength 6

class EXPORT ApplePS2SynapticsTouchPad : public IOService
{
    typedef IOService super;
	OSDeclareDefaultStructors(ApplePS2SynapticsTouchPad);

private:
    IOService *voodooInputInstance {nullptr};
    ApplePS2MouseDevice * _device {nullptr};
	bool                _interruptHandlerInstalled {false};
    bool                _powerControlHandlerInstalled {false};
	RingBuffer<UInt8, kPacketLength*32> _ringBuffer {};
	UInt32              _packetByteCount {0};
    UInt8               _lastdata {0};
    
    synaptics_identify_trackpad _identity {0};
    synaptics_capabilities      _capabilities {0};
    synaptics_extended_id       _extended_id {0};
    synaptics_securepad_id      _securepad {0};
    synaptics_scale             _scale {0};
    synaptics_cont_capabilities _cont_caps {0};
    
	IOCommandGate*      _cmdGate {nullptr};
    IOACPIPlatformDevice*_provider {nullptr};
    
	VoodooInputEvent inputEvent {};
    TrackpointReport trackpointReport {};
    
    // buttons and scroll wheel
    bool _clickpad_pressed { false };

    int margin_size_x {0}, margin_size_y {0};
    uint32_t logical_max_x {0};
    uint32_t logical_max_y {0};
    uint32_t logical_min_x {0};
    uint32_t logical_min_y {0};

    uint32_t physical_max_x {0};
    uint32_t physical_max_y {0};

    synaptics_hw_state fingerStates[SYNAPTICS_MAX_FINGERS] {};
    synaptics_virtual_finger_state virtualFingerStates[SYNAPTICS_MAX_FINGERS] {};
    bool freeFingerTypes[kMT2FingerTypeCount];

    bool disableDeepSleep {false};

    static_assert(SYNAPTICS_MAX_FINGERS <= kMT2FingerTypeLittleFinger, "Too many fingers for one hand");

    void assignVirtualFinger(int physicalFinger);
    void assignFingerType(synaptics_virtual_finger_state &vf);
    int lastFingerCount;
    int lastSentFingerCount;
    bool hadLiftFinger;
    int upperFingerIndex() const;
    const synaptics_hw_state& upperFinger() const;
    void swapFingers(int dst, int src);
    
    void synaptics_parse_normal_packet(const UInt8 buf[], const int w);
    void synaptics_parse_agm_packet(const UInt8 buf[]);
    void synaptics_parse_passthru(const UInt8 buf[], const UInt32 buttons);
    int synaptics_parse_ext_btns(const UInt8 buf[], const int w);
    void synaptics_parse_hw_state(const UInt8 buf[]);
    
    /// Translates physical fingers into virtual fingers so that host software doesn't see 'jumps' and has coordinates for all fingers.
    /// @return True if is ready to send finger state to host interface
    bool renumberFingers();
    void sendTouchData();
    void freeAndMarkVirtualFingers();
    int dist(int physicalFinger, int virtualFinger);

	ForceTouchMode _forceTouchMode {FORCE_TOUCH_BUTTON};
	int _forceTouchPressureThreshold {100};

    int _forceTouchCustomDownThreshold {90};
    int _forceTouchCustomUpThreshold {20};
    int _forceTouchCustomPower {8};
    
    int clampedFingerCount {0};
    int agmFingerCount {0};
	bool wasSkipped {false};
	int z_finger {45};
    int zlimit {0};
    int noled {0};
    uint64_t maxaftertyping {500000000};
    uint64_t maxafterspecialtyping {0};
    int specialKey {0x80};
    int wakedelay {1000};
    int hwresetonstart {0};
    int diszl {0}, diszr {0}, diszt {0}, diszb {0};
    int minXOverride {-1}, minYOverride {-1}, maxXOverride {-1}, maxYOverride {-1};

    int _lastExtendedButtons {0};
    int _lastPassthruButtons {0};
    
    // Trackpoint information
    int _scrollMultiplierX {1};
    int _scrollMultiplierY {1};
    int _scrollDivisorX {1};
    int _scrollDivisorY {1};
    int _mouseMultiplierX {1};
    int _mouseMultiplierY {1};
    int _mouseDivisorX {1};
    int _mouseDivisorY {1};
    int _deadzone {1};
    
    // state related to secondary packets/extendedwmode
    bool tracksecondary {false};
    
    // normal state
    uint64_t keytime {0};
    UInt16 keycode {0};
    bool ignoreall {false};
#ifdef SIMULATE_PASSTHRU
	UInt32 trackbuttons {0};
#endif
    bool usb_mouse_stops_trackpad {true};
    
    int _processusbmouse {true};
    int _processbluetoothmouse {true};

    const OSSymbol* _smbusCompanion {nullptr};
    
    OSSet* attachedHIDPointerDevices {nullptr};
    
    IONotifier* usb_hid_publish_notify {nullptr};     // Notification when an USB mouse HID device is connected
    IONotifier* usb_hid_terminate_notify {nullptr}; // Notification when an USB mouse HID device is disconnected
    
    IONotifier* bluetooth_hid_publish_notify {nullptr}; // Notification when a bluetooth HID device is connected
    IONotifier* bluetooth_hid_terminate_notify {nullptr}; // Notification when a bluetooth HID device is disconnected
    
	int _modifierdown {0}; // state of left+right control keys
    
    inline bool isInDisableZone(int x, int y)
        { return x > diszl && x < diszr && y > diszb && y < diszt; }
	
    // Sony: coordinates captured from single touch event
    // Don't know what is the exact value of x and y on edge of touchpad
    // the best would be { return x > xmax/2 && y < ymax/4; }

    virtual void   setTouchPadEnable( bool enable );
    virtual bool   getTouchPadData( UInt8 dataSelector, UInt8 buf3[] );
    virtual bool   getTouchPadStatus(  UInt8 buf3[] );
	virtual PS2InterruptResult interruptOccurred(UInt8 data);
    virtual void packetReady();
    virtual void   setDevicePowerState(UInt32 whatToDo);
    
    void updateTouchpadLED();
    bool setTouchpadLED(UInt8 touchLED);
    void initTouchPad();
    bool enterAdvancedGestureMode();
    bool setModeByte(bool sleep);

    inline bool isFingerTouch(int z) { return z>z_finger && z<zlimit; }
    
    void queryCapabilities(void);
    void doHardwareReset(void);

    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    bool handleIsOpen(const IOService *forClient) const override;
    
    void setPropertiesGated(OSDictionary* dict);
    void injectVersionDependentProperties(OSDictionary* dict);
    
    void setTrackpointProperties();

    void registerHIDPointerNotifications();
    void unregisterHIDPointerNotifications();
    
    void notificationHIDAttachedHandlerGated(IOService * newService, IONotifier * notifier);
    bool notificationHIDAttachedHandler(void * refCon, IOService * newService, IONotifier * notifier);

public:
    bool init( OSDictionary * properties ) override;
    IOService * probe( IOService * provider, SInt32 * score ) override;
    bool start( IOService * provider ) override;
    void stop( IOService * provider ) override;

	IOReturn setProperties(OSObject *props) override;
    
    IOReturn message(UInt32 type, IOService* provider, void* argument) override;
};

#endif /* _APPLEPS2SYNAPTICSTOUCHPAD_H */
