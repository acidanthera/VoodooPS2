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

// enable for trackpad debugging
#ifdef DEBUG_MSG
#define DEBUG_VERBOSE
#endif

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
	
    if (!super::init(properties))  return false;

    _device = NULL;
    _interruptHandlerInstalled = false;
    _powerControlHandlerInstalled = false;
    _messageHandlerInstalled = false;
    _packetByteCount = 0;
    _resolution = (2300) << 16; // (230 dpi, 4 counts/mm)
    _touchPadModeByte = 0x80; //default: absolute, low-rate, no w-mode

    // set defaults for configuration items
    
	z_finger=30;
	divisor=1;
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
	maxtaptime=130000000;
	maxdragtime=300000000;
	hsticky=0;
	vsticky=0;
	wsticky=0;
	tapstable=1;
	wlimit=9;
	wvdivisor=30;
	whdivisor=30;
	clicking=true;
	dragging=true;
	draglock=false;
	hscroll=false;
	scroll=true;
    outzone_wt = palm = palm_wt = false;
    zlimit = 100;
    noled = false;
    maxaftertyping = 500000000;
    mouseyinverter = 1;   // 1 for normal, -1 for inverting
    wakedelay = 1000;
    skippassthru = false;
    tapthreshx = tapthreshy = 50;
    dblthreshx = dblthreshy = 100;
    zonel = 1700;  zoner = 5200;
    zonet = 99999; zoneb = 0;

    // intialize state
    
	lastx=0;
	lasty=0;
	xrest=0;
	yrest=0;
	scrollrest=0;
	xmoved=ymoved=xscrolled=yscrolled=0;
    touchtime=untouchtime=0;
	wasdouble=false;
    keytime = 0;
    ignoreall = false;
    passbuttons = 0;
    passthru = false;
    ledpresent = false;
    
	touchmode=MODE_NOTOUCH;
    
	IOLog ("VoodooPS2SynapticsTouchPad Version 1.7.5 loaded...\n");
    
	setProperty ("Revision", 24, 32);
    
	inited=0;
	OSDictionary* pdict = OSDynamicCast(OSDictionary, properties->getObject("Configuration"));
	if (NULL != pdict)
		setParamProperties(pdict);
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
   
    if (!super::probe(provider, score))
        return 0;

    _device  = (ApplePS2MouseDevice*)provider;
    
    // for diagnostics...
    UInt8 buf3[3];
    bool success = getTouchPadData(0, buf3);
    if (!success)
    {
        IOLog("VoodooPS2Trackpad: Identify TouchPad command failed\n");
    }
    else
    {
        DEBUG_LOG("VoodooPS2Trackpad: Identify bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        if (0x46 != buf3[1] && 0x47 != buf3[1])
        {
            IOLog("VoodooPS2Trackpad: Identify TouchPad command returned incorrect byte 2 (of 3): 0x%02x\n", buf3[1]);
        }
        _touchPadType = buf3[1];
    }
    
    if (success)
    {
        // some synaptics touchpads return 0x46 in byte2 and have a different numbering scheme
        // this is all experimental for those touchpads
        
        // most synaptics touchpads return 0x47, and we only support v4.0 or better
        // in the case of 0x46, we allow versions as low as v2.0
        
        _touchPadVersion = (buf3[2] & 0x0f) << 8 | buf3[0];
        if (0x47 == buf3[1])
        {
            // for diagnostics...
            if ( _touchPadVersion < 0x400)
            {
                IOLog("VoodooPS2Trackpad: TouchPad(0x47) v%d.%d is not supported\n",
                      (UInt8)(_touchPadVersion >> 8), (UInt8)(_touchPadVersion));
            }
            // Only support 4.x or later touchpads.
            success = _touchPadVersion >= 0x400;
        }
        if (0x46 == buf3[1])
        {
            // for diagnostics...
            if ( _touchPadVersion < 0x200)
            {
                IOLog("VoodooPS2Trackpad: TouchPad(0x46) v%d.%d is not supported\n",
                      (UInt8)(_touchPadVersion >> 8), (UInt8)(_touchPadVersion));
            }
            // Only support 2.x or later touchpads.
            success = _touchPadVersion >= 0x200;
            
            //REVIEW: led might cause problems for this kind of touchpad
            noled = true;
            //REVIEW: this touchpad might require longer time to wake up, try it...
            if (1000 == wakedelay)
                wakedelay = 2000;
        }

        // deal with pass through capability
        if (!skippassthru)
        {
            // see if guest device for pass through is present
            UInt8 passthru1 = 0, passthru2 = 0;
            if (getTouchPadData(0x1, buf3))
            {
                // first byte, bit 0 indicates guest present
                passthru1 = buf3[0] & 0x01;
            }
            // query capabilities to find out about pass through
            if (getTouchPadData(0x2, buf3))
            {
                // trackpad must have both guest present and pass through capability
                passthru2 = buf3[2] >> 7;
            }
            passthru = passthru1 & passthru2;
            DEBUG_LOG("VoodooPS2Trackpad: passthru1=%d, passthru2=%d, passthru=%d\n", passthru1, passthru2, passthru);
        }
        
        // deal with LED capability
        if (getTouchPadData(0x9, buf3))
        {
            ledpresent = (buf3[0] >> 6) & 1;
            DEBUG_LOG("VoodooPS2Trackpad: ledpresent=%d\n", ledpresent);
        }
        
#ifdef DEBUG_VERBOSE
        if (getTouchPadData(0x1, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Mode/model($1) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0x2, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Capabilities($2) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        // now gather some more information about the touchpad
        if (getTouchPadData(0x3, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Model ID($3) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0x6, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: SN Prefix($6) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0x7, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: SN Suffix($7) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0x8, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Resolutions($8) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0x9, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Extended Model($9) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0xd, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Maximum coords($D) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0xe, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Deluxe LED bytes($E) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0xf, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Minimum coords bytes($F) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
#endif
    }
    
    _device = NULL;

    return success ? this : 0;
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

    IOLog("VoodooPS2Trackpad starting: Synaptics TouchPad reports type 0x%02x, version %d.%d\n",
          _touchPadType, (UInt8)(_touchPadVersion >> 8), (UInt8)(_touchPadVersion));

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
    setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
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

	_device->installPowerControlAction( this,
        OSMemberFunctionCast(PS2PowerControlAction, this, &ApplePS2SynapticsTouchPad::setDevicePowerState) );
	_powerControlHandlerInstalled = true;
    
    //
    // Install message hook for keyboard to trackpad communication
    //
    
    _device->installMessageAction( this,
        OSMemberFunctionCast(PS2MessageAction, this, &ApplePS2SynapticsTouchPad::receiveMessage));
    _messageHandlerInstalled = true;
    
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

    if (_interruptHandlerInstalled)
    {
        _device->uninstallInterruptAction();
        _interruptHandlerInstalled = false;
    }

    //
    // Uninstall the power control handler.
    //

    if (_powerControlHandlerInstalled)
    {
        _device->uninstallPowerControlAction();
        _powerControlHandlerInstalled = false;
    }
    
    //
    // Uinstall message handler.
    //
    if (_messageHandlerInstalled)
    {
        _device->uninstallMessageAction();
        _messageHandlerInstalled = false;
    }

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
    dispatchRelativePointerEventWithPacket(UInt8 * packet, UInt32  packetSize)
{
    // Note: This is the three byte relative format packet. Which pretty
    //  much is not used.  I kept it here just for reference.
    // This is a "mouse compatible" packet.
    //
    //      7  6  5  4  3  2  1  0
    //     -----------------------
    // [0] YO XO YS XS  1  M  R  L  (Y/X overflow, Y/X sign, buttons)
    // [1] X7 X6 X5 X4 X3 X3 X1 X0  (X delta)
    // [2] Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (Y delta)
    // optional 4th byte for 5-button wheel mouse
    // [3]  0  0 B5 B4 Z3 Z2 Z1 Z0  (B4,B5 buttons, Z=wheel)

    // Here is the format of the 6-byte absolute format packet.
    // This is with wmode on, which is pretty much what this driver assumes.
    // This is a "trackpad specific" packet.
    //
    //      7  6  5  4  3  2  1  0
    //    -----------------------
    // [0]  1  0 W3 W2  0 W1  R  L  (W bits 3..2, W bit 1, R/L buttons)
    // [1] YB YA Y9 Y8 XB XA X9 X8  (Y bits 11..8, X bits 11..8)
    // [2] Z7 Z6 Z5 Z4 Z3 Z2 Z1 Z0  (Z-pressure, bits 7..0)
    // [3]  1  1 YC XC  0 W0 RD LD  (Y bit 12, X bit 12, W bit 0, RD/LD)
    // [4] X7 X6 X5 X4 X3 X2 X1 X0  (X bits 7..0)
    // [5] Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (Y bits 7..0)
    
    // This is the format of the 6-byte encapsulation packet.
    // Encapsulation packets are used for PS2 pass through mode, which
    // allows another PS2 device to be connected as a slave to the
    // touchpad.  The touchpad acts as a host for the second evice
    // and forwards packets with a special value for w (w=3)
    // So when w=3 (W3=0,W2=0,W1=1,W0=1), this is what the packets
    // look like.
    //
    //      7  6  5  4  3  2  1  0
    //    -----------------------
    // [0]  1  0  0  0  0  1  R  L  (R/L are for touchpad)
    // [1] YO XO YS XS  1  M  R  L  (packet byte 0, Y/X overflow, Y/X sign, buttons)
    // [2]  0  0 B5 B4 Z3 Z2 Z1 Z0  (packet byte 3, B4,B5 buttons, Z=wheel)
    // [3]  1  1  x  x  0  1  R  L  (x=reserved, R/L are for touchpad)
    // [4] X7 X6 X5 X4 X3 X3 X1 X0  (packet byte 1, X delta)
    // [5] Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (packet byte 2, Y delta)

	uint64_t now;
	clock_get_uptime(&now);

    //
    // Parse the packet
    //

	int w = ((packet[3]&0x4)>>2)|((packet[0]&0x4)>>1)|((packet[0]&0x30)>>2);
    UInt32 buttons = packet[0] & 0x03; // mask for just R L
    
    // deal with pass through packet
    if (passthru && 3 == w)
    {
        passbuttons = packet[1] & 0x3; // mask for just R L
        buttons |= passbuttons;
        SInt32 dx = ((packet[1] & 0x10) ? 0xffffff00 : 0 ) | packet[4];
        SInt32 dy = -(((packet[1] & 0x20) ? 0xffffff00 : 0 ) | packet[5]);
        dispatchRelativePointerEvent(dx, mouseyinverter*dy, buttons, now);
#ifdef DEBUG_VERBOSE
        IOLog("ps2: passthru packet dx=%d, dy=%d, buttons=%d\n", dx, mouseyinverter*dy, buttons);
#endif
        return;
    }
    
    // if trackpad input is supposed to be ignored, then don't do anything
    if (ignoreall)
    {
        return;
    }
    
    // otherwise, deal with touchpad packet
	int x = packet[4]|((packet[1]&0x0f)<<8)|((packet[3]&0x10)<<8);
	int y = packet[5]|((packet[1]&0xf0)<<4)|((packet[3]&0x20)<<7);
	int z = packet[2];
   
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    buttons |= passbuttons;
    
    // unsmooth input (probably just for testing)
    // by default the trackpad itself does a simple decaying average (1/2 each)
    // we can undo it here
    if (unsmoothinput)
    {
        if (!isTouchMode() && isFingerTouch(z))
        {
            x_undo.clear();
            y_undo.clear();
        }
        x = x_undo.filter(x);
        y = y_undo.filter(y);
    }
    
    // smooth input by unweighted average
    if (smoothinput)
    {
        if (!isTouchMode() && isFingerTouch(z))
        {
            x_avg.clear();
            y_avg.clear();
        }
        x = x_avg.filter(x);
        y = y_avg.filter(y);
    }
    
    // deal with "OutsidezoneNoAction When Typing"
    if (outzone_wt && z>z_finger && now-keytime < maxaftertyping &&
        (x < zonel || x > zoner || y < zoneb || y > zonet))
    {
        // touch input was shortly after typing and outside the "zone"
        // ignore it...
        return;
    }
    
#ifdef DEBUG_VERBOSE
    int tm1 = touchmode;
#endif
    
	if (z<z_finger && isTouchMode())
	{
		xrest=yrest=scrollrest=0;
		untouchtime=now;
        DEBUG_LOG("ps2: now-touchtime=%lld (%s)\n", (uint64_t)(now-touchtime)/1000, now-touchtime < maxtaptime?"true":"false");
		if (now-touchtime < maxtaptime && clicking)
        {
			switch (touchmode)
			{
				case MODE_DRAG:
                    //REVIEW: not quite sure why sending button down here because if we
                    // are in MODE_DRAG it should have already been sent, right?
					buttons&=~0x3;
					dispatchRelativePointerEvent(0, 0, buttons|0x1, now);
					dispatchRelativePointerEvent(0, 0, buttons, now);
					if (wasdouble && rtap)
						buttons|=0x2;
					else
						buttons|=0x1;
					touchmode=MODE_NOTOUCH;
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
						touchmode=dragging ? MODE_PREDRAG : MODE_NOTOUCH;
					}
                    break;
			}
        }
		else
		{
			xmoved=ymoved=xscrolled=yscrolled=0;
			if ((touchmode==MODE_DRAG || touchmode==MODE_DRAGLOCK) && draglock)
				touchmode=MODE_DRAGNOTOUCH;
			else
				touchmode=MODE_NOTOUCH;
		}
		wasdouble=false;
	}
    
    // cancel pre-drag mode if second tap takes too long
	if (touchmode==MODE_PREDRAG && now-untouchtime >= maxdbltaptime)
		touchmode=MODE_NOTOUCH;
    
    // cancel tap if touch point moves too far
    if (isTouchMode() && isFingerTouch(z))
    {
        int dx = x > touchx ? x - touchx : touchx - x;
        int dy = y > touchy ? y - touchy : touchy - y;
        if (!wasdouble && (dx > tapthreshx || dy > tapthreshy))
            touchtime = 0;
        else if (dx > dblthreshx || dy > dblthreshy)
            touchtime = 0;
    }

#ifdef DEBUG_VERBOSE
    int tm2 = touchmode;
    int dx = 0, dy = 0;
#endif
    
	switch (touchmode)
	{
		case MODE_DRAG:
		case MODE_DRAGLOCK:
			buttons|=0x1;
            // fall through
		case MODE_MOVE:
			if (!divisor)
				break;
            if (w>wlimit || z>zlimit)
                break;
#ifdef DEBUG_VERBOSE
            dx = x-lastx+xrest;
            dy = lasty-y+yrest;
#endif
			dispatchRelativePointerEvent((x-lastx+xrest)/divisor, (lasty-y+yrest)/divisor, buttons, now);
            //REVIEW: why add this up?  it has already been dispatched...
			//xmoved+=(x-lastx+xrest)/divisor;
			//ymoved+=(lasty-y+yrest)/divisor;
			xrest=(x-lastx+xrest)%divisor;
			yrest=(lasty-y+yrest)%divisor;
			break;
            
		case MODE_MTOUCH:
            if (w>wlimit || z>zlimit)
                break;
			if (!wsticky && w<wlimit && w>3)
			{
				touchmode=MODE_MOVE;
				break;
			}
            if (now-keytime < maxaftertyping)
                break;
			dispatchScrollWheelEvent(wvdivisor?(y-lasty+yrest)/wvdivisor:0,
									 (whdivisor&&hscroll)?(lastx-x+xrest)/whdivisor:0, 0, now);
            //REVIEW: same question as xmoved/ymoved above
			//xscrolled+=wvdivisor?(y-lasty+yrest)/wvdivisor:0;
			//yscrolled+=whdivisor?(lastx-x+xrest)/whdivisor:0;
			xrest=whdivisor?(lastx-x+xrest)%whdivisor:0;
			yrest=wvdivisor?(y-lasty+yrest)%wvdivisor:0;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;
			
        case MODE_VSCROLL:
			if (!vsticky && (x<redge || w>wlimit || z>zlimit))
			{
				touchmode=MODE_MOVE;
				break;
			}
            if (now-keytime < maxaftertyping)
                break;
			dispatchScrollWheelEvent((y-lasty+scrollrest)/vscrolldivisor, 0, 0, now);
			//xscrolled+=(y-lasty+scrollrest)/vscrolldivisor;
			scrollrest=(y-lasty+scrollrest)%vscrolldivisor;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;
            
		case MODE_HSCROLL:
			if (!hsticky && (y>bedge || w>wlimit || z>zlimit))
			{
				touchmode=MODE_MOVE;
				break;
			}			
            if (now-keytime < maxaftertyping)
                break;
			dispatchScrollWheelEvent(0,(lastx-x+scrollrest)/hscrolldivisor, 0, now);
			//yscrolled+=(lastx-x+scrollrest)/hscrolldivisor;
			scrollrest=(lastx-x+scrollrest)%hscrolldivisor;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;
            
		case MODE_CSCROLL:
            if (now-keytime < maxaftertyping)
                break;
            //REVIEW: what is "circular scroll"
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
                //xscrolled+=(mov+scrollrest)/cscrolldivisor;
                scrollrest=(mov+scrollrest)%cscrolldivisor;
            }
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;			

            
		case MODE_DRAGNOTOUCH:
            buttons |= 0x1;
            // fall through
		case MODE_PREDRAG:
            if (now-keytime >= maxaftertyping)
                buttons |= 0x1;
		case MODE_NOTOUCH:
            //REVIEW: what is "StabilizeTapping" (tapstable) supposed to do???
			//if (!tapstable)
			//	xmoved=ymoved=xscrolled=yscrolled=0;
            //if (now-keytime > maxaftertyping)
            //  _dispatchScrollWheelEvent(-xscrolled, -yscrolled, 0, now);
			dispatchRelativePointerEvent(-xmoved, -ymoved, buttons, now);
			xmoved=ymoved=xscrolled=yscrolled=0;
			break;
	}
    
    // always save last seen position for calculating deltas later
	lastx=x;
	lasty=y;

    // capture time of tap, and watch for double tap
	if (now-keytime >= maxaftertyping && isFingerTouch(z))
    {
        if (!isTouchMode())
        {
            touchtime=now;
            touchx=x;
            touchy=y;
        }
        if (w>=wlimit || w<3)
            wasdouble=true;
    }

    // switch modes, depending on input
	if (touchmode==MODE_PREDRAG && isFingerTouch(z))
		touchmode=MODE_DRAG;
	if (touchmode==MODE_DRAGNOTOUCH && isFingerTouch(z))
		touchmode=MODE_DRAGLOCK;
	if ((w>=wlimit || w<3) && isFingerTouch(z) && scroll && (wvdivisor || (hscroll && whdivisor)))
		touchmode=MODE_MTOUCH;
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
    
#ifdef DEBUG_VERBOSE
    int tm3 = touchmode;
#endif
    
#ifdef DEBUG_VERBOSE
    IOLog("ps2: dx=%d, dy=%d (%d,%d) z=%d w=%d mode=(%d,%d,%d) buttons=%d wasdouble=%d\n", dx, dy, x, y, z, w, tm1, tm2, tm3, buttons, wasdouble);
#endif
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

bool ApplePS2SynapticsTouchPad::getTouchPadData(UInt8 dataSelector, UInt8 buf3[])
{
    PS2Request * request = _device->allocateRequest();
    bool success = false;

    if ( !request ) return false;

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
    request->commands[13].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[13].inOrOut = kDP_SetDefaultsAndDisable;
    request->commandsCount = 14;
    _device->submitRequestAndBlock(request);

    if (request->commandsCount == 14) // success?
    {
        success = true;
        buf3[0] = request->commands[10].inOrOut;
        buf3[1] = request->commands[11].inOrOut;
        buf3[2] = request->commands[12].inOrOut;
    }

    _device->freeRequest(request);

    return success;
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
	if (NULL == config)
		return 0;
    
	const struct {const char *name; int *var;} int32vars[]={
		{"FingerZ",							&z_finger},
		{"Divisor",							&divisor},
		{"RightEdge",						&redge},
		{"LeftEdge",						&ledge},
		{"TopEdge",							&tedge},
		{"BottomEdge",						&bedge},
		{"VerticalScrollDivisor",			&vscrolldivisor},
		{"HorizontalScrollDivisor",			&hscrolldivisor},
		{"CircularScrollDivisor",			&cscrolldivisor},
		{"CenterX",							&centerx},
		{"CenterY",							&centery},
		{"CircularScrollTrigger",			&ctrigger},
		{"MultiFingerWLimit",				&wlimit},
		{"MultiFingerVerticalDivisor",		&wvdivisor},
		{"MultiFingerHorizontalDivisor",	&whdivisor},
        {"ZLimit",                          &zlimit},
        {"MouseYInverter",                  &mouseyinverter},
        {"WakeDelay",                       &wakedelay},
        {"TapThresholdX",                   &tapthreshx},
        {"TapThresholdY",                   &tapthreshy},
        {"DoubleTapThresholdX",             &dblthreshx},
        {"DoubleTapThresholdY",             &dblthreshy},
        {"ZoneLeft",                        &zonel},
        {"ZoneRight",                       &zoner},
        {"ZoneTop",                         &zonet},
        {"ZoneBottom",                      &zoneb},
	};
	const struct {const char *name; int *var;} boolvars[]={
		{"StickyHorizontalScrolling",		&hsticky},
		{"StickyVerticalScrolling",			&vsticky},
		{"StickyMultiFingerScrolling",		&wsticky},
		{"StabilizeTapping",				&tapstable},
        {"DisableLEDUpdate",                &noled},
        {"SmoothInput",                     &smoothinput},
        {"UnsmoothInput",                   &unsmoothinput},
        {"SkipPassThrough",                 &skippassthru},
	};
    const struct {const char* name; bool* var;} lowbitvars[]={
        {"TrackpadRightClick",              &rtap},
        {"Clicking",                        &clicking},
        {"Dragging",                        &dragging},
        {"DragLock",                        &draglock},
        {"TrackpadHorizScroll",             &hscroll},
        {"TrackpadScroll",                  &scroll},
        {"OutsidezoneNoAction When Typing", &outzone_wt},
        {"PalmNoAction Permanent",          &palm},
        {"PalmNoAction When Typing",        &palm_wt},
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
        {"MaxDragTime",                     &maxdragtime},
        {"MaxTapTime",                      &maxtaptime},
        {"HIDClickTime",                    &maxdbltaptime},
        {"QuietTimeAfterTyping",            &maxaftertyping},
    };
    
	uint8_t oldmode=_touchPadModeByte;
    // highrate?
	OSBoolean *bl;
	if ((bl=OSDynamicCast (OSBoolean, config->getObject ("UseHighRate"))))
    {
		if (bl->isTrue())
			_touchPadModeByte |= 1<<6;
		else
			_touchPadModeByte &=~(1<<6);
    }
    
	OSNumber *num;
    // 64-bit config items
    for (int i = 0; i < countof(int64vars); i++)
        if ((num=OSDynamicCast(OSNumber, config->getObject(int64vars[i].name))))
            *int64vars[i].var = num->unsigned64BitValue();
    // boolean config items
	for (int i = 0; i < countof(boolvars); i++)
		if ((bl=OSDynamicCast (OSBoolean,config->getObject (boolvars[i].name))))
			*boolvars[i].var = bl->isTrue();
    // 32-bit config items
	for (int i = 0; i < countof(int32vars);i++)
		if ((num=OSDynamicCast (OSNumber,config->getObject (int32vars[i].name))))
			*int32vars[i].var = num->unsigned32BitValue();
    // lowbit config items
	for (int i = 0; i < countof(lowbitvars); i++)
		if ((num=OSDynamicCast (OSNumber,config->getObject(lowbitvars[i].name))))
			*lowbitvars[i].var = (num->unsigned32BitValue()&0x1)?true:false;
    
    // special case for HIDClickTime (which is really max time for a double-click)
    // we can let it go no more than maxdragtime because otherwise taps on
    // the menu bar take too long if drag mode is enabled.  The code in that case
    // has to "hold button 1 down" for the duration of maxdbltaptime because if
    // it didn't then dragging on the caption of a window will not work
    // (some other apps too) because these apps will see a double tap+hold as
    // a single click, then double click and they don't go into drag mode when
    // initiated with a double click.
    //
    // this all happens during MODE_PREDRAG
    //
    // summary:
    //  if the code releases button 1 after a tap, then dragging windows
    //    breaks
    //  if the maxdbltaptime is too large (200ms is small enough, 500ms is too large)
    //    then clicking on menus breaks because the system sees it as a long
    //    press and hold
    //
    // fyi:
    //  also tried to allow release of button 1 during MODE_PREDRAG, and then when
    //   attempting to initiate the drag (in the case the second touch comes soon
    //   enough), modifying the time such that it is not seen as a double tap.
    //  unfortunately, that destroys double tap as well, probably because the
    //   system is confused seeing input "out of order"
    
    if (maxdbltaptime > maxdragtime)
        maxdbltaptime = maxdragtime;

	// wmode?
	if (whdivisor || wvdivisor)
		_touchPadModeByte |= 1<<0;
	else
		_touchPadModeByte &=~(1<<0);

	// if changed, setup touchpad mode
	if (_touchPadModeByte!=oldmode && inited)
    {
		setTouchPadModeByte (_touchPadModeByte);
        _packetByteCount=0;
    }
    
	touchmode=MODE_NOTOUCH;
    
    // 64-bit config items
	for (int i = 0; i < countof(int64vars); i++)
		setProperty(int64vars[i].name, *int64vars[i].var, 64);
    // bool config items
	for (int i = 0; i < countof(boolvars); i++)
		setProperty(boolvars[i].name, *boolvars[i].var ? kOSBooleanTrue : kOSBooleanFalse);
	// 32-bit config items
	for (int i = 0; i < countof(int32vars); i++)
		setProperty(int32vars[i].name, *int32vars[i].var, 32);
    // lowbit config items
	for (int i = 0; i < countof(lowbitvars); i++)
		setProperty(lowbitvars[i].name, *lowbitvars[i].var ? 1 : 0, 32);
    
    // others
	setProperty("UseHighRate", _touchPadModeByte & (1 << 6) ? kOSBooleanTrue : kOSBooleanFalse);

    return super::setParamProperties(config);
}

IOReturn ApplePS2SynapticsTouchPad::setProperties(OSObject *props)
{
	OSDictionary *pdict = OSDynamicCast(OSDictionary, props);
    if (NULL == pdict)
        return kIOReturnError;
    
	return setParamProperties (pdict);
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

            IOSleep(wakedelay);

            if (0x46 == _touchPadType)
            {
                //REVIEW: special for type 0x46 synaptics... maybe this will help wake it up...
                setTouchPadEnable(false);
            }

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
            
            // clear passbuttons, just in case buttons were down when system
            // went to sleep (now just assume they are up)
            passbuttons = 0;

            //
            // Finally, we enable the trackpad itself, so that it may
            // start reporting asynchronous events.
            //

            setTouchPadEnable( true );
            
            //
            // Set LED state as it is lost after sleep
            //
            
            //REVIEW: might want this test in updateTouchpadLED function
            if (!noled)
                updateTouchpadLED();
            
            break;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::receiveMessage(int message, void* data)
{
    //
    // Here is where we receive messages from the keyboard driver
    //
    // This allows for the keyboard driver to enable/disable the trackpad
    // when a certain keycode is pressed.
    //
    // It also allows the trackpad driver to learn the last time a key
    //  has been pressed, so it can implement various "ignore trackpad
    //  input while typing" options.
    //
    switch (message)
    {
        case kPS2M_getDisableTouchpad:
        {
            bool* pResult = (bool*)data;
            *pResult = !ignoreall;
            break;
        }
            
        case kPS2M_setDisableTouchpad:
        {
            bool enable = *((bool*)data);
            // ignoreall is true when trackpad has been disabled
            if (enable == ignoreall)
            {
                // save state, and update LED
                ignoreall = !enable;
                updateTouchpadLED();
            }
            break;
        }
            
        case kPS2M_notifyKeyPressed:
        {
            // just remember last time key pressed... this can be used in
            // interrupt handler to detect unintended input while typing
            PS2KeyInfo* pInfo = (PS2KeyInfo*)data;
            switch (pInfo->adbKeyCode)
            {
                // don't store key time for modifier keys going down
                case 0x38:  // left shift
                case 0x3c:  // right shift
                case 0x3b:  // left control
                case 0x3e:  // right control
                case 0x3a:  // left alt
                case 0x3d:  // right alt
                    if (pInfo->goingDown)
                        break;
                default:
                    keytime = pInfo->time;
            }
            break;
        }
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// This code is specific to Synaptics Touchpads that have an LED indicator, but
//  it does no harm to Synaptics units that don't have an LED.
//
// Generally it is used to indicate that the touchpad has been made inactive.
//
// In the case of this package, we can disable the touchpad with both keyboard
//  and the touchpad itself.
//
// Linux sources were very useful in figuring this out...
// This patch to support HP Probook Synaptics LED in Linux was where found the
//  information:
// https://github.com/mmonaco/PKGBUILDs/blob/master/synaptics-led/synled.patch
//
// To quote from the email:
//
// From: Takashi Iwai <tiwai@suse.de>
// Date: Sun, 16 Sep 2012 14:19:41 -0600
// Subject: [PATCH] input: Add LED support to Synaptics device
//
// The new Synaptics devices have an LED on the top-left corner.
// This patch adds a new LED class device to control it.  It's created
// dynamically upon synaptics device probing.
//
// The LED is controlled via the command 0x0a with parameters 0x88 or 0x10.
// This seems only on/off control although other value might be accepted.
//
// The detection of the LED isn't clear yet.  It should have been the new
// capability bits that indicate the presence, but on real machines, it
// doesn't fit.  So, for the time being, the driver checks the product id
// in the ext capability bits and assumes that LED exists on the known
// devices.
//
// Signed-off-by: Takashi Iwai <tiwai@suse.de>
//

void ApplePS2SynapticsTouchPad::updateTouchpadLED()
{
    if (ledpresent)
        setTouchpadLED(ignoreall ? 0x88 : 0x10);
}

bool ApplePS2SynapticsTouchPad::setTouchpadLED(UInt8 touchLED)
{
    PS2Request * request = _device->allocateRequest();
    bool         success;
    
    if ( !request ) return false;
    
    // send NOP before special command sequence
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetMouseScaling1To1;
    
    // 4 set resolution commands, each encode 2 data bits of LED level
    request->commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut  = kDP_SetMouseResolution;
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = (touchLED >> 6) & 0x3;
    
    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseResolution;
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = (touchLED >> 4) & 0x3;
    
    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = kDP_SetMouseResolution;
    request->commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[6].inOrOut  = (touchLED >> 2) & 0x3;
    
    request->commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[7].inOrOut  = kDP_SetMouseResolution;
    request->commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[8].inOrOut  = (touchLED >> 0) & 0x3;
    
    // Set sample rate 10 (10 is command for setting LED)
    request->commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[9].inOrOut  = kDP_SetMouseSampleRate;
    request->commands[10].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[10].inOrOut = 10; // 0x0A command for setting LED
    
    // finally send NOP command to end the special sequence
    request->commands[11].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[11].inOrOut  = kDP_SetMouseScaling1To1;
    
    request->commandsCount = 12;
    _device->submitRequestAndBlock(request);
    
    success = (request->commandsCount == 12);
    
    _device->freeRequest(request);
    
    return success;
}

