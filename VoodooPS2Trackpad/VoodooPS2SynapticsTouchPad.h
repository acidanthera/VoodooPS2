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
#include "LegacyIOHIPointing.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#include <IOKit/IOCommandGate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#pragma clang diagnostic pop

#include "../VoodooInput/VoodooInput/VoodooInputMultitouch/VoodooInputEvent.h"

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

/*
 Если touch = false, то палец игнорируется.
 Соответствие физических и виртуальных пальцев - динамическое.
 transducers заполняются каждый раз с номером виртуального пальца столько, сколько надо.
 Будут ли при этом отжиматься отпущенные пальцы?
 */
struct virtual_finger_state {
    SimpleAverage<int, 5> x_avg;
    SimpleAverage<int, 5> y_avg;
    uint8_t pressure;
    uint8_t width;
    bool touch;
    bool button;
};

typedef enum {
    FORCE_TOUCH_DISABLED = 0,
    FORCE_TOUCH_BUTTON = 1,
    FORCE_TOUCH_THRESHOLD = 2,
    FORCE_TOUCH_VALUE = 3
} ForceTouchMode;

#define SYNAPTICS_MAX_FINGERS 4

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
    UInt16              _touchPadVersion {0};
    UInt8               _touchPadType {0}; // from identify: either 0x46 or 0x47
    UInt8               _touchPadModeByte {0x80}; //default: absolute, low-rate, no w-mode
    
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
    virtual_finger_state virtualFingerStates[SYNAPTICS_MAX_FINGERS] {};

    void assignVirtualFinger(int physicalFinger);
    int lastFingerCount;
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
    
    int clampedFingerCount {0};
    int agmFingerCount {0};
	bool wasSkipped {false};
	int z_finger {45};
	bool outzone_wt {false}, palm {false}, palm_wt {false};
    int zlimit {0};
	int noled {0};
    uint64_t maxaftertyping {500000000};
    int wakedelay {1000};
    int skippassthru {0};
    int forcepassthru {0};
    int hwresetonstart {0};
    int diszl {0}, diszr {0}, diszt {0}, diszb {0};
    int _resolution {2300}, _scrollresolution {2300};
    int _buttonCount {2};
    uint64_t clickpadclicktime {300000000}; // 300ms default
    int clickpadtrackboth {true};
    int ignoredeltasstart {0};
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

    
    int rczl {0}, rczr {0}, rczb {0}, rczt {0}; // rightclick zone for 1-button ClickPads
    
    // state related to secondary packets/extendedwmode
    bool tracksecondary {false};
    bool _extendedwmode {false}, _extendedwmodeSupported {false};
    int _dynamicEW {0};

    // normal state
	UInt32 passbuttons {0};
    UInt32 lastbuttons {0};
    uint64_t keytime {0};
    bool ignoreall {false};
#ifdef SIMULATE_PASSTHRU
	UInt32 trackbuttons {0};
#endif
    bool passthru {false};
    bool ledpresent {false};
    bool _reportsv {false};
    int clickpadtype {0};   //0=not, 1=1button, 2=2button, 3=reserved
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
    int scrollzoommask {0};
    
    // for scaling x/y values
    int xupmm {50}, yupmm {50}; // 50 is just arbitrary, but same
    
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

    inline bool isInRightClickZone(int x, int y)
        { return x > rczl && x < rczr && y > rczb && y < rczt; }
    inline bool isInLeftClickZone(int x, int y)
        { return x <= rczl && x <= rczr && y > rczb && y < rczt; }
        
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
