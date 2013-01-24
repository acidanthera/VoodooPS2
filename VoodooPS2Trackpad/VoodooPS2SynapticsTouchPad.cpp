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

// enable for "Extended W Mode" support (secondary fingers, etc.)
#define EXTENDED_WMODE
#define SIMULATE_CLICKPAD

// enable for trackpad debugging
#ifdef DEBUG_MSG
//#define DEBUG_VERBOSE
#endif

#include <IOKit/assert.h>
#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
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

IOItemCount ApplePS2SynapticsTouchPad::buttonCount() { return _buttonCount; };
IOFixed     ApplePS2SynapticsTouchPad::resolution()  { return _resolution << 16; };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::init( OSDictionary * properties )
{
    //
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.
    //
	
    if (!super::init(properties))
        return false;

    _device = NULL;
    _interruptHandlerInstalled = false;
    _powerControlHandlerInstalled = false;
    _messageHandlerInstalled = false;
    _packetByteCount = 0;
    _touchPadModeByte = 0x80; //default: absolute, low-rate, no w-mode

    // set defaults for configuration items
    
	z_finger=45;
	divisorx=divisory=1;
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
	maxdragtime=230000000;
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
    draglocktemp=0;
	hscroll=false;
#ifdef EXTENDED_WMODE
    _supporteW=false;
    _extendedwmode=false;
#endif
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
    diszl = 0; diszr = 1700;
    diszt = 99999; diszb = 4200;
    diszctrl = 0;
    _resolution = 2300;
    _scrollresolution = 2300;
    swipedx = swipedy = 800;
    rczl = 3800; rczt = 2000;
    rczr = 99999; rczb = 0;
    _buttonCount = 2;
    swapdoubletriple = false;
    
    // intialize state
    
	lastx=0;
	lasty=0;
    lastf=0;
	xrest=0;
	yrest=0;
#ifdef EXTENDED_WMODE
    xrest2=0;
    yrest2=0;
    clickedprimary=false;
    lastx2=0;
    lasty2=0;
    tracksecondary=false;
#endif
	scrollrest=0;
    touchtime=untouchtime=0;
	wastriple=wasdouble=false;
    keytime = 0;
    ignoreall = false;
    passbuttons = 0;
    passthru = false;
    ledpresent = false;
    clickpadtype = 0;
    _clickbuttons = 0;
    _reportsv = false;
    mousecount = 0;
    usb_mouse_stops_trackpad = true;
    _controldown = 0;
    scrollzoommask = 0;
    
    inSwipeLeft=inSwipeRight=inSwipeDown=inSwipeUp=0;
    xmoved=ymoved=0;
    
    momentumscroll = true;
    scrollTimer = 0;
    momentumscrolltimer = 10000000;
    momentumscrollthreshy = 7;
    momentumscrollmultiplier = 98;
    momentumscrolldivisor = 100;
    momentumscrollcurrent = 0;
    
	touchmode=MODE_NOTOUCH;
    
	IOLog ("VoodooPS2SynapticsTouchPad Version 1.7.8 loaded...\n");
    
	setProperty ("Revision", 24, 32);
    
	OSDictionary* pdict = OSDynamicCast(OSDictionary, properties->getObject("Configuration"));
	if (NULL != pdict)
		setParamProperties(pdict);
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2SynapticsTouchPad *
ApplePS2SynapticsTouchPad::probe( IOService * provider, SInt32 * score )
{
    DEBUG_LOG("ApplePS2SynapticsTouchPad::probe entered...\n");
    
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
        }
        
        // get TouchPad general capabilities
        UInt8 buf3Caps[3];
        int nExtendedQueries = 0;
        if (!getTouchPadData(0x2, buf3Caps) || !(buf3Caps[0] & 0x80))
            buf3Caps[0] = 0, buf3Caps[2] = 0;
        // TouchPad supports (8 + nExtendedQueries)
        nExtendedQueries = (buf3Caps[0] & 0x70) >> 4;
        DEBUG_LOG("VoodooPS2Trackpad: nExtendedQueries=%d\n", nExtendedQueries);
        
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
            // trackpad must have both guest present and pass through capability
            passthru2 = buf3Caps[2] >> 7;
            passthru = passthru1 & passthru2;
            DEBUG_LOG("VoodooPS2Trackpad: passthru1=%d, passthru2=%d, passthru=%d\n", passthru1, passthru2, passthru);
        }

        // deal with LED capability
        if (nExtendedQueries >= 1 && getTouchPadData(0x9, buf3))
        {
            ledpresent = (buf3[0] >> 6) & 1;
            DEBUG_LOG("VoodooPS2Trackpad: ledpresent=%d\n", ledpresent);
        }
        
        // determine ClickPad type
        if (nExtendedQueries >= 4 && getTouchPadData(0xC, buf3))
        {
            clickpadtype = ((buf3[0] & 0x10) >> 4) | ((buf3[1] & 0x01) << 1);
#ifdef SIMULATE_CLICKPAD
            clickpadtype = 1;
#endif
            DEBUG_LOG("VoodooPS2Trackpad: clickpadtype=%d\n", clickpadtype);
            _reportsv = (buf3[1] >> 3) & 0x01;
            DEBUG_LOG("VoodooPS2Trackpad: _reportsv=%d\n", _reportsv);
        }
        
#ifdef EXTENDED_WMODE
        // deal with extended W mode
        if (getTouchPadData(0x2, buf3))
        {
            _supporteW= (buf3[0] >> 7) & 1;
            DEBUG_LOG("VoodooPS2Trackpad: _supporteW=%d\n", _supporteW);
        }
#endif
        
#ifdef DEBUG_VERBOSE
        // now gather some more information about the touchpad
        if (getTouchPadData(0x1, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Mode/model($1) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (getTouchPadData(0x2, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Capabilities($2) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
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
        if (nExtendedQueries >= 1 && getTouchPadData(0x9, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Extended Model($9) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (nExtendedQueries >= 4 && getTouchPadData(0xc, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Continued Capabilities($C) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (nExtendedQueries >= 5 && getTouchPadData(0xd, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Maximum coords($D) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (nExtendedQueries >= 6 && getTouchPadData(0xe, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Deluxe LED bytes($E) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
        if (nExtendedQueries >= 7 && getTouchPadData(0xf, buf3))
        {
            DEBUG_LOG("VoodooPS2Trackpad: Minimum coords bytes($F) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
        }
#endif
    }
    
    _device = NULL;

    DEBUG_LOG("ApplePS2SynapticsTouchPad::probe leaving.\n");
    
    return success ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::start( IOService * provider )
{ 
    //
    // The driver has been instructed to start. This is called after a
    // successful probe and match.
    //

    if (!super::start(provider))
        return false;

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
    // Advertise the current state of the tapping feature.
    //
    // Must add this property to let our superclass know that it should handle
    // trackpad acceleration settings from user space.  Without this, tracking
    // speed adjustments from the mouse prefs panel have no effect.
    //

    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
    setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDTrackpadScrollAccelerationKey);
	setProperty(kIOHIDScrollResolutionKey, _scrollresolution << 16, 32);
    
    //
    // Setup workloop and timer event source for scroll momentum
    //
    
    IOWorkLoop* pWorkLoop = getWorkLoop();
    if (pWorkLoop)
    {
        scrollTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2SynapticsTouchPad::onScrollTimer));
        if (scrollTimer)
            pWorkLoop->addEventSource(scrollTimer);
    }
    
    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //

    _device->installInterruptAction(this,
        OSMemberFunctionCast(PS2InterruptAction,this,&ApplePS2SynapticsTouchPad::interruptOccurred));
    _interruptHandlerInstalled = true;

    //
    // Set the touchpad mode byte, which will also...
    // Enable the mouse clock (should already be so) and the mouse IRQ line.
    // Enable the touchpad itself.
    //
#ifdef EXTENDED_WMODE
    if (_supporteW && _extendedwmode)
        _touchPadModeByte |= (1<<2);
    else
        _touchPadModeByte &= ~(1<<2);
#endif
    setTouchPadModeByte(_touchPadModeByte);

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
    
    //
    // Update LED -- it could have been disabled then computer was restarted
    //
    updateTouchpadLED();
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::stop( IOService * provider )
{
    //REVIEW: for some reason ::stop is never called, so this driver
    //  doesn't really like kextunload very much...
    
    //
    // The driver has been instructed to stop.  Note that we must break all
    // connections to other service objects now (ie. no registered actions,
    // no pointers and retains to objects, etc), if any.
    //

    assert(_device == provider);

    // free up timer for scroll momentum
    IOWorkLoop* pWorkLoop = getWorkLoop();
    if (pWorkLoop)
    {
        if (scrollTimer)
        {
            scrollTimer->release();
            scrollTimer = 0;
        }
    }
    
    //
    // turn off the LED just in case it was on
    //
    
    ignoreall = false;
    updateTouchpadLED();
    
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

void ApplePS2SynapticsTouchPad::onScrollTimer(void)
{
    //
    // This will be invoked by our workloop timer event source to implement
    // momentum scroll.
    //
    
    //REVIEW: need to adjust for correct "feel" (compare against real Mac)
    
    if (!momentumscrollcurrent)
        return;
    
    uint64_t now;
	clock_get_uptime(&now);
    
    int64_t dy64 = momentumscrollcurrent / (int64_t)momentumscrollinterval;
    int dy = (int)dy64;
    
    dy += momentumscrollrest2;
    dispatchScrollWheelEvent(wvdivisor ? dy / wvdivisor : 0, 0, 0, now);
    momentumscrollrest2 = wvdivisor ? dy % wvdivisor : 0;
    
    // adjust momentumscrollcurrent
    momentumscrollcurrent *= momentumscrollmultiplier;
    momentumscrollcurrent += momentumscrollrest1;
    momentumscrollrest1 %= momentumscrolldivisor;
    momentumscrollcurrent /= momentumscrolldivisor;
    
    // determine next to see if below threshhold
    int64_t dy_next64 = momentumscrollcurrent / (int64_t)momentumscrollinterval;
    int dy_next = (int)dy_next64;
    
    if (dy_next > momentumscrollthreshy || dy_next < -momentumscrollthreshy)
        scrollTimer->setTimeout(momentumscrolltimer);
    else
        momentumscrollcurrent = 0;
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
        dispatchEventsWithPacket(_packetBuffer, 6);
        _packetByteCount = 0;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::dispatchEventsWithPacket(UInt8* packet, UInt32 packetSize)
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
    
#ifdef EXTENDED_WMODE
    if (_extendedwmode && 2 == w)
    {
        // deal with extended W mode encapsulated packet
        dispatchEventsWithPacketEW(packet, packetSize);
        return;
    }
#endif
    
#ifdef SIMULATE_CLICKPAD
    packet[3] &= ~0x3;
    packet[3] |= packet[0] & 0x1;
    packet[0] &= ~0x3;
#endif
    
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
    
    // otherwise, deal with normal wmode touchpad packet
    int xraw = packet[4]|((packet[1]&0x0f)<<8)|((packet[3]&0x10)<<8);
    int yraw = packet[5]|((packet[1]&0xf0)<<4)|((packet[3]&0x20)<<7);
    int z = packet[2];
    int f = z>z_finger ? w>=4 ? 1 : w+2 : 0;   // number of fingers
#ifdef EXTENDED_WMODE
    int v = w;  //REVIEW: v is not currently used... but maybe should be using it
    if (_extendedwmode && _reportsv && f > 1)
    {
        // in extended w mode, v field (width) is encoded in x & y & z, with multifinger
        v = (((xraw & 0x2)>>1) | ((yraw & 0x2)) | ((z & 0x1)<<2)) + 8;
        xraw &= ~0x2;
        yraw &= ~0x2;
        z &= ~0x1;
    }
#endif
    int x = xraw;
    int y = yraw;
    
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    buttons |= passbuttons;

    // unsmooth input (probably just for testing)
    // by default the trackpad itself does a simple decaying average (1/2 each)
    // we can undo it here
    if (unsmoothinput)
    {
        x = x_undo.filter(x, f);
        y = y_undo.filter(y, f);
    }
    
    // smooth input by unweighted average
    if (smoothinput)
    {
        x = x_avg.filter(x, f);
        y = y_avg.filter(y, f);
    }
    
    //REVIEW: this probably should be different for two button ClickPads,
    // but we really don't know much about it and how/what the second button
    // on such a ClickPad is used.
    
    // deal with ClickPad touchpad packet
    if (clickpadtype)
    {
        // ClickPad puts its "button" presses in a different location
        // And for single button ClickPad we have to provide a way to simulate right clicks
        int clickbuttons = packet[3] & 0x3;
        if (!_clickbuttons && clickbuttons)
        {
            DEBUG_LOG("ps2: now=%lld, touchtime=%lld, diff=%lld\n", now, touchtime, now-touchtime);
            // change to right click if in right click zone, or was two finger "click"
            //REVIEW: should probably have independent config for maxdbltaptime here...
            if (isInRightClickZone(x, y) || (0 == w && (now-touchtime < maxdbltaptime || MODE_NOTOUCH == touchmode)))
            {
                DEBUG_LOG("ps2: setting clickbuttons to indicate right\n");
                clickbuttons = 0x2;
            }
            _clickbuttons = clickbuttons;
#ifdef EXTENDED_WMODE
            clickedprimary = true;
#endif
        }
        // always clear _clickbutton state, when ClickPad is not clicked
        if (!clickbuttons)
            _clickbuttons = 0;
        buttons |= _clickbuttons;
    }
    
    // deal with "OutsidezoneNoAction When Typing"
    if (outzone_wt && z>z_finger && now-keytime < maxaftertyping &&
        (x < zonel || x > zoner || y < zoneb || y > zonet))
    {
        // touch input was shortly after typing and outside the "zone"
        // ignore it...
        return;
    }

    // double tap in "disable zone" (upper left) for trackpad enable/disable
    //    diszctrl = 0  means automatic enable this feature if trackpad has LED
    //    diszctrl = 1  means always enable this feature
    //    diszctrl = -1 means always disable this feature
    if ((0 == diszctrl && ledpresent) || 1 == diszctrl)
    {
        // deal with taps in the disable zone
        // look for a double tap inside the disable zone to enable/disable touchpad
        switch (touchmode)
        {
            case MODE_NOTOUCH:
                if (isFingerTouch(z) && 4 == w && isInDisableZone(x, y))
                {
                    touchtime = now;
                    touchmode = MODE_WAIT1RELEASE;
                    DEBUG_LOG("ps2: detected touch1 in disable zone\n");
                }
                break;
            case MODE_WAIT1RELEASE:
                if (z<z_finger)
                {
                    DEBUG_LOG("ps2: detected untouch1 in disable zone... ");
                    if (now-touchtime < maxtaptime)
                    {
                        DEBUG_LOG("ps2: setting MODE_WAIT2TAP.\n");
                        untouchtime = now;
                        touchmode = MODE_WAIT2TAP;
                    }
                    else
                    {
                        DEBUG_LOG("ps2: setting MODE_NOTOUCH.\n");
                        touchmode = MODE_NOTOUCH;
                    }
                }
                else
                {
                    if (!isInDisableZone(x, y))
                    {
                        DEBUG_LOG("ps2: moved outside of disable zone in MODE_WAIT1RELEASE\n");
                        touchmode = MODE_NOTOUCH;
                    }
                }
                break;
            case MODE_WAIT2TAP:
                if (isFingerTouch(z))
                {
                    if (isInDisableZone(x, y) && 4 == w)
                    {
                        DEBUG_LOG("ps2: detected touch2 in disable zone... ");
                        if (now-untouchtime < maxdbltaptime)
                        {
                            DEBUG_LOG("ps2: setting MODE_WAIT2RELEASE.\n");
                            touchtime = now;
                            touchmode = MODE_WAIT2RELEASE;
                        }
                        else
                        {
                            DEBUG_LOG("ps2: setting MODE_NOTOUCH.\n");
                            touchmode = MODE_NOTOUCH;
                        }
                    }
                    else
                    {
                        DEBUG_LOG("ps2: bad input detected in MODE_WAIT2TAP x=%d, y=%d, z=%d, w=%d\n", x, y, z, w);
                        touchmode = MODE_NOTOUCH;
                    }
                }
                break;
            case MODE_WAIT2RELEASE:
                if (z<z_finger)
                {
                    DEBUG_LOG("ps2: detected untouch2 in disable zone... ");
                    if (now-touchtime < maxtaptime)
                    {
                        DEBUG_LOG("ps2: %s trackpad.\n", ignoreall ? "enabling" : "disabling");
                        // enable/disable trackpad here
                        ignoreall = !ignoreall;
                        updateTouchpadLED();
                        touchmode = MODE_NOTOUCH;
                    }
                    else
                    {
                        DEBUG_LOG("ps2: not in time, ignoring... setting MODE_NOTOUCH\n");
                        touchmode = MODE_NOTOUCH;
                    }
                }
                else
                {
                    if (!isInDisableZone(x, y))
                    {
                        DEBUG_LOG("ps2: moved outside of disable zone in MODE_WAIT2RELEASE\n");
                        touchmode = MODE_NOTOUCH;
                    }
                }
                break;
            default:
                ; // nothing...
        }
        if (touchmode >= MODE_WAIT1RELEASE)
            return;
    }
    
    // if trackpad input is supposed to be ignored, then don't do anything
    if (ignoreall)
    {
        return;
    }
    
#ifdef DEBUG_VERBOSE
    int tm1 = touchmode;
#endif
    
	if (z<z_finger && isTouchMode())
	{
		xrest=yrest=scrollrest=0;
        inSwipeLeft=inSwipeRight=inSwipeUp=inSwipeDown=0;
        xmoved=ymoved=0;
		untouchtime=now;
#ifdef EXTENDED_WMODE
        tracksecondary=false;
#endif
        
#ifdef DEBUG_VERBOSE
        if (dy_history.count())
            IOLog("ps2: newest=%llu, oldest=%llu, diff=%llu, avg: %d/%d=%d\n", time_history.newest(), time_history.oldest(), time_history.newest()-time_history.oldest(), dy_history.sum(), dy_history.count(), dy_history.average());
        else
            IOLog("ps2: no time/dy history\n");
#endif
        
        // here is where we would kick off scroll momentum timer...
        if (MODE_MTOUCH == touchmode && momentumscroll && momentumscrolltimer)
        {
            // releasing when we were in touchmode -- check for momentum scroll
            int avg = dy_history.average();
            int absavg = avg < 0 ? -avg : avg;
            if (absavg >= momentumscrollthreshy)
            {
                momentumscrollinterval = now - time_history.oldest();
                momentumscrollsum = dy_history.sum();
                momentumscrollcurrent = momentumscrolltimer * momentumscrollsum;
                momentumscrollrest1 = 0;
                momentumscrollrest2 = 0;
                if (!momentumscrollinterval)
                    momentumscrollinterval=1;
                scrollTimer->setTimeout(momentumscrolltimer);
            }
        }
        time_history.clear();
        dy_history.clear();
        DEBUG_LOG("ps2: now-touchtime=%lld (%s)\n", (uint64_t)(now-touchtime)/1000, now-touchtime < maxtaptime?"true":"false");
		if (now-touchtime < maxtaptime && clicking)
        {
			switch (touchmode)
			{
				case MODE_DRAG:
                    //REVIEW: not quite sure why sending button down here because if we
                    // are in MODE_DRAG it should have already been sent, right?
					buttons&=~0x7;
					dispatchRelativePointerEvent(0, 0, buttons|0x1, now);
					dispatchRelativePointerEvent(0, 0, buttons, now);
                    if (wastriple && rtap)
                        buttons |= !swapdoubletriple ? 0x4 : 0x02;
					else if (wasdouble && rtap)
						buttons |= !swapdoubletriple ? 0x2 : 0x04;
					else
						buttons |= 0x1;
					touchmode=MODE_NOTOUCH;
					break;
                    
				case MODE_DRAGLOCK:
					touchmode = MODE_NOTOUCH;
					break;
                    
				default:
                    if (wastriple && rtap)
                    {
						buttons |= !swapdoubletriple ? 0x4 : 0x02;
                        touchmode=MODE_NOTOUCH;
                    }
					else if (wasdouble && rtap)
					{
						buttons |= !swapdoubletriple ? 0x2 : 0x04;
						touchmode=MODE_NOTOUCH;
					}
					else
					{
						buttons |= 0x1;
						touchmode=dragging ? MODE_PREDRAG : MODE_NOTOUCH;
					}
                    break;
			}
        }
		else
		{
			//xmoved=ymoved=xscrolled=yscrolled=0; REVIEW: not used
			if ((touchmode==MODE_DRAG || touchmode==MODE_DRAGLOCK) && (draglock || draglocktemp))
				touchmode=MODE_DRAGNOTOUCH;
			else
            {
				touchmode=MODE_NOTOUCH;
                draglocktemp=0;
            }
		}
		wasdouble=false;
        wastriple=false;
	}
    
    // cancel pre-drag mode if second tap takes too long
	if (touchmode==MODE_PREDRAG && now-untouchtime >= maxdbltaptime)
		touchmode=MODE_NOTOUCH;

//REVIEW: this test should probably be done somewhere else, especially if to
// implement more gestures in the future, because this information we are
// erasing here (time of touch) might be useful for certain gestures...
    // cancel tap if touch point moves too far
    if (isTouchMode() && isFingerTouch(z))
    {
        int dx = xraw > touchx ? xraw - touchx : touchx - xraw;
        int dy = yraw > touchy ? yraw - touchy : touchy - yraw;
        if (!wasdouble && !wastriple && (dx > tapthreshx || dy > tapthreshy))
            touchtime = 0;
        else if (dx > dblthreshx || dy > dblthreshy)
            touchtime = 0;
    }

#ifdef DEBUG_VERBOSE
    int tm2 = touchmode;
#endif
    int dx = 0, dy = 0;
    
	switch (touchmode)
	{
		case MODE_DRAG:
		case MODE_DRAGLOCK:
			buttons|=0x1;
            // fall through
		case MODE_MOVE:
			if (lastf == f && (!palm || (w<=wlimit && z<=zlimit)))
            {
                dx = x-lastx+xrest;
                dy = lasty-y+yrest;
                xrest = dx % divisorx;
                yrest = dy % divisory;
            }
            dispatchRelativePointerEvent(dx / divisorx, dy / divisory, buttons, now);
			break;
            
		case MODE_MTOUCH:
            switch (w)
            {
                default: // two finger (0 is really two fingers, but...)
#ifdef EXTENDED_WMODE
                    if (_extendedwmode && 0 == w && _clickbuttons)
                    {
                        // clickbuttons are set, so no scrolling, but...
                        if (!clickedprimary)
                        {
                            // clickbuttons set by secondary finger, so move with primary delta...
                            if (lastf == f && (!palm || (w<=wlimit && z<=zlimit)))
                            {
                                dx = x-lastx+xrest;
                                dy = lasty-y+yrest;
                                xrest = dx % divisorx;
                                yrest = dy % divisory;
                            }
                        }
                        dispatchRelativePointerEvent(dx / divisorx, dy / divisory, buttons, now);
                        break;
                    }
#endif
                    ////if (palm && (w>wlimit || z>zlimit))
                    if (lastf != f)
                        break;
                    if (palm && z>zlimit)
                        break;
                    if (!wsticky && w<=wlimit && w>3)
                    {
                        dy_history.clear();
                        time_history.clear();
#ifdef EXTENDED_WMODE
                        tracksecondary=false;
#endif
                        touchmode=MODE_MOVE;
                        break;
                    }
                    if (palm_wt && now-keytime < maxaftertyping)
                        break;
                    dy = (wvdivisor) ? (y-lasty+yrest) : 0;
                    dx = (whdivisor&&hscroll) ? (lastx-x+xrest) : 0;
                    yrest = (wvdivisor) ? dy % wvdivisor : 0;
                    xrest = (whdivisor&&hscroll) ? dx % whdivisor : 0;
                    // check for stopping or changing direction
                    if ((dy < 0) != (dy_history.newest() < 0) || dy == 0)
                    {
                        // stopped or changed direction, clear history
                        dy_history.clear();
                        time_history.clear();
                    }
                    // put movement and time in history for later
                    dy_history.filter(dy);
                    time_history.filter(now);
                    if (0 != dy || 0 != dx)
                    {
                        dispatchScrollWheelEvent(wvdivisor ? dy / wvdivisor : 0, (whdivisor && hscroll) ? dx / whdivisor : 0, 0, now);
                    }
                    dispatchRelativePointerEvent(0, 0, buttons, now);
                    break;
                        
                case 1: // three finger
                    xmoved += lastx-x;
                    ymoved += y-lasty;
                    // dispatching 3 finger movement
                    if (ymoved > swipedy && !inSwipeUp)
                    {
                        inSwipeUp=1;
                        inSwipeDown=0;
                        ymoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeUp, &now);
                        break;
                    }
                    if (ymoved < -swipedy && !inSwipeDown)
                    {
                        inSwipeDown=1;
                        inSwipeUp=0;
                        ymoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeDown, &now);
                        break;
                    }
                    if (xmoved < -swipedx && !inSwipeRight)
                    {
                        inSwipeRight=1;
                        inSwipeLeft=0;
                        xmoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeRight, &now);
                        break;
                    }
                    if (xmoved > swipedx && !inSwipeLeft)
                    {
                        inSwipeLeft=1;
                        inSwipeRight=0;
                        xmoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeLeft, &now);
                        break;
                    }
            }
            break;
			
        case MODE_VSCROLL:
			if (!vsticky && (x<redge || w>wlimit || z>zlimit))
			{
				touchmode=MODE_NOTOUCH;
				break;
			}
            if (palm_wt && now-keytime < maxaftertyping)
                break;
            dy = y-lasty+scrollrest;
			dispatchScrollWheelEvent(dy / vscrolldivisor, 0, 0, now);
			//yscrolled += dy/vscrolldivisor;
			scrollrest = dy % vscrolldivisor;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;
            
		case MODE_HSCROLL:
			if (!hsticky && (y>bedge || w>wlimit || z>zlimit))
			{
				touchmode=MODE_NOTOUCH;
				break;
			}			
            if (palm_wt && now-keytime < maxaftertyping)
                break;
            dx = lastx-x+scrollrest;
			dispatchScrollWheelEvent(0, dx / hscrolldivisor, 0, now);
			//xscrolled += dx / hscrolldivisor;
			scrollrest = dx % hscrolldivisor;
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;
            
		case MODE_CSCROLL:
            if (palm_wt && now-keytime < maxaftertyping)
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
            if (!palm_wt || now-keytime >= maxaftertyping)
                buttons |= 0x1;
		case MODE_NOTOUCH:
            //REVIEW: what is "StabilizeTapping" (tapstable) supposed to do???
			//if (!tapstable)
			//	xmoved=ymoved=xscrolled=yscrolled=0;
            //if (now-keytime > maxaftertyping)
            //  _dispatchScrollWheelEvent(-xscrolled, -yscrolled, 0, now);
			//dispatchRelativePointerEvent(-xmoved, -ymoved, buttons, now);
			//xmoved=ymoved=xscrolled=yscrolled=0; //REVIEW: not used
			dispatchRelativePointerEvent(0, 0, buttons, now);
			break;
        
        default:
            ; // nothing
	}
    
    // always save last seen position for calculating deltas later
	lastx=x;
	lasty=y;
    lastf=f;

    // capture time of tap, and watch for double tap
	if (isFingerTouch(z))
    {
        // taps don't count if too close to typing or if currently in momentum scroll
        if ((!palm_wt || now-keytime >= maxaftertyping) && !momentumscrollcurrent)
        {
            if (!isTouchMode())
            {
                touchtime=now;
                touchx=x;
                touchy=y;
            }
            ////if (w>wlimit || w<3)
            if (0 == w)
                wasdouble = true;
            else if (_buttonCount >= 3 && 1 == w)
                wastriple = true;
        }
        // any touch cancels momentum scroll
        momentumscrollcurrent = 0;
    }

    // switch modes, depending on input
	if (touchmode==MODE_PREDRAG && isFingerTouch(z))
    {
		touchmode=MODE_DRAG;
        draglocktemp = _controldown & 0x040004;
    }
	if (touchmode==MODE_DRAGNOTOUCH && isFingerTouch(z))
		touchmode=MODE_DRAGLOCK;
	////if ((w>wlimit || w<3) && isFingerTouch(z) && scroll && (wvdivisor || (hscroll && whdivisor)))
	if ((w>wlimit || w<2) && isFingerTouch(z))
    {
		touchmode=MODE_MTOUCH;
#ifdef EXTENDED_WMODE
        tracksecondary=false;
#endif
    }
    
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
	if ((MODE_NOTOUCH==touchmode || (MODE_HSCROLL==touchmode && y>=bedge)) &&
        z>z_finger && x>redge && vscrolldivisor && scroll)
    {
		touchmode=MODE_VSCROLL;
        scrollrest=0;
    }
	if ((MODE_NOTOUCH==touchmode || (MODE_VSCROLL==touchmode && x<=redge)) &&
        z>z_finger && y<bedge && hscrolldivisor && hscroll && scroll)
    {
		touchmode=MODE_HSCROLL;
        scrollrest=0;
    }
	if (touchmode==MODE_NOTOUCH && z>z_finger)
		touchmode=MODE_MOVE;
    
#ifdef DEBUG_VERBOSE
    IOLog("ps2: dx=%d, dy=%d (%d,%d) z=%d w=%d mode=(%d,%d,%d) buttons=%d wasdouble=%d\n", dx, dy, x, y, z, w, tm1, tm2, touchmode, buttons, wasdouble);
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//////Extended Wmmode

#ifdef EXTENDED_WMODE

void ApplePS2SynapticsTouchPad::dispatchEventsWithPacketEW(UInt8* packet, UInt32 packetSize)
{
    // if trackpad input is supposed to be ignored, then don't do anything
    if (ignoreall)
    {
        return;
    }
    
    UInt8 packetCode = packet[5] >> 4;    // bits 7-4 define packet code
    
    // deal only with secondary finger packets (never saw any of the others)
    if (1 != packetCode)
    {
        DEBUG_LOG("ps2: unknown extended wmode packet = { %02x, %02x, %02x, %02x, %02x, %02x }\n", packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
        return;
    }
    
    uint64_t now;
	clock_get_uptime(&now);
    
    //
    // Parse the packet
    //
    
#ifdef SIMULATE_CLICKPAD
    packet[3] &= ~0x3;
    packet[3] |= packet[0] & 0x1;
    packet[0] &= ~0x3;
#endif

    int buttons = packet[0] & 0x03; // mask for just R L
    int xraw = (packet[1]<<1) | (packet[4]&0x0F)<<9;
    int yraw = (packet[2]<<1) | (packet[4]&0xF0)<<5;
    DEBUG_LOG("ps2: secondary finger pkt (%d, %d) (%04x, %04x) = { %02x, %02x, %02x, %02x, %02x, %02x }\n", xraw, yraw, xraw, yraw, packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
    int z = (packet[5]&0x0F)<<1 | (packet[3]&0x30)<<1;
    int v = 0;
    if (_reportsv)
    {
        // if _reportsv is 1, v field (width) is encoded in x & y & z
        v = (packet[5]&0x1)<<2 | (packet[2]&0x1)<<1 | (packet[1]&0x1)<<0;
        xraw &= ~0x2;
        yraw &= ~0x2;
        z &= ~0x2;
    }
    int x = xraw;
    int y = yraw;
    //int w = z + 8;
    
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    buttons |= passbuttons;
    
    // if first secondary packet, clear some state...
    if (!tracksecondary)
    {
        x2_undo.clear();
        y2_undo.clear();
        x2_avg.clear();
        y2_avg.clear();
        xrest2 = 0;
        yrest2 = 0;
    }
    
    // unsmooth input (probably just for testing)
    // by default the trackpad itself does a simple decaying average (1/2 each)
    // we can undo it here
    if (unsmoothinput)
    {
        x = x2_undo.filter(x, 2);   // always send fingers=2 (filters cleared elsewhere)
        y = y2_undo.filter(y, 2);
    }
    
    // smooth input by unweighted average
    if (smoothinput)
    {
        x = x2_avg.filter(x, 2);
        y = y2_avg.filter(y, 2);
    }

    // deal with "OutsidezoneNoAction When Typing"
    if (outzone_wt && z>z_finger && now-keytime < maxaftertyping &&
        (x < zonel || x > zoner || y < zoneb || y > zonet))
    {
        // touch input was shortly after typing and outside the "zone"
        // ignore it...
        return;
    }
    
    // two things could be happening with secondary packets...
    // we are either tracking movement because the primary finger is holding ClickPad
    //  -or-
    // we are tracking movement with primary finger and secondary finger is being
    //  watched in case ClickPad goes down...
    // both cases in MODE_MTOUCH...
    
    int dx = 0;
    int dy = 0;
    
    if (clickedprimary && _clickbuttons)
    {
        // cannot calculate deltas first thing through...
        if (tracksecondary)
        {
            //if ((palm && (w>wlimit || z>zlimit)))
            //    return;
            dx = x-lastx2+xrest2;
            dy = lasty2-y+yrest2;
            xrest2 = dx % divisorx;
            yrest2 = dy % divisory;
            dispatchRelativePointerEvent(dx / divisorx, dy / divisory, buttons|_clickbuttons, now);
        }
    }
    else
    {
        //REVIEW: this probably should be different for two button ClickPads,
        // but we really don't know much about it and how/what the second button
        // on such a ClickPad is used.
        
        // deal with ClickPad touchpad packet
        if (clickpadtype)
        {
            // ClickPad puts its "button" presses in a different location
            // And for single button ClickPad we have to provide a way to simulate right clicks
            int clickbuttons = packet[3] & 0x3;
            if (!_clickbuttons && clickbuttons)
            {
                // change to right click if in right click zone, or was two finger "click"
                //REVIEW: should probably have different than maxtaptime for this timer...
                if (isInRightClickZone(x, y))
                    clickbuttons = 0x2;
                _clickbuttons = clickbuttons;
                clickedprimary = false;
            }
            // always clear _clickbutton state, when ClickPad is not clicked
            if (!clickbuttons)
                _clickbuttons = 0;
            buttons |= _clickbuttons;
        }
        dispatchRelativePointerEvent(0, 0, buttons, now);
    }

    lastx2 = x;
    lasty2 = y;
    tracksecondary = true;
    
    DEBUG_LOG("ps2: secondary finger dx=%d, dy=%d (%d,%d) z=%d\n", dx, dy, x, y, z);
}

#endif // EXTENDED_WMODE

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

// - -  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::getTouchPadStatus(  UInt8 buf3[] )
{
    PS2Request * request = _device->allocateRequest();
    if (NULL == request)
        return false;
    
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
    request->commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut  = kDP_GetMouseInformation;
    request->commands[2].command = kPS2C_ReadDataPort;
    request->commands[2].inOrOut = 0;
    request->commands[3].command = kPS2C_ReadDataPort;
    request->commands[3].inOrOut = 0;
    request->commands[4].command = kPS2C_ReadDataPort;
    request->commands[4].inOrOut = 0;
    request->commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut = kDP_SetDefaultsAndDisable;
    request->commandsCount = 6;
    
    _device->submitRequestAndBlock(request);
    
    bool success = false;
    if (request->commandsCount == 6) // success?
    {
        success = true;
        buf3[0] = request->commands[2].inOrOut;
        buf3[1] = request->commands[3].inOrOut;
        buf3[2] = request->commands[4].inOrOut;
    }
    return success;
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

bool ApplePS2SynapticsTouchPad::setTouchPadModeByte(UInt8 modeByteValue)
{
    if (!_device)
        return false;
    PS2Request * request = _device->allocateRequest();
    if (!request)
        return false;
    
    // Disable the mouse clock and the mouse IRQ line.
    setCommandByte(kCB_DisableMouseClock, kCB_EnableMouseIRQ);
    
    //
    // This sequence was reversed engineered by obvserving what the Windows
    // driver does (by analysing the data/clock lines of the hardware)
    // Credit to 'chiby' on tonymacx86.com for this bit of secret sauce.
    //
    // Here is a portion of his post:
    // Yehaaaa!!!! Success!
    //
    // Well, after many days of analysing the signals on the PS/2 bus while
    // the win7 driver initializes the touchpad, now I can read the data
    // and clock signals like letters in the book :-)))
    //
    // And this is what is responsible for the magic:
    //
    //  F5
    //  E6, E6, E8, 03, E8, 00, E8, 01, E8, 01, F3, 14
    //  E6, E6, E8, 00, E8, 00, E8, 00, E8, 03, F3, C8
    //  F4
    //
    // So... this will magically turn the Multifinger capability ON even
    // if it is disabled/locked by the fw! (at least on mine...)
    //
    // According to the official documentation, this would make little sense..
    
    //
    // Parts of this make sense as documented by Synaptics but some of
    // it remains a mystery and undocumented.
    //
    // Currently we are doing some of this, but not all...
    // (not the F5, but probably should be at startup only)
    
    // Disable stream mode before the command sequence.
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
    request->commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut  = kDP_SetDefaultsAndDisable;
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = kDP_SetMouseScaling1To1;
    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseScaling1To1;

    // 4 set resolution commands, each encode 2 data bits.
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = kDP_SetMouseResolution;
    request->commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut  = (modeByteValue >> 6) & 0x3;

    request->commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[6].inOrOut  = kDP_SetMouseResolution;
    request->commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[7].inOrOut  = (modeByteValue >> 4) & 0x3;

    request->commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[8].inOrOut  = kDP_SetMouseResolution;
    request->commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[9].inOrOut  = (modeByteValue >> 2) & 0x3;

    request->commands[10].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[10].inOrOut = kDP_SetMouseResolution;
    request->commands[11].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[11].inOrOut = (modeByteValue >> 0) & 0x3;

    // Set sample rate 20 to set mode byte 2. Older pads have 4 mode
    // bytes (0,1,2,3), but only mode byte 2 remain in modern pads.
    request->commands[12].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[12].inOrOut = kDP_SetMouseSampleRate;
    request->commands[13].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[13].inOrOut = 20;

    // maybe this is commit?
    request->commands[14].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[14].inOrOut = kDP_SetMouseScaling1To1;
    request->commands[15].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[15].inOrOut = kDP_SetMouseScaling1To1;
    request->commands[16].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[16].inOrOut = kDP_SetMouseResolution;
    request->commands[17].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[17].inOrOut = 0x0;
    request->commands[18].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[18].inOrOut = kDP_SetMouseResolution;
    request->commands[19].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[19].inOrOut = 0x0;
    request->commands[20].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[20].inOrOut = kDP_SetMouseResolution;
    request->commands[21].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[21].inOrOut =  0x0;
    request->commands[22].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[22].inOrOut = kDP_SetMouseResolution;
    request->commands[23].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[23].inOrOut =  0x3;
    request->commands[24].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[24].inOrOut = kDP_SetMouseSampleRate;
    request->commands[25].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[25].inOrOut = 0xC8;
    
    request->commands[26].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[26].inOrOut = kDP_Enable;
    
    request->commandsCount = 27;
    _device->submitRequestAndBlock(request);
    bool success = (request->commandsCount == 27);
    _device->freeRequest(request);

    // Enable Mouse IRQ for async events
    setCommandByte(kCB_EnableMouseIRQ, kCB_DisableMouseClock);
    
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
		{"DivisorX",						&divisorx},
		{"DivisorY",						&divisory},
		{"EdgeRight",						&redge},
		{"EdgeLeft",						&ledge},
		{"EdgeTop",							&tedge},
		{"EdgeBottom",						&bedge},
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
        {"DisableZoneLeft",                 &diszl},
        {"DisableZoneRight",                &diszr},
        {"DisableZoneTop",                  &diszt},
        {"DisableZoneBottom",               &diszb},
        {"DisableZoneControl",              &diszctrl},
        {"Resolution",                      &_resolution},
        {"ScrollResolution",                &_scrollresolution},
        {"SwipeDeltaX",                     &swipedx},
        {"SwipeDeltaY",                     &swipedy},
        {"MouseCount",                      &mousecount},
        {"RightClickZoneLeft",              &rczl},
        {"RightClickZoneRight",             &rczr},
        {"RightClickZoneTop",               &rczt},
        {"RightClickZoneBottom",            &rczb},
        {"HIDScrollZoomModifierMask",       &scrollzoommask},
        {"ButtonCount",                     &_buttonCount},
        {"MomentumScrollThreshY",           &momentumscrollthreshy},
        {"MomentumScrollMultiplier",        &momentumscrollmultiplier},
        {"MomentumScrollDivisor",           &momentumscrolldivisor},
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
        {"SwapDoubleTriple",                &swapdoubletriple},
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
        {"USBMouseStopsTrackpad",           &usb_mouse_stops_trackpad},
        {"TrackpadMomentumScroll",          &momentumscroll},
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
        {"MaxDragTime",                     &maxdragtime},
        {"MaxTapTime",                      &maxtaptime},
        {"HIDClickTime",                    &maxdbltaptime},
        {"QuietTimeAfterTyping",            &maxaftertyping},
        {"MomentumScrollTimer",             &momentumscrolltimer},
    };
    
	uint8_t oldmode = _touchPadModeByte;
    int oldmousecount = mousecount;
    bool old_usb_mouse_stops_trackpad = usb_mouse_stops_trackpad;
    
    // highrate?
	OSBoolean *bl;
	if ((bl=OSDynamicCast (OSBoolean, config->getObject ("UseHighRate"))))
    {
		if (bl->isTrue())
			_touchPadModeByte |= 1<<6;
		else
			_touchPadModeByte &= ~(1<<6);
    }
#ifdef EXTENDED_WMODE
    // extended W mode?
    if ((bl=OSDynamicCast (OSBoolean, config->getObject ("ExtendedWmode"))))
    {
		if (bl->isTrue())
            _extendedwmode=true;
		else
            _extendedwmode=false;
    }
#endif
    
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
    // same thing going on with the forward/back buttons in Finder, except the
    // timeout OS X is using is different (shorter)
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
    
    // DivisorX and DivisorY cannot be zero, but don't crash if they are...
    if (!divisorx)
        divisorx = 1;
    if (!divisory)
        divisory = 1;

    // this driver assumes wmode is available and used (6-byte packets)
    _touchPadModeByte |= 1<<0;
#ifdef EXTENDED_WMODE
    if (_supporteW && _extendedwmode)
        _touchPadModeByte |= (1<<2);
    else
        _touchPadModeByte &= ~(1<<2);
#endif
	// if changed, setup touchpad mode
	if (_touchPadModeByte != oldmode)
    {
		setTouchPadModeByte(_touchPadModeByte);
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

    // check for special terminating sequence from PS2Daemon
    if (-1 == mousecount)
    {
        // when system is shutting down/restarting we want to force LED off
        if (ledpresent && !noled)
            setTouchpadLED(0x10);
        mousecount = oldmousecount;
    }

    // disable trackpad when USB mouse is plugged in
    // check for mouse count changing...
    if ((oldmousecount != 0) != (mousecount != 0) || old_usb_mouse_stops_trackpad != usb_mouse_stops_trackpad)
    {
        // either last mouse removed or first mouse added
        ignoreall = (mousecount != 0) && usb_mouse_stops_trackpad;
        updateTouchpadLED();
    }
    
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
            
            //
            // Clear packet buffer pointer to avoid issues caused by
            // stale packet fragments.
            //
            
            _packetByteCount = 0;
            
            // clear passbuttons, just in case buttons were down when system
            // went to sleep (now just assume they are up)
            passbuttons = 0;
            _clickbuttons = 0;
            tracksecondary=false;
            
            // clear state of control key cache
            _controldown = 0;
            
            //
            // Resend the touchpad mode byte sequence
            // IRQ is enabled as side effect of setting mode byte
            // Also touchpad is enabled as side effect
            //

            setTouchPadModeByte(_touchPadModeByte);

            //
            // Set LED state as it is lost after sleep
            //
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
            static const int masks[] =
            {
                0x10,       // 0x36
                0x100000,   // 0x37
                0,          // 0x38
                0,          // 0x39
                0x080000,   // 0x3a
                0x040000,   // 0x3b
                0,          // 0x3c
                0x08,       // 0x3d
                0x04,       // 0x3e
            };
            switch (pInfo->adbKeyCode)
            {
                // don't store key time for modifier keys going down
                // track modifiers for scrollzoom feature...
                // (note: it turns out we didn't need to do this, but leaving this code in for now in case it is useful)
                case 0x38:  // left shift
                case 0x3c:  // right shift
                case 0x3b:  // left control
                case 0x3e:  // right control
                case 0x3a:  // left alt (command)
                case 0x3d:  // right alt
                case 0x37:  // left windows (option)
                case 0x36:  // right windows
                    if (pInfo->goingDown)
                    {
                        _controldown |= masks[pInfo->adbKeyCode-0x36];
                        break;
                    }
                    _controldown &= ~masks[pInfo->adbKeyCode-0x36];
                    keytime = pInfo->time;
                    break;
                    
                default:
                    momentumscrollcurrent = 0;  // keys cancel momentum scroll
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
    if (ledpresent && !noled)
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

