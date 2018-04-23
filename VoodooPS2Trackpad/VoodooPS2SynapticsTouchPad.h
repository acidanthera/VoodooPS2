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

#include "ApplePS2MouseDevice.h"
#include <IOKit/hidsystem/IOHIPointing.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

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
    inline int count() { return m_count; }
    inline int sum() { return m_sum; }
    T oldest()
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
    T newest()
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
    T average()
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2SynapticsTouchPad Class Declaration
//

#define kPacketLength 6

class EXPORT ApplePS2SynapticsTouchPad : public IOHIPointing
{
    typedef IOHIPointing super;
	OSDeclareDefaultStructors(ApplePS2SynapticsTouchPad);
    
private:
    ApplePS2MouseDevice * _device;
    bool                _interruptHandlerInstalled;
    bool                _powerControlHandlerInstalled;
    bool                _messageHandlerInstalled;
    RingBuffer<UInt8, kPacketLength*32> _ringBuffer;
    UInt32              _packetByteCount;
    UInt8               _lastdata;
    UInt16              _touchPadVersion;
    UInt8               _touchPadType; // from identify: either 0x46 or 0x47
    UInt8               _touchPadModeByte;
    
    IOCommandGate*      _cmdGate;
    IOACPIPlatformDevice*_provider;
    
	int z_finger;
	int divisorx, divisory;
	int ledge;
	int redge;
	int tedge;
	int bedge;
	int vscrolldivisor, hscrolldivisor, cscrolldivisor;
	int ctrigger;
	int centerx;
	int centery;
	uint64_t maxtaptime;
	uint64_t maxdragtime;
    uint64_t maxdbltaptime;
	int hsticky,vsticky, wsticky, tapstable;
	int wlimit, wvdivisor, whdivisor;
	bool clicking;
	bool dragging;
	bool draglock;
    int draglocktemp;
	bool hscroll, scroll;
	bool rtap;
    bool outzone_wt, palm, palm_wt;
    int zlimit;
    int noled;
    uint64_t maxaftertyping;
    int mousemultiplierx, mousemultipliery;
    int mousescrollmultiplierx, mousescrollmultipliery;
    int mousemiddlescroll;
    int wakedelay;
    int smoothinput;
    int unsmoothinput;
    int skippassthru;
    int forcepassthru;
    int hwresetonstart;
    int tapthreshx, tapthreshy;
    int dblthreshx, dblthreshy;
    int zonel, zoner, zonet, zoneb;
    int diszl, diszr, diszt, diszb;
    int diszctrl; // 0=automatic (ledpresent), 1=enable always, -1=disable always
    int _resolution, _scrollresolution;
    int swipedx, swipedy;
    int _buttonCount;
    int swapdoubletriple;
    int draglocktempmask;
    uint64_t clickpadclicktime;
    int clickpadtrackboth;
    int ignoredeltasstart;
    int bogusdxthresh, bogusdythresh;
    int scrolldxthresh, scrolldythresh;
    int immediateclick;

    //vars for clickpad and middleButton support (thanks jakibaki)
    int isthinkpad;
    int thinkpadButtonState;
    int thinkpadNubScrollXMultiplier;
    int thinkpadNubScrollYMultiplier;
    bool thinkpadMiddleScrolled;
    bool thinkpadMiddleButtonPressed;
    
    // more properties added by usr-sse2
    int rightclick_corner;

    // three finger state
    uint8_t inSwipeLeft, inSwipeRight;
    uint8_t inSwipeUp, inSwipeDown;
    int xmoved, ymoved;
    
    int rczl, rczr, rczb, rczt; // rightclick zone for 1-button ClickPads
    
    // state related to secondary packets/extendedwmode
    int lastx2, lasty2;
    bool tracksecondary;
    int xrest2, yrest2;
    bool clickedprimary;
    bool _extendedwmode, _extendedwmodeSupported;
    int _dynamicEW;

    // normal state
	int lastx, lasty, lastf;
    UInt32 lastbuttons;
    int ignoredeltas;
	int xrest, yrest, scrollrest;
    int touchx, touchy;
	uint64_t touchtime;
	uint64_t untouchtime;
	bool wasdouble,wastriple;
    uint64_t keytime;
    bool ignoreall;
    UInt32 passbuttons;
#ifdef SIMULATE_PASSTHRU
    UInt32 trackbuttons;
#endif
    bool passthru;
    bool ledpresent;
    bool _reportsv;
    int clickpadtype;   //0=not, 1=1button, 2=2button, 3=reserved
    UInt32 _clickbuttons;  //clickbuttons to merge into buttons
    int mousecount;
    bool usb_mouse_stops_trackpad;
    
    int _modifierdown; // state of left+right control keys
    int scrollzoommask;
    
    // for scaling x/y values
    int xupmm, yupmm;
    
    // for middle button simulation
    enum mbuttonstate
    {
        STATE_NOBUTTONS,
        STATE_MIDDLE,
        STATE_WAIT4TWO,
        STATE_WAIT4NONE,
        STATE_NOOP,
    } _mbuttonstate;
    
    UInt32 _pendingbuttons;
    uint64_t _buttontime;
    IOTimerEventSource* _buttonTimer;
    uint64_t _maxmiddleclicktime;
    int _fakemiddlebutton;

    // momentum scroll state
    bool momentumscroll;
    SimpleAverage<int, 32> dy_history;
    SimpleAverage<uint64_t, 32> time_history;
    IOTimerEventSource* scrollTimer;
    uint64_t momentumscrolltimer;
    int momentumscrollthreshy;
    uint64_t momentumscrollinterval;
    int momentumscrollsum;
    int64_t momentumscrollcurrent;
    int64_t momentumscrollrest1;
    int momentumscrollmultiplier;
    int momentumscrolldivisor;
    int momentumscrollrest2;
    int momentumscrollsamplesmin;
    
    // timer for drag delay
    uint64_t dragexitdelay;
    IOTimerEventSource* dragTimer;
   
    SimpleAverage<int, 5> x_avg;
    SimpleAverage<int, 5> y_avg;
    //DecayingAverage<int, int64_t, 1, 1, 2> x_avg;
    //DecayingAverage<int, int64_t, 1, 1, 2> y_avg;
    UndecayAverage<int, int64_t, 1, 1, 2> x_undo;
    UndecayAverage<int, int64_t, 1, 1, 2> y_undo;
    
    SimpleAverage<int, 5> x2_avg;
    SimpleAverage<int, 5> y2_avg;
    //DecayingAverage<int, int64_t, 1, 1, 2> x2_avg;
    //DecayingAverage<int, int64_t, 1, 1, 2> y2_avg;
    UndecayAverage<int, int64_t, 1, 1, 2> x2_undo;
    UndecayAverage<int, int64_t, 1, 1, 2> y2_undo;
    
	enum
    {
        // "no touch" modes... must be even (see isTouchMode)
        MODE_NOTOUCH =      0,
		MODE_PREDRAG =      2,
        MODE_DRAGNOTOUCH =  4,

        // "touch" modes... must be odd (see isTouchMode)
        MODE_MOVE =         1,
        MODE_VSCROLL =      3,
        MODE_HSCROLL =      5,
        MODE_CSCROLL =      7,
        MODE_MTOUCH =       9,
        MODE_DRAG =         11,
        MODE_DRAGLOCK =     13,
        
        // special modes for double click in LED area to enable/disable
        // same "touch"/"no touch" odd/even rule (see isTouchMode)
        MODE_WAIT1RELEASE = 101,    // "touch"
        MODE_WAIT2TAP =     102,    // "no touch"
        MODE_WAIT2RELEASE = 103,    // "touch"
    } touchmode;

    void setClickButtons(UInt32 clickButtons);
    
    inline bool isTouchMode() { return touchmode & 1; }
    
    inline bool isInDisableZone(int x, int y)
        { return x > diszl && x < diszr && y > diszb && y < diszt; }
	
    // Sony: coordinates captured from single touch event
    // Don't know what is the exact value of x and y on edge of touchpad
    // the best would be { return x > xmax/2 && y < ymax/4; }

    inline bool isInRightClickZone(int x, int y)
        { return x > rczl && x < rczr && y > rczb && y < rczt; }
    inline bool isInLeftClickZone(int x, int y)
        { return x <= rczl && x <= rczr && y > rczb && y < rczt; }
        
    virtual void   dispatchEventsWithPacket(UInt8* packet, UInt32 packetSize);
    virtual void   dispatchEventsWithPacketEW(UInt8* packet, UInt32 packetSize);
    // virtual void   dispatchSwipeEvent ( IOHIDSwipeMask swipeType, AbsoluteTime now);
    
    virtual void   setTouchPadEnable( bool enable );
    virtual bool   getTouchPadData( UInt8 dataSelector, UInt8 buf3[] );
    virtual bool   getTouchPadStatus(  UInt8 buf3[] );
    virtual bool   setTouchPadModeByte(UInt8 modeByteValue);
	virtual PS2InterruptResult interruptOccurred(UInt8 data);
    virtual void packetReady();
    virtual void   setDevicePowerState(UInt32 whatToDo);
    
    virtual void   receiveMessage(int message, void* data);
    
    void updateTouchpadLED();
    bool setTouchpadLED(UInt8 touchLED);
    bool setTouchpadModeByte(); // set based on state
    void initTouchPad();
    bool setModeByte(UInt8 modeByteValue);
    bool setModeByte(); // set based on state

    inline bool isFingerTouch(int z) { return z>z_finger && z<zlimit; }
    
    void onScrollTimer(void);
    void queryCapabilities(void);
    void doHardwareReset(void);
    
    void onButtonTimer(void);
    
    void onDragTimer(void);
    
    enum MBComingFrom { fromPassthru, fromTimer, fromTrackpad, fromCancel };
    UInt32 middleButton(UInt32 butttons, uint64_t now, MBComingFrom from);
    
    void setParamPropertiesGated(OSDictionary* dict);
    void injectVersionDependentProperties(OSDictionary* dict);

protected:
	virtual IOItemCount buttonCount();
	virtual IOFixed     resolution();
    inline void dispatchRelativePointerEventX(int dx, int dy, UInt32 buttonState, uint64_t now)
        { dispatchRelativePointerEvent(dx, dy, buttonState, *(AbsoluteTime*)&now); }
    inline void dispatchScrollWheelEventX(short deltaAxis1, short deltaAxis2, short deltaAxis3, uint64_t now)
        { dispatchScrollWheelEvent(deltaAxis1, deltaAxis2, deltaAxis3, *(AbsoluteTime*)&now); }
    inline void setTimerTimeout(IOTimerEventSource* timer, uint64_t time)
        { timer->setTimeout(*(AbsoluteTime*)&time); }
    inline void cancelTimer(IOTimerEventSource* timer)
        { timer->cancelTimeout(); }
    
public:
    virtual bool init( OSDictionary * properties );
    virtual ApplePS2SynapticsTouchPad * probe( IOService * provider,
                                               SInt32 *    score );
    virtual bool start( IOService * provider );
    virtual void stop( IOService * provider );
    
    virtual UInt32 deviceType();
    virtual UInt32 interfaceID();

	virtual IOReturn setParamProperties(OSDictionary * dict);
	virtual IOReturn setProperties(OSObject *props);
};

#endif /* _APPLEPS2SYNAPTICSTOUCHPAD_H */
