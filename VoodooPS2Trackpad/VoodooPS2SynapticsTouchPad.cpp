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


//#define SIMULATE_CLICKPAD
//#define SIMULATE_PASSTHRU

//#define FULL_HW_RESET
//#define SET_STREAM_MODE
//#define UNDOCUMENTED_INIT_SEQUENCE_PRE
#define UNDOCUMENTED_INIT_SEQUENCE_POST

// enable for trackpad debugging
#ifdef DEBUG_MSG
#define DEBUG_VERBOSE
//#define PACKET_DEBUG
#endif

#define kTPDN "TPDN" // Trackpad Disable Notification

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include "VoodooPS2Controller.h"
#include "VoodooPS2SynapticsTouchPad.h"

//REVIEW: avoids problem with Xcode 5.1.0 where -dead_strip eliminates these required symbols
#include <libkern/OSKextLib.h>
void* _org_rehabman_dontstrip_[] =
{
    (void*)&OSKextGetCurrentIdentifier,
    (void*)&OSKextGetCurrentLoadTag,
    (void*)&OSKextGetCurrentVersionString,
};

// =============================================================================
// ApplePS2SynapticsTouchPad Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2SynapticsTouchPad, IOHIPointing);

UInt32 ApplePS2SynapticsTouchPad::deviceType()
{ return NX_EVS_DEVICE_TYPE_MOUSE; };

UInt32 ApplePS2SynapticsTouchPad::interfaceID()
{ return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

IOItemCount ApplePS2SynapticsTouchPad::buttonCount() { return _buttonCount; };
IOFixed     ApplePS2SynapticsTouchPad::resolution()  { return _resolution << 16; };

#define abs(x) ((x) < 0 ? -(x) : (x))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::init(OSDictionary * dict)
{
    //
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.
    //
    
    if (!super::init(dict))
        return false;

    // initialize state...
    _device = NULL;
    _interruptHandlerInstalled = false;
    _powerControlHandlerInstalled = false;
    _messageHandlerInstalled = false;
    _packetByteCount = 0;
    _lastdata = 0;
    _touchPadModeByte = 0x80; //default: absolute, low-rate, no w-mode
    _cmdGate = 0;
    _provider = NULL;

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
	scroll=true;
    outzone_wt = palm = palm_wt = false;
    zlimit = 100;
    noled = false;
    maxaftertyping = 500000000;
    mousemultiplierx = 20;
    mousemultipliery = 20;
    mousescrollmultiplierx = 20;
    mousescrollmultipliery = 20;
    mousemiddlescroll = true;
    wakedelay = 1000;
    skippassthru = false;
    forcepassthru = false;
    hwresetonstart = false;
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
    draglocktempmask = 0x0100010; // default is Command key
    clickpadclicktime = 300000000; // 300ms default
    clickpadtrackboth = true;
    
    bogusdxthresh = 400;
    bogusdythresh = 350;
    
    scrolldxthresh = 10;
    scrolldythresh = 10;
    
    immediateclick = true;

    xupmm = yupmm = 50; // 50 is just arbitrary, but same
    
    _extendedwmode=false;
    _extendedwmodeSupported=false;
    _dynamicEW=false;
    
    // added by usr-sse2
    rightclick_corner=2;    // default to right corner for old trackpad prefs
    
    //vars for clickpad and middleButton support (thanks jakibaki)
    isthinkpad = false;
    thinkpadButtonState = 0;
    thinkpadNubScrollXMultiplier = 1;
    thinkpadNubScrollYMultiplier = 1;
    thinkpadMiddleScrolled = false;
    thinkpadMiddleButtonPressed = false;

    // intialize state
    
	lastx=0;
	lasty=0;
    lastf=0;
	xrest=0;
	yrest=0;
    lastbuttons=0;
    
    // intialize state for secondary packets/extendedwmode
    xrest2=0;
    yrest2=0;
    clickedprimary=false;
    lastx2=0;
    lasty2=0;
    tracksecondary=false;
    
    // state for middle button
    _buttonTimer = 0;
    _mbuttonstate = STATE_NOBUTTONS;
    _pendingbuttons = 0;
    _buttontime = 0;
    _maxmiddleclicktime = 100000000;
    _fakemiddlebutton = true;
    
    ignoredeltas=0;
    ignoredeltasstart=0;
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
    _modifierdown = 0;
    scrollzoommask = 0;
    
    inSwipeLeft=inSwipeRight=inSwipeDown=inSwipeUp=0;
    xmoved=ymoved=0;
    
    momentumscroll = true;
    scrollTimer = 0;
    momentumscrolltimer = 10000000;
    momentumscrollthreshy = 7;
    momentumscrollmultiplier = 98;
    momentumscrolldivisor = 100;
    momentumscrollsamplesmin = 3;
    momentumscrollcurrent = 0;
    
    dragexitdelay = 100000000;
    dragTimer = 0;
    
	touchmode=MODE_NOTOUCH;
    
    // announce version
    extern kmod_info_t kmod_info;
    IOLog("VoodooPS2SynapticsTouchPad: Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);

    // place version/build info in ioreg properties RM,Build and RM,Version
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s", kmod_info.name, kmod_info.version);
    setProperty("RM,Version", buf);
#ifdef DEBUG
    setProperty("RM,Build", "Debug-" LOGNAME);
#else
    setProperty("RM,Build", "Release-" LOGNAME);
#endif

	setProperty ("Revision", 24, 32);
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::injectVersionDependentProperties(OSDictionary *config)
{
    // inject properties specific to the version of Darwin that is runnning...
    char buf[32];
    OSDictionary* dict = NULL;
    do
    {
        // check for "Darwin major.minor"
        snprintf(buf, sizeof(buf), "Darwin %d.%d", version_major, version_minor);
        if ((dict = OSDynamicCast(OSDictionary, config->getObject(buf))))
            break;
        // check for "Darwin major.x"
        snprintf(buf, sizeof(buf), "Darwin %d.x", version_major);
        if ((dict = OSDynamicCast(OSDictionary, config->getObject(buf))))
            break;
        // check for "Darwin 16+" (this is what is used currently, other formats are for future)
        if (version_major >= 16 && (dict = OSDynamicCast(OSDictionary, config->getObject("Darwin 16+"))))
            break;
    } while (0);

    if (dict)
    {
        // found version specific properties above, inject...
        if (OSCollectionIterator* iter = OSCollectionIterator::withCollection(dict))
        {
            // Note: OSDictionary always contains OSSymbol*
            while (const OSSymbol* key = static_cast<const OSSymbol*>(iter->getNextObject()))
            {
                if (OSObject* value = dict->getObject(key))
                    setProperty(key, value);
            }
            iter->release();
        }
    }
}

ApplePS2SynapticsTouchPad* ApplePS2SynapticsTouchPad::probe(IOService * provider, SInt32 * score)
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
    bool forceSynaptics = false;

    // find config specific to Platform Profile
    OSDictionary* list = OSDynamicCast(OSDictionary, getProperty(kPlatformProfile));
    OSDictionary* config = _device->getController()->makeConfigurationNode(list, "Synaptics TouchPad");
    if (config)
    {
        // if DisableDevice is Yes, then do not load at all...
        OSBoolean* disable = OSDynamicCast(OSBoolean, config->getObject(kPlatformProfile));
        if (disable && disable->isTrue())
        {
            config->release();
            return 0;
        }
        if (OSBoolean* force = OSDynamicCast(OSBoolean, config->getObject("ForceSynapticsDetect")))
        {
            // "ForceSynapticsDetect" can be set to treat a trackpad as Synpaptics which does not identify itself properly...
            forceSynaptics = force->isTrue();
        }
#ifdef DEBUG
        // save configuration for later/diagnostics...
        setProperty(kMergedConfiguration, config);
#endif
    }

    // load settings specific to Platform Profile
    setParamPropertiesGated(config);
    injectVersionDependentProperties(config);
    OSSafeRelease(config);

    // for diagnostics...
    UInt8 buf3[3];
    bool success = getTouchPadData(0x0, buf3);
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
        
        success = false;
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
        if (forceSynaptics)
        {
            IOLog("VoodooPS2Trackpad: Forcing Synaptics detection due to ForceSynapticsDetect\n");
            success = true;
        }
    }
    
    _device = 0;

    DEBUG_LOG("ApplePS2SynapticsTouchPad::probe leaving.\n");
    
    return success ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::doHardwareReset()
{
    TPS2Request<> request;
    int i = 0;
    request.commands[i].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i].command = kPS2C_WriteCommandPort;
    request.commands[i++].inOrOut = kCP_TransmitToMouse;
    request.commands[i].command = kPS2C_WriteDataPort;
    request.commands[i++].inOrOut = kDP_Reset;                     // FF
    request.commands[i].command = kPS2C_ReadDataPortAndCompare;
    request.commands[i++].inOrOut = kSC_Acknowledge;
    request.commands[i].command = kPS2C_SleepMS;
    request.commands[i++].inOrOut32 = wakedelay*2;
    request.commands[i].command = kPS2C_ReadMouseDataPortAndCompare;
    request.commands[i++].inOrOut = 0xAA;
    request.commands[i].command = kPS2C_ReadMouseDataPortAndCompare;
    request.commands[i++].inOrOut = 0x00;
    request.commandsCount = i;
    DEBUG_LOG("VoodooPS2Trackpad: sending kDP_Reset $FF\n");
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (i != request.commandsCount)
        DEBUG_LOG("VoodooPS2Trackpad: sending $FF failed: %d\n", request.commandsCount);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::queryCapabilities()
{
    // get TouchPad general capabilities
    UInt8 buf3[3];
    if (!getTouchPadData(0x2, buf3) || !(buf3[0] & 0x80))
        buf3[0] = buf3[2] = 0;
    int nExtendedQueries = (buf3[0] & 0x70) >> 4;
    DEBUG_LOG("VoodooPS2Trackpad: nExtendedQueries=%d\n", nExtendedQueries);
    UInt8 supportsEW = buf3[2] & (1<<5);
    DEBUG_LOG("VoodooPS2Trackpad: supports EW=%d\n", supportsEW != 0);
    
    // deal with pass through capability
    if (!skippassthru)
    {
        UInt8 passthru2 = buf3[2] >> 7;
        // see if guest device for pass through is present
        UInt8 passthru1 = 0;
        if (getTouchPadData(0x1, buf3))
        {
            // first byte, bit 0 indicates guest present
            passthru1 = buf3[0] & 0x01;
        }
        // trackpad must have both guest present and pass through capability
        passthru = passthru1 & passthru2;
#ifdef SIMULATE_PASSTHRU
        passthru = true;
#endif
        DEBUG_LOG("VoodooPS2Trackpad: passthru1=%d, passthru2=%d, passthru=%d\n", passthru1, passthru2, passthru);
    }
    
    if (forcepassthru) {
        passthru = true;
        DEBUG_LOG("VoodooPS2Trackpad: Forcing Passthru\n");
    }
    
    // deal with LED capability
    if (0x46 == _touchPadType)
    {
        ledpresent = true;
        DEBUG_LOG("VoodooPS2Trackpad: ledpresent=%d (forced for type 0x46)\n", ledpresent);
    }
    else if (nExtendedQueries >= 1 && getTouchPadData(0x9, buf3))
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
        DEBUG_LOG("VoodooPS2Trackpad: clickpadtype=1 simulation set\n");
#endif
        DEBUG_LOG("VoodooPS2Trackpad: clickpadtype=%d\n", clickpadtype);
        _reportsv = (buf3[1] >> 3) & 0x01;
        DEBUG_LOG("VoodooPS2Trackpad: _reportsv=%d\n", _reportsv);

        // automatically set extendedwmode for clickpads, if supported
        if (supportsEW && clickpadtype)
        {
            _extendedwmodeSupported = true;
            DEBUG_LOG("VoodooPS2Trackpad: Clickpad supports extendedW mode\n");
        }
    }
    
    // get resolution data for scaling x -> y or y -> x depending
    if ((xupmm < 0 || yupmm < 0) && getTouchPadData(0x8, buf3) && (buf3[1] & 0x80) && buf3[0] && buf3[2])
    {
        if (xupmm < 0)
            xupmm = buf3[0];
        if (yupmm < 0)
            yupmm = buf3[2];
    }
    
#ifdef DEBUG
    // now gather some more information about the touchpad
    if (getTouchPadData(0x1, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Mode/model($01) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (getTouchPadData(0x2, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Capabilities($02) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (getTouchPadData(0x3, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Model ID($03) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (getTouchPadData(0x6, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: SN Prefix($06) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (getTouchPadData(0x7, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: SN Suffix($07) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (getTouchPadData(0x8, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Resolutions($08) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 1 && getTouchPadData(0x9, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Extended Model($09) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 4 && getTouchPadData(0xc, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Continued Capabilities($0C) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 5 && getTouchPadData(0xd, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Maximum coords($0D) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 6 && getTouchPadData(0xe, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Deluxe LED bytes($0E) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 7 && getTouchPadData(0xf, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Minimum coords bytes($0F) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
#endif
}

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
    char buf[128];
    snprintf(buf, sizeof(buf), "type 0x%02x, version %d.%d", _touchPadType, (UInt8)(_touchPadVersion >> 8), (UInt8)(_touchPadVersion));
    setProperty("RM,TrackpadInfo", buf);

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
    // added for Sierra precise scrolling (credit usr-sse2)
    setProperty("HIDScrollResolutionX", _scrollresolution << 16, 32);
    setProperty("HIDScrollResolutionY", _scrollresolution << 16, 32);
    
    //
    // Setup workloop with command gate for thread syncronization...
    //
    IOWorkLoop* pWorkLoop = getWorkLoop();
    _cmdGate = IOCommandGate::commandGate(this);
    if (!pWorkLoop || !_cmdGate)
    {
        _device->release();
        return false;
    }
    
    //
    // Setup button timer event source
    //
    if (_buttonCount >= 3)
    {
        _buttonTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2SynapticsTouchPad::onButtonTimer));
        if (!_buttonTimer)
        {
            _device->release();
            return false;
        }
        pWorkLoop->addEventSource(_buttonTimer);
    }
    
    pWorkLoop->addEventSource(_cmdGate);
    
    //
    // Setup scrolltimer event source
    //
    scrollTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2SynapticsTouchPad::onScrollTimer));
    if (scrollTimer)
        pWorkLoop->addEventSource(scrollTimer);
    
    //
    // Setup dragTimer event source
    //
    if (dragexitdelay)
    {
        dragTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2SynapticsTouchPad::onDragTimer));
        if (dragTimer)
            pWorkLoop->addEventSource(dragTimer);
    }

    //
    // Lock the controller during initialization
    //
    
    _device->lock();
    
    //
    // Some machines require a hw reset in order to work correctly -- notably Thinkpads with Trackpoints
    //
    if (hwresetonstart) {
        doHardwareReset();
    }
    
    //
    // Query the touchpad for the capabilities we need to know.
    //
    
    queryCapabilities();
    
    //
    // Set the touchpad mode byte, which will also...
    // Enable the mouse clock (should already be so) and the mouse IRQ line.
    // Enable the touchpad itself.
    //
    setTouchpadModeByte();

    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //
    
    _device->installInterruptAction(this,
                                    OSMemberFunctionCast(PS2InterruptAction,this,&ApplePS2SynapticsTouchPad::interruptOccurred),
                                    OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2SynapticsTouchPad::packetReady));
    _interruptHandlerInstalled = true;
    
    // now safe to allow other threads
    _device->unlock();
    
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
    
    // get IOACPIPlatformDevice for Device (PS2M)
    //REVIEW: should really look at the parent chain for IOACPIPlatformDevice instead.
    _provider = (IOACPIPlatformDevice*)IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2M");
    if (_provider && kIOReturnSuccess != _provider->validateObject(kTPDN))
    {
        _provider->release();
        _provider = NULL;
    }

    //
    // Update LED -- it could have been disabled then computer was restarted
    //
    updateTouchpadLED();
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::stop( IOService * provider )
{
    DEBUG_LOG("%s: stop called\n", getName());
    
    //
    // The driver has been instructed to stop.  Note that we must break all
    // connections to other service objects now (ie. no registered actions,
    // no pointers and retains to objects, etc), if any.
    //

    assert(_device == provider);

    //
    // turn off the LED just in case it was on
    //
    
    ignoreall = false;
    updateTouchpadLED();
    
    //
    // Disable the mouse itself, so that it may stop reporting mouse events.
    //

    setTouchPadEnable(false);

    // free up timer for scroll momentum
    IOWorkLoop* pWorkLoop = getWorkLoop();
    if (pWorkLoop)
    {
        if (scrollTimer)
        {
            pWorkLoop->removeEventSource(scrollTimer);
            scrollTimer->release();
            scrollTimer = 0;
        }
        if (_buttonTimer)
        {
            pWorkLoop->removeEventSource(_buttonTimer);
            _buttonTimer->release();
            _buttonTimer = 0;
        }
        if (_cmdGate)
        {
            pWorkLoop->removeEventSource(_cmdGate);
            _cmdGate->release();
            _cmdGate = 0;
        }
    }
    
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

    //
    // Release the pointer to the provider object.
    //
    
    OSSafeReleaseNULL(_device);
    
    //
    // Release ACPI provider for PS2M ACPI device
    //
    OSSafeReleaseNULL(_provider);

	super::stop(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::onScrollTimer(void)
{
    //
    // This will be invoked by our workloop timer event source to implement
    // momentum scroll.
    //
    
    if (!momentumscrollcurrent)
        return;
    
    uint64_t now_abs;
	clock_get_uptime(&now_abs);
    
    int64_t dy64 = momentumscrollcurrent / (int64_t)momentumscrollinterval + momentumscrollrest2;
    int dy = (int)dy64;
    if (abs(dy) > momentumscrollthreshy)
    {
        // dispatch the scroll event
        dispatchScrollWheelEventX(wvdivisor ? dy / wvdivisor : 0, 0, 0, now_abs);
        momentumscrollrest2 = wvdivisor ? dy % wvdivisor : 0;
    
        // adjust momentumscrollcurrent
        momentumscrollcurrent = momentumscrollcurrent * momentumscrollmultiplier + momentumscrollrest1;
        momentumscrollrest1 = momentumscrollcurrent % momentumscrolldivisor;
        momentumscrollcurrent /= momentumscrolldivisor;
        
        // start another timer
        setTimerTimeout(scrollTimer, momentumscrolltimer);
    }
    else
    {
        // no more scrolling...
        momentumscrollcurrent = 0;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2InterruptResult ApplePS2SynapticsTouchPad::interruptOccurred(UInt8 data)
{
    //
    // This will be invoked automatically from our device when asynchronous
    // events need to be delivered. Buffer the trackpad data. Do NOT issue
    // any BLOCKING commands to our device in this context.
    //
    
    UInt8* packet = _ringBuffer.head();

    // special case for $AA $00, spontaneous reset (usually due to static electricity)
    if (kSC_Reset == _lastdata && 0x00 == data)
    {
        IOLog("%s: Unexpected reset (%02x %02x) request from PS/2 controller\n", getName(), _lastdata, data);
        
        // spontaneous reset, device has announced with $AA $00, schedule a reset
        packet[0] = 0x00;
        packet[1] = kSC_Reset;
        _ringBuffer.advanceHead(kPacketLength);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }
    _lastdata = data;
    
    // Ignore all bytes until we see the start of a packet, otherwise the
    // packets may get out of sequence and things will get very confusing.
    if (0 == _packetByteCount && (data & 0xc8) != 0x80)
    {
        IOLog("%s: Unexpected byte0 data (%02x) from PS/2 controller\n", getName(), data);
        
        packet[0] = 0x00;
        packet[1] = 0;  // reason=byte0
        _ringBuffer.advanceHead(kPacketLength);
        return kPS2IR_packetReady;
    }
    if (3 == _packetByteCount && (data & 0xc8) != 0xc0)
    {
        IOLog("%s: Unexpected byte3 data (%02x) from PS/2 controller\n", getName(), data);
        
        packet[0] = 0x00;
        packet[1] = 3;  // reason=byte3
        _ringBuffer.advanceHead(kPacketLength);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }

#ifdef PACKET_DEBUG
    if (_packetByteCount == 0)
        DEBUG_LOG("%s: packet { %02x, ", getName(), data);
    else
        DEBUG_LOG("%02x%s", data, _packetByteCount == 5 ? " }\n" : ", ");
#endif

    //
    // Add this byte to the packet buffer. If the packet is complete, that is,
    // we have the six bytes, allow main thread to process packets by
    // returning kPS2IR_packetReady
    //
    
    packet[_packetByteCount++] = data;
    if (kPacketLength == _packetByteCount)
    {
        _ringBuffer.advanceHead(kPacketLength);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }
    return kPS2IR_packetBuffering;
}

void ApplePS2SynapticsTouchPad::packetReady()
{
    // empty the ring buffer, dispatching each packet...
    while (_ringBuffer.count() >= kPacketLength)
    {
        UInt8* packet = _ringBuffer.tail();
        if (0x00 != packet[0])
        {
            // normal packet
            dispatchEventsWithPacket(_ringBuffer.tail(), kPacketLength);
        }
        else
        {
            // a reset packet was buffered... schedule a complete reset
            ////initTouchPad();
        }
        _ringBuffer.advanceTail(kPacketLength);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::onButtonTimer(void)
{
	uint64_t now_abs;
	clock_get_uptime(&now_abs);
    
    middleButton(lastbuttons, now_abs, fromTimer);
}

UInt32 ApplePS2SynapticsTouchPad::middleButton(UInt32 buttons, uint64_t now_abs, MBComingFrom from)
{
    if (!_fakemiddlebutton || _buttonCount <= 2 || (ignoreall && fromTrackpad == from))
        return buttons;
    
    // cancel timer if we see input before timeout has fired, but after expired
    bool timeout = false;
    uint64_t now_ns;
    absolutetime_to_nanoseconds(now_abs, &now_ns);
    if (fromTimer == from || fromCancel == from || now_ns - _buttontime > _maxmiddleclicktime)
        timeout = true;

    //
    // A state machine to simulate middle buttons with two buttons pressed
    // together.
    //
    switch (_mbuttonstate)
    {
        // no buttons down, waiting for something to happen
        case STATE_NOBUTTONS:
            if (fromCancel != from)
            {
                if (buttons & 0x4)
                    _mbuttonstate = STATE_NOOP;
                else if (0x3 == buttons)
                    _mbuttonstate = STATE_MIDDLE;
                else if (0x0 != buttons)
                {
                    // only single button, so delay this for a bit
                    _pendingbuttons = buttons;
                    _buttontime = now_ns;
                    setTimerTimeout(_buttonTimer, _maxmiddleclicktime);
                    _mbuttonstate = STATE_WAIT4TWO;
                }
            }
            break;
            
        // waiting for second button to come down or timeout
        case STATE_WAIT4TWO:
            if (!timeout && 0x3 == buttons)
            {
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                _mbuttonstate = STATE_MIDDLE;
            }
            else if (timeout || buttons != _pendingbuttons)
            {
                if (fromTimer == from || !(buttons & _pendingbuttons))
                    dispatchRelativePointerEventX(0, 0, buttons|_pendingbuttons, now_abs);
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                if (0x0 == buttons)
                    _mbuttonstate = STATE_NOBUTTONS;
                else
                    _mbuttonstate = STATE_NOOP;
            }
            break;
            
        // both buttons down and delivering middle button
        case STATE_MIDDLE:
            if (0x0 == buttons)
                _mbuttonstate = STATE_NOBUTTONS;
            else if (0x3 != (buttons & 0x3))
            {
                // only single button, so delay to see if we get to none
                _pendingbuttons = buttons;
                _buttontime = now_ns;
                setTimerTimeout(_buttonTimer, _maxmiddleclicktime);
                _mbuttonstate = STATE_WAIT4NONE;
            }
            break;
            
        // was middle button, but one button now up, waiting for second to go up
        case STATE_WAIT4NONE:
            if (!timeout && 0x0 == buttons)
            {
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                _mbuttonstate = STATE_NOBUTTONS;
            }
            else if (timeout || buttons != _pendingbuttons)
            {
                if (fromTimer == from)
                    dispatchRelativePointerEventX(0, 0, buttons|_pendingbuttons, now_abs);
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                if (0x0 == buttons)
                    _mbuttonstate = STATE_NOBUTTONS;
                else
                    _mbuttonstate = STATE_NOOP;
            }
            break;
            
        case STATE_NOOP:
            if (0x0 == buttons)
                _mbuttonstate = STATE_NOBUTTONS;
            break;
    }
    
    // modify buttons after new state set
    switch (_mbuttonstate)
    {
        case STATE_MIDDLE:
            buttons = 0x4;
            break;
            
        case STATE_WAIT4NONE:
        case STATE_WAIT4TWO:
            buttons &= ~0x3;
            break;
            
        case STATE_NOBUTTONS:
        case STATE_NOOP:
            break;
    }
    
    // return modified buttons
    return buttons;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::onDragTimer(void)
{
    touchmode=MODE_NOTOUCH;
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    UInt32 buttons = middleButton(lastbuttons, now_abs, fromPassthru);
    //If on a Thinkpad, the middle mouse (trackpoint) button is down and we're already scrolling then don't take action
    if (isthinkpad && mousemiddlescroll && buttons == 4) return;
    dispatchRelativePointerEventX(0, 0, buttons, now_abs);
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

	uint64_t now_abs;
	clock_get_uptime(&now_abs);
    uint64_t now_ns;
    absolutetime_to_nanoseconds(now_abs, &now_ns);

    //
    // Parse the packet
    //

	int w = ((packet[3]&0x4)>>2)|((packet[0]&0x4)>>1)|((packet[0]&0x30)>>2);
    
    if (_extendedwmode && 2 == w)
    {
        // deal with extended W mode encapsulated packet
        dispatchEventsWithPacketEW(packet, packetSize);
        return;
    }
    
#ifdef SIMULATE_CLICKPAD
    packet[3] &= ~0x3;
    packet[3] |= (packet[0] & 0x1) | (packet[0] & 0x2)>>1;
    packet[0] &= ~0x3;
#endif

    // allow middle click to be simulated the other two physical buttons
    UInt32 buttonsraw = packet[0] & 0x03; // mask for just R L
    UInt32 buttons = buttonsraw;
    
#ifdef SIMULATE_PASSTHRU
    if (passthru && 3 != w)
        trackbuttons = buttons;
#endif
    
    // deal with pass through packet buttons
    if (passthru && 3 == w)
        passbuttons = packet[1] & 0x7; // mask for just M R L
    
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    buttons |= passbuttons;
    lastbuttons = buttons;

    // allow middle button to be simulated with two buttons down
    if (!clickpadtype || 3 == w)
        buttons = middleButton(buttons, now_abs, 3 == w ? fromPassthru : fromTrackpad);

    // now deal with pass through packet moving/scrolling
    if (passthru && 3 == w)
    {
        // New Lenovo clickpads do not have buttons, so LR in packet byte 1 is zero and thus
        // passbuttons is 0.  Instead we need to check the trackpad buttons in byte 0 and byte 3
        // However for clickpads that would miss right clicks, so use the last clickbuttons that
        // were saved.
        UInt32 combinedButtons = buttons | ((packet[0] & 0x3) | (packet[3] & 0x3)) | _clickbuttons | thinkpadButtonState;

        SInt32 dx = ((packet[1] & 0x10) ? 0xffffff00 : 0 ) | packet[4];
        SInt32 dy = ((packet[1] & 0x20) ? 0xffffff00 : 0 ) | packet[5];
        if (mousemiddlescroll && ((packet[1] & 0x4) || thinkpadButtonState == 4)) // only for physical middle button
        {
            if (dx != 0 || dy != 0) {
                thinkpadMiddleScrolled = true;
            }
            // middle button treats deltas for scrolling
            SInt32 scrollx = 0, scrolly = 0;
            if (abs(dx) > abs(dy))
                scrollx = dx * mousescrollmultiplierx;
            else
                scrolly = dy * mousescrollmultipliery;
            
            if (isthinkpad && thinkpadMiddleButtonPressed) {
                scrolly = scrolly * thinkpadNubScrollYMultiplier;
                scrollx = scrollx * thinkpadNubScrollXMultiplier;
            }
            
            dispatchScrollWheelEventX(scrolly, -scrollx, 0, now_abs);
            dx = dy = 0;
        }
        dx *= mousemultiplierx;
        dy *= mousemultipliery;
        //If this is a thinkpad, we do extra logic here to see if we're doing a middle click
        if (isthinkpad) {
            if (mousemiddlescroll && combinedButtons == 4) {
                thinkpadMiddleButtonPressed = true;
            }
            else {
                if (thinkpadMiddleButtonPressed && !thinkpadMiddleScrolled) {
                    dispatchRelativePointerEventX(dx, -dy, 4, now_abs);
                }
                dispatchRelativePointerEventX(dx, -dy, combinedButtons, now_abs);
                thinkpadMiddleButtonPressed = false;
                thinkpadMiddleScrolled = false;
            }
        }
        else {
            dispatchRelativePointerEventX(dx, -dy, combinedButtons, now_abs);
        }
#ifdef DEBUG_VERBOSE
        static int count = 0;
        IOLog("ps2: passthru packet dx=%d, dy=%d, buttons=%d (%d)\n", dx, dy, combinedButtons, count++);
#endif
        return;
    }
    
    // otherwise, deal with normal wmode touchpad packet
    int xraw = packet[4]|((packet[1]&0x0f)<<8)|((packet[3]&0x10)<<8);
    int yraw = packet[5]|((packet[1]&0xf0)<<4)|((packet[3]&0x20)<<7);
    // scale x & y to the axis which has the most resolution
    if (xupmm < yupmm)
        xraw = xraw * yupmm / xupmm;
    else if (xupmm > yupmm)
        yraw = yraw * xupmm / yupmm;
    int z = packet[2];
    int f = z>z_finger ? w>=4 ? 1 : w+2 : 0;   // number of fingers
    ////int v = w;  // v is not currently used... but maybe should be using it
    if (_extendedwmode && _reportsv && f > 1)
    {
        // in extended w mode, v field (width) is encoded in x & y & z, with multifinger
        ////v = (((xraw & 0x2)>>1) | ((yraw & 0x2)) | ((z & 0x1)<<2)) + 8;
        xraw &= ~0x2;
        yraw &= ~0x2;
        z &= ~0x1;
    }
    int x = xraw;
    int y = yraw;
    
    // recalc middle buttons if finger is going down
    if (0 == lastf && f > 0)
        buttons = middleButton(buttonsraw | passbuttons, now_abs, fromCancel);
    
    if (lastf > 0 && f > 0 && lastf != f)
    {
        // ignore deltas for a while after finger change
        ignoredeltas = ignoredeltasstart;
    }
    
    if (lastf != f)
    {
        // reset averages after finger change
        x_undo.reset();
        y_undo.reset();
        x_avg.reset();
        y_avg.reset();
    }
    
    // unsmooth input (probably just for testing)
    // by default the trackpad itself does a simple decaying average (1/2 each)
    // we can undo it here
    if (unsmoothinput)
    {
        x = x_undo.filter(x);
        y = y_undo.filter(y);
    }
    
    // smooth input by unweighted average
    if (smoothinput)
    {
        x = x_avg.filter(x);
        y = y_avg.filter(y);
    }
    
    if (ignoredeltas)
    {
        lastx = x;
        lasty = y;
        if (--ignoredeltas == 0)
        {
            x_undo.reset();
            y_undo.reset();
            x_avg.reset();
            y_avg.reset();
        }
    }
    
    // Note: This probably should be different for two button ClickPads,
    // but we really don't know much about it and how/what the second button
    // on such a ClickPad is used.
    
    // deal with ClickPad touchpad packet
    if (clickpadtype)
    {
        // ClickPad puts its "button" presses in a different location
        // And for single button ClickPad we have to provide a way to simulate right clicks
        int clickbuttons = packet[3] & 0x3;
        
        //Let's quickly do some extra logic to see if we are pressing any of the physical buttons for the trackpoint
        if (isthinkpad) {
            // parse packets for buttons - TrackPoint Buttons may not be passthru
            int bp = packet[3] & 0x3; // 1 on clickpad or 2 for the 2 real buttons
            int lb = packet[4] & 0x3; // 1 for left real button
            int rb = packet[5] & 0x3; // 1 for right real button
            
            if (bp == 2)
            {
                if      ( lb == 1 )
                { // left click
                    clickbuttons = 0x1;
                }
                else if ( rb == 1 )
                { // right click
                    clickbuttons = 0x2;
                }
                else if ( lb == 2 )
                { // middle click
                    clickbuttons = 0x4;
                }
                else
                {
                    clickbuttons = 0x0;
                }
                thinkpadButtonState = clickbuttons;
                buttons=clickbuttons;
                setClickButtons(clickbuttons);
            }
            else
            {
                clickbuttons = bp;
            }
        }
        
        if (!_clickbuttons && clickbuttons)
        {
            // use primary packet by default
            int xx = x;
            int yy = y;
            clickedprimary = (MODE_MTOUCH != touchmode);
            // need to use secondary packet if receiving them
            if (_extendedwmode && !clickedprimary && tracksecondary)
            {
                xx = lastx2;
                yy = lasty2;
            }
            DEBUG_LOG("ps2: now_ns=%lld, touchtime=%lld, diff=%lld cpct=%lld (%s) w=%d (%d,%d)\n", now_ns, touchtime, now_ns-touchtime, clickpadclicktime, now_ns-touchtime < clickpadclicktime ? "true" : "false", w, isFingerTouch(z), isInRightClickZone(xx, yy));
            // change to right click if in right click zone, or was two finger "click"
            if (isFingerTouch(z) &&
                (((rightclick_corner == 2 && isInRightClickZone(xx, yy)) ||
                 (rightclick_corner == 1 && isInLeftClickZone(xx, yy)))
                || (0 == w && (now_ns-touchtime < clickpadclicktime || MODE_NOTOUCH == touchmode))))
            {
                DEBUG_LOG("ps2p: setting clickbuttons to indicate right\n");
                clickbuttons = 0x2;
            }
            else
                DEBUG_LOG("ps2p: setting clickbuttons to indicate left\n");
            setClickButtons(clickbuttons);
        }
        // always clear _clickbutton state, when ClickPad is not clicked
        if (!clickbuttons)
            setClickButtons(0);
        
        //Remember the button state on thinkpads.. this is required so we can handle the middle click vs middle scrolling appropriately.
        if (isthinkpad) {
            if (thinkpadButtonState)
                _clickbuttons = thinkpadButtonState;
        }
        buttons |= _clickbuttons;
        lastbuttons = buttons;
    }
    
    // deal with "OutsidezoneNoAction When Typing"
    if (outzone_wt && z>z_finger && now_ns-keytime < maxaftertyping &&
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
                if (isFingerTouch(z) && (4 <= w && w <= 5) && isInDisableZone(x, y))
                {
                    touchtime = now_ns;
                    touchmode = MODE_WAIT1RELEASE;
                    DEBUG_LOG("ps2: detected touch1 in disable zone\n");
                }
                break;
            case MODE_WAIT1RELEASE:
                if (z<z_finger)
                {
                    DEBUG_LOG("ps2: detected untouch1 in disable zone... ");
                    if (now_ns-touchtime < maxtaptime)
                    {
                        DEBUG_LOG("ps2: setting MODE_WAIT2TAP.\n");
                        untouchtime = now_ns;
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
                    if (isInDisableZone(x, y) && (4 <= w && w <= 5))
                    {
                        DEBUG_LOG("ps2: detected touch2 in disable zone... ");
                        if (now_ns-untouchtime < maxdragtime)
                        {
                            DEBUG_LOG("ps2: setting MODE_WAIT2RELEASE.\n");
                            touchtime = now_ns;
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
                    if (now_ns-touchtime < maxtaptime)
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
		untouchtime=now_ns;
        tracksecondary=false;
        
#ifdef DEBUG_VERBOSE
        if (dy_history.count())
            IOLog("ps2: newest=%llu, oldest=%llu, diff=%llu, avg: %d/%d=%d\n", time_history.newest(), time_history.oldest(), time_history.newest()-time_history.oldest(), dy_history.sum(), dy_history.count(), dy_history.average());
        else
            IOLog("ps2: no time/dy history\n");
#endif
        
        // check for scroll momentum start
        if (MODE_MTOUCH == touchmode && momentumscroll && momentumscrolltimer)
        {
            // releasing when we were in touchmode -- check for momentum scroll
            if (dy_history.count() > momentumscrollsamplesmin &&
                (momentumscrollinterval = time_history.newest() - time_history.oldest()))
            {
                momentumscrollsum = dy_history.sum();
                momentumscrollcurrent = momentumscrolltimer * momentumscrollsum;
                momentumscrollrest1 = 0;
                momentumscrollrest2 = 0;
                setTimerTimeout(scrollTimer, momentumscrolltimer);
            }
        }
        time_history.reset();
        dy_history.reset();
        DEBUG_LOG("ps2: now_ns-touchtime=%lld (%s)\n", (uint64_t)(now_ns-touchtime)/1000, now_ns-touchtime < maxtaptime?"true":"false");
		if (now_ns-touchtime < maxtaptime && clicking)
        {
			switch (touchmode)
			{
				case MODE_DRAG:
                    if (!immediateclick)
                    {
                        buttons&=~0x7;
                        //If on a Thinkpad, the middle mouse (trackpoint) button is down and we're already scrolling then don't take action
                        if (isthinkpad && mousemiddlescroll && buttons == 4) {
                            //Do Nothing Here
                        }
                        else {
                            dispatchRelativePointerEventX(0, 0, buttons|0x1, now_abs);
                            dispatchRelativePointerEventX(0, 0, buttons, now_abs);
                        }
                    }
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
			if ((touchmode==MODE_DRAG || touchmode==MODE_DRAGLOCK)
                && (draglock || draglocktemp || (dragTimer && dragexitdelay)))
            {
                touchmode=MODE_DRAGNOTOUCH;
                if (!draglock && !draglocktemp)
                {
                    cancelTimer(dragTimer);
                    setTimerTimeout(dragTimer, dragexitdelay);
                }
            }
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
	if (touchmode==MODE_PREDRAG && now_ns-untouchtime >= maxdragtime)
		touchmode=MODE_NOTOUCH;

    // Note: This test should probably be done somewhere else, especially if to
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
            if (MODE_DRAGLOCK == touchmode || (!immediateclick || now_ns-touchtime > maxdbltaptime))
                buttons|=0x1;
            // fall through
		case MODE_MOVE:
			if (lastf == f && (!palm || (w<=wlimit && z<=zlimit)))
            {
                dx = x-lastx+xrest;
                dy = lasty-y+yrest;
                xrest = dx % divisorx;
                yrest = dy % divisory;
                if (abs(dx) > bogusdxthresh || abs(dy) > bogusdythresh)
                    dx = dy = xrest = yrest = 0;
            }
			break;
            
		case MODE_MTOUCH:
            switch (w)
            {
                default: // two finger (0 is really two fingers, but...)
                    if (_extendedwmode && 0 == w && _clickbuttons)
                    {
                        // clickbuttons are set, so no scrolling, but...
                        if (clickpadtrackboth || !clickedprimary)
                        {
                            // clickbuttons set by secondary finger, so move with primary delta...
                            if (lastf == f && (!palm || (w<=wlimit && z<=zlimit)))
                            {
                                dx = x-lastx+xrest;
                                dy = lasty-y+yrest;
                                xrest = dx % divisorx;
                                yrest = dy % divisory;
                                if (abs(dx) > bogusdxthresh || abs(dy) > bogusdythresh)
                                    dx = dy = xrest = yrest = 0;
                            }
                        }
                        break;
                    }
                    ////if (palm && (w>wlimit || z>zlimit))
                    if (lastf != f)
                        break;
                    if (palm && z>zlimit)
                        break;
                    if (!wsticky && w<=wlimit && w>3)
                    {
                        dy_history.reset();
                        time_history.reset();
                        clickedprimary = _clickbuttons;
                        tracksecondary=false;
                        touchmode=MODE_MOVE;
                        break;
                    }
                    if (palm_wt && now_ns-keytime < maxaftertyping)
                        break;
                    dy = (wvdivisor) ? (y-lasty+yrest) : 0;
                    dx = (whdivisor&&hscroll) ? (lastx-x+xrest) : 0;
                    yrest = (wvdivisor) ? dy % wvdivisor : 0;
                    xrest = (whdivisor&&hscroll) ? dx % whdivisor : 0;
                    // check for stopping or changing direction
                    if ((dy < 0) != (dy_history.newest() < 0) || dy == 0)
                    {
                        // stopped or changed direction, clear history
                        dy_history.reset();
                        time_history.reset();
                    }
                    // put movement and time in history for later
                    dy_history.filter(dy);
                    time_history.filter(now_ns);
                    //REVIEW: filter out small movements (Mavericks issue)
                    if (abs(dx) < scrolldxthresh)
                    {
                        xrest = dx;
                        dx = 0;
                    }
                    if (abs(dy) < scrolldythresh)
                    {
                        yrest = dy;
                        dy = 0;
                    }
                    if (0 != dy || 0 != dx)
                    {
                        dispatchScrollWheelEventX(wvdivisor ? dy / wvdivisor : 0, (whdivisor && hscroll) ? dx / whdivisor : 0, 0, now_abs);
                        ////IOLog("ps2: dx=%d, dy=%d (%d,%d) z=%d w=%d\n", dx, dy, x, y, z, w);
                        dx = dy = 0;
                    }
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
                        _device->dispatchKeyboardMessage(kPS2M_swipeUp, &now_abs);
                        break;
                    }
                    if (ymoved < -swipedy && !inSwipeDown)
                    {
                        inSwipeDown=1;
                        inSwipeUp=0;
                        ymoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeDown, &now_abs);
                        break;
                    }
                    if (xmoved < -swipedx && !inSwipeRight)
                    {
                        inSwipeRight=1;
                        inSwipeLeft=0;
                        xmoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeRight, &now_abs);
                        break;
                    }
                    if (xmoved > swipedx && !inSwipeLeft)
                    {
                        inSwipeLeft=1;
                        inSwipeRight=0;
                        xmoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeLeft, &now_abs);
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
            if (palm_wt && now_ns-keytime < maxaftertyping)
                break;
            dy = y-lasty+scrollrest;
			scrollrest = dy % vscrolldivisor;
            //REVIEW: filter out small movements (Mavericks issue)
            if (abs(dy) < scrolldythresh)
            {
                scrollrest = dy;
                dy = 0;
            }
            if (dy)
            {
                dispatchScrollWheelEventX(dy / vscrolldivisor, 0, 0, now_abs);
                dy = 0;
            }
			break;
            
		case MODE_HSCROLL:
			if (!hsticky && (y>bedge || w>wlimit || z>zlimit))
			{
				touchmode=MODE_NOTOUCH;
				break;
			}			
            if (palm_wt && now_ns-keytime < maxaftertyping)
                break;
            dx = lastx-x+scrollrest;
			scrollrest = dx % hscrolldivisor;
            //REVIEW: filter out small movements (Mavericks issue)
            if (abs(dx) < scrolldxthresh)
            {
                scrollrest = dx;
                dx = 0;
            }
            if (dx)
            {
                dispatchScrollWheelEventX(0, dx / hscrolldivisor, 0, now_abs);
                dx = 0;
            }
			break;
            
		case MODE_CSCROLL:
            if (palm_wt && now_ns-keytime < maxaftertyping)
                break;
            if (y < centery)
                dx = x-lastx;
            else
                dx = lastx-x;
            if (x < centerx)
                dx += lasty-y;
            else
                dx += y-lasty;
            dx += scrollrest;
            scrollrest = dx % cscrolldivisor;
            //REVIEW: filter out small movements (Mavericks issue)
            if (abs(dx) < scrolldxthresh)
            {
                scrollrest = dx;
                dx = 0;
            }
            if (dx)
            {
                dispatchScrollWheelEventX(dx / cscrolldivisor, 0, 0, now_abs);
                dx = 0;
            }
			break;

		case MODE_DRAGNOTOUCH:
            buttons |= 0x1;
            // fall through
		case MODE_PREDRAG:
            if (!immediateclick && (!palm_wt || now_ns-keytime >= maxaftertyping))
                buttons |= 0x1;
		case MODE_NOTOUCH:
			break;
        
        default:
            ; // nothing
	}
    
    // capture time of tap, and watch for double tap
	if (isFingerTouch(z))
    {
        // taps don't count if too close to typing or if currently in momentum scroll
        if ((!palm_wt || now_ns-keytime >= maxaftertyping) && !momentumscrollcurrent)
        {
            if (!isTouchMode())
            {
                touchtime=now_ns;
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
        draglocktemp = _modifierdown & draglocktempmask;
    }
	if (touchmode==MODE_DRAGNOTOUCH && isFingerTouch(z))
    {
        if (dragTimer)
            cancelTimer(dragTimer);
		touchmode=MODE_DRAGLOCK;
    }
	////if ((w>wlimit || w<3) && isFingerTouch(z) && scroll && (wvdivisor || (hscroll && whdivisor)))
	if (MODE_MTOUCH != touchmode && (w>wlimit || w<2) && isFingerTouch(z))
    {
		touchmode=MODE_MTOUCH;
        tracksecondary=false;
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
    
    // dispatch dx/dy and current button status
    // if this isn't a thinkpad, dispatch the event like normal
    if (!isthinkpad) {
        dispatchRelativePointerEventX(dx / divisorx, dy / divisory, buttons, now_abs);
    }
    else {
        //On thinkpads we are going to filer out the middle mouse click if scrolling and issue the middle button on release
        if (mousemiddlescroll && buttons == 4) {
            thinkpadMiddleButtonPressed = true;
        }
        else {
            if (thinkpadMiddleButtonPressed) {
                if (!thinkpadMiddleScrolled) {
                    dispatchRelativePointerEventX(dx / divisorx, dy / divisory, 4, now_abs);
                }
            }
            else {
                dispatchRelativePointerEventX(dx / divisorx, dy / divisory, buttons, now_abs);
            }
            thinkpadMiddleButtonPressed = false;
            thinkpadMiddleScrolled = false;
        }
    }
    // always save last seen position for calculating deltas later
	lastx=x;
	lasty=y;
    lastf=f;
    
#ifdef DEBUG_VERBOSE
    IOLog("ps2: dx=%d, dy=%d (%d,%d) z=%d w=%d mode=(%d,%d,%d) buttons=%d wasdouble=%d\n", dx, dy, x, y, z, w, tm1, tm2, touchmode, buttons, wasdouble);
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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
    
    //
    // Parse the packet
    //
    
#ifdef SIMULATE_CLICKPAD
    packet[3] &= ~0x3;
    packet[3] |= (packet[0] & 0x1) | (packet[0] & 0x2)>>1;
    packet[0] &= ~0x3;
#endif
    
    UInt32 buttons = packet[0] & 0x03; // mask for just R L
    
    int xraw = (packet[1]<<1) | (packet[4]&0x0F)<<9;
    int yraw = (packet[2]<<1) | (packet[4]&0xF0)<<5;
#ifdef DEBUG_VERBOSE
    DEBUG_LOG("ps2: secondary finger pkt (%d, %d) (%04x, %04x) = { %02x, %02x, %02x, %02x, %02x, %02x }\n", xraw, yraw, xraw, yraw, packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
#endif
    // scale x & y to the axis which has the most resolution
    if (xupmm < yupmm)
        xraw = xraw * yupmm / xupmm;
    else if (xupmm > yupmm)
        yraw = yraw * xupmm / yupmm;
    int z = (packet[5]&0x0F)<<1 | (packet[3]&0x30)<<1;
    if (!isFingerTouch(z))
    {
        DEBUG_LOG("ps2: secondary finger packet received without finger touch (z=%d)\n", z);
        return;
    }
    ////int v = 0;
    if (_reportsv)
    {
        // if _reportsv is 1, v field (width) is encoded in x & y & z
        ////v = (packet[5]&0x1)<<2 | (packet[2]&0x1)<<1 | (packet[1]&0x1)<<0;
        xraw &= ~0x2;
        yraw &= ~0x2;
        z &= ~0x2;
    }
    int x = xraw;
    int y = yraw;
    ////int w = z + 8;
    
    uint64_t now_abs;
	clock_get_uptime(&now_abs);
    uint64_t now_ns;
    absolutetime_to_nanoseconds(now_abs, &now_ns);
    
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    buttons |= passbuttons;
    
    // if first secondary packet, clear some state...
    if (!tracksecondary)
    {
        x2_undo.reset();
        y2_undo.reset();
        x2_avg.reset();
        y2_avg.reset();
        xrest2 = 0;
        yrest2 = 0;
    }
    
    // unsmooth input (probably just for testing)
    // by default the trackpad itself does a simple decaying average (1/2 each)
    // we can undo it here
    if (unsmoothinput)
    {
        x = x2_undo.filter(x);
        y = y2_undo.filter(y);
    }
    
    // smooth input by unweighted average
    if (smoothinput)
    {
        x = x2_avg.filter(x);
        y = y2_avg.filter(y);
    }

    // deal with "OutsidezoneNoAction When Typing"
    if (outzone_wt && z>z_finger && now_ns-keytime < maxaftertyping &&
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
    
    if ((clickpadtrackboth || clickedprimary) && _clickbuttons)
    {
        // cannot calculate deltas first thing through...
        if (tracksecondary)
        {
            ////if ((palm && (w>wlimit || z>zlimit)))
            ////    return;
            dx = x-lastx2+xrest2;
            dy = lasty2-y+yrest2;
            xrest2 = dx % divisorx;
            yrest2 = dy % divisory;
            if (abs(dx) > bogusdxthresh || abs(dy) > bogusdythresh)
                dx = dy = xrest = yrest = 0;
            //If on a Thinkpad, the middle mouse (trackpoint) button is down and we're already scrolling then don't take action
            if (isthinkpad && mousemiddlescroll && (buttons | _clickbuttons) == 4) {
                //Do Nothing
            }
            else {
                dispatchRelativePointerEventX(dx / divisorx, dy / divisory, buttons|_clickbuttons, now_abs);
            }
        }
    }
    else
    {
        // Note: This probably should be different for two button ClickPads,
        // but we really don't know much about it and how/what the second button
        // on such a ClickPad is used.
        
        // deal with ClickPad touchpad packet
        if (clickpadtype)
        {
            // ClickPad puts its "button" presses in a different location
            // And for single button ClickPad we have to provide a way to simulate right clicks
            int clickbuttons = packet[3] & 0x3;
            
            //Let's quickly do some extra logic to see if we are pressing any of the physical buttons for the trackpoint
            if (isthinkpad) {
                // parse packets for buttons - TrackPoint Buttons may not be passthru
                int bp = packet[3] & 0x3; // 1 on clickpad or 2 for the 2 real buttons
                int lb = packet[4] & 0x3; // 1 for left real button
                int rb = packet[5] & 0x3; // 1 for right real button
                
                if (bp == 2)
                {
                    if      ( lb == 1 )
                    { // left click
                        clickbuttons = 0x1;
                    }
                    else if ( rb == 1 )
                    { // right click
                        clickbuttons = 0x2;
                    }
                    else if ( lb == 2 )
                    { // middle click
                        clickbuttons = 0x4;
                    }
                    else
                    {
                        clickbuttons = 0x0;
                    }
                    thinkpadButtonState = clickbuttons;
                    buttons=clickbuttons;
                    setClickButtons(clickbuttons);
                }
                else
                {
                    clickbuttons = bp;
                }
            }
            
            if (!_clickbuttons && clickbuttons)
            {
                // change to right click if in right click zone
                if (isInRightClickZone(x, y)
                    || (now_ns-touchtime < clickpadclicktime || MODE_NOTOUCH == touchmode))
                {
                    DEBUG_LOG("ps2s: setting clickbuttons to indicate right\n");
                    clickbuttons = 0x2;
                }
                else
                    DEBUG_LOG("ps2s: setting clickbuttons to indicate left\n");
                setClickButtons(clickbuttons);
                clickedprimary = false;
            }
            // always clear _clickbutton state, when ClickPad is not clicked
            if (!clickbuttons)
                setClickButtons(0);
            
            //Remember the button state on thinkpads.. this is required so we can handle the middle click vs middle scrolling appropriately.
            if (isthinkpad) {
                if (thinkpadButtonState)
                    _clickbuttons = thinkpadButtonState;
            }
            
            buttons |= _clickbuttons;
        }
        
        //If on a Thinkpad, the middle mouse (trackpoint) button is down and we're already scrolling then don't take action
        if (isthinkpad && mousemiddlescroll && buttons == 4) {
            //Do Nothing
        }
        else {
            dispatchRelativePointerEventX(0, 0, buttons, now_abs);
        }

    }

#ifdef DEBUG_VERBOSE
    DEBUG_LOG("ps2: (%d,%d,%d) secondary finger dx=%d, dy=%d (%d,%d) z=%d (%d,%d,%d,%d)\n", clickedprimary, _clickbuttons, tracksecondary, dx, dy, x, y, z, lastx2, lasty2, xrest2, yrest2);
#endif
    
    lastx2 = x;
    lasty2 = y;
    tracksecondary = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setTouchPadEnable( bool enable )
{
    //
    // Instructs the trackpad to start or stop the reporting of data packets.
    // It is safe to issue this request from the interrupt/completion context.
    //
    
    // (mouse enable/disable command)
    TPS2Request<1> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = enable ? kDP_Enable : kDP_SetDefaultsAndDisable;
    request.commandsCount = 1;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
}

// - -  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::getTouchPadStatus(  UInt8 buf3[] )
{
    TPS2Request<6> request;
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_GetMouseInformation;
    request.commands[2].command = kPS2C_ReadDataPort;
    request.commands[2].inOrOut = 0;
    request.commands[3].command = kPS2C_ReadDataPort;
    request.commands[3].inOrOut = 0;
    request.commands[4].command = kPS2C_ReadDataPort;
    request.commands[4].inOrOut = 0;
    request.commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut = kDP_SetDefaultsAndDisable;
    request.commandsCount = 6;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (6 != request.commandsCount)
        return false;
    
    buf3[0] = request.commands[2].inOrOut;
    buf3[1] = request.commands[3].inOrOut;
    buf3[2] = request.commands[4].inOrOut;
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::getTouchPadData(UInt8 dataSelector, UInt8 buf3[])
{
    TPS2Request<14> request;

    // Disable stream mode before the command sequence.
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;

    // 4 set resolution commands, each encode 2 data bits.
    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_SetMouseResolution;
    request.commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut  = (dataSelector >> 6) & 0x3;

    request.commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut  = kDP_SetMouseResolution;
    request.commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut  = (dataSelector >> 4) & 0x3;

    request.commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut  = kDP_SetMouseResolution;
    request.commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut  = (dataSelector >> 2) & 0x3;

    request.commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[7].inOrOut  = kDP_SetMouseResolution;
    request.commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[8].inOrOut  = (dataSelector >> 0) & 0x3;

    // Read response bytes.
    request.commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[9].inOrOut  = kDP_GetMouseInformation;
    request.commands[10].command = kPS2C_ReadDataPort;
    request.commands[10].inOrOut = 0;
    request.commands[11].command = kPS2C_ReadDataPort;
    request.commands[11].inOrOut = 0;
    request.commands[12].command = kPS2C_ReadDataPort;
    request.commands[12].inOrOut = 0;
    request.commands[13].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[13].inOrOut = kDP_SetDefaultsAndDisable;
    request.commandsCount = 14;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (14 != request.commandsCount)
        return false;
    
    // store results
    buf3[0] = request.commands[10].inOrOut;
    buf3[1] = request.commands[11].inOrOut;
    buf3[2] = request.commands[12].inOrOut;
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::initTouchPad()
{
    //
    // Clear packet buffer pointer to avoid issues caused by
    // stale packet fragments.
    //
    
    _packetByteCount = 0;
    _ringBuffer.reset();
    
    // clear passbuttons, just in case buttons were down when system
    // went to sleep (now just assume they are up)
    passbuttons = 0;
    _clickbuttons = 0;
    tracksecondary=false;
    
    // clear state of control key cache
    _modifierdown = 0;
    
    //
    // Resend the touchpad mode byte sequence
    // IRQ is enabled as side effect of setting mode byte
    // Also touchpad is enabled as side effect
    //
    
    setTouchpadModeByte();
    
    //
    // Set LED state as it is lost after sleep
    //
    updateTouchpadLED();
}

bool ApplePS2SynapticsTouchPad::setTouchpadModeByte()
{
    if (!_dynamicEW)
    {
        _touchPadModeByte = _extendedwmodeSupported ? _touchPadModeByte | (1<<2) : _touchPadModeByte & ~(1<<2);
        _extendedwmode = _extendedwmodeSupported;
    }
    return setTouchPadModeByte(_touchPadModeByte);
}

bool ApplePS2SynapticsTouchPad::setTouchPadModeByte(UInt8 modeByteValue)
{
    // make sure we are not early in the initialization...
    if (!_device)
        return false;
    
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
    
    // IMPORTANT: Currently this init sequence is 30 commands.  Current limit
    //  for a PS2Request is 30.  So don't add any. Break it into multiple
    //  requests!
    
    int i;
    TPS2Request<> request;
    
#ifdef FULL_HW_RESET
    // This was an attempt to solve wake from sleep problems.  Not needed.
    doHardwareReset();
#endif

#ifdef SET_STREAM_MODE
    // This was another attempt to solve wake from sleep problems.  Not needed.
    i = 0;
    request.commands[i++].inOrOut = kDP_SetMouseStreamMode;        // EA
    for (int x = 0; x < i; x++)
        request.commands[x].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commandsCount = i;
    assert(request.commandsCount <= countof(request.commands));
    DEBUG_LOG("VoodooPS2Trackpad: sending kDP_SetMouseStreamMode $EA\n");
    _device->submitRequestAndBlock(&request);
    if (i != request.commandsCount)
        DEBUG_LOG("VoodooPS2Trackpad: sending $EA failed: %d\n", request.commandsCount);
#endif
    
#ifdef UNDOCUMENTED_INIT_SEQUENCE_PRE
    // Also another attempt to solve wake from sleep problems.  Probably not needed.
    i = 0;
    // From chiby's latest post... to take care of wakup issues?
    request.commands[i++].inOrOut = kDP_SetMouseScaling2To1;       // E7
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6
    request.commands[i++].inOrOut = kDP_Enable;                    // F4
    for (int x = 0; x < i; x++)
        request.commands[x].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commandsCount = i;
    DEBUG_LOG("VoodooPS2Trackpad: sending undoc pre\n");
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (i != request.commandsCount)
        DEBUG_LOG("VoodooPS2Trackpad: sending undoc pre failed: %d\n", request.commandsCount);
#endif
    
    // Disable stream mode before the command sequence.
    i = 0;
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6
    
    // 4 set resolution commands, each encode 2 data bits.
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 6) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 4) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 2) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 0) & 0x3;    // 0x (depends on mode byte)
    
    // Set sample rate 20 to set mode byte 2. Older pads have 4 mode
    // bytes (0,1,2,3), but only mode byte 2 remain in modern pads.
    request.commands[i++].inOrOut = kDP_SetMouseSampleRate;        // F3
    request.commands[i++].inOrOut = 20;                            // 14
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6

#ifdef UNDOCUMENTED_INIT_SEQUENCE_POST
    // maybe this is commit?
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = 0x0;                           // 00
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = 0x0;                           // 00
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = 0x0;                           // 00
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = 0x3;                           // 03
    request.commands[i++].inOrOut = kDP_SetMouseSampleRate;        // F3
    request.commands[i++].inOrOut = 200;                           // C8
#endif

    // enable trackpad
    request.commands[i++].inOrOut = kDP_Enable;                    // F4
    
    DEBUG_LOG("VoodooPS2Trackpad: sending final init sequence...\n");
    
    // all these commands are "send mouse" and "compare ack"
    for (int x = 0; x < i; x++)
        request.commands[x].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commandsCount = i;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (i != request.commandsCount)
        DEBUG_LOG("VoodooPS2Trackpad: sending final init sequence failed: %d\n", request.commandsCount);

    return i == request.commandsCount;
}


void ApplePS2SynapticsTouchPad::setClickButtons(UInt32 clickButtons)
{
    UInt32 oldClickButtons = _clickbuttons;
    _clickbuttons = clickButtons;

    if (!!oldClickButtons != !!clickButtons)
        setModeByte();
}

bool ApplePS2SynapticsTouchPad::setModeByte()
{
    if (!_dynamicEW || !_extendedwmodeSupported)
        return false;

    _touchPadModeByte = _clickbuttons ? _touchPadModeByte | (1<<2) : _touchPadModeByte & ~(1<<2);
    _extendedwmode = _clickbuttons;

    return setModeByte(_touchPadModeByte);
}

// simplified setModeByte for switching between normal mode and EW mode
bool ApplePS2SynapticsTouchPad::setModeByte(UInt8 modeByteValue)
{
    // make sure we are not early in the initialization...
    if (!_device)
        return false;

    int i;
    TPS2Request<> request;

    // Disable stream mode before the command sequence.
    i = 0;
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6

    // 4 set resolution commands, each encode 2 data bits.
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 6) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 4) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 2) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByteValue >> 0) & 0x3;    // 0x (depends on mode byte)

    // Set sample rate 20 to set mode byte 2. Older pads have 4 mode
    // bytes (0,1,2,3), but only mode byte 2 remain in modern pads.
    request.commands[i++].inOrOut = kDP_SetMouseSampleRate;        // F3
    request.commands[i++].inOrOut = 20;                            // 14
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6

    // enable trackpad
    request.commands[i++].inOrOut = kDP_Enable;                    // F4

    // all these commands are "send mouse" and "compare ack"
    for (int x = 0; x < i; x++)
        request.commands[x].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commandsCount = i;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (i != request.commandsCount)
        DEBUG_LOG("VoodooPS2Trackpad: sestModeByte failed: %d\n", request.commandsCount);

    return i == request.commandsCount;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setParamPropertiesGated(OSDictionary * config)
{
	if (NULL == config)
		return;
    
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
        {"MouseMultiplierX",                &mousemultiplierx},
        {"MouseMultiplierY",                &mousemultipliery},
        {"MouseScrollMultiplierX",          &mousescrollmultiplierx},
        {"MouseScrollMultiplierY",          &mousescrollmultipliery},
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
        {"DragLockTempMask",                &draglocktempmask},
        {"MomentumScrollThreshY",           &momentumscrollthreshy},
        {"MomentumScrollMultiplier",        &momentumscrollmultiplier},
        {"MomentumScrollDivisor",           &momentumscrolldivisor},
        {"MomentumScrollSamplesMin",        &momentumscrollsamplesmin},
        {"FingerChangeIgnoreDeltas",        &ignoredeltasstart},
        {"BogusDeltaThreshX",               &bogusdxthresh},
        {"BogusDeltaThreshY",               &bogusdythresh},
        {"UnitsPerMMX",                     &xupmm},
        {"UnitsPerMMY",                     &yupmm},
        {"ScrollDeltaThreshX",              &scrolldxthresh},
        {"ScrollDeltaThreshY",              &scrolldythresh},
        // usr-sse2 added
        {"TrackpadCornerSecondaryClick",    &rightclick_corner},
        {"TrackpointScrollXMultiplier",     &thinkpadNubScrollXMultiplier},
        {"TrackpointScrollYMultiplier",     &thinkpadNubScrollYMultiplier},
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
        {"ForcePassThrough",                &forcepassthru},
        {"Thinkpad",                        &isthinkpad},
        {"HWResetOnStart",                  &hwresetonstart},
        {"SwapDoubleTriple",                &swapdoubletriple},
        {"ClickPadTrackBoth",               &clickpadtrackboth},
        {"ImmediateClick",                  &immediateclick},
        {"MouseMiddleScroll",               &mousemiddlescroll},
        {"FakeMiddleButton",                &_fakemiddlebutton},
        {"DynamicEWMode",                   &_dynamicEW},
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
        {"ClickPadClickTime",               &clickpadclicktime},
        {"MiddleClickTime",                 &_maxmiddleclicktime},
        {"DragExitDelayTime",               &dragexitdelay},
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
        setProperty("UseHighRate", bl->isTrue());
    }
    
    OSNumber *num;
    // 64-bit config items
    for (int i = 0; i < countof(int64vars); i++)
        if ((num=OSDynamicCast(OSNumber, config->getObject(int64vars[i].name))))
        {
            *int64vars[i].var = num->unsigned64BitValue();
            setProperty(int64vars[i].name, *int64vars[i].var, 64);
        }
    // boolean config items
	for (int i = 0; i < countof(boolvars); i++)
		if ((bl=OSDynamicCast (OSBoolean,config->getObject (boolvars[i].name))))
        {
			*boolvars[i].var = bl->isTrue();
            setProperty(boolvars[i].name, *boolvars[i].var ? kOSBooleanTrue : kOSBooleanFalse);
        }
    // 32-bit config items
	for (int i = 0; i < countof(int32vars);i++)
		if ((num=OSDynamicCast (OSNumber,config->getObject (int32vars[i].name))))
        {
			*int32vars[i].var = num->unsigned32BitValue();
            setProperty(int32vars[i].name, *int32vars[i].var, 32);
        }
    // lowbit config items
	for (int i = 0; i < countof(lowbitvars); i++)
    {
		if ((num=OSDynamicCast (OSNumber,config->getObject(lowbitvars[i].name))))
        {
			*lowbitvars[i].var = (num->unsigned32BitValue()&0x1)?true:false;
            setProperty(lowbitvars[i].name, *lowbitvars[i].var ? 1 : 0, 32);
        }
        //REVIEW: are these items ever carried in a boolean?
        else if ((bl=OSDynamicCast(OSBoolean, config->getObject(lowbitvars[i].name))))
        {
            *lowbitvars[i].var = bl->isTrue();
            setProperty(lowbitvars[i].name, *lowbitvars[i].var ? kOSBooleanTrue : kOSBooleanFalse);
        }
    }

    // special case for MaxDragTime (which is really max time for a double-click)
    // we can let it go no more than 230ms because otherwise taps on
    // the menu bar take too long if drag mode is enabled.  The code in that case
    // has to "hold button 1 down" for the duration of maxdragtime because if
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
    //  if the maxdragtime is too large (200ms is small enough, 500ms is too large)
    //    then clicking on menus breaks because the system sees it as a long
    //    press and hold
    //
    // fyi:
    //  also tried to allow release of button 1 during MODE_PREDRAG, and then when
    //   attempting to initiate the drag (in the case the second touch comes soon
    //   enough), modifying the time such that it is not seen as a double tap.
    //  unfortunately, that destroys double tap as well, probably because the
    //   system is confused seeing input "out of order"
    
    //if (maxdragtime > 230000000)
    //    maxdragtime = 230000000;
    
    // DivisorX and DivisorY cannot be zero, but don't crash if they are...
    if (!divisorx)
        divisorx = 1;
    if (!divisory)
        divisory = 1;

    // bogusdeltathreshx/y = 0 is MAX_INT
    if (!bogusdxthresh)
        bogusdxthresh = 0x7FFFFFFF;
    if (!bogusdythresh)
        bogusdythresh = 0x7FFFFFFF;

    // this driver assumes wmode is available (6-byte packets)
    _touchPadModeByte |= 1<<0;
    // extendedwmode is optional, used automatically for ClickPads
    if (!_dynamicEW)
        _touchPadModeByte = _extendedwmodeSupported ? _touchPadModeByte | (1<<2) : _touchPadModeByte & ~(1<<2);
	// if changed, setup touchpad mode
	if (_touchPadModeByte != oldmode)
    {
		setTouchpadModeByte();
        _packetByteCount=0;
        _ringBuffer.reset();
    }

//REVIEW: this should be done maybe only when necessary...
    touchmode=MODE_NOTOUCH;

    // check for special terminating sequence from PS2Daemon
    if (-1 == mousecount)
    {
        // when system is shutting down/restarting we want to force LED off
        if (ledpresent && !noled)
            setTouchpadLED(0x10);

        // if PS2M implements "TPDN" then, we can notify it of the shutdown
        // (allows implementation of LED change in ACPI)
        if (_provider)
        {
            if (OSNumber* num = OSNumber::withNumber(0xFFFF, 32))
            {
                _provider->evaluateObject(kTPDN, NULL, (OSObject**)&num, 1);
                num->release();
            }
        }

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
}

IOReturn ApplePS2SynapticsTouchPad::setParamProperties(OSDictionary* dict)
{
    ////IOReturn result = super::IOHIDevice::setParamProperties(dict);
    if (_cmdGate)
    {
        // syncronize through workloop...
        ////_cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2SynapticsTouchPad::setParamPropertiesGated), dict);
        setParamPropertiesGated(dict);
    }
    
    return super::setParamProperties(dict);
    ////return result;
}

IOReturn ApplePS2SynapticsTouchPad::setProperties(OSObject *props)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, props);
    if (dict && _cmdGate)
    {
        // syncronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2SynapticsTouchPad::setParamPropertiesGated), dict);
    }
    
	return super::setProperties(props);
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
            
            // Reset and enable the touchpad.
            initTouchPad();
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
                0x200000,   // 0x3f
            };
#ifdef SIMULATE_PASSTHRU
            static int buttons = 0;
            int button;
            switch (pInfo->adbKeyCode)
            {
                // make right Alt,Menu,Ctrl into three button passthru
                case 0x36:
                    button = 0x1;
                    goto dispatch_it;
                case 0x3f:
                    button = 0x4;
                    goto dispatch_it;
                case 0x3e:
                    button = 0x2;
                    // fall through...
                dispatch_it:
                    if (pInfo->goingDown)
                        buttons |= button;
                    else
                        buttons &= ~button;
                    UInt8 packet[6];
                    packet[0] = 0x84 | trackbuttons;
                    packet[1] = 0x08 | buttons;
                    packet[2] = 0;
                    packet[3] = 0xC4 | trackbuttons;
                    packet[4] = 0;
                    packet[5] = 0;
                    dispatchEventsWithPacket(packet, 6);
                    pInfo->eatKey = true;
            }
#endif
            switch (pInfo->adbKeyCode)
            {
                // don't store key time for modifier keys going down
                // track modifiers for scrollzoom feature...
                // (note: it turns out we didn't need to do this, but leaving this code in for now in case it is useful)
                case 0x38:  // left shift
                case 0x3c:  // right shift
                case 0x3b:  // left control
                case 0x3e:  // right control
                case 0x3a:  // left windows (option)
                case 0x3d:  // right windows
                case 0x37:  // left alt (command)
                case 0x36:  // right alt
                case 0x3f:  // osx fn (function)
                    if (pInfo->goingDown)
                    {
                        _modifierdown |= masks[pInfo->adbKeyCode-0x36];
                        break;
                    }
                    _modifierdown &= ~masks[pInfo->adbKeyCode-0x36];
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

    // if PS2M implements "TPDN" then, we can notify it of changes to LED state
    // (allows implementation of LED change in ACPI)
    if (_provider)
    {
        if (OSNumber* num = OSNumber::withNumber(ignoreall, 32))
        {
            _provider->evaluateObject(kTPDN, NULL, (OSObject**)&num, 1);
            num->release();
        }
    }
}

bool ApplePS2SynapticsTouchPad::setTouchpadLED(UInt8 touchLED)
{
    TPS2Request<12> request;
    
    // send NOP before special command sequence
    request.commands[0].inOrOut  = kDP_SetMouseScaling1To1;
    
    // 4 set resolution commands, each encode 2 data bits of LED level
    request.commands[1].inOrOut  = kDP_SetMouseResolution;
    request.commands[2].inOrOut  = (touchLED >> 6) & 0x3;
    request.commands[3].inOrOut  = kDP_SetMouseResolution;
    request.commands[4].inOrOut  = (touchLED >> 4) & 0x3;
    request.commands[5].inOrOut  = kDP_SetMouseResolution;
    request.commands[6].inOrOut  = (touchLED >> 2) & 0x3;
    request.commands[7].inOrOut  = kDP_SetMouseResolution;
    request.commands[8].inOrOut  = (touchLED >> 0) & 0x3;
    
    // Set sample rate 10 (10 is command for setting LED)
    request.commands[9].inOrOut  = kDP_SetMouseSampleRate;
    request.commands[10].inOrOut = 10; // 0x0A command for setting LED
    
    // finally send NOP command to end the special sequence
    request.commands[11].inOrOut  = kDP_SetMouseScaling1To1;
    request.commandsCount = 12;
    assert(request.commandsCount <= countof(request.commands));
    
    // all these commands are "send mouse" and "compare ack"
    for (int x = 0; x < request.commandsCount; x++)
        request.commands[x].command = kPS2C_SendMouseCommandAndCompareAck;
    _device->submitRequestAndBlock(&request);
    
    return 12 == request.commandsCount;
}

