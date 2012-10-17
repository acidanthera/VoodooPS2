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
    void init()
    {
        m_count = 0;
        m_sum = 0;
        m_index = 0;
    }
    
public:
    inline SimpleAverage() { init(); }
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
    inline void clear() { init(); }
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
    inline void init() { m_lastvalid = false; }
    
public:
    inline DecayingAverage() { init(); }
    T filter(T data)
    {
        TT result = data;
        TT last = m_last;
        if (m_lastvalid)
            result = (result * N1) / D + (last * N2) / D;
        m_lastvalid = true;
        m_last = (T)result;
        return m_last;
    }
    inline void clear() { init(); }
};

template <class T, class TT, int N1, int N2, int D>
class UndecayAverage
{
private:
    T m_last;
    bool m_lastvalid;
    inline void init() { m_lastvalid = false; }
    
public:
    inline UndecayAverage() { init(); }
    T filter(T data)
    {
        TT result = data;
        TT last = m_last;
        if (m_lastvalid)
            result = (result * D) / N1 - (last * N2) / N1;
        m_lastvalid = true;
        m_last = (T)data;
        return m_last;
    }
    inline void clear() { init(); }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2SynapticsTouchPad Class Declaration
//

class ApplePS2SynapticsTouchPad : public IOHIPointing
{
	OSDeclareDefaultStructors( ApplePS2SynapticsTouchPad );

private:
    ApplePS2MouseDevice * _device;
    //REVIEW: really? bitfields?
    UInt32                _interruptHandlerInstalled:1;
    UInt32                _powerControlHandlerInstalled:1;
    UInt32                _messageHandlerInstalled:1;
    //REVIEW: why is packet buffer 50 bytes (we only need 6)
    UInt8                 _packetBuffer[50];
    UInt32                _packetByteCount;
    IOFixed               _resolution;
    UInt16                _touchPadVersion;
    UInt8                 _touchPadType; // from identify: either 0x46 or 0x47
    UInt8                 _touchPadModeByte;
    
	int z_finger;
	int divisor;
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
	bool hscroll, scroll;
	bool rtap;
    bool outzone_wt, palm, palm_wt;
    int zlimit;
    int noled;
    uint64_t maxaftertyping;
    int mouseyinverter;
    int wakedelay;
    int smoothinput;
    int unsmoothinput;
    int skippassthru;
    int tapthreshx, tapthreshy;
    int dblthreshx, dblthreshy;
    
	int inited;
	int lastx, lasty;
	int xrest, yrest, scrollrest;
	int xmoved,ymoved,xscrolled, yscrolled;
    int touchx, touchy;
	uint64_t touchtime;
	uint64_t untouchtime;
	bool wasdouble;
    uint64_t keytime;
    bool ignoreall;
    int passbuttons;
    bool passthru;
    bool ledpresent;
//REVIEW: decide on which input smoothing to use
    SimpleAverage<int, 4> x_avg;
    SimpleAverage<int, 4> y_avg;
//    DecayingAverage<int, int64_t, 1, 1, 2> x_avg;
//    DecayingAverage<int, int64_t, 1, 1, 2> y_avg;
    UndecayAverage<int, int64_t, 1, 1, 2> x_undo;
    UndecayAverage<int, int64_t, 1, 1, 2> y_undo;
    
	enum
    {
        MODE_NOTOUCH =      0,
        MODE_MOVE =         1,
        MODE_VSCROLL =      2,
        MODE_HSCROLL =      3,
        MODE_CSCROLL =      4,
        MODE_MTOUCH =       5,
		MODE_PREDRAG =      6,
        MODE_DRAG =         7,
        MODE_DRAGNOTOUCH =  8,
        MODE_DRAGLOCK =     9,
    } touchmode;
    
    inline bool isTouchMode()
    { return MODE_NOTOUCH != touchmode && MODE_PREDRAG != touchmode && MODE_DRAGNOTOUCH != touchmode; }
	
	virtual void   dispatchRelativePointerEventWithPacket( UInt8 * packet,
                                                           UInt32  packetSize );

    virtual void   setCommandByte( UInt8 setBits, UInt8 clearBits );

    virtual void   setTouchPadEnable( bool enable );
    virtual bool   getTouchPadData( UInt8 dataSelector, UInt8 buf3[] );
    virtual bool   setTouchPadModeByte( UInt8 modeByteValue,
                                        bool  enableStreamMode = false );

	virtual void   free();
	virtual void   interruptOccurred( UInt8 data );
    virtual void   setDevicePowerState(UInt32 whatToDo);
    
    virtual void   receiveMessage(int message, void* data);
    
    void updateTouchpadLED();
    bool setTouchpadLED(UInt8 touchLED);
    
    inline bool isFingerTouch(int z) { return z>z_finger && z<zlimit; }

protected:
	virtual IOItemCount buttonCount();
	virtual IOFixed     resolution();

public:
    virtual bool init( OSDictionary * properties );
    virtual ApplePS2SynapticsTouchPad * probe( IOService * provider,
                                               SInt32 *    score );
    
    virtual bool start( IOService * provider );
    virtual void stop( IOService * provider );
    
    virtual UInt32 deviceType();
    virtual UInt32 interfaceID();

	virtual IOReturn setParamProperties( OSDictionary * dict );
	virtual IOReturn setProperties (OSObject *props);
};

#endif /* _APPLEPS2SYNAPTICSTOUCHPAD_H */
