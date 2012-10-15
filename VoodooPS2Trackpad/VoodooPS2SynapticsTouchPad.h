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
    
	int inited;
	int lastx, lasty;
	int xrest, yrest, scrollrest;
	int xmoved,ymoved,xscrolled, yscrolled;
	uint64_t touchtime;
	uint64_t untouchtime;
	bool wasdouble;
    uint64_t keytime;
    bool ignoreall;
    
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
    virtual UInt32 getTouchPadData( UInt8 dataSelector );
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
