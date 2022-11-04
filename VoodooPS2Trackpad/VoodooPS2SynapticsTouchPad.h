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
#include <IOKit/hidsystem/IOHIPointing.h>
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
#define SYNA_CAPS_PASSTHROUGH(x)        TEST_BIT(x, 7)
#define SYNA_CAPS_EXTENDED_W(x)         TEST_BIT(x, 5)
#define SYNA_CAPS_MULTI_FINGER(x)       TEST_BIT(x, 1)
#define SYNA_CAPS_PALM_DETECT(x)        TEST_BIT(x, 0)
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
    uint8_t caps;
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
#define SYNA_EXTENDED_ID_LED(x)         TEST_BIT(x, 6)
struct synaptics_extended_id {
    uint8_t caps;
    uint8_t reserved: 2;
    uint8_t info_sensor: 2;
    uint8_t extended_btns: 4;
    uint8_t product_id;
};
static_assert(sizeof(synaptics_extended_id) == 3, "Invalid Extended ID packet size");

#define SYNA_CONT_CAPS_QUERY            0x0C
#define SYNA_CONT_CAPS_REPORTS_MAX(x)   TEST_BIT(x, 1)
#define SYNA_CONT_CAPS_CLICKPAD(x)      (((x >> 4) & 0x1) | ((x >> 7) & 0x2))
#define SYNA_CONT_CAPS_REPORTS_V(x)     TEST_BIT(x, 11)
#define SYNA_CONT_CAPS_REPORTS_MIN(x)   TEST_BIT(x, 13)
#define SYNA_CONT_CAPS_INTERTOUCH(x)    TEST_BIT(x, 14)
struct synaptics_cont_capabilities {
    uint16_t caps;
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

#pragma pack(pop)

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

class EXPORT ApplePS2SynapticsTouchPad : public IOHIPointing
{
    typedef IOHIPointing super;
	OSDeclareDefaultStructors(ApplePS2SynapticsTouchPad);

private:
    IOService *voodooInputInstance {nullptr};
    ApplePS2MouseDevice * _device {nullptr};
	bool                _interruptHandlerInstalled {false};
    bool                _powerControlHandlerInstalled {false};
	RingBuffer<UInt8, kPacketLength*32> _ringBuffer {};
	UInt32              _packetByteCount {0};
    UInt8               _lastdata {0};
    UInt8               _touchPadModeByte {0x80}; //default: absolute, low-rate, no w-mode
    
    synaptics_identify_trackpad _identity {0};
    synaptics_capabilities      _capabilities {0};
    synaptics_extended_id       _extended_id {0};
    synaptics_securepad_id      _securepad {0};
    synaptics_scale             _scale {0};
    synaptics_cont_capabilities _cont_caps {0};
    
	IOCommandGate*      _cmdGate {nullptr};
    IOACPIPlatformDevice*_provider {nullptr};
    
	VoodooInputEvent inputEvent {};
    
    // buttons and scroll wheel
    bool left {false};
    bool right {false};

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
    uint64_t maxaftertyping {500000000};
    uint64_t maxafterspecialtyping {0};
    int specialKey {0x80};
    int wakedelay {1000};
    int hwresetonstart {0};
    int diszl {0}, diszr {0}, diszt {0}, diszb {0};
    int _resolution {2300}, _scrollresolution {2300};
    int _buttonCount {2};
    int minXOverride {-1}, minYOverride {-1}, maxXOverride {-1}, maxYOverride {-1};

    //vars for clickpad and middleButton support (thanks jakibaki)
    int isthinkpad {0};
    int thinkpadButtonState {0};
    int thinkpadNubScrollXMultiplier {1};
    int thinkpadNubScrollYMultiplier {1};
    bool thinkpadMiddleScrolled {false};
    bool thinkpadMiddleButtonPressed {false};
    int mousemultiplierx {1};
    int mousemultipliery {1};

    
    // state related to secondary packets/extendedwmode
    bool tracksecondary {false};
    bool _extendedwmode {false}, _extendedwmodeSupported {false};
    
    // normal state
	UInt32 passbuttons {0};
    UInt32 lastbuttons {0};
    uint64_t keytime {0};
    UInt16 keycode {0};
    bool ignoreall {false};
    bool otherBusInUse {false}; // Trackpad being used over SMBus/I2C
#ifdef SIMULATE_PASSTHRU
	UInt32 trackbuttons {0};
#endif
    UInt32 _clickbuttons {0};  //clickbuttons to merge into buttons
    bool usb_mouse_stops_trackpad {true};
    
    int _processusbmouse {true};
    int _processbluetoothmouse {true};

    OSSet* attachedHIDPointerDevices {nullptr};
    
    IONotifier* usb_hid_publish_notify {nullptr};     // Notification when an USB mouse HID device is connected
    IONotifier* usb_hid_terminate_notify {nullptr}; // Notification when an USB mouse HID device is disconnected
    
    IONotifier* bluetooth_hid_publish_notify {nullptr}; // Notification when a bluetooth HID device is connected
    IONotifier* bluetooth_hid_terminate_notify {nullptr}; // Notification when a bluetooth HID device is disconnected
    
	int _modifierdown {0}; // state of left+right control keys
    
    // for middle button simulation
    enum mbuttonstate
    {
        STATE_NOBUTTONS,
        STATE_MIDDLE,
        STATE_WAIT4TWO,
        STATE_WAIT4NONE,
        STATE_NOOP,
	} _mbuttonstate {STATE_NOBUTTONS};
    
    UInt32 _pendingbuttons {0};
    uint64_t _buttontime {0};
	IOTimerEventSource* _buttonTimer {nullptr};
    uint64_t _maxmiddleclicktime {100000000};
    int _fakemiddlebutton {true};

    void setClickButtons(UInt32 clickButtons);
    
    inline bool isInDisableZone(int x, int y)
        { return x > diszl && x < diszr && y > diszb && y < diszt; }
	
    // Sony: coordinates captured from single touch event
    // Don't know what is the exact value of x and y on edge of touchpad
    // the best would be { return x > xmax/2 && y < ymax/4; }

    virtual void   setTouchPadEnable( bool enable );
    virtual bool   getTouchPadData( UInt8 dataSelector, UInt8 buf3[] );
    virtual bool   getTouchPadStatus(  UInt8 buf3[] );
    virtual bool   setTouchPadModeByte(UInt8 modeByteValue);
	virtual PS2InterruptResult interruptOccurred(UInt8 data);
    virtual void packetReady();
    virtual void   setDevicePowerState(UInt32 whatToDo);
    
    void updateTouchpadLED();
    bool setTouchpadLED(UInt8 touchLED);
    bool setTouchpadModeByte(); // set based on state
    void initTouchPad();
    bool setModeByte(UInt8 modeByteValue);
    bool setModeByte(); // set based on state

    inline bool isFingerTouch(int z) { return z>z_finger && z<zlimit; }
    
    void queryCapabilities(void);
    void doHardwareReset(void);
    
    void onButtonTimer(void);

    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;

    enum MBComingFrom { fromPassthru, fromTimer, fromTrackpad, fromCancel };
    UInt32 middleButton(UInt32 butttons, uint64_t now, MBComingFrom from);
    
    void setParamPropertiesGated(OSDictionary* dict);
    void injectVersionDependentProperties(OSDictionary* dict);

    void registerHIDPointerNotifications();
    void unregisterHIDPointerNotifications();
    
    void notificationHIDAttachedHandlerGated(IOService * newService, IONotifier * notifier);
    bool notificationHIDAttachedHandler(void * refCon, IOService * newService, IONotifier * notifier);
protected:
	IOItemCount buttonCount() override;
	IOFixed     resolution() override;
    inline void dispatchRelativePointerEventX(int dx, int dy, UInt32 buttonState, uint64_t now)
        { dispatchRelativePointerEvent(dx, dy, buttonState, *(AbsoluteTime*)&now); }
    inline void dispatchScrollWheelEventX(short deltaAxis1, short deltaAxis2, short deltaAxis3, uint64_t now)
        { dispatchScrollWheelEvent(deltaAxis1, deltaAxis2, deltaAxis3, *(AbsoluteTime*)&now); }
    inline void setTimerTimeout(IOTimerEventSource* timer, uint64_t time)
        { timer->setTimeout(*(AbsoluteTime*)&time); }
    inline void cancelTimer(IOTimerEventSource* timer)
        { timer->cancelTimeout(); }
    
public:
    bool init( OSDictionary * properties ) override;
    ApplePS2SynapticsTouchPad * probe( IOService * provider,
                                               SInt32 *    score ) override;
    bool start( IOService * provider ) override;
    void stop( IOService * provider ) override;
    
    UInt32 deviceType() override;
    UInt32 interfaceID() override;

	IOReturn setParamProperties(OSDictionary * dict) override;
	IOReturn setProperties(OSObject *props) override;
    
    IOReturn message(UInt32 type, IOService* provider, void* argument) override;
};

#endif /* _APPLEPS2SYNAPTICSTOUCHPAD_H */
