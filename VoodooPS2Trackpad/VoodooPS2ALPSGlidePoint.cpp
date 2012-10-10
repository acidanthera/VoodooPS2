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
#include "VoodooPS2ALPSGlidePoint.h"

enum {
    //
    //
    kTapEnabled  = 0x01
};

// =============================================================================
// ApplePS2ALPSGlidePoint Class Implementation
//

#define super IOHIPointing
OSDefineMetaClassAndStructors(ApplePS2ALPSGlidePoint, IOHIPointing);

UInt32 ApplePS2ALPSGlidePoint::deviceType()
{ return NX_EVS_DEVICE_TYPE_MOUSE; };

UInt32 ApplePS2ALPSGlidePoint::interfaceID()
{ return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

IOItemCount ApplePS2ALPSGlidePoint::buttonCount() { return 2; };
IOFixed     ApplePS2ALPSGlidePoint::resolution()  { return _resolution; };
bool IsItALPS(ALPSStatus_t *E6,ALPSStatus_t *E7);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2ALPSGlidePoint::init( OSDictionary * properties )
{
    //
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.
    //
    
    if (!super::init(properties))  return false;

    _device                    = 0;
    _interruptHandlerInstalled = false;
    _packetByteCount           = 0;
    _resolution                = (100) << 16; // (100 dpi, 4 counts/mm)
    _touchPadModeByte          = kTapEnabled;
    _scrolling                 = SCROLL_NONE;
    _zscrollpos                = 0;
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2ALPSGlidePoint *
ApplePS2ALPSGlidePoint::probe( IOService * provider, SInt32 * score )
{
	ALPSStatus_t E6,E7;
    //
    // The driver has been instructed to verify the presence of the actual
    // hardware we represent. We are guaranteed by the controller that the
    // mouse clock is enabled and the mouse itself is disabled (thus it
    // won't send any asynchronous mouse data that may mess up the
    // responses expected by the commands we send it).
    //

    bool                  success = false;
    
    if (!super::probe(provider, score))
        return 0;

    _device = (ApplePS2MouseDevice *) provider;

    getModel(&E6, &E7);

    DEBUG_LOG("E7: { 0x%02x, 0x%02x, 0x%02x } E6: { 0x%02x, 0x%02x, 0x%02x }",
        E7.byte0, E7.byte1, E7.byte2, E6.byte0, E6.byte1, E6.byte2);

    success = IsItALPS(&E6,&E7);
	DEBUG_LOG("ALPS Device? %s", (success ? "yes" : "no"));

    // override
    success = true;
    _touchPadVersion = (E7.byte2 & 0x0f) << 8 | E7.byte0;

    return (success) ? this : 0;

}

bool IsItALPS(ALPSStatus_t *E6,ALPSStatus_t *E7)
{
	bool	success = false;
	short   i;
	
	UInt8 byte0, byte1, byte2;
	byte0 = E7->byte0;
	byte1 = E7->byte1;
	byte2 = E7->byte2;
	
	#define NUM_SINGLES 10
	static int singles[NUM_SINGLES * 3] ={
		0x33,0x2,0x0a,
		0x53,0x2,0x0a,
		0x53,0x2,0x14,
		0x63,0x2,0xa,
		0x63,0x2,0x14,
		0x73,0x2,0x0a,	// 3622947
		0x63,0x2,0x28,
		0x63,0x2,0x3c,
		0x63,0x2,0x50,
		0x63,0x2,0x64};
	#define NUM_DUALS 3
	static int duals[NUM_DUALS * 3]={
		0x20,0x2,0xe,
		0x22,0x2,0xa,
		0x22,0x2,0x14};

	for (i = 0; i < NUM_SINGLES; i++)
    {
		if ((byte0 == singles[i * 3]) && (byte1 == singles[i * 3 + 1]) && 
            (byte2 == singles[i * 3 + 2]))
		{
			success = true;
			break;
		}
	}
    
	if (!success)
	{
		for(i = 0;i < NUM_DUALS;i++)
		{
			if ((byte0 == duals[i * 3]) && (byte1 == duals[i * 3 + 1]) && 
                (byte2 == duals[i * 3 + 2]))
			{
				success = true;
				break;
			}
		}
	}
	return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2ALPSGlidePoint::start( IOService * provider )
{ 
    UInt64 enabledProperty;

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

    IOLog("ApplePS2Trackpad: ALPS GlidePoint v%d.%d\n",
          (UInt8)(_touchPadVersion >> 8), (UInt8)(_touchPadVersion));

    //
    // Advertise some supported features (tapping, edge scrolling).
    //

    enabledProperty = 1; 

    setProperty("Clicking", enabledProperty, 
        sizeof(enabledProperty) * 8);
    setProperty("TrackpadScroll", enabledProperty, 
        sizeof(enabledProperty) * 8);
    setProperty("TrackpadHorizScroll", enabledProperty, 
        sizeof(enabledProperty) * 8);

    // Enable tapping
    setTapEnable( true );

    // Enable Absolute Mode
	setAbsoluteMode();

    //
    // Must add this property to let our superclass know that it should handle
    // trackpad acceleration settings from user space.  Without this, tracking
    // speed adjustments from the mouse prefs panel have no effect.
    //

    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);

    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //

    _device->installInterruptAction(this,
        OSMemberFunctionCast(PS2InterruptAction,this,&ApplePS2ALPSGlidePoint::interruptOccurred));
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
             &ApplePS2ALPSGlidePoint::setDevicePowerState) );
	_powerControlHandlerInstalled = true;

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2ALPSGlidePoint::stop( IOService * provider )
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

void ApplePS2ALPSGlidePoint::free()
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

void ApplePS2ALPSGlidePoint::interruptOccurred( UInt8 data )
{
    //
    // This will be invoked automatically from our device when asynchronous
    // events need to be delivered. Process the trackpad data. Do NOT issue
    // any BLOCKING commands to our device in this context.
    //
    // Ignore all bytes until we see the start of a packet, otherwise the
    // packets may get out of sequence and things will get very confusing.
    //
		
	if(_packetByteCount == 6) {
		IOLog("_packetByteCount error!");
		_packetByteCount = 0;
        return;
    }

    _packetBuffer[_packetByteCount++] = data;
    
	if((_packetBuffer[0] & 0xc8) == 0x08) {
		if(_packetByteCount == 3) {
        dispatchRelativePointerEventWithPacket(_packetBuffer, 3);
        _packetByteCount = 0;
			return;
		}
		return;
    }
	if((_packetBuffer[0] & 0xf8) != 0xf8) {
		IOLog("Bad data: %d bytes\n",(int)_packetByteCount);
		_packetByteCount = 0;
		return; //bad data.
	}
	if(_packetByteCount >= 2 && _packetByteCount <=6 && _packetBuffer[_packetByteCount-1] == 0x80)
	{
		IOLog("Bad data2: %d bytes\n",(int)_packetByteCount);
		_packetByteCount = 0;
		return; //bad data
	}
	if(_packetByteCount == 6) {
		dispatchAbsolutePointerEventWithPacket(_packetBuffer,6);
		_packetByteCount = 0;
		return;
	}
	return;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ApplePS2ALPSGlidePoint::dispatchAbsolutePointerEventWithPacket(
        UInt8* packet,
        UInt32 packetSize
    )
{
    UInt32 buttons = 0;
    int left = 0, right = 0, middle = 0;
    int xdiff, ydiff, scroll;
    AbsoluteTime time;
    uint64_t now;
    bool wasNotScrolling, willScroll;
    
    int x = (packet[1] & 0x7f) | ((packet[2] & 0x78) << (7-3));
    int y = (packet[4] & 0x7f) | ((packet[3] & 0x70) << (7-4));
    int z = packet[5]; // touch pression
    
    clock_get_uptime(&now);
    time = *(AbsoluteTime*)&now;
    
    left  |= (packet[2]) & 1;
    left  |= (packet[3]) & 1;
    right |= (packet[3] >> 1) & 1;

    if (packet[0] != 0xff)
    {
        left   |= (packet[0]) & 1;
        right  |= (packet[0] >> 1) & 1;
        middle |= (packet[0] >> 2) & 1;
        middle |= (packet[3] >> 2) & 1;
    }

    buttons |= left ? 0x01 : 0;
    buttons |= right ? 0x02 : 0;
    buttons |= middle ? 0x04 : 0;

    /*DEBUG_LOG("Absolute packet: x: %d, y: %d, xpos: %d, ypos: %d, buttons: %x, "
              "z: %d, zpos: %d\n", x, y, (int)_xpos, (int)_ypos, (int)buttons, 
              (int)z, (int)_zpos);*/
    
    wasNotScrolling = _scrolling == SCROLL_NONE;
    scroll = insideScrollArea(x, y);
    
    willScroll = ((scroll & SCROLL_VERT) && _edgevscroll) || 
                    ((scroll & SCROLL_HORIZ) && _edgehscroll);

    // Make sure we are still relative
    if (z == 0 || (_zpos >= 1 && z != 0 && !willScroll))
    {
        _xpos = x;
        _ypos = y;
    }
    
    // Are we scrolling?
    if (willScroll)
    {
        if (_zscrollpos <= 0 || wasNotScrolling)
        {
            _xscrollpos = x;
            _yscrollpos = y;
        }
        
        xdiff = x - _xscrollpos;
        ydiff = y - _yscrollpos;
        
        ydiff = (scroll == SCROLL_VERT) ? -((int)((double)ydiff * _edgeaccellvalue)) : 0;
        xdiff = (scroll == SCROLL_HORIZ) ? -((int)((double)xdiff * _edgeaccellvalue)) : 0;
        
        // Those "if" should provide angle tapping (simulate click on up/down
        // buttons of a scrollbar), but i have to investigate more on the values,
        // since currently they don't work...
        if (ydiff == 0 && scroll == SCROLL_HORIZ)
            ydiff = ((x >= 950 ? 25 : (x <= 100 ? -25 : 0)) / max(_edgeaccellvalue, 1));
        
        if (xdiff == 0 && scroll == SCROLL_VERT)
            xdiff = ((y >= 950 ? 25 : (y <= 100 ? -25 : 0)) / max(_edgeaccellvalue, 1));
        
        dispatchScrollWheelEvent(ydiff, xdiff, 0, time);
        _zscrollpos = z;
        return;
    }

    _zpos = z == 0 ? _zpos + 1 : 0;
    _scrolling = SCROLL_NONE;
    
    xdiff = x - _xpos;
    ydiff = y - _ypos;
    
    _xpos = x;
    _ypos = y;
    
    //DEBUG_LOG("Sending event: %d,%d,%d\n",xdiff,ydiff,(int)buttons);
    dispatchRelativePointerEvent(xdiff, ydiff, buttons, time);
	return;
}

int ApplePS2ALPSGlidePoint::insideScrollArea(int x, int y)
{
    int scroll = 0;
    if (x > 900) scroll |= SCROLL_VERT;
    if (y > 650) scroll |= SCROLL_HORIZ;
    
    if (x > 900 && y > 650)
    {
        if (_scrolling == SCROLL_VERT)
            scroll = SCROLL_VERT;
        else
            scroll = SCROLL_HORIZ;
    }
    
    _scrolling = scroll;
    return scroll;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2ALPSGlidePoint::
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
    SInt32       dx, dy;
	AbsoluteTime now;

    if ( (packet[0] & 0x1) ) buttons |= 0x1;  // left button   (bit 0 in packet)
    if ( (packet[0] & 0x2) ) buttons |= 0x2;  // right button  (bit 1 in packet)
    if ( (packet[0] & 0x4) ) buttons |= 0x4;  // middle button (bit 2 in packet)
    
	dx = packet[1];
	if(packet[0] & 0x10)
		dx = dx -256;
	 	
	dy = packet[2];
	if(packet[0] & 0x20)
		dy = dy  - 256;

    clock_get_uptime((uint64_t*)&now);
    dispatchRelativePointerEvent(dx, dy, buttons, now);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2ALPSGlidePoint::setTapEnable( bool enable )
{
    //
    // Instructs the trackpad to honor or ignore tapping
    //
	ALPSStatus_t Status;
	
    PS2Request * request = _device->allocateRequest();
    if ( !request ) return;

	getStatus(&Status);
	if (Status.byte0 & 0x04) 
    {
        DEBUG_LOG("Tapping can only be toggled.\n");
		enable = false;
	}

	int cmd = enable ? kDP_SetMouseSampleRate : kDP_SetMouseResolution; 
	int arg = enable ? 0x0A : 0x00;

    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
	request->commands[0].inOrOut =  kDP_GetMouseInformation; //sync..
	request->commands[1].command = kPS2C_ReadDataPort;
	request->commands[1].inOrOut =  0;
	request->commands[2].command = kPS2C_ReadDataPort;
	request->commands[2].inOrOut =  0;
	request->commands[3].command = kPS2C_ReadDataPort;
	request->commands[3].inOrOut =  0;
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = kDP_SetDefaultsAndDisable;
    request->commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
	request->commands[6].inOrOut = cmd;
	request->commands[7].command = kPS2C_WriteCommandPort;
	request->commands[7].inOrOut = kCP_TransmitToMouse;
	request->commands[8].command = kPS2C_WriteDataPort;
	request->commands[8].inOrOut = arg;
	request->commands[9].command = kPS2C_ReadDataPortAndCompare;
	request->commands[9].inOrOut = kSC_Acknowledge;	
	request->commandsCount = 10;

	_device->submitRequestAndBlock(request);

	getStatus(&Status);

    _device->freeRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ApplePS2ALPSGlidePoint::setTouchPadEnable( bool enable )
{
    //
    // Instructs the trackpad to start or stop the reporting of data packets.
    // It is safe to issue this request from the interrupt/completion context.
    //

    PS2Request * request = _device->allocateRequest();
    if ( !request ) return;

    // (mouse enable/disable command)
    request->commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut = kDP_SetDefaultsAndDisable;

	// (mouse or pad enable/disable command)
    request->commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut = (enable)?kDP_Enable:kDP_SetDefaultsAndDisable;
    request->commandsCount = 5;
    _device->submitRequest(request); // asynchronous, auto-free'd
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2ALPSGlidePoint::setCommandByte( UInt8 setBits, UInt8 clearBits )
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

IOReturn ApplePS2ALPSGlidePoint::setParamProperties( OSDictionary * dict )
{
    OSNumber * clicking = OSDynamicCast( OSNumber, dict->getObject("Clicking") );
	OSNumber * dragging = OSDynamicCast( OSNumber, dict->getObject("Dragging") );
	OSNumber * draglock = OSDynamicCast( OSNumber, dict->getObject("DragLock") );
    OSNumber * hscroll  = OSDynamicCast( OSNumber, dict->getObject("TrackpadHorizScroll") );
    OSNumber * vscroll  = OSDynamicCast( OSNumber, dict->getObject("TrackpadScroll") );
    OSNumber * eaccell  = OSDynamicCast( OSNumber, dict->getObject("HIDTrackpadScrollAcceleration") );

    OSCollectionIterator* iter = OSCollectionIterator::withCollection( dict );
    OSObject* obj;
    
    iter->reset();
    while ((obj = iter->getNextObject()) != NULL)
    {
        OSString* str = OSDynamicCast( OSString, obj );
        OSNumber* val = OSDynamicCast( OSNumber, dict->getObject( str ) );
        
        if (val)
            IOLog("%s: Dictionary Object: %s Value: %d\n", getName(), 
                str->getCStringNoCopy(), val->unsigned32BitValue());
        else
            IOLog("%s: Dictionary Object: %s Value: ??\n", getName(), 
                str->getCStringNoCopy());
    }
    if ( clicking )
    {    
        UInt8  newModeByteValue = clicking->unsigned32BitValue() & 0x1 ?
                                  kTapEnabled :
                                  0;

        if (_touchPadModeByte != newModeByteValue)
        {
            _touchPadModeByte = newModeByteValue;
			setTapEnable(_touchPadModeByte);
			setProperty("Clicking", clicking);
			setAbsoluteMode(); //restart the mouse...
        }
    }

	if (dragging)
	{
		_dragging = dragging->unsigned32BitValue() & 0x1 ? true : false;
		setProperty("Dragging", dragging);
	}

	if (draglock)
	{
		_draglock = draglock->unsigned32BitValue() & 0x1 ? true : false;
		setProperty("DragLock", draglock);
	}

    if (hscroll)
    {
        _edgehscroll = hscroll->unsigned32BitValue() & 0x1 ? true : false;
        setProperty("TrackpadHorizScroll", hscroll);
    }

    if (vscroll)
    {
        _edgevscroll = vscroll->unsigned32BitValue() & 0x1 ? true : false;
        setProperty("TrackpadScroll", vscroll);
        }
    if (eaccell)
    {
        _edgeaccell = eaccell->unsigned32BitValue();
        _edgeaccellvalue = (((double)(_edgeaccell / 1966.08)) / 75.0); 
        _edgeaccellvalue = _edgeaccellvalue == 0 ? 0.01 : _edgeaccellvalue;
        setProperty("HIDTrackpadScrollAcceleration", eaccell);
    }

    return super::setParamProperties(dict);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2ALPSGlidePoint::setDevicePowerState( UInt32 whatToDo )
{
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            
            //
            // Disable touchpad.
            //

            setTouchPadEnable( false );
            break;

        case kPS2C_EnableDevice:

            setTapEnable( _touchPadModeByte );


            //
            // Enable the mouse clock (should already be so) and the
            // mouse IRQ line.
            //

            setCommandByte( kCB_EnableMouseIRQ, kCB_DisableMouseClock );

            //
            // Finally, we enable the trackpad itself, so that it may
            // start reporting asynchronous events.
            //
			setAbsoluteMode();
            setTouchPadEnable( true );
            break;
	}
}

void ApplePS2ALPSGlidePoint::getStatus(ALPSStatus_t *status)
{
    PS2Request * request = _device->allocateRequest();

    if ( !request ) 
        return;

    // (read command byte)
	request->commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_GetMouseInformation;
    request->commands[4].command  = kPS2C_ReadDataPort;
    request->commands[4].inOrOut  = 0;
    request->commands[5].command = kPS2C_ReadDataPort;
    request->commands[5].inOrOut = 0;
    request->commands[6].command = kPS2C_ReadDataPort;
    request->commands[6].inOrOut = 0;
	
    request->commandsCount = 7;
    _device->submitRequestAndBlock(request);
	
	status->byte0 = request->commands[4].inOrOut;
	status->byte1 = request->commands[5].inOrOut;
	status->byte2 = request->commands[6].inOrOut;
	
    DEBUG_LOG("getStatus(): [%02x %02x %02x]\n", status->byte0, status->byte1,
        status->byte2);
	
	_device->freeRequest(request);
}

void ApplePS2ALPSGlidePoint::getModel(ALPSStatus_t *E6,ALPSStatus_t *E7)
{
	PS2Request * request = _device->allocateRequest();

	if ( !request )
        return;
    
    // "E6 report"
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetMouseResolution;
	request->commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
	request->commands[1].inOrOut = 0;

    // 3X set mouse scaling 1 to 1
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = kDP_SetMouseScaling1To1;
    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseScaling1To1;
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = kDP_SetMouseScaling1To1;
    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = kDP_GetMouseInformation;
	
    request->commands[6].command  = kPS2C_ReadDataPort;
    request->commands[6].inOrOut  = 0;
    request->commands[7].command = kPS2C_ReadDataPort;
    request->commands[7].inOrOut = 0;
    request->commands[8].command = kPS2C_ReadDataPort;
    request->commands[8].inOrOut = 0;
	
	request->commandsCount = 9;
    _device->submitRequestAndBlock(request);
	
    // result is "E6 report"
	E6->byte0 = request->commands[6].inOrOut;
	E6->byte1 = request->commands[7].inOrOut;
	E6->byte2 = request->commands[8].inOrOut;
    _device->freeRequest(request);
	
    request = _device->allocateRequest();
    if (!request)
        return;

    // Now fetch "E7 report"
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetMouseResolution;
	request->commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
	request->commands[1].inOrOut = 0;
	
    // 3X set mouse scaling 2 to 1
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = kDP_SetMouseScaling2To1;
    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseScaling2To1;
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = kDP_SetMouseScaling2To1;
    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = kDP_GetMouseInformation;
	
	
    request->commands[6].command  = kPS2C_ReadDataPort;
    request->commands[6].inOrOut  = 0;
    request->commands[7].command = kPS2C_ReadDataPort;
    request->commands[7].inOrOut = 0;
    request->commands[8].command = kPS2C_ReadDataPort;
    request->commands[8].inOrOut = 0;
	
	request->commandsCount = 9;
    _device->submitRequestAndBlock(request);

	E7->byte0 = request->commands[6].inOrOut;
	E7->byte1 = request->commands[7].inOrOut;
	E7->byte2 = request->commands[8].inOrOut;

	_device->freeRequest(request);
}

void ApplePS2ALPSGlidePoint::setAbsoluteMode() {
    PS2Request * request = _device->allocateRequest();

    if ( !request )
        return;

    // (read command byte)
	request->commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut = kDP_SetDefaultsAndDisable;
    request->commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut = kDP_SetDefaultsAndDisable;
	request->commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
	request->commands[4].inOrOut = kDP_Enable;
	request->commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
	request->commands[5].inOrOut = 0xF0; //Set poll ??!
	
    request->commandsCount = 6;
    _device->submitRequestAndBlock(request);
	
	_device->freeRequest(request);
}
