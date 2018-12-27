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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2ALPSGlidePoint Class Declaration
//

typedef struct ALPSStatus
{
	UInt8 byte0;
	UInt8 byte1;
	UInt8 byte2;
} ALPSStatus_t;

#define SCROLL_NONE  0
#define SCROLL_HORIZ 1
#define SCROLL_VERT  2

#define kPacketLengthSmall  3
#define kPacketLengthLarge  6
#define kPacketLengthMax    6

class EXPORT ApplePS2ALPSGlidePoint : public IOHIPointing
{
    typedef IOHIPointing super;
	OSDeclareDefaultStructors( ApplePS2ALPSGlidePoint );

private:
    ApplePS2MouseDevice * _device;
    bool                  _interruptHandlerInstalled;
    bool                  _powerControlHandlerInstalled;
    RingBuffer<UInt8, kPacketLengthMax*32> _ringBuffer;
    UInt32                _packetByteCount;
    IOFixed               _resolution;
    UInt16                _touchPadVersion;
    UInt8                 _touchPadModeByte;

	bool				  _dragging;
	bool				  _edgehscroll;
	bool				  _edgevscroll;
    UInt32                _edgeaccell;
    double                _edgeaccellvalue;
	bool				  _draglock;

private:
    SInt32				  _xpos, _xscrollpos;
    SInt32				  _ypos, _yscrollpos;
    SInt32				  _zpos, _zscrollpos;
    int                   _xdiffold, _ydiffold;
    short                 _scrolling;
    
protected:
	virtual void   dispatchRelativePointerEventWithPacket( UInt8 * packet,
                                                           UInt32  packetSize );
	virtual void   dispatchAbsolutePointerEventWithPacket(UInt8 *packet,UInt32 packetSize);
	virtual void   getModel(ALPSStatus_t *e6,ALPSStatus_t *e7);
	virtual void   setAbsoluteMode();
	virtual void   getStatus(ALPSStatus_t *status);
	virtual int    insideScrollArea(int x,int y);

	virtual void   setTapEnable( bool enable );
    virtual void   setTouchPadEnable( bool enable );
#if _NO_TOUCHPAD_ENABLE_
    virtual UInt32 getTouchPadData( UInt8 dataSelector );
    virtual bool   setTouchPadModeByte( UInt8 modeByteValue,
                                        bool  enableStreamMode = false );
#endif
	virtual PS2InterruptResult interruptOccurred(UInt8 data);
    virtual void packetReady();
    virtual void   setDevicePowerState(UInt32 whatToDo);
    
    inline void dispatchRelativePointerEventX(int dx, int dy, UInt32 buttonState, uint64_t now)
        { dispatchRelativePointerEvent(dx, dy, buttonState, *(AbsoluteTime*)&now); }
    inline void dispatchScrollWheelEventX(short deltaAxis1, short deltaAxis2, short deltaAxis3, uint64_t now)
        { dispatchScrollWheelEvent(deltaAxis1, deltaAxis2, deltaAxis3, *(AbsoluteTime*)&now); }

protected:
	virtual IOItemCount buttonCount();
	virtual IOFixed     resolution();

public:
    virtual bool init( OSDictionary * properties );
    virtual ApplePS2ALPSGlidePoint * probe( IOService * provider,
                                               SInt32 *    score );
    
    virtual bool start( IOService * provider );
    virtual void stop( IOService * provider );
    
    virtual UInt32 deviceType();
    virtual UInt32 interfaceID();

	virtual IOReturn setParamProperties( OSDictionary * dict );
};

#endif /* _APPLEPS2SYNAPTICSTOUCHPAD_H */
