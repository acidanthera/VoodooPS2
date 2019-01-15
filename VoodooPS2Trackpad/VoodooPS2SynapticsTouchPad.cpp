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
#include <IOKit/usb/USBSpec.h>
#include <IOKit/bluetooth/BluetoothAssignedNumbers.h>
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
    
    transducers = OSArray::withCapacity(4);
    if (!transducers) {
        return false;
    }
    DigitiserTransducerType type = kDigitiserTransducerFinger;
    for (int i = 0; i < 4; i++) {
        VoodooI2CDigitiserTransducer* transducer = VoodooI2CDigitiserTransducer::transducer(type, NULL);
        transducers->setObject(transducer);
    }

    // initialize state...
    _device = NULL;
    _interruptHandlerInstalled = false;
    _powerControlHandlerInstalled = false;
    _packetByteCount = 0;
    _lastdata = 0;
    _touchPadModeByte = 0x80; //default: absolute, low-rate, no w-mode
    _cmdGate = 0;
    _provider = NULL;
    
    // init my stuff
    memset(&agmState, 0, sizeof(struct synaptics_hw_state));
    agmFingerCount = 0;
    lastFingerCount = 0;
    
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
    
    _processusbmouse = true;
    _processbluetoothmouse = true;
    
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
    usb_mouse_stops_trackpad = true;
    _modifierdown = 0;
    scrollzoommask = 0;
    
    inSwipeLeft=inSwipeRight=inSwipeDown=inSwipeUp=0;
    xmoved=ymoved=0;
    
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
        OSBoolean* disable = OSDynamicCast(OSBoolean, config->getObject(kDisableDevice));
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
    OSSafeReleaseNULL(config);

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
    
    if (forcepassthru)
    {
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
        setProperty("0xc Query", buf3, 3);
        DEBUG_LOG("VoodooPS2Trackpad: Continued Capabilities($0C) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 5 && getTouchPadData(0xd, buf3))
    {
        if(mt_interface) {
            mt_interface->logical_max_x = (buf3[0] << 5) | ((buf3[1] & 0x0f) << 1);
            mt_interface->logical_max_y = (buf3[2] << 5) | ((buf3[1] & 0xf0) >> 3);

           // mt_interface->logical_max_x = xupmm;
           // mt_interface->logical_max_y = yupmm;
        }
        
        DEBUG_LOG("VoodooPS2Trackpad: Maximum coords($0D) bytes = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 6 && getTouchPadData(0xe, buf3))
    {
        DEBUG_LOG("VoodooPS2Trackpad: Deluxe LED bytes($0E) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
    if (nExtendedQueries >= 7 && getTouchPadData(0xf, buf3))
    {
        if(mt_interface) {
            mt_interface->logical_min_x = (buf3[0] << 5) | ((buf3[1] & 0x0f) << 1);
            mt_interface->logical_min_y = (buf3[2] << 5) | ((buf3[1] & 0xf0) >> 3);
            
            mt_interface->logical_max_x -= mt_interface->logical_min_x;
            mt_interface->logical_max_y -= mt_interface->logical_min_y;
            
            setProperty("Min X", mt_interface->logical_min_x);
            setProperty("Min Y", mt_interface->logical_min_y);
            
            setProperty("Max X", mt_interface->logical_max_x);
            setProperty("Max Y", mt_interface->logical_max_y);
            
            mt_interface->physical_max_x = 1.0 / ( (mt_interface->logical_max_x - mt_interface->logical_min_x) * xupmm * 1.0f);
            mt_interface->physical_max_y = 1.0 / ( (mt_interface->logical_max_y - mt_interface->logical_min_y) * yupmm * 1.0f);
        }
        DEBUG_LOG("VoodooPS2Trackpad: Minimum coords bytes($0F) = { 0x%x, 0x%x, 0x%x }\n", buf3[0], buf3[1], buf3[2]);
    }
}

bool ApplePS2SynapticsTouchPad::publish_multitouch_interface() {
    mt_interface = new VoodooI2CMultitouchInterface();
    if (!mt_interface) {
        DEBUG_LOG("VoodooPS2Trackpad No memory to allocate VoodooI2CMultitouchInterface instance\n");
        goto multitouch_exit;
    }
    if (!mt_interface->init(NULL)) {
        DEBUG_LOG("VoodooPS2Trackpad Failed to init multitouch interface\n");
        goto multitouch_exit;
    }
    if (!mt_interface->attach(this)) {
        DEBUG_LOG("VoodooPS2Trackpad Failed to attach multitouch interface\n");
        goto multitouch_exit;
    }
    if (!mt_interface->start(this)) {
        DEBUG_LOG("VoodooPS2Trackpad Failed to start multitouch interface\n");
        goto multitouch_exit;
    }
    // Assume we are a touchpad
    mt_interface->setProperty(kIOHIDDisplayIntegratedKey, false);
    // 0x04f3 is Elan's Vendor Id
    mt_interface->setProperty(kIOHIDVendorIDKey, 0x04f3, 32);
    mt_interface->setProperty(kIOHIDProductIDKey, 0x01, 32);

    
    return true;
multitouch_exit:
    unpublish_multitouch_interface();
    return false;
}


void ApplePS2SynapticsTouchPad::unpublish_multitouch_interface() {
    if (mt_interface) {
        mt_interface->stop(this);
        // mt_interface->release();
        // mt_interface = NULL;
    }
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

    attachedHIDPointerDevices = OSSet::withCapacity(1);
    registerHIDPointerNotifications();

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
    // Lock the controller during initialization
    //
    
    _device->lock();
    
    //
    // Some machines require a hw reset in order to work correctly -- notably Thinkpads with Trackpoints
    //
    if (hwresetonstart)
    {
        doHardwareReset();
    }
    
    // Set up MT interface
    publish_multitouch_interface();
    
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
    // Request message registration for keyboard to trackpad communication
    //
    //setProperty(kDeliverNotifications, true);
    
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
    
    if(mt_interface) {
        mt_interface->registerService();
    }
    
    
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

    unregisterHIDPointerNotifications();
    OSSafeReleaseNULL(attachedHIDPointerDevices);
    
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
    // Release the pointer to the provider object.
    //
    
    OSSafeReleaseNULL(_device);
    
    //
    // Release ACPI provider for PS2M ACPI device
    //
    OSSafeReleaseNULL(_provider);
    
    unpublish_multitouch_interface();
    
    if (transducers) {
        for (int i = 0; i < transducers->getCount(); i++) {
            OSObject* object = transducers->getObject(i);
            if (object) {
                object->release();
            }
        }
        OSSafeReleaseNULL(transducers);
    }

	super::stop(provider);
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
            //dispatchEventsWithPacket(_ringBuffer.tail(), kPacketLength);
            if (!ignoreall)
                parse_input(_ringBuffer.tail(), kPacketLength);
        }
        else
        {
            // a reset packet was buffered... schedule a complete reset
            ////initTouchPad();
        }
        _ringBuffer.advanceTail(kPacketLength);
    }
}

int ApplePS2SynapticsTouchPad::synaptics_parse_hw_state(const UInt8 buf[])
{
    if(!mt_interface) {
        return -1;
    }

    // Ignore input for specified time after keyboard usage
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    uint64_t timestamp_ns;
    absolutetime_to_nanoseconds(timestamp, &timestamp_ns);
    
    int w = (((buf[0] & 0x30) >> 2) |
             ((buf[0] & 0x04) >> 1) |
             ((buf[3] & 0x04) >> 2));
    
    synaptics_hw_state hw;
    memset(&hw, 0, sizeof(synaptics_hw_state));
    int x, y, z = 0;


    DEBUG_LOG("VoodooPS2 w: %d\n", w);
    
    // advanced gesture packet (half-resolution packets)
    // my port of synaptics_parse_agm from synaptics.c from Linux Kernel
    if(w == 2) {
        int agmPacketType = (buf[5] & 0x30) >> 4;
        
        switch(agmPacketType) {
            case 1:
                agmState.w = w;
                agmState.x = (((buf[4] & 0x0f) << 8) | buf[1]) << 1;
                agmState.y = (((buf[4] & 0xf0) << 4) | buf[2]) << 1;
                agmState.z = ((buf[3] & 0x30) | (buf[5] & 0x0f)) << 1;
                
                if (agmState.x > X_MAX_POSITIVE)
                    agmState.x -= 1 << ABS_POS_BITS;
                else if (agmState.x == X_MAX_POSITIVE)
                    agmState.x = XMAX;
                
                if (agmState.y > Y_MAX_POSITIVE)
                    agmState.y -= 1 << ABS_POS_BITS;
                else if (agmState.y == Y_MAX_POSITIVE)
                    agmState.y = YMAX;
            case 2:
                agmFingerCount = buf[1];
                break;
            default:
                break;
        }
        
        return 1;
    } else {
        // normal "packet"
        // my port of synaptics_parse_hw_state from synaptics.c from Linux Kernel
        x = (((buf[3] & 0x10) << 8) |
                 ((buf[1] & 0x0f) << 8) |
                 buf[4]);
        y = (((buf[3] & 0x20) << 7) |
                 ((buf[1] & 0xf0) << 4) |
                 buf[5]);
        z = buf[2]; // pressure
        
        hw.x = x;
        hw.y = y;
        hw.z = z;
        
        hw.left = (w >= 4 && ((buf[0] ^ buf[3]) & 0x01));

        if (hw.x > X_MAX_POSITIVE)
            hw.x -= 1 << ABS_POS_BITS;
        else if (hw.x == X_MAX_POSITIVE)
            hw.x = XMAX;
        
        if (hw.y > Y_MAX_POSITIVE)
            hw.y -= 1 << ABS_POS_BITS;
        else if (hw.y == Y_MAX_POSITIVE)
            hw.y = YMAX;
    }
    
    // count the number of fingers
    // my port of synaptics_image_sensor_process from synaptics.c from Linux Kernel
    int fingerCount = 0;
    if(hw.z == 0) {
        fingerCount = 0;
    } else if(w >= 4) {
        fingerCount = 1;
    } else if(w == 0) {
        fingerCount = 2;
    } else if(w == 1) {
        fingerCount = 3;
    } else {
        fingerCount = 4;
    }
    
    int clampedFingerCount = fingerCount;
    if(!fingerCount) {
        clampedFingerCount = lastFingerCount;
    }
    
    if (clampedFingerCount > 4) {
        clampedFingerCount = 4;
    } else if(clampedFingerCount < 0) {
        clampedFingerCount = 0;
    }
    // We really need to send the "no touch" event
    // multiple times, because if we don't do it and return,
    // gestures like desktop switching or inertial scrolling
    // got stuck midway until the next touch.
    //if(!lastFingerCount && !clampedFingerCount) {
    //    return 0;
    //}

    synaptics_hw_state midpoint;
    midpoint.x = (hw.x + agmState.x) / 2;
    midpoint.y = (hw.y + agmState.y) / 2;
    
    synaptics_hw_state *states[3] =  { &hw, &agmState, &midpoint};  

    for(int i = 0; i < clampedFingerCount; i++) {
        VoodooI2CDigitiserTransducer* transducer = OSDynamicCast(VoodooI2CDigitiserTransducer, transducers->getObject(i));
        if(!transducer) {
            continue;
        }
        
        synaptics_hw_state *state;
        if(i >= 3) {
            state = &midpoint;
        } else {
            state = states[i];
        }
        
        transducer->type = kDigitiserTransducerFinger;
        transducer->is_valid = fingerCount;
        
        if(transducer->is_valid) {
            int posX = state->x;
            int posY = state->y;
            
            // fake finger data
            if(i >= 4) {
                posX = state->x + (i * 100);
                posY = state->y + (i * 100);
            }
            
            posX -= mt_interface->logical_min_x;
            posY = mt_interface->logical_max_y - posY + mt_interface->logical_min_y;
            
            transducer->coordinates.x.update(posX, timestamp);
            transducer->coordinates.y.update(posY, timestamp);
            transducer->physical_button.update(state->left, timestamp);
            transducer->tip_switch.update(1, timestamp);
            transducer->id = i;
            transducer->secondary_id = i;
        } else {
            transducer->id = i;
            transducer->secondary_id = i;
            transducer->coordinates.x.update(transducer->coordinates.x.last.value, timestamp);
            transducer->coordinates.y.update(transducer->coordinates.y.last.value, timestamp);
            transducer->physical_button.update(0, timestamp);
            transducer->tip_switch.update(0, timestamp);
        }
    }
    
    // create new VoodooI2CMultitouchEvent
    VoodooI2CMultitouchEvent event;
    event.contact_count = clampedFingerCount;
    event.transducers = transducers;
    // send the event into the multitouch interface
    
    mt_interface->handleInterruptReport(event, timestamp);
    
    lastFingerCount = fingerCount;

    if(!fingerCount) {
        memset(&agmState, 0, sizeof(struct synaptics_hw_state));
    }
    
    return 0;
}

void ApplePS2SynapticsTouchPad::parse_input(UInt8* packet, UInt32 packetSize) {
    synaptics_parse_hw_state(packet);
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
        {"ProcessUSBMouseStopsTrackpad",    &_processusbmouse},
        {"ProcessBluetoothMouseStopsTrackpad", &_processbluetoothmouse},
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
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
        {"MaxDragTime",                     &maxdragtime},
        {"MaxTapTime",                      &maxtaptime},
        {"HIDClickTime",                    &maxdbltaptime},
        {"QuietTimeAfterTyping",            &maxaftertyping},
        {"ClickPadClickTime",               &clickpadclicktime},
        {"MiddleClickTime",                 &_maxmiddleclicktime},
    };
    
	uint8_t oldmode = _touchPadModeByte;
    
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

    // disable trackpad when USB mouse is plugged in and this functionality is requested
    if (attachedHIDPointerDevices && attachedHIDPointerDevices->getCount() > 0) {
        ignoreall = usb_mouse_stops_trackpad;
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
        // synchronize through workloop...
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

IOReturn ApplePS2SynapticsTouchPad::message(UInt32 type, IOService* provider, void* argument)
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
    switch (type)
    {
        case kPS2M_getDisableTouchpad:
        {
            bool* pResult = (bool*)argument;
            *pResult = !ignoreall;
            break;
        }
            
        case kPS2M_setDisableTouchpad:
        {
            bool enable = *((bool*)argument);
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
            PS2KeyInfo* pInfo = (PS2KeyInfo*)argument;
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
            /*
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
             */
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
                    keytime = pInfo->time;
            }
            break;
        }
    }
    
    return kIOReturnSuccess;
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

void ApplePS2SynapticsTouchPad::registerHIDPointerNotifications()
{
    IOServiceMatchingNotificationHandler notificationHandler = OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this, &ApplePS2SynapticsTouchPad::notificationHIDAttachedHandler);
    
    // Determine if we should listen for USB mouse attach events as per configuration
    if (_processusbmouse) {
        // USB mouse HID description as per USB spec: http://www.usb.org/developers/hidpage/HID1_11.pdf
        OSDictionary* matchingDictionary = serviceMatching("IOUSBInterface");
        
        propertyMatching(OSSymbol::withCString(kUSBInterfaceClass), OSNumber::withNumber(kUSBHIDInterfaceClass, 8), matchingDictionary);
        propertyMatching(OSSymbol::withCString(kUSBInterfaceSubClass), OSNumber::withNumber(kUSBHIDBootInterfaceSubClass, 8), matchingDictionary);
        propertyMatching(OSSymbol::withCString(kUSBInterfaceProtocol), OSNumber::withNumber(kHIDMouseInterfaceProtocol, 8), matchingDictionary);
        
        // Register for future services
        usb_hid_publish_notify = addMatchingNotification(gIOFirstPublishNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        usb_hid_terminate_notify = addMatchingNotification(gIOTerminatedNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        OSSafeReleaseNULL(matchingDictionary);
    }
    
    // Determine if we should listen for bluetooth mouse attach events as per configuration
    if (_processbluetoothmouse) {
        // Bluetooth HID devices
        OSDictionary* matchingDictionary = serviceMatching("IOBluetoothHIDDriver");
        propertyMatching(OSSymbol::withCString(kIOHIDVirtualHIDevice), OSBoolean::withBoolean(false), matchingDictionary);
        
        // Register for future services
        bluetooth_hid_publish_notify = addMatchingNotification(gIOFirstPublishNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        bluetooth_hid_terminate_notify = addMatchingNotification(gIOTerminatedNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        OSSafeReleaseNULL(matchingDictionary);
    }
}

void ApplePS2SynapticsTouchPad::unregisterHIDPointerNotifications()
{
    // Free device matching notifiers
    if (usb_hid_publish_notify) {
        usb_hid_publish_notify->remove();
        OSSafeReleaseNULL(usb_hid_publish_notify);
    }
    
    if (usb_hid_terminate_notify) {
        usb_hid_terminate_notify->remove();
        OSSafeReleaseNULL(usb_hid_terminate_notify);
    }
    
    if (bluetooth_hid_publish_notify) {
        bluetooth_hid_publish_notify->remove();
        OSSafeReleaseNULL(bluetooth_hid_publish_notify);
    }
    
    if (bluetooth_hid_terminate_notify) {
        bluetooth_hid_terminate_notify->remove();
        OSSafeReleaseNULL(bluetooth_hid_terminate_notify);
    }
    
    attachedHIDPointerDevices->flushCollection();
}

void ApplePS2SynapticsTouchPad::notificationHIDAttachedHandlerGated(IOService * newService,
                                                                    IONotifier * notifier)
{
    char path[256];
    int len = 255;
    memset(path, 0, len);
    newService->getPath(path, &len, gIOServicePlane);
    
    if (notifier == usb_hid_publish_notify) {
        attachedHIDPointerDevices->setObject(newService);
        DEBUG_LOG("%s: USB pointer HID device published: %s, # devices: %d\n", getName(), path, attachedHIDPointerDevices->getCount());
    }
    
    if (notifier == usb_hid_terminate_notify) {
        attachedHIDPointerDevices->removeObject(newService);
        DEBUG_LOG("%s: USB pointer HID device terminated: %s, # devices: %d\n", getName(), path, attachedHIDPointerDevices->getCount());
    }
    
    if (notifier == bluetooth_hid_publish_notify) {
        
        // Filter on specific CoD (Class of Device) bluetooth devices only
        OSNumber* propDeviceClass = OSDynamicCast(OSNumber, newService->getProperty("ClassOfDevice"));
        
        if (propDeviceClass != NULL) {
            
            long classOfDevice = propDeviceClass->unsigned32BitValue();
            
            long deviceClassMajor = (classOfDevice & 0x1F00) >> 8;
            long deviceClassMinor = (classOfDevice & 0xFF) >> 2;
            
            if (deviceClassMajor == kBluetoothDeviceClassMajorPeripheral) { // Bluetooth peripheral devices
                
                long deviceClassMinor1 = (deviceClassMinor) & 0x30;
                long deviceClassMinor2 = (deviceClassMinor) & 0x0F;
                
                if (deviceClassMinor1 == kBluetoothDeviceClassMinorPeripheral1Pointing || // Seperate pointing device
                    deviceClassMinor1 == kBluetoothDeviceClassMinorPeripheral1Combo) // Combo bluetooth keyboard/touchpad
                {
                    if (deviceClassMinor2 == kBluetoothDeviceClassMinorPeripheral2Unclassified || // Mouse
                        deviceClassMinor2 == kBluetoothDeviceClassMinorPeripheral2DigitizerTablet || // Magic Touchpad
                        deviceClassMinor2 == kBluetoothDeviceClassMinorPeripheral2DigitalPen) // Wacom Tablet
                    {
                        
                        attachedHIDPointerDevices->setObject(newService);
                        DEBUG_LOG("%s: Bluetooth pointer HID device published: %s, # devices: %d\n", getName(), path, attachedHIDPointerDevices->getCount());
                    }
                }
            }
        }
    }
    
    if (notifier == bluetooth_hid_terminate_notify) {
        attachedHIDPointerDevices->removeObject(newService);
        DEBUG_LOG("%s: Bluetooth pointer HID device terminated: %s, # devices: %d\n", getName(), path, attachedHIDPointerDevices->getCount());
    }
    
    if (notifier == usb_hid_publish_notify || notifier == bluetooth_hid_publish_notify) {
        if (usb_mouse_stops_trackpad && attachedHIDPointerDevices->getCount() > 0) {
            // One or more USB or Bluetooth pointer devices attached, disable trackpad
            ignoreall = true;
            updateTouchpadLED();
        }
    }
    
    if (notifier == usb_hid_terminate_notify || notifier == bluetooth_hid_terminate_notify) {
        if (usb_mouse_stops_trackpad && attachedHIDPointerDevices->getCount() == 0) {
            // No USB or bluetooth pointer devices attached, re-enable trackpad
            ignoreall = false;
            updateTouchpadLED();
        }
    }
}

bool ApplePS2SynapticsTouchPad::notificationHIDAttachedHandler(void * refCon,
                                                               IOService * newService,
                                                               IONotifier * notifier)
{
    if (_cmdGate) { // defensive
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2SynapticsTouchPad::notificationHIDAttachedHandlerGated), newService, notifier);
    }

    return true;
}

