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

#include <IOKit/assert.h>
#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include "VoodooPS2SynapticsTouchPad.h"

// =============================================================================
// ApplePS2SynapticsTouchPad Class Implementation
//

#define super IOHIPointing
OSDefineMetaClassAndStructors(ApplePS2SynapticsTouchPad, IOHIPointing);

UInt32 ApplePS2SynapticsTouchPad::deviceType()
{ return NX_EVS_DEVICE_TYPE_MOUSE; };

UInt32 ApplePS2SynapticsTouchPad::interfaceID()
{ return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

IOItemCount ApplePS2SynapticsTouchPad::buttonCount() { return 2; };
IOFixed     ApplePS2SynapticsTouchPad::resolution()  { return _resolution; };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


bool ApplePS2SynapticsTouchPad::init( OSDictionary * properties )
{
    //
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.
    //
    OSObject *tmp;
	
    if (!super::init(properties))  return false;

    _device                    = 0;
    _interruptHandlerInstalled = false;
    _packetByteCount           = 0;
    _resolution                = (2300) << 16; // (100 dpi, 4 counts/mm)
    _touchPadModeByte          = 0x80; //default: absolute, low-rate, no w-mode
	z_finger=30;
	divisor=23;
	ledge=1700;
	redge=5200;
	tedge=4200;
	bedge=1700;
	vscrolldivisor=30;
	hscrolldivisor=30;
	cscrolldivisor=0;
	ctrigger=0;
	centerx=3000;
	centery=3000;
	lastx=0;
	lasty=0;
	xrest=0; 
	yrest=0; 
	scrollrest=0;
	inited=0;
	maxtaptime=100000000;
	clicking=true;
	maxdragtime=300000000;
	dragging=true;
	draglock=false;
	hscroll=false;
	scroll=true;
	hsticky=0;
	vsticky=0;
	wsticky=1;
	tapstable=1;
	wlimit=9;
	wvdivisor=30;
	whdivisor=30;
	xmoved=ymoved=xscrolled=yscrolled=0;
	touchmode=MODE_NOTOUCH;
	wasdouble=false;
	
	IOLog ("VoodooPS2SynapticsTouchPad loaded\n");
	
	setProperty ("Revision", 24, 32);
	tmp=properties->getObject ("Configuration");
	if (tmp && OSDynamicCast (OSDictionary, tmp))
		setParamProperties (OSDynamicCast (OSDictionary, tmp));

	inited=1;
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2SynapticsTouchPad *
ApplePS2SynapticsTouchPad::probe( IOService * provider, SInt32 * score )
{
    //
    // The driver has been instructed to verify the presence of the actual
    // hardware we represent. We are guaranteed by the controller that the
    // mouse clock is enabled and the mouse itself is disabled (thus it
    // won't send any asynchronous mouse data that may mess up the
    // responses expected by the commands we send it).
    //

    ApplePS2MouseDevice * device  = (ApplePS2MouseDevice *) provider;
    PS2Request *          request = device->allocateRequest();
    bool                  success = false;
    
    if (!super::probe(provider, score) || !request) return 0;

    //
    // Send an "Identify TouchPad" command and see if the device is
    // a Synaptics TouchPad based on its response.  End the command
    // chain with a "Set Defaults" command to clear all state.
    //

    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
    request->commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut  = kDP_SetMouseResolution;
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = 0;
    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseResolution;
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = 0;
    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = kDP_SetMouseResolution;
    request->commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[6].inOrOut  = 0;
    request->commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[7].inOrOut  = kDP_SetMouseResolution;
    request->commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[8].inOrOut  = 0;
    request->commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[9].inOrOut  = kDP_GetMouseInformation;
    request->commands[10].command = kPS2C_ReadDataPort;
    request->commands[10].inOrOut = 0;
    request->commands[11].command = kPS2C_ReadDataPort;
    request->commands[11].inOrOut = 0;
    request->commands[12].command = kPS2C_ReadDataPort;
    request->commands[12].inOrOut = 0;
    request->commands[13].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[13].inOrOut = kDP_SetDefaultsAndDisable;
    request->commandsCount = 14;
    device->submitRequestAndBlock(request);

    if ( request->commandsCount == 14 &&
         request->commands[11].inOrOut == 0x47 )
    {
        _touchPadVersion = (request->commands[12].inOrOut & 0x0f) << 8
                         |  request->commands[10].inOrOut;

        //
        // Only support 4.x or later touchpads.
        //

        if ( _touchPadVersion >= 0x400 ) success = true;
    }

    device->freeRequest(request);

    return (success) ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::start( IOService * provider )
{ 
    //
    // The driver has been instructed to start. This is called after a
    // successful probe and match.
    //

    if (!super::start(provider)) return false;

    //
    // Maintain a pointer to and retain the provider object.
    //

    _device = (ApplePS2MouseDevice *) provider;
    _device->retain();

    //
    // Announce hardware properties.
    //

    IOLog("VoodooPS2Trackpad: Synaptics TouchPad v%d.%d\n",
          (UInt8)(_touchPadVersion >> 8), (UInt8)(_touchPadVersion));

    //
    // Write the TouchPad mode byte value.
    //

    setTouchPadModeByte(_touchPadModeByte);

    //
    // Advertise the current state of the tapping feature.
    //


    //
    // Must add this property to let our superclass know that it should handle
    // trackpad acceleration settings from user space.  Without this, tracking
    // speed adjustments from the mouse prefs panel have no effect.
    //

    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
    setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDTrackpadScrollAccelerationKey);
	
	setProperty(kIOHIDScrollResolutionKey, (100 << 16), 32);
    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //

    _device->installInterruptAction(this,
        OSMemberFunctionCast(PS2InterruptAction,this,&ApplePS2SynapticsTouchPad::interruptOccurred));
    _interruptHandlerInstalled = true;

    //
    // Enable the mouse clock (should already be so) and the mouse IRQ line.
    //

    setCommandByte( kCB_EnableMouseIRQ, kCB_DisableMouseClock );

    //
    // Finally, we enable the trackpad itself, so that it may start reporting
    // asynchronous events.
    //

    setTouchPadEnable(true);

    //
	// Install our power control handler.
	//

	_device->installPowerControlAction( this, OSMemberFunctionCast(PS2PowerControlAction,this, 
             &ApplePS2SynapticsTouchPad::setDevicePowerState) );
	_powerControlHandlerInstalled = true;

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::stop( IOService * provider )
{
    //
    // The driver has been instructed to stop.  Note that we must break all
    // connections to other service objects now (ie. no registered actions,
    // no pointers and retains to objects, etc), if any.
    //

    assert(_device == provider);

    //
    // Disable the mouse itself, so that it may stop reporting mouse events.
    //

    setTouchPadEnable(false);

    //
    // Disable the mouse clock and the mouse IRQ line.
    //

    setCommandByte( kCB_DisableMouseClock, kCB_EnableMouseIRQ );

    //
    // Uninstall the interrupt handler.
    //

    if ( _interruptHandlerInstalled )  _device->uninstallInterruptAction();
    _interruptHandlerInstalled = false;

    //
    // Uninstall the power control handler.
    //

    if ( _powerControlHandlerInstalled ) _device->uninstallPowerControlAction();
    _powerControlHandlerInstalled = false;

	super::stop(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::free()
{
    //
    // Release the pointer to the provider object.
    //

    if (_device)
    {
        _device->release();
        _device = 0;
    }

    super::free();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::interruptOccurred( UInt8 data )
{
    //
    // This will be invoked automatically from our device when asynchronous
    // events need to be delivered. Process the trackpad data. Do NOT issue
    // any BLOCKING commands to our device in this context.
    //
    // Ignore all bytes until we see the start of a packet, otherwise the
    // packets may get out of sequence and things will get very confusing.
    //
    if (_packetByteCount == 0 && ((data == kSC_Acknowledge) || ((data & 0xc0)!=0x80)))
    {
        return;
    }

    //
    // Add this byte to the packet buffer. If the packet is complete, that is,
    // we have the three bytes, dispatch this packet for processing.
    //

    _packetBuffer[_packetByteCount++] = data;
    
    if (_packetByteCount == 6)
    {
        dispatchRelativePointerEventWithPacket(_packetBuffer, 6);
        _packetByteCount = 0;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::
     dispatchRelativePointerEventWithPacket( UInt8 * packet,
                                             UInt32  packetSize )
{
    //
    // Process the three byte relative format packet that was retreived from the
    // trackpad. The format of the bytes is as follows:
    //
    //  7  6  5  4  3  2  1  0
    // -----------------------
    // YO XO YS XS  1  M  R  L
    // X7 X6 X5 X4 X3 X3 X1 X0  (X delta)
    // Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (Y delta)
    //

    UInt32       buttons = 0;
	AbsoluteTime now;
	int x,y,z,w;

	clock_get_uptime((uint64_t*)&now);
    if ( (packet[0] & 0x1)) buttons |= 0x1;  // left button   (bit 0 in packet)
    if ( (packet[0] & 0x2) ) buttons |= 0x2;  // right button  (bit 1 in packet)
    
	x=packet[4]|((packet[1]&0xf)<<8)|((packet[3]&0x10)<<8);
	y=packet[5]|((packet[1]&0xf0)<<4)|((packet[3]&0x20)<<7);
	z=packet[2];
	w=((packet[3]&0x4)>>2)|((packet[0]&0x4)>>1)|((packet[0]&0x30)>>2);
	if (z < z_finger && touchmode!=MODE_NOTOUCH && touchmode!=MODE_PREDRAG && touchmode!=MODE_DRAGNOTOUCH)
	{
		xrest=yrest=scrollrest=0;
		untouchtime=(*(uint64_t*)&now);
		if ((*(uint64_t*)&now)-touchtime<maxtaptime && clicking)
			switch (touchmode)
			{
				case MODE_DRAG:
					buttons&=~0x3;
					dispatchRelativePointerEvent(0, 0, buttons|0x1, now);
					dispatchRelativePointerEvent(0, 0, buttons, now);
					if (wasdouble && rtap)
						buttons|=0x2;
					else
						buttons|=0x1;
					touchmode = MODE_NOTOUCH;
					break;
				case MODE_DRAGLOCK:
					touchmode=MODE_NOTOUCH;
					break;
				default:
					if (wasdouble && rtap)
					{
						buttons|=0x2;
						touchmode=MODE_NOTOUCH;
					}
					else
					{
						buttons|=0x1;
						touchmode = dragging?MODE_PREDRAG:MODE_NOTOUCH;
					}
			}
		else
		{
			xmoved=ymoved=xscrolled=yscrolled=0;
			if ((touchmode==MODE_DRAG || touchmode==MODE_DRAGLOCK) && draglock)
				touchmode = MODE_DRAGNOTOUCH;
			else
				touchmode = MODE_NOTOUCH;
		}
		wasdouble=false;
	}
	if (touchmode==MODE_PREDRAG && (*(uint64_t*)&now)-untouchtime>maxdragtime)
		touchmode = MODE_NOTOUCH;
	switch (touchmode)
	{
		case MODE_DRAG:
		case MODE_DRAGLOCK:
			buttons|=0x1;
		case MODE_MOVE:
			if (!divisor)
				break;
			dispatchRelativePointerEvent((x-lastx+xrest)/divisor, (lasty-y+yrest)/divisor, buttons, now);
			xmoved+=(x-lastx+xrest)/divisor;
			ymoved+=(lasty-y+yrest)/divisor;
			xrest=(x-lastx+xrest)%divisor;
			yrest=(lasty-y+yrest)%divisor;
			break;
		case MODE_MTOUCH:
			if (!wsticky && w<wlimit && w>=3)
			{
				touchmode=MODE_MOVE;
				break;
			}			
			dispatchScrollWheelEvent(wvdivisor?(y-lasty+yrest)/wvdivisor:0, 
									 (whdivisor&&hscroll)?(lastx-x+xrest)/whdivisor:0, 0, now);
			xscrolled+=wvdivisor?(y-lasty+yrest)/wvdivisor:0;
			yscrolled+=whdivisor?(lastx-x+xrest)/whdivisor:0;
			xrest=whdivisor?(lastx-x+xrest)%whdivisor:0;
			yrest=wvdivisor?(y-lasty+yrest)%wvdivisor:0;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;
			
		case MODE_VSCROLL:
			if (!vsticky && x<redge)
			{
				touchmode=MODE_MOVE;
				break;
			}
			dispatchScrollWheelEvent((y-lasty+scrollrest)/vscrolldivisor, 0, 0, now);
			xscrolled+=(y-lasty+scrollrest)/vscrolldivisor;
			scrollrest=(y-lasty+scrollrest)%vscrolldivisor;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;			
		case MODE_HSCROLL:
			if (!hsticky && y>bedge)
			{
				touchmode=MODE_MOVE;
				break;
			}			
			dispatchScrollWheelEvent(0,(lastx-x+scrollrest)/hscrolldivisor, 0, now);
			yscrolled+=(lastx-x+scrollrest)/hscrolldivisor;
			scrollrest=(lastx-x+scrollrest)%hscrolldivisor;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;			
		case MODE_CSCROLL:
		{
			int mov=0;
			if (y<centery)
				mov=x-lastx;
			else
				mov=lastx-x;
			if (x<centerx)
				mov+=lasty-y;
			else
				mov+=y-lasty;
			
			dispatchScrollWheelEvent((mov+scrollrest)/cscrolldivisor, 0, 0, now);
			xscrolled+=(mov+scrollrest)/cscrolldivisor;
			scrollrest=(mov+scrollrest)%cscrolldivisor;
		}
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;			

		case MODE_PREDRAG:
		case MODE_DRAGNOTOUCH:
			buttons |= 0x1;
		case MODE_NOTOUCH:
			if (!tapstable)
				xmoved=ymoved=xscrolled=yscrolled=0;
			dispatchScrollWheelEvent(-xscrolled, -yscrolled, 0, now);			
			dispatchRelativePointerEvent(-xmoved, -ymoved, buttons, now);
			xmoved=ymoved=xscrolled=yscrolled=0;
			break;
	}
	lastx=x;
	lasty=y;
	if ((touchmode==MODE_NOTOUCH || touchmode==MODE_PREDRAG || touchmode==MODE_DRAGNOTOUCH) && z>z_finger)
		touchtime=*(uint64_t*)&now;
	if ((w>=wlimit || w<3) && z>z_finger)
		wasdouble=true;
	if ((w>=wlimit || w<3) && z>z_finger && scroll && (wvdivisor || (hscroll && whdivisor)))
		touchmode=MODE_MTOUCH;
	if (touchmode==MODE_PREDRAG && z>z_finger)
		touchmode=MODE_DRAG;
	if (touchmode==MODE_DRAGNOTOUCH && z>z_finger)
		touchmode=MODE_DRAGLOCK;
	
	if (scroll && cscrolldivisor)
	{
		if (touchmode==MODE_NOTOUCH && z>z_finger && y>tedge && (ctrigger==1 || ctrigger==9))
			touchmode=MODE_CSCROLL;
		if (touchmode==MODE_NOTOUCH && z>z_finger && y>tedge && x>redge && (ctrigger==2))
			touchmode=MODE_CSCROLL;
		if (touchmode==MODE_NOTOUCH && z>z_finger && x>redge && (ctrigger==3 || ctrigger==9))
			touchmode=MODE_CSCROLL;
		if (touchmode==MODE_NOTOUCH && z>z_finger && x>redge && y<bedge && (ctrigger==4))
			touchmode=MODE_CSCROLL;
		if (touchmode==MODE_NOTOUCH && z>z_finger && y<bedge && (ctrigger==5 || ctrigger==9))
			touchmode=MODE_CSCROLL;
		if (touchmode==MODE_NOTOUCH && z>z_finger && y<bedge && x<ledge && (ctrigger==6))
			touchmode=MODE_CSCROLL;
		if (touchmode==MODE_NOTOUCH && z>z_finger && x<ledge && (ctrigger==7 || ctrigger==9))
			touchmode=MODE_CSCROLL;
		if (touchmode==MODE_NOTOUCH && z>z_finger && x<ledge && y>tedge && (ctrigger==8))
			touchmode=MODE_CSCROLL;
	}
	if (touchmode==MODE_NOTOUCH && z>z_finger && x>redge && vscrolldivisor && scroll)
		touchmode=MODE_VSCROLL;
	if (touchmode==MODE_NOTOUCH && z>z_finger && y<bedge && hscrolldivisor && hscroll && scroll)
		touchmode=MODE_HSCROLL;
	if (touchmode==MODE_NOTOUCH && z>z_finger)
		touchmode=MODE_MOVE;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setTouchPadEnable( bool enable )
{
    //
    // Instructs the trackpad to start or stop the reporting of data packets.
    // It is safe to issue this request from the interrupt/completion context.
    //

    PS2Request * request = _device->allocateRequest();
    if ( !request ) return;

    // (mouse enable/disable command)
    request->commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut = (enable)?kDP_Enable:kDP_SetDefaultsAndDisable;
    request->commandsCount = 1;
    _device->submitRequestAndBlock(request);
    _device->freeRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt32 ApplePS2SynapticsTouchPad::getTouchPadData( UInt8 dataSelector )
{
    PS2Request * request     = _device->allocateRequest();
    UInt32       returnValue = (UInt32)(-1);

    if ( !request ) return returnValue;

    // Disable stream mode before the command sequence.
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetDefaultsAndDisable;

    // 4 set resolution commands, each encode 2 data bits.
    request->commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut  = kDP_SetMouseResolution;
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = (dataSelector >> 6) & 0x3;

    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseResolution;
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = (dataSelector >> 4) & 0x3;

    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = kDP_SetMouseResolution;
    request->commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[6].inOrOut  = (dataSelector >> 2) & 0x3;

    request->commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[7].inOrOut  = kDP_SetMouseResolution;
    request->commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[8].inOrOut  = (dataSelector >> 0) & 0x3;

    // Read response bytes.
    request->commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[9].inOrOut  = kDP_GetMouseInformation;
    request->commands[10].command = kPS2C_ReadDataPort;
    request->commands[10].inOrOut = 0;
    request->commands[11].command = kPS2C_ReadDataPort;
    request->commands[11].inOrOut = 0;
    request->commands[12].command = kPS2C_ReadDataPort;
    request->commands[12].inOrOut = 0;

    request->commandsCount = 13;
    _device->submitRequestAndBlock(request);

    if (request->commandsCount == 13) // success?
    {
        returnValue = ((UInt32)request->commands[10].inOrOut << 16) |
                      ((UInt32)request->commands[11].inOrOut <<  8) |
                      ((UInt32)request->commands[12].inOrOut);
    }

    _device->freeRequest(request);

    return returnValue;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::setTouchPadModeByte( UInt8 modeByteValue,
                                                     bool  enableStreamMode )
{
    PS2Request * request = _device->allocateRequest();
    bool         success;

    if ( !request ) return false;

    // Disable stream mode before the command sequence.
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetDefaultsAndDisable;

    // 4 set resolution commands, each encode 2 data bits.
    request->commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut  = kDP_SetMouseResolution;
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = (modeByteValue >> 6) & 0x3;

    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseResolution;
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = (modeByteValue >> 4) & 0x3;

    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = kDP_SetMouseResolution;
    request->commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[6].inOrOut  = (modeByteValue >> 2) & 0x3;

    request->commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[7].inOrOut  = kDP_SetMouseResolution;
    request->commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[8].inOrOut  = (modeByteValue >> 0) & 0x3;

    // Set sample rate 20 to set mode byte 2. Older pads have 4 mode
    // bytes (0,1,2,3), but only mode byte 2 remain in modern pads.
    request->commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[9].inOrOut  = kDP_SetMouseSampleRate;
    request->commands[10].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[10].inOrOut = 20;

    request->commands[11].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[11].inOrOut  = enableStreamMode ?
                                     kDP_Enable :
                                     kDP_SetMouseScaling1To1; /* Nop */

    request->commandsCount = 12;
    _device->submitRequestAndBlock(request);

    success = (request->commandsCount == 12);

    _device->freeRequest(request);
    
    return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setCommandByte( UInt8 setBits, UInt8 clearBits )
{
    //
    // Sets the bits setBits and clears the bits clearBits "atomically" in the
    // controller's Command Byte.   Since the controller does not provide such
    // a read-modify-write primitive, we resort to a test-and-set try loop.
    //
    // Do NOT issue this request from the interrupt/completion context.
    //

    UInt8        commandByte;
    UInt8        commandByteNew;
    PS2Request * request = _device->allocateRequest();

    if ( !request ) return;

    do
    {
        // (read command byte)
        request->commands[0].command = kPS2C_WriteCommandPort;
        request->commands[0].inOrOut = kCP_GetCommandByte;
        request->commands[1].command = kPS2C_ReadDataPort;
        request->commands[1].inOrOut = 0;
        request->commandsCount = 2;
        _device->submitRequestAndBlock(request);

        //
        // Modify the command byte as requested by caller.
        //

        commandByte    = request->commands[1].inOrOut;
        commandByteNew = (commandByte | setBits) & (~clearBits);

        // ("test-and-set" command byte)
        request->commands[0].command = kPS2C_WriteCommandPort;
        request->commands[0].inOrOut = kCP_GetCommandByte;
        request->commands[1].command = kPS2C_ReadDataPortAndCompare;
        request->commands[1].inOrOut = commandByte;
        request->commands[2].command = kPS2C_WriteCommandPort;
        request->commands[2].inOrOut = kCP_SetCommandByte;
        request->commands[3].command = kPS2C_WriteDataPort;
        request->commands[3].inOrOut = commandByteNew;
        request->commandsCount = 4;
        _device->submitRequestAndBlock(request);

        //
        // Repeat this loop if last command failed, that is, if the
        // old command byte was modified since we first read it.
        //

    } while (request->commandsCount != 4);  

    _device->freeRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2SynapticsTouchPad::setParamProperties( OSDictionary * config )
{
	OSNumber *num;
	OSBoolean *bl;
	uint8_t oldmode=_touchPadModeByte;
	struct {const char *name; int *var;} int32vars[]={
		{"FingerZ",							&z_finger		},
		{"Divisor",							&divisor		},
		{"RightEdge",						&redge			},
		{"LeftEdge",						&ledge			},
		{"TopEdge",							&tedge			},
		{"BottomEdge",						&bedge			},
		{"VerticalScrollDivisor",			&vscrolldivisor	},
		{"HorizontalScrollDivisor",			&hscrolldivisor	},
		{"CircularScrollDivisor",			&cscrolldivisor	},
		{"CenterX",							&centerx		},
		{"CenterY",							&centery		},
		{"CircularScrollTrigger",			&ctrigger		},
		{"MultiFingerWLimit",				&wlimit			},
		{"MultiFingerVerticalDivisor",		&wvdivisor		},
		{"MultiFingerHorizontalDivisor",	&whdivisor		}
	};
	struct {const char *name; int *var;} boolvars[]={
		{"StickyHorizontalScrolling",		&hsticky},
		{"StickyVerticalScrolling",			&vsticky},
		{"StickyMultiFingerScrolling",		&wsticky},
		{"StabilizeTapping",				&tapstable}
	};
	int i;
	if (!config)
		return 0;
	if ((bl=OSDynamicCast (OSBoolean, config->getObject ("UseHighRate"))))
    {
		if (bl->isTrue())
			_touchPadModeByte |= 1<<6;
		else
			_touchPadModeByte &=~(1<<6);
    }

	if ((num=OSDynamicCast (OSNumber, config->getObject ("TrackpadRightClick"))))
		rtap = (num->unsigned32BitValue()&0x1)?true:false;
	if ((num=OSDynamicCast (OSNumber, config->getObject ("Clicking"))))
		clicking = (num->unsigned32BitValue()&0x1)?true:false;
	if ((num=OSDynamicCast (OSNumber, config->getObject ("Dragging"))))
		dragging = (num->unsigned32BitValue()&0x1)?true:false;
	if ((num=OSDynamicCast (OSNumber, config->getObject ("DragLock"))))
		draglock = (num->unsigned32BitValue()&0x1)?true:false;
	if ((num=OSDynamicCast (OSNumber, config->getObject ("TrackpadHorizScroll"))))
		hscroll = (num->unsigned32BitValue()&0x1)?true:false;
	if ((num=OSDynamicCast (OSNumber, config->getObject ("TrackpadScroll"))))
		scroll = (num->unsigned32BitValue()&0x1)?true:false;
	
	if ((num=OSDynamicCast (OSNumber, config->getObject ("MaxTapTime"))))
		maxtaptime = num->unsigned64BitValue();
	if ((num=OSDynamicCast (OSNumber, config->getObject ("HIDClickTime"))))
		maxdragtime = num->unsigned64BitValue();
	for (i=0;(unsigned)i<sizeof (boolvars)/sizeof(boolvars[0]);i++)		
		if ((bl=OSDynamicCast (OSBoolean,config->getObject (boolvars[i].name))))
			*(boolvars[i].var) = bl->isTrue();	
	for (i=0;(unsigned)i<sizeof (int32vars)/sizeof(int32vars[0]);i++)		
		if ((num=OSDynamicCast (OSNumber,config->getObject (int32vars[i].name))))
			*(int32vars[i].var) = num->unsigned32BitValue();
	
	if (whdivisor || wvdivisor)
		_touchPadModeByte |= 1<<0;
	else
		_touchPadModeByte &=~(1<<0);
	
	if (_touchPadModeByte!=oldmode && inited)
		setTouchPadModeByte (_touchPadModeByte);
	_packetByteCount=0;
	touchmode = MODE_NOTOUCH;
	
	for (i=0;(unsigned)i<sizeof (int32vars)/sizeof(int32vars[0]);i++)		
		setProperty (int32vars[i].name,*(int32vars[i].var),32);
	for (i=0;(unsigned)i<sizeof (boolvars)/sizeof(int32vars[0]);i++)		
		setProperty (boolvars[i].name,*(boolvars[i].var)?kOSBooleanTrue:kOSBooleanFalse);

	setProperty ("MaxTapTime", maxtaptime, 64);
	setProperty ("HIDClickTime", maxdragtime, 64);
	setProperty ("UseHighRate",_touchPadModeByte&(1<<6)?kOSBooleanTrue:kOSBooleanFalse);

	setProperty ("Clicking", clicking?1:0, 32);
	setProperty ("Dragging", dragging?1:0, 32);
	setProperty ("DragLock", draglock?1:0, 32);
	setProperty ("TrackpadHorizScroll", hscroll?1:0, 32);
	setProperty ("TrackpadScroll", scroll?1:0, 32);
	setProperty ("TrackpadRightClick", rtap?1:0, 32);
	
    return super::setParamProperties(config);
}

IOReturn ApplePS2SynapticsTouchPad::setProperties (OSObject *props)
{
	OSDictionary *pdict;
	if ((pdict=OSDynamicCast (OSDictionary, props)))
		return setParamProperties (pdict);
	return kIOReturnError;
}
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setDevicePowerState( UInt32 whatToDo )
{
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            
            //
            // Disable touchpad (synchronous).
            //

            setTouchPadEnable( false );
            break;

        case kPS2C_EnableDevice:

            //
            // Must not issue any commands before the device has
            // completed its power-on self-test and calibration.
            //

            IOSleep(1000);

            setTouchPadModeByte( _touchPadModeByte );

            //
            // Enable the mouse clock (should already be so) and the
            // mouse IRQ line.
            //

            setCommandByte( kCB_EnableMouseIRQ, kCB_DisableMouseClock );

            //
            // Clear packet buffer pointer to avoid issues caused by
            // stale packet fragments.
            //

            _packetByteCount = 0;

            //
            // Finally, we enable the trackpad itself, so that it may
            // start reporting asynchronous events.
            //

            setTouchPadEnable( true );
            break;
    }
}
