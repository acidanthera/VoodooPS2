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

#ifndef _APPLEPS2SENTILICSFSP_H
#define _APPLEPS2SENTILICSFSP_H

#include "ApplePS2MouseDevice.h"
#include <IOKit/hidsystem/IOHIPointing.h>

#define kPacketLengthMax          4
#define kPacketLengthStandard     3
#define kPacketLengthLarge        4

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2SentelicFSP Class Declaration
//

class EXPORT ApplePS2SentelicFSP : public IOHIPointing
{
    typedef IOHIPointing super;
    OSDeclareDefaultStructors( ApplePS2SentelicFSP );
    
private:
    ApplePS2MouseDevice * _device;
    bool                  _interruptHandlerInstalled;
    bool                  _powerControlHandlerInstalled;
    RingBuffer<UInt8, kPacketLengthMax*32> _ringBuffer;
    UInt32                _packetByteCount;
    UInt8                 _packetSize;
    IOFixed               _resolution;
    UInt16                _touchPadVersion;
    UInt8                 _touchPadModeByte;
    
    virtual void   dispatchRelativePointerEventWithPacket( UInt8 * packet, UInt32  packetSize ); 
    
    virtual void   setTouchPadEnable( bool enable );
    virtual UInt32 getTouchPadData( UInt8 dataSelector );
    virtual bool   setTouchPadModeByte( UInt8 modeByteValue,
                                       bool  enableStreamMode = false );
    
    virtual PS2InterruptResult interruptOccurred(UInt8 data);
    virtual void packetReady();
    virtual void   setDevicePowerState(UInt32 whatToDo);
    
protected:
    virtual IOItemCount buttonCount();
    virtual IOFixed     resolution();
    
    inline void dispatchRelativePointerEventX(int dx, int dy, UInt32 buttonState, uint64_t now)
        { dispatchRelativePointerEvent(dx, dy, buttonState, *(AbsoluteTime*)&now); }
    inline void dispatchScrollWheelEventX(short deltaAxis1, short deltaAxis2, short deltaAxis3, uint64_t now)
        { dispatchScrollWheelEvent(deltaAxis1, deltaAxis2, deltaAxis3, *(AbsoluteTime*)&now); }
    
    
public:
    virtual bool init( OSDictionary * properties );
    virtual ApplePS2SentelicFSP * probe( IOService * provider,
                                              SInt32 *    score );
    
    virtual bool start( IOService * provider );
    virtual void stop( IOService * provider );
    
    virtual UInt32 deviceType();
    virtual UInt32 interfaceID();
    
    virtual IOReturn setParamProperties( OSDictionary * dict );
};

#endif /* _APPLEPS2SENTILICSFSP_H */
