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

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/usb/IOUSBHostFamily.h>
#include <IOKit/usb/IOUSBHostHIDDevice.h>
#include <IOKit/bluetooth/BluetoothAssignedNumbers.h>
#include "VoodooPS2Controller.h"
#include "VoodooPS2SynapticsTouchPad.h"
#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"

// =============================================================================
// ApplePS2SynapticsTouchPad Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2SynapticsTouchPad, IOService);

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
    for (int i = 0; i < SYNAPTICS_MAX_FINGERS; i++)
        fingerStates[i].virtualFingerIndex = -1;

	memset(freeFingerTypes, true, kMT2FingerTypeCount);
	freeFingerTypes[kMT2FingerTypeUndefined] = false;

    // announce version
	extern kmod_info_t kmod_info;
    DEBUG_LOG("VoodooPS2SynapticsTouchPad: Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);

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
			_device = 0;
            return 0;
        }
#ifdef DEBUG
        // save configuration for later/diagnostics...
        setProperty(kMergedConfiguration, config);
#endif

      // load settings specific to Platform Profile
      setPropertiesGated(config);
      injectVersionDependentProperties(config);
      OSSafeReleaseNULL(config);
    }

    bool success = getTouchPadData(SYNAPTICS_IDENTIFY_QUERY, reinterpret_cast<uint8_t*>(&_identity));
    if (!success)
    {
        IOLog("VoodooPS2Trackpad: Identify TouchPad command failed\n");
        return 0;
    }
    
    INFO_LOG("VoodooPS2Trackpad: Identity = { 0x%x.%x, constant: %x }\n",
             _identity.major_ver, _identity.minor_ver, _identity.synaptics_const);
    
    // 0x46 seemed to be returned on laptops with muxed PS/2 ports. Since muxing was added,
    // I have not seen any devices return 0x46 ~ Avery
    if (_identity.synaptics_const != 0x47)
    {
        IOLog("VoodooPS2Trackpad: Identify TouchPad command returned incorrect byte 2 (of 3): 0x%02x\n",
              _identity.synaptics_const);
        return 0;
    }
    
    // Only support 4.x or later touchpads.
    if (_identity.major_ver < 0x4)
    {
        IOLog("VoodooPS2Trackpad: TouchPad(0x47) v%d.%d is not supported\n",
              _identity.major_ver, _identity.minor_ver);
        return 0;
    }
    
    _device = 0;

    DEBUG_LOG("ApplePS2SynapticsTouchPad::probe leaving.\n");
    
    return this;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::doHardwareReset()
{
    TPS2Request<> request;
    int i = 0;
    request.commands[i].command = kPS2C_SendCommandAndCompareAck;
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i].command = kPS2C_SendCommandAndCompareAck;
    request.commands[i++].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commands[i].command = kPS2C_WriteDataPort;
    request.commands[i++].inOrOut = kDP_Reset;                     // FF
    request.commands[i].command = kPS2C_ReadDataPortAndCompare;
    request.commands[i++].inOrOut = kSC_Acknowledge;
    request.commands[i].command = kPS2C_SleepMS;
    request.commands[i++].inOrOut32 = wakedelay*2;
    request.commands[i].command = kPS2C_ReadDataPortAndCompare;
    request.commands[i++].inOrOut = 0xAA;
    request.commands[i].command = kPS2C_ReadDataPortAndCompare;
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
    synaptics_logic_min_max logic_size;
    synaptics_model model_data;
    UInt8 *buf;
    
    bzero(&logic_size, sizeof(synaptics_logic_min_max));
    bzero(&model_data, sizeof(synaptics_model));
    
    // get TouchPad general capabilities
    buf = reinterpret_cast<UInt8 *>(&_capabilities);
    if (!getTouchPadData(SYNA_CAPABILITIES_QUERY, buf)) {
        bzero(&_capabilities, sizeof(_capabilities));
    }
        
    INFO_LOG("VoodooPS2Trackpad: nExtendedQueries=%d\n", _capabilities.extended_queries);
    INFO_LOG("VoodooPS2Trackpad: supports EW=%d\n", _capabilities.extended_w_supported);
    
    buf = reinterpret_cast<UInt8 *>(&model_data);
    if (getTouchPadData(SYNA_MODEL_QUERY, buf)) {
        UInt16 combined_version = (_identity.major_ver << 8) | _identity.minor_ver;
        if (combined_version >= 0x705) {
            setProperty("Board ID", model_data.model_number, 32);
        }
    }
    
#ifdef SIMULATE_PASSTHRU
    _capabilities.passthrough = 1;
#endif
    
    INFO_LOG("VoodooPS2Trackpad: Passthrough=%d, Guest Present=%d\n",
             _capabilities.passthrough, model_data.guest_present);
    
    // Get button data in case VoodooRMI needs it
    if (model_data.more_extended_caps) {
        buf = reinterpret_cast<UInt8 *>(&_securepad);
        if (!getTouchPadData(SYNA_SECUREPAD_QUERY, buf)) {
            bzero(&_securepad, sizeof(synaptics_securepad_id));
        }
    }
    
    buf = reinterpret_cast<UInt8 *>(&_extended_id);
    if (_capabilities.extended_queries >= 1 && getTouchPadData(SYNA_EXTENDED_ID_QUERY, buf)) {
        INFO_LOG("VoodooPS2Trackpad: ledpresent=%d\n", _extended_id.has_leds);
        
        if (_extended_id.extended_btns > SYNAPTICS_MAX_EXT_BTNS) {
            INFO_LOG("VoodooPS2Trackpad: Too many external buttons!\n");
            _extended_id.extended_btns = 0;
        }
    }
    
    // get resolution data for scaling x -> y or y -> x depending
    if (!getTouchPadData(SYNA_SCALE_QUERY, reinterpret_cast<uint8_t *>(&_scale)) ||
        _scale.xupmm == 0 || _scale.yupmm == 0) {
        // "Typical" values from docs
        _scale.xupmm = 85;
        _scale.yupmm = 94;
    }
    
    bool reports_min = false;
    bool reports_max = false;
    
    buf = reinterpret_cast<UInt8 *>(&_cont_caps);
    if (_capabilities.extended_queries >= 4 && getTouchPadData(SYNA_CONT_CAPS_QUERY, buf)) {
        INFO_LOG("VoodooPS2Trackpad: Continued Capabilities($0C) = { smbus_addr=0x%x }\n",  _cont_caps.intertouch_addr);

#ifdef SIMULATE_CLICKPAD
        _const_caps.one_btn_clickpad = 1;
        DEBUG_LOG("VoodooPS2Trackpad: clickpadtype=1 simulation set\n");
#endif
        
        reports_min = _cont_caps.reports_min;
        reports_max = _cont_caps.reports_max;
        INFO_LOG("VoodooPS2Trackpad: clickpadtype=%d\n", _cont_caps.one_btn_clickpad);
        INFO_LOG("VoodooPS2Trackpad: _reportsv=%d\n", _cont_caps.reports_v);
        
        if (_cont_caps.intertouch) {
            IOLog("VoodooPS2Trackpad: Trackpad supports Intertouch/SMBus operation\n");
            setProperty("Intertouch Support", kOSBooleanTrue);
        }
    }
    
    buf = reinterpret_cast<UInt8 *>(&logic_size);
    if (reports_max && getTouchPadData(SYNA_LOGIC_MAX_QUERY, buf))
    {
        logical_max_x = SYNA_LOGIC_X(logic_size);
        logical_max_y = SYNA_LOGIC_Y(logic_size);
        INFO_LOG("VoodooPS2Trackpad: Maximum coords($0D) = { 0x%x, 0x%x }\n",
                 logical_max_x, logical_max_y);
    }

    // 5 mm margins
    margin_size_x = 5 * _scale.xupmm;
    margin_size_y = 5 * _scale.yupmm;

    if (reports_min && getTouchPadData(SYNA_LOGIC_MIN_QUERY, buf))
    {
        logical_min_x = SYNA_LOGIC_X(logic_size);
        logical_min_y = SYNA_LOGIC_Y(logic_size);
        DEBUG_LOG("VoodooPS2Trackpad: Minimum coords($0F) = { 0x%x, 0x%x }\n",
                  logical_min_x, logical_min_y);
    }
    else {
        logical_min_x = logical_max_x - 3 * margin_size_x;
        logical_min_y = logical_max_y - 3 * margin_size_y;
    }

    // We should set physical dimensions anyway
    logical_min_x += margin_size_x;
    logical_min_y += margin_size_y;
    logical_max_x -= margin_size_x;
    logical_max_y -= margin_size_y;

    if (minXOverride != -1)
        logical_min_x = minXOverride;
    if (minYOverride != -1)
        logical_min_y = minYOverride;
    if (maxXOverride != -1)
        logical_max_x = maxXOverride;
    if (maxYOverride != -1)
        logical_max_y = maxYOverride;

    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, logical_max_x - logical_min_x, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, logical_max_y - logical_min_y, 32);

    // physical dimensions are specified in 0.01 mm units
    physical_max_x = (logical_max_x + 1 - (reports_min ? logical_min_x : 0)) * 100 / _scale.xupmm;
    physical_max_y = (logical_max_y + 1 - (reports_min ? logical_min_y : 0)) * 100 / _scale.yupmm;

    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, physical_max_x, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, physical_max_y, 32);

    setProperty(VOODOO_INPUT_TRANSFORM_KEY, 0ull, 32);
    
    setTrackpointProperties();
    
    setProperty("VoodooInputSupported", kOSBooleanTrue);

    // Helpful information for SMBus drivers
    OSDictionary *dictionary = OSDictionary::withCapacity(2);
    dictionary->setObject("TrackstickButtons", _securepad.trackstick_btns ?
                          kOSBooleanTrue : kOSBooleanFalse);
    dictionary->setObject("Clickpad", _cont_caps.one_btn_clickpad ?
                          kOSBooleanTrue : kOSBooleanFalse);
    setProperty("GPIO Data", dictionary);
    
    OSSafeReleaseNULL(dictionary);
    
    INFO_LOG("VoodooPS2Trackpad: logical %dx%d-%dx%d physical_max %dx%d upmm %dx%d",
          logical_min_x, logical_min_y,
          logical_max_x, logical_max_y,
          physical_max_x, physical_max_y,
          _scale.xupmm, _scale.yupmm);
}

bool ApplePS2SynapticsTouchPad::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();

        return true;
    }

    return super::handleOpen(forClient, options, arg);
}

void ApplePS2SynapticsTouchPad::handleClose(IOService *forClient, IOOptionBits options) {
    OSSafeReleaseNULL(voodooInputInstance);
    super::handleClose(forClient, options);
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

    IOLog("VoodooPS2Trackpad starting: Synaptics TouchPad reports version %d.%d\n",
          _identity.major_ver, _identity.minor_ver);
    char buf[128];
    snprintf(buf, sizeof(buf), "type 0x%02x, version %d.%d",
             0x47, _identity.major_ver, _identity.minor_ver);
    setProperty("RM,TrackpadInfo", buf);
    
    //
    // Setup workloop with command gate for thread syncronization...
    //
    IOWorkLoop* pWorkLoop = getWorkLoop();
    _cmdGate = IOCommandGate::commandGate(this);
    if (!pWorkLoop || !_cmdGate)
    {
        _device->release();
		_device = nullptr;
        return false;
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

    attachedHIDPointerDevices = OSSet::withCapacity(1);
    registerHIDPointerNotifications();
    
    //
    // Query the touchpad for the capabilities we need to know.
    //
    queryCapabilities();
    
    //
    // Set the touchpad mode byte, which will also...
    // Enable the mouse clock (should already be so) and the mouse IRQ line.
    // Enable the touchpad itself.
    //
    enterAdvancedGestureMode();

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
    registerService();
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
            if (!ignoreall)
                synaptics_parse_hw_state(_ringBuffer.tail());
        }
        else
        {
            // a reset packet was buffered... schedule a complete reset
            //initTouchPad();
        }
        _ringBuffer.advanceTail(kPacketLength);
    }
}

#define sqr(x) ((x) * (x))
int ApplePS2SynapticsTouchPad::dist(int physicalFinger, int virtualFinger) {
    const auto &phy = fingerStates[physicalFinger];
    const auto &virt = virtualFingerStates[virtualFinger];
    return sqr(phy.x - virt.x_avg.newest()) + sqr(phy.y - virt.y_avg.newest());
}

void ApplePS2SynapticsTouchPad::assignVirtualFinger(int physicalFinger) {
    if (physicalFinger < 0 || physicalFinger >= SYNAPTICS_MAX_FINGERS) {
        IOLog("VoodooPS2SynapticsTouchPad::assignVirtualFinger ERROR: invalid physical finger %d", physicalFinger);
        return;
    }
	for (int j = 0; j < SYNAPTICS_MAX_FINGERS; j++) {
		synaptics_virtual_finger_state &vfj = virtualFingerStates[j];
        if (!vfj.touch) {
            fingerStates[physicalFinger].virtualFingerIndex = j;
            vfj.touch = true;
            vfj.x_avg.reset();
            vfj.y_avg.reset();
			assignFingerType(vfj);
            break;
        }
	}
}

void ApplePS2SynapticsTouchPad::assignFingerType(synaptics_virtual_finger_state &vf) {
	vf.fingerType = kMT2FingerTypeUndefined;
	for (MT2FingerType i = kMT2FingerTypeIndexFinger; i < kMT2FingerTypeCount; i = (MT2FingerType)(i + 1))
		if (freeFingerTypes[i]) {
			freeFingerTypes[i] = false;
			vf.fingerType = i;
			break;
		}

}

void ApplePS2SynapticsTouchPad::synaptics_parse_normal_packet(const UInt8 buf[], const int w) {
    DEBUG_LOG("synaptics_parse_hw_state: =============NORMAL PACKET=============");
    
    int x = buf[4] |
       ((buf[1] & 0x0f) << 8) |
       ((buf[3] & 0x10) << 8);

    int y = buf[5] |
       ((buf[1] & 0xf0) << 4) |
       ((buf[3] & 0x20) << 7);
    
    // normal "packet"
    if (w >= 4) { // One finger
        fingerStates[0].x = x;
        fingerStates[0].y = y;
        fingerStates[0].z = buf[2]; // pressure
        fingerStates[0].w = w; // width
    }
    else { // Multiple fingers, read virtual V field
        fingerStates[0].x = x;
        fingerStates[0].y = y;
        fingerStates[0].z = buf[2] & 0xfe; // pressure
        fingerStates[0].w = 8 + ((buf[2] & 1) << 2 | (buf[5] & 2) | (buf[4] & 2 >> 1));
    }
    DEBUG_LOG("synaptics_parse_hw_state: finger 0 pressure %d width %d\n", fingerStates[0].z, fingerStates[0].w);

    if (fingerStates[0].x > X_MAX_POSITIVE)
        fingerStates[0].x -= 1 << ABS_POS_BITS;
    else if (fingerStates[0].x == X_MAX_POSITIVE)
        fingerStates[0].x = XMAX;
    
    if (fingerStates[0].y > Y_MAX_POSITIVE)
        fingerStates[0].y -= 1 << ABS_POS_BITS;
    else if (fingerStates[0].y == Y_MAX_POSITIVE)
        fingerStates[0].y = YMAX;
    
    // count the number of fingers
    // my port of synaptics_image_sensor_process from synaptics.c from Linux Kernel
    int fingerCount = 0;
    if(fingerStates[0].z < z_finger) {
        fingerCount = 0;
        agmFingerCount = 0;
        fingerStates[0].w = 0;
    }
    else if(w >= 4) {
        fingerCount = 1;
        agmFingerCount = 0;
    } else if(w == 0)
        fingerCount = MAX(2, MIN(agmFingerCount, SYNAPTICS_MAX_FINGERS));
    else if(w == 1)
        fingerCount = MAX(3, MIN(agmFingerCount, SYNAPTICS_MAX_FINGERS));

    clampedFingerCount = fingerCount;
    
    if (clampedFingerCount > SYNAPTICS_MAX_FINGERS)
        clampedFingerCount = SYNAPTICS_MAX_FINGERS;

    if (renumberFingers())
        sendTouchData();
}

// advanced gesture packet (half-resolution packets)
// my port of synaptics_parse_agm from synaptics.c from Linux Kernel
void ApplePS2SynapticsTouchPad::synaptics_parse_agm_packet(const UInt8 buf[]) {
    int agmPacketType = (buf[5] & 0x30) >> 4;
    
    switch(agmPacketType) {
        case 1:
            DEBUG_LOG("synaptics_parse_hw_state: ===========EXTENDED PACKET===========");
            fingerStates[1].x = (((buf[4] & 0x0f) << 8) | buf[1]) << 1;
            fingerStates[1].y = (((buf[4] & 0xf0) << 4) | buf[2]) << 1;
            fingerStates[1].z = ((buf[3] & 0x30) | (buf[5] & 0x0f)) << 1;
            fingerStates[1].w = 8 + ((buf[5] & 1) << 2 | (buf[2] & 1) << 1 | (buf[1] & 1));

            DEBUG_LOG("synaptics_parse_hw_state: finger 1 pressure %d width %d\n", fingerStates[1].z, fingerStates[1].w);
            
            if (fingerStates[1].x > X_MAX_POSITIVE)
                fingerStates[1].x -= 1 << ABS_POS_BITS;
            else if (fingerStates[1].x == X_MAX_POSITIVE)
                fingerStates[1].x = XMAX;
            
            if (fingerStates[1].y > Y_MAX_POSITIVE)
                fingerStates[1].y -= 1 << ABS_POS_BITS;
            else if (fingerStates[1].y == Y_MAX_POSITIVE)
                fingerStates[1].y = YMAX;

            break;
        case 2:
            DEBUG_LOG("synaptics_parse_hw_state: ===========FINGER COUNT PACKET===========");
            agmFingerCount = buf[1] & 0x0f;
            DEBUG_LOG("synaptics_parse_hw_state: %d fingers, prim_idx: %d, sec_idx: %d\n",
                      agmFingerCount, buf[2], buf[4]);
            break;
        default:
            break;
    }
}

void ApplePS2SynapticsTouchPad::synaptics_parse_passthru(const UInt8 buf[], UInt32 buttons) {
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    
    UInt32 passbuttons = buf[1] & 0x7; // mask for just M R L
    
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    _lastPassthruButtons = passbuttons;
    
    SInt32 dx = ((buf[1] & 0x10) ? 0xffffff00 : 0 ) | buf[4];
    SInt32 dy = ((buf[1] & 0x20) ? 0xffffff00 : 0 ) | buf[5];
    buttons |= passbuttons;
    
#ifdef DEBUG_VERBOSE
    static int count = 0;
    IOLog("ps2: passthru packet dx=%d, dy=%d, buttons=%d (%d)\n", dx, dy, buttons, count++);
#endif
    
    trackpointReport.timestamp = timestamp;
    trackpointReport.dx = dx;
    trackpointReport.dy = -dy;
    trackpointReport.buttons = buttons;
    
    super::messageClient(kIOMessageVoodooTrackpointMessage, voodooInputInstance,
                         &trackpointReport, sizeof(TrackpointReport));
}

int ApplePS2SynapticsTouchPad::synaptics_parse_ext_btns(const UInt8 buf[], const int w) {
    UInt8 extBtnsChanged = (buf[0] ^ buf[3]) & 0x2;
    
    // The extended buttons override bits from x/y position, so are only part of the
    // packet if their state changes and the packet isn't an Extended W packet.
    if (extBtnsChanged == 0 || w == SYNA_W_EXTENDED) {
        return _lastExtendedButtons;
    }
    
    UInt8 btnBits = (_extended_id.extended_btns + 1) / 2;
    int extendedBtns = 0;
    
    // Extended buttons have a pattern of:
    // Byte 4 X7 X6 X5 X4 B6 B4 B2 B0
    // Byte 5 Y7 Y6 Y5 Y4 B7 B5 B3 B1
    // This needs to be converted to one value with up to 8 buttons in it
    for (int i = 0; i < btnBits; i++) {
        UInt8 mask = 1 << i;
        extendedBtns |= (buf[4] & mask) << i;
        extendedBtns |= (buf[5] & mask) << (i + 1);
    }
    
    extendedBtns &= (1 << _extended_id.extended_btns) - 1;
    _lastExtendedButtons = extendedBtns;
    return _lastExtendedButtons;
}

void ApplePS2SynapticsTouchPad::synaptics_parse_hw_state(const UInt8 buf[]) {
    // Check if input is disabled via ApplePS2Keyboard request
    if (ignoreall)
        return;
    
    int w = (((buf[0] & 0x30) >> 2) |
             ((buf[0] & 0x04) >> 1) |
             ((buf[3] & 0x04) >> 2));
  
    DEBUG_LOG("VoodooPS2 w: %d\n", w);
    
    // ------ Buttons ------
    
    UInt32 buttons = buf[0] & 0x3; // R L
    UInt32 xorBtns = buf[0] ^ buf[3]; // Useful for extended buttons below
    
    if (_cont_caps.one_btn_clickpad) {
        // Clickpad reports it's button in the middle mouse button position
        _clickpad_pressed = xorBtns & 0x1;
    }
    
    if (_capabilities.middle_btn && (xorBtns & 0x1)) {
        buttons |= 0x4;
    }
    
    if (_extended_id.extended_btns > 0) {
        buttons |= synaptics_parse_ext_btns(buf, w);
    }
    
    DEBUG_LOG("VoodooPS2 Buttons %d\n", buttons);
    
    // ------ Motion Data ------
    
    switch (w) {
        case SYNA_W_EXTENDED:
            buttons |= _lastPassthruButtons;
            synaptics_parse_agm_packet(buf);
            break;
        case SYNA_W_PASSTHRU:
            synaptics_parse_passthru(buf, buttons);
            return;
        default:
            buttons |= _lastPassthruButtons;
            synaptics_parse_normal_packet(buf, w);
            break;
    }
    
    // ------ Report buttons ------
    
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    
    trackpointReport.timestamp = timestamp;
    trackpointReport.dx = 0;
    trackpointReport.dy = 0;
    trackpointReport.buttons = buttons;
    
    super::messageClient(kIOMessageVoodooTrackpointMessage, voodooInputInstance,
                         &trackpointReport, sizeof(TrackpointReport));
}

template <typename TValue, typename TLimit, typename TMargin>
static void clip_no_update_limits(TValue& value, TLimit minimum, TLimit maximum, TMargin margin)
{
    if (value < minimum)
        value = minimum;
    if (value > maximum)
        value = maximum;
}

template <typename TValue, typename TLimit, typename TMargin>
static void clip(TValue& value, TLimit& minimum, TLimit& maximum, TMargin margin, bool &dimensions_changed)
{
    if (value < minimum - margin) {
        dimensions_changed = true;
        minimum = value + margin;
    }
    if (value > maximum + margin) {
        dimensions_changed = true;
        maximum = value - margin;
    }
    clip_no_update_limits(value, minimum, maximum, margin);
}

void ApplePS2SynapticsTouchPad::freeAndMarkVirtualFingers() {
	memset(freeFingerTypes, true, kMT2FingerTypeCount);
	freeFingerTypes[kMT2FingerTypeUndefined] = false;

    for (int i = 0; i < SYNAPTICS_MAX_FINGERS; i++) { // free up all virtual fingers
        auto &vfi = virtualFingerStates[i];
        vfi.touch = false;
        vfi.x_avg.reset(); // maybe it should be done only for unpressed fingers?
        vfi.y_avg.reset();
        vfi.pressure = 0;
        vfi.width = 0;
    }
    for (int i = 0; i < clampedFingerCount; i++) { // mark virtual fingers as used
		int j = fingerStates[i].virtualFingerIndex;
        if (j == -1) {
            IOLog("synaptics_parse_hw_state: WTF!? Finger %d has no virtual finger", i);
            continue;
        }
		auto &vfj = virtualFingerStates[j];
        vfj.touch = true;
		freeFingerTypes[vfj.fingerType] = false;
    }
	for (int i = 0; i < SYNAPTICS_MAX_FINGERS; i++) {
		auto &vfi = virtualFingerStates[i];
		if (!vfi.touch)
			vfi.fingerType = kMT2FingerTypeUndefined;
	}
}

static void clone(synaptics_hw_state &dst, const synaptics_hw_state &src) {
    dst.x = src.x;
    dst.y = src.y;
    dst.z = src.z;
    dst.w = src.w;
}

int ApplePS2SynapticsTouchPad::upperFingerIndex() const {
    return fingerStates[0].y < fingerStates[1].y ? 1 : 0;
}

const synaptics_hw_state& ApplePS2SynapticsTouchPad::upperFinger() const {
    return fingerStates[upperFingerIndex()];
}

void ApplePS2SynapticsTouchPad::swapFingers(int dst, int src) {
    int j = fingerStates[src].virtualFingerIndex;
    const auto &vfj = virtualFingerStates[j];
    fingerStates[dst].x = vfj.x_avg.average();
    fingerStates[dst].y = vfj.y_avg.average();
    fingerStates[dst].virtualFingerIndex = j;
    assignVirtualFinger(src);
}

#define FINGER_DIST 1000000

bool ApplePS2SynapticsTouchPad::renumberFingers() {
    const auto &f0 = fingerStates[0];
    const auto &f1 = fingerStates[1];
    auto &f2 = fingerStates[2];
    auto &f3 = fingerStates[3];
	auto &f4 = fingerStates[4];

    if (clampedFingerCount == lastFingerCount && clampedFingerCount >= 3) {
        // update imaginary finger states
        if (f0.virtualFingerIndex != -1 && f1.virtualFingerIndex != -1) {
            if (clampedFingerCount >= 4) {
                const auto &fi = upperFinger();
                const auto &fiv = virtualFingerStates[fi.virtualFingerIndex];
                for (int j = 2; j < clampedFingerCount; j++) {
                    auto &fj = fingerStates[j];
                    fj.x += fi.x - fiv.x_avg.newest();
                    fj.y += fi.y - fiv.y_avg.newest();
                    fj.z = fi.z;
                    fj.w = fi.w;

                    clip_no_update_limits(fj.x, logical_min_x, logical_max_x, margin_size_x);
                    clip_no_update_limits(fj.y, logical_min_y, logical_max_y, margin_size_y);
                }
            }
            else if (clampedFingerCount == 3) {
                const auto &f0v = virtualFingerStates[f0.virtualFingerIndex];
                const auto &f1v = virtualFingerStates[f1.virtualFingerIndex];
                auto &fs2 = fingerStates[2];
                fs2.x += ((f0.x - f0v.x_avg.newest()) + (f1.x - f1v.x_avg.newest())) / 2;
                fs2.y += ((f0.y - f0v.y_avg.newest()) + (f1.y - f1v.y_avg.newest())) / 2;
                fs2.z = (f0.z + f1.z) / 2;
                fs2.w = (f0.w + f1.w) / 2;

                clip_no_update_limits(fs2.x, logical_min_x, logical_max_x, margin_size_x);
                clip_no_update_limits(fs2.y, logical_min_y, logical_max_y, margin_size_y);
            }
        }
        else
            IOLog("synaptics_parse_hw_state: WTF - have %d fingers, but first 2 don't have virtual finger", clampedFingerCount);
    }
    
    // We really need to send the "no touch" event
    // multiple times, because if we don't do it and return,
    // gestures like desktop switching or inertial scrolling
    // got stuck midway until the next touch.
    //if(!lastFingerCount && !clampedFingerCount) {
    //    return 0;
    //}

	// Finger type detection:
	// We think that fingers are added beginning with the index finger,
	// then middle, ring and little.
	// However, when the finger count reaches 4, the lowest finger becomes thumb,
	// but other fingers don't change their types.
	// All fingers preserve their types during the gesture.
	// Though it would be nice to see what MT2 does.


    if (clampedFingerCount == lastFingerCount && clampedFingerCount == 1) {
        int i = 0;
        int j = fingerStates[i].virtualFingerIndex;
        int d = dist(i, j);
        if (d > FINGER_DIST) { // this number was determined experimentally
            // Prevent jumps by unpressing finger. Other way could be leaving the old finger pressed.
            DEBUG_LOG("synaptics_parse_hw_state: unpressing finger: dist is %d", d);
            auto &vfj = virtualFingerStates[j];
            vfj.x_avg.reset();
            vfj.y_avg.reset();
            vfj.pressure = 0;
            vfj.width = 0;
			vfj.fingerType = kMT2FingerTypeUndefined;
            clampedFingerCount = 0;
        }
    }
    if (clampedFingerCount != lastFingerCount) {
        if (clampedFingerCount > lastFingerCount && clampedFingerCount >= 3) {
            // Skip sending touch data once because we need to wait for the next extended packet
            if (wasSkipped)
                wasSkipped = false;
            else {
                DEBUG_LOG("synaptics_parse_hw_state: Skip sending touch data");
                wasSkipped = true;
                return false;
            }
        }

        if (lastFingerCount == 0) {
            // Assign to identity mapping
            for (int i = 0; i < clampedFingerCount; i++) {
                auto &fi = fingerStates[i];
                fi.virtualFingerIndex = i;
                auto &vfi = virtualFingerStates[i];
                vfi.touch = true;
				assignFingerType(vfi);
                vfi.x_avg.reset();
                vfi.y_avg.reset();
                if (i >= 2) // more than 3 fingers added simultaneously
                    clone(fi, upperFinger()); // Copy from the upper finger
            }
        }
        else if (clampedFingerCount > lastFingerCount && !hadLiftFinger) {
            // First finger already exists
            // Can add 1, 2 or 3 fingers at once
            // Newly added finger is always in secondary finger packet
            switch (clampedFingerCount - lastFingerCount) {
                case 1:
                    if (lastFingerCount >= 2)
                        swapFingers(lastFingerCount, 1);
                    else // lastFingerCount = 1
                        assignVirtualFinger(1);
                    break;
                case 2:
                    if (lastFingerCount == 1) { // added second and third
                        assignVirtualFinger(1);
                        clone(f2, upperFinger()); // We don't know better
                        assignVirtualFinger(2);
                    }
                    else { // added third and fourth
                        swapFingers(lastFingerCount, 1);

                        // add fourth
                        clone(f3, upperFinger());
                        assignVirtualFinger(3);
                    }
                    break;
                case 3:
                    assignVirtualFinger(1);
                    clone(f2, upperFinger());
                    assignVirtualFinger(2);
                    clone(f3, upperFinger());
                    assignVirtualFinger(3);
                    break;
				case 4:
                    assignVirtualFinger(1);
                    clone(f2, upperFinger());
                    assignVirtualFinger(2);
                    clone(f3, upperFinger());
                    assignVirtualFinger(3);
					clone(f4, upperFinger());
					assignVirtualFinger(4);
					break;
                default:
                    IOLog("synaptics_parse_hw_state: WTF!? fc=%d lfc=%d", clampedFingerCount, lastFingerCount);
            }
        }
        else if (clampedFingerCount > lastFingerCount && hadLiftFinger) {
            for (int i = 0; i < SYNAPTICS_MAX_FINGERS; i++) // clean virtual finger numbers
                fingerStates[i].virtualFingerIndex = -1;
            
            int maxMinDist = 0, maxMinDistIndex = -1;
            int secondMaxMinDist = 0, secondMaxMinDistIndex = -1;

            // find new physical finger for each existing virtual finger
            for (int j = 0; j < SYNAPTICS_MAX_FINGERS; j++) {
                if (!virtualFingerStates[j].touch)
                    continue; // free
                int minDist = INT_MAX, minIndex = -1;
                for (int i = 0; i < lastFingerCount; i++) {
                    if (fingerStates[i].virtualFingerIndex != -1)
                        continue; // already taken
                    int d = dist(i, j);
                    if (d < minDist) {
                        minDist = d;
                        minIndex = i;
                    }
                }
                if (minIndex == -1) {
                    IOLog("synaptics_parse_hw_state: WTF!? minIndex is -1");
                    continue;
                }
                if (minDist > maxMinDist) {
                    secondMaxMinDist = maxMinDist;
                    secondMaxMinDistIndex = maxMinDistIndex;
                    maxMinDist = minDist;
                    maxMinDistIndex = minIndex;
                }
                fingerStates[minIndex].virtualFingerIndex = j;
            }
            
            // assign new virtual fingers for all new fingers
            for (int i = 0; i < min(2, clampedFingerCount); i++) // third and fourth 'fingers' are handled separately
                if (fingerStates[i].virtualFingerIndex == -1)
                    assignVirtualFinger(i); // here OK

            if (clampedFingerCount == 3) {
                DEBUG_LOG("synaptics_parse_hw_state: adding third finger, maxMinDist=%d", maxMinDist);
                f2.z = (f0.z + f1.z) / 2;
                f2.w = (f0.w + f1.w) / 2;
                if (maxMinDist > FINGER_DIST && maxMinDistIndex >= 0) {
                    // i-th physical finger was replaced, save its old coordinates to the 3rd physical finger and map it to a new virtual finger.
                    // The third physical finger should now be mapped to the old fingerStates[i].virtualFingerIndex.
                    swapFingers(2, maxMinDistIndex);
                    DEBUG_LOG("synaptics_parse_hw_state: swapped, saving location");
                }
                else {
                    // existing fingers didn't change or were swapped, so we don't know the location of the third finger
                    const auto &fj = upperFinger();

                    f2.x = fj.x;
                    f2.y = fj.y;
                    assignVirtualFinger(2);
                    DEBUG_LOG("synaptics_parse_hw_state: not swapped, taking upper finger position");
                }
            }
            else if (clampedFingerCount >= 4) {
                // Is it possible that both 0 and 1 fingers were swapped with 2 and 3?
                DEBUG_LOG("synaptics_parse_hw_state: adding third and fourth fingers, maxMinDist=%d, secondMaxMinDist=%d", maxMinDist, secondMaxMinDist);
                f2.z = f3.z = (f0.z + f1.z) / 2;
                f2.w = f3.w = (f0.w + f1.w) / 2;

                // Possible situations:
                // 1. maxMinDist ≤ 1000000, lastFingerCount = 3 - no fingers swapped, just adding 4th finger
                // 2. maxMinDist ≤ 1000000, lastFingerCount = 2 - no fingers swapped, just adding 3rd and 4th fingers
                // 3. maxMinDist > 1000000, secondMaxMinDist ≤ 1000000, lastFingerCount = 3 - i'th finger was swapped with 4th, 3rd left in place (i∈{0,1}):
                //      4th.xy = i'th.xy
                //      p2v[2] = j
                //      p2v[i] = next free
                // 4. maxMinDist > 1000000, secondMaxMinDist > 1000000, lastFingerCount = 3 - i'th finger was swapped with 3rd and k'th finger was swapped with 4th (i,k∈{0,1}):
                //      is it possible that only imaginary finger was left in place?!
                // 5. maxMinDist > 1000000, secondMaxMinDist ≤ 1000000, lastFingerCount = 2 - one finger swapped, one finger left in place.


                if (maxMinDist > FINGER_DIST && maxMinDistIndex >= 0) {
                    if (lastFingerCount < 3) {
                        // i-th physical finger was replaced, save its old coordinates to the 3rd physical finger and map it to a new virtual finger.
                        // The third physical finger should now be mapped to the old fingerStates[i].virtualFingerIndex.
                        swapFingers(2, maxMinDistIndex);
                        if (secondMaxMinDist > FINGER_DIST && secondMaxMinDistIndex >= 0) {
                            // both fingers were swapped with new ones
                            // i-th physical finger was replaced, save its old coordinates to the 4th physical finger and map it to a new virtual finger.
                            // The fourth physical finger should now be mapped to the old fingerStates[i].virtualFingerIndex.
                            swapFingers(3, secondMaxMinDistIndex);
                        }
                        else {
                            // fourth finger is new
                            clone(f3, upperFinger());
                            assignVirtualFinger(3);
                        }
                    }
                    else {
                        // i-th physical finger was replaced, save its old coordinates to the 4th physical finger and map it to a new virtual finger.
                        // The fourth physical finger should now be mapped to the old fingerStates[i].virtualFingerIndex.
                        swapFingers(3, maxMinDistIndex);
                        if (secondMaxMinDist > FINGER_DIST && secondMaxMinDistIndex >= 0) {
                            IOLog("synaptics_parse_hw_state: WTF, I thought it is impossible: fc=%d, lfc=%d, mdi=%d(%d), smdi=%d(%d)", clampedFingerCount, lastFingerCount, maxMinDist, maxMinDistIndex, secondMaxMinDist, secondMaxMinDistIndex);
                        }
                    }
                    DEBUG_LOG("synaptics_parse_hw_state: swapped, saving location");
                }
                else {
                    // existing fingers didn't change or were swapped, so we don't know the location of the third and fourth fingers
                    const auto &fj = upperFinger();
                    clone(f2, fj);
                    if (lastFingerCount < 3)
                        assignVirtualFinger(2);
                    clone(f3, fj);
                    assignVirtualFinger(3);
                    DEBUG_LOG("synaptics_parse_hw_state: not swapped, cloning existing fingers");
                }

				if (clampedFingerCount >= 5) {
					// Don't bother with 5th finger, always clone
					clone(f4, upperFinger());
					assignVirtualFinger(4);
					DEBUG_LOG("cloning 5th finger");
				}
            }
            freeAndMarkVirtualFingers();
        }
        else if (clampedFingerCount < lastFingerCount) {
            // Set hadLiftFinger if lifted some fingers
            // Reset hadLiftFinger if lifted all fingers
            hadLiftFinger = clampedFingerCount > 0;

            // some fingers removed, need renumbering
            bool used[SYNAPTICS_MAX_FINGERS];
            for (int i = 0; i < SYNAPTICS_MAX_FINGERS; i++) { // clean virtual finger numbers
                fingerStates[i].virtualFingerIndex = -1;
                used[i] = false;
            }
            for (int i = 0; i < clampedFingerCount; i++) {
                // find new virtual finger number with nearest coordinates for this finger
                int minDist = INT_MAX, minIndex = -1;
                for (int j = 0; j < SYNAPTICS_MAX_FINGERS; j++) {
                    if (!virtualFingerStates[j].touch || used[j])
                        continue;
                    int d = dist(i, j);
                    if (d < minDist) {
                        minDist = d;
                        minIndex = j;
                    }
                }
                fingerStates[i].virtualFingerIndex = minIndex;
                if (minIndex == -1) {
                    IOLog("synaptics_parse_hw_state: WTF: renumbering failed, minIndex for %d is -1", i);
                    continue;
                }
                used[minIndex] = true;
            }
            freeAndMarkVirtualFingers();
        }
    }
    
    for (int i = 0; i < clampedFingerCount; i++) {
        const auto &fi = fingerStates[i];
        DEBUG_LOG("synaptics_parse_hw_state: finger %d -> virtual finger %d", i, fi.virtualFingerIndex);
        if (fi.virtualFingerIndex < 0 || fi.virtualFingerIndex >= SYNAPTICS_MAX_FINGERS) {
            IOLog("synaptics_parse_hw_state: ERROR: invalid physical finger %d", fi.virtualFingerIndex);
            continue;
        }
        synaptics_virtual_finger_state &fiv = virtualFingerStates[fi.virtualFingerIndex];
        fiv.x_avg.filter(fi.x);
        fiv.y_avg.filter(fi.y);
        fiv.width = fi.w;
        fiv.pressure = fi.z;
        fiv.button = _clickpad_pressed;
    }

	// Thumb detection. Must happen after setting coordinates (filter)
	if (clampedFingerCount > lastFingerCount && clampedFingerCount >= 4) {
		// find the lowest finger
		int lowestFingerIndex = -1;
		int min_y = INT_MAX;
		for (int i = 0; i < SYNAPTICS_MAX_FINGERS; i++) {
			const auto &vfi = virtualFingerStates[i];
			DEBUG_LOG("finger %d: touch %d, y %d", i, vfi.touch, vfi.y_avg.average());
			if (vfi.touch && vfi.y_avg.average() < min_y) {
				lowestFingerIndex = i;
				min_y = vfi.y_avg.average();
			}
		}
		DEBUG_LOG("lowest finger: %d", lowestFingerIndex);
		if (lowestFingerIndex == -1)
			IOLog("synaptics_parse_hw_state: WTF?! lowest finger not found!");
		else {
			auto &vf = virtualFingerStates[lowestFingerIndex];
			freeFingerTypes[vf.fingerType] = true;
			vf.fingerType = kMT2FingerTypeThumb;
			freeFingerTypes[kMT2FingerTypeThumb] = false;
		}
	}
    
    DEBUG_LOG("synaptics_parse_hw_state: lastFingerCount=%d clampedFingerCount=%d clickpad=%d",
              lastFingerCount,  clampedFingerCount, _clickpad_pressed);
    return true;
}

void ApplePS2SynapticsTouchPad::sendTouchData() {
    // Ignore input for specified time after keyboard usage
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    uint64_t timestamp_ns;
    absolutetime_to_nanoseconds(timestamp, &timestamp_ns);

    // Lenovo Yoga tablet mode works by sending this key every second to disable the touchpad.
    // That key is mapped to ADB dead key (0x80).
    if (timestamp_ns - keytime < (keycode == specialKey ? maxafterspecialtyping : maxaftertyping))
        return;

    if (lastFingerCount != clampedFingerCount) {
        lastFingerCount = clampedFingerCount;
        return; // Skip while fingers are placed on the touchpad or removed
    }

    static_assert(VOODOO_INPUT_MAX_TRANSDUCERS >= SYNAPTICS_MAX_FINGERS, "Trackpad supports too many fingers");

    bool dimensions_changed = false;

    int transducers_count = 0;
    for(int i = 0; i < SYNAPTICS_MAX_FINGERS; i++) {
        const auto& state = virtualFingerStates[i];
        if (!state.touch)
            continue;

        auto& transducer = inputEvent.transducers[transducers_count++];

        transducer.type = FINGER;
        transducer.isValid = true;
        transducer.supportsPressure = true;
        
        int posX = state.x_avg.average();
        int posY = state.y_avg.average();

        clip(posX, logical_min_x, logical_max_x, margin_size_x, dimensions_changed);
        clip(posY, logical_min_y, logical_max_y, margin_size_y, dimensions_changed);

        posX -= logical_min_x;
        posY = logical_max_y + 1 - posY;
        
        DEBUG_LOG("synaptics_parse_hw_state finger[%d] x=%d y=%d raw_x=%d raw_y=%d", i, posX, posY, state.x_avg.average(), state.y_avg.average());

        transducer.previousCoordinates = transducer.currentCoordinates;

        transducer.currentCoordinates.x = posX;
        transducer.currentCoordinates.y = posY;
        transducer.timestamp = timestamp;

        switch (_forceTouchMode)
        {
            case FORCE_TOUCH_BUTTON: // Physical button is translated into force touch instead of click
                transducer.isPhysicalButtonDown = false;
                transducer.currentCoordinates.pressure = state.button ? 255 : 0;
                break;

            case FORCE_TOUCH_THRESHOLD: // Force touch is touch with pressure over threshold
                transducer.isPhysicalButtonDown = state.button;
                transducer.currentCoordinates.pressure = state.pressure > _forceTouchPressureThreshold ? 255 : 0;
                break;

            case FORCE_TOUCH_VALUE: // Pressure is passed to system as is
                transducer.isPhysicalButtonDown = state.button;
                transducer.currentCoordinates.pressure = state.pressure;
                break;

            case FORCE_TOUCH_CUSTOM: // Pressure is passed, but with locking
                transducer.isPhysicalButtonDown = state.button;
                
                if (clampedFingerCount != 1) {
                    transducer.currentCoordinates.pressure = state.pressure > _forceTouchPressureThreshold ? 255 : 0;
                    break;
                }

                double value;
                if (state.pressure >= _forceTouchCustomDownThreshold) {
                    value = 1.0;
                } else if (state.pressure <= _forceTouchCustomUpThreshold) {
                    value = 0.0;
                } else {
                    double base = ((double) (state.pressure - _forceTouchCustomUpThreshold)) / ((double) (_forceTouchCustomDownThreshold - _forceTouchCustomUpThreshold));
                    value = 1;
                    for (int j = 0; j < _forceTouchCustomPower; ++j) {
                        value *= base;
                    }
                }

                transducer.currentCoordinates.pressure = (int) (value * 255);
                break;

            case FORCE_TOUCH_DISABLED:
            default:
                transducer.isPhysicalButtonDown = state.button;
                transducer.currentCoordinates.pressure = 0;
                break;

        }

        transducer.isTransducerActive = 1;
        transducer.currentCoordinates.width = state.pressure / 2;
		if (state.fingerType == kMT2FingerTypeUndefined)
			IOLog("synaptics_parse_hw_state: WTF!? finger type is undefined");
		if (state.fingerType < kMT2FingerTypeUndefined || state.fingerType > kMT2FingerTypeLittleFinger)
			IOLog("synaptics_parse_hw_state: WTF!? finger type is out of range");
		if (freeFingerTypes[state.fingerType])
			IOLog("synaptics_parse_hw_state: WTF!? finger type is marked free");
		transducer.fingerType = state.fingerType;
		transducer.secondaryId = i;
    }

	for (int i = 0; i < transducers_count; i++)
		for (int j = i + 1; j < transducers_count; j++)
			if (inputEvent.transducers[i].fingerType == inputEvent.transducers[j].fingerType)
				IOLog("synaptics_parse_hw_state: WTF!? equal finger types");

    if (transducers_count != clampedFingerCount)
        IOLog("synaptics_parse_hw_state: WTF?! tducers_count %d clampedFingerCount %d", transducers_count, clampedFingerCount);

    // create new VoodooI2CMultitouchEvent
    inputEvent.contact_count = transducers_count;
    inputEvent.timestamp = timestamp;


    if (dimensions_changed) {
        VoodooInputDimensions d;
        d.min_x = logical_min_x;
        d.max_x = logical_max_x;
        d.min_y = logical_min_y;
        d.max_y = logical_max_y;
        super::messageClient(kIOMessageVoodooInputUpdateDimensionsMessage, voodooInputInstance, &d, sizeof(VoodooInputDimensions));
    }

    // send the event into the multitouch interface
    // send the 0 finger message only once
    if (inputEvent.contact_count != 0 || lastSentFingerCount != 0) {
        super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &inputEvent, sizeof(VoodooInputEvent));
    }
    lastFingerCount = clampedFingerCount;
    lastSentFingerCount = inputEvent.contact_count;
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
    request.commands[0].command = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut = enable ? kDP_Enable : kDP_SetDefaultsAndDisable;
    request.commandsCount = 1;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
}

// - -  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SynapticsTouchPad::getTouchPadStatus(  UInt8 buf3[] )
{
    TPS2Request<6> request;
    request.commands[0].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
    request.commands[1].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_GetMouseInformation;
    request.commands[2].command = kPS2C_ReadDataPort;
    request.commands[2].inOrOut = 0;
    request.commands[3].command = kPS2C_ReadDataPort;
    request.commands[3].inOrOut = 0;
    request.commands[4].command = kPS2C_ReadDataPort;
    request.commands[4].inOrOut = 0;
    request.commands[5].command = kPS2C_SendCommandAndCompareAck;
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
    request.commands[0].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;

    // 4 set resolution commands, each encode 2 data bits.
    request.commands[1].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_SetMouseResolution;
    request.commands[2].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[2].inOrOut  = (dataSelector >> 6) & 0x3;

    request.commands[3].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[3].inOrOut  = kDP_SetMouseResolution;
    request.commands[4].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[4].inOrOut  = (dataSelector >> 4) & 0x3;

    request.commands[5].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[5].inOrOut  = kDP_SetMouseResolution;
    request.commands[6].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[6].inOrOut  = (dataSelector >> 2) & 0x3;

    request.commands[7].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[7].inOrOut  = kDP_SetMouseResolution;
    request.commands[8].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[8].inOrOut  = (dataSelector >> 0) & 0x3;

    // Read response bytes.
    request.commands[9].command  = kPS2C_SendCommandAndCompareAck;
    request.commands[9].inOrOut  = kDP_GetMouseInformation;
    request.commands[10].command = kPS2C_ReadDataPort;
    request.commands[10].inOrOut = 0;
    request.commands[11].command = kPS2C_ReadDataPort;
    request.commands[11].inOrOut = 0;
    request.commands[12].command = kPS2C_ReadDataPort;
    request.commands[12].inOrOut = 0;
    request.commands[13].command = kPS2C_SendCommandAndCompareAck;
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
    
    _clickpad_pressed = 0;
    _lastPassthruButtons = 0;
    _lastExtendedButtons = 0;
    tracksecondary=false;
    
    // clear state of control key cache
    _modifierdown = 0;
    
    //
    // Resend the touchpad mode byte sequence
    // IRQ is enabled as side effect of setting mode byte
    // Also touchpad is enabled as side effect
    //
    
    enterAdvancedGestureMode();
    
    //
    // Set LED state as it is lost after sleep
    //
    updateTouchpadLED();
}

bool ApplePS2SynapticsTouchPad::enterAdvancedGestureMode()
{
    int i = 0;
    TPS2Request<> request;
    
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
    
    setModeByte(false);

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
        request.commands[x].command = kPS2C_SendCommandAndCompareAck;
    request.commandsCount = i;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (i != request.commandsCount)
        DEBUG_LOG("VoodooPS2Trackpad: sending final init sequence failed: %d\n", request.commandsCount);

    return i == request.commandsCount;
}

// simplified setModeByte for switching between normal mode and EW mode
bool ApplePS2SynapticsTouchPad::setModeByte(bool sleep)
{
    // make sure we are not early in the initialization...
    if (!_device)
        return false;
    
    uint8_t modeByte = SYNA_MODE_ABSOLUTE | SYNA_MODE_W_MODE | SYNA_MODE_HIGH_RATE;

    if (_capabilities.extended_w_supported ||
        // Linux checks these bits too
        _cont_caps.advanced_gestures || _cont_caps.reports_v)
        modeByte |= SYNA_MODE_EXT_W;
    
    if (sleep)
        modeByte |= SYNA_MODE_SLEEP;

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
    request.commands[i++].inOrOut = (modeByte >> 6) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByte >> 4) & 0x3;    // 0x mode_byte
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByte >> 2) & 0x3;    // 0x (depends on mode byte)
    request.commands[i++].inOrOut = kDP_SetMouseResolution;        // E8
    request.commands[i++].inOrOut = (modeByte >> 0) & 0x3;    // 0x (depends on mode byte)

    // Set sample rate 20 to set mode byte 2. Older pads have 4 mode
    // bytes (0,1,2,3), but only mode byte 2 remain in modern pads.
    request.commands[i++].inOrOut = kDP_SetMouseSampleRate;        // F3
    request.commands[i++].inOrOut = 20;                            // 14
    request.commands[i++].inOrOut = kDP_SetMouseScaling1To1;       // E6

    // all these commands are "send mouse" and "compare ack"
    for (int x = 0; x < i; x++)
        request.commands[x].command = kPS2C_SendCommandAndCompareAck;
    request.commandsCount = i;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (i != request.commandsCount)
        DEBUG_LOG("VoodooPS2Trackpad: setModeByte failed: %d\n", request.commandsCount);

    return i == request.commandsCount;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setPropertiesGated(OSDictionary * config)
{
	if (NULL == config)
		return;
    
	const struct {const char *name; int *var;} int32vars[]={
        {"FingerZ",                         &z_finger},
        {"WakeDelay",                       &wakedelay},
        {"MinLogicalXOverride",             &minXOverride},
        {"MinLogicalYOverride",             &minYOverride},
        {"MaxLogicalXOverride",             &maxXOverride},
        {"MaxLogicalYOverride",             &maxYOverride},
        {"TrackpointDeadzone",              &_deadzone},
        {"TrackpointScrollXMultiplier",     &_scrollMultiplierX},
        {"TrackpointScrollYMultiplier",     &_scrollMultiplierY},
        {"TrackpointScrollXDivisor",        &_scrollDivisorX},
        {"TrackpointScrollYDivisor",        &_scrollDivisorY},
        {"MouseMultiplierX",                &_mouseMultiplierX},
        {"MouseMultiplierY",                &_mouseMultiplierY},
        {"MouseDivisorX",                   &_mouseDivisorX},
        {"MouseDivisorY",                   &_mouseDivisorY},
        {"ForceTouchMode",                  (int*)&_forceTouchMode}, // 0 - disable, 1 - left button, 2 - pressure threshold, 3 - pass pressure value
        {"ForceTouchPressureThreshold",     &_forceTouchPressureThreshold}, // used in mode 2
        {"SpecialKeyForQuietTime",          &specialKey},
        {"ForceTouchCustomDownThreshold",   &_forceTouchCustomDownThreshold}, // used in mode 4
        {"ForceTouchCustomUpThreshold",     &_forceTouchCustomUpThreshold}, // used in mode 4
        {"ForceTouchCustomPower",           &_forceTouchCustomPower}, // used in mode 4
	};
	const struct {const char *name; int *var;} boolvars[]={
        {"DisableLEDUpdate",                &noled},
        {"HWResetOnStart",                  &hwresetonstart},
        {"ProcessUSBMouseStopsTrackpad",    &_processusbmouse},
        {"ProcessBluetoothMouseStopsTrackpad", &_processbluetoothmouse},
 	};
    const struct {const char* name; bool* var;} lowbitvars[]={
        {"USBMouseStopsTrackpad",           &usb_mouse_stops_trackpad},
        {"DisableDeepSleep",                &disableDeepSleep}
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
        {"QuietTimeAfterTyping",            &maxaftertyping},
        {"QuietTimeAfterSpecialKey",        &maxafterspecialtyping}
    };
    
	OSBoolean *bl;
    
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

    // disable trackpad when USB mouse is plugged in and this functionality is requested
    if (attachedHIDPointerDevices && attachedHIDPointerDevices->getCount() > 0) {
        ignoreall = usb_mouse_stops_trackpad;
        updateTouchpadLED();
    }

    if (_forceTouchMode == FORCE_TOUCH_BUTTON) {
        int val[16];
        if (PE_parse_boot_argn("rp0", val, sizeof(val)) ||
            PE_parse_boot_argn("rp", val, sizeof(val)) ||
            PE_parse_boot_argn("container-dmg", val, sizeof(val)) ||
            PE_parse_boot_argn("root-dmg", val, sizeof(val)) ||
            PE_parse_boot_argn("auth-root-dmg", val, sizeof(val)))
        _forceTouchMode = FORCE_TOUCH_DISABLED;
    }
    
    setTrackpointProperties();
    if (voodooInputInstance != nullptr) {
        super::messageClient(kIOMessageVoodooTrackpointUpdatePropertiesNotification,
                             voodooInputInstance, 0, 0);
    }
}

IOReturn ApplePS2SynapticsTouchPad::setProperties(OSObject *props)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, props);
    if (dict && _cmdGate)
    {
        // synchronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2SynapticsTouchPad::setPropertiesGated), dict);
    }
    
	return super::setProperties(props);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setTrackpointProperties()
{
    // Trackpoint information for VoodooInput
    OSDictionary *trackpoint = OSDictionary::withCapacity(5);
    if (trackpoint == nullptr)
        return;
    
    OSNumber *deadzone = OSNumber::withNumber(_deadzone, 32);
    OSNumber *buttonCnt = OSNumber::withNumber(3, 32);
    OSNumber *multX = OSNumber::withNumber(_mouseMultiplierX, 32);
    OSNumber *multY = OSNumber::withNumber(_mouseMultiplierY, 32);
    OSNumber *divX = OSNumber::withNumber(_mouseDivisorX, 32);
    OSNumber *divY = OSNumber::withNumber(_mouseDivisorY, 32);
    OSNumber *scrollMultX = OSNumber::withNumber(_scrollMultiplierX, 32);
    OSNumber *scrollMultY = OSNumber::withNumber(_scrollMultiplierY, 32);
    OSNumber *scrollDivX = OSNumber::withNumber(_scrollDivisorX, 32);
    OSNumber *scrollDivY = OSNumber::withNumber(_scrollDivisorY, 32);
    
    trackpoint->setObject(VOODOO_TRACKPOINT_DEADZONE, deadzone);
    trackpoint->setObject(VOODOO_TRACKPOINT_BTN_CNT, buttonCnt);
    trackpoint->setObject(VOODOO_TRACKPOINT_MOUSE_MULT_X, multX);
    trackpoint->setObject(VOODOO_TRACKPOINT_MOUSE_MULT_Y, multY);
    trackpoint->setObject(VOODOO_TRACKPOINT_MOUSE_DIV_X, divX);
    trackpoint->setObject(VOODOO_TRACKPOINT_MOUSE_DIV_Y, divY);
    trackpoint->setObject(VOODOO_TRACKPOINT_SCROLL_MULT_X, scrollMultX);
    trackpoint->setObject(VOODOO_TRACKPOINT_SCROLL_MULT_Y, scrollMultY);
    trackpoint->setObject(VOODOO_TRACKPOINT_SCROLL_DIV_X, scrollDivX);
    trackpoint->setObject(VOODOO_TRACKPOINT_SCROLL_DIV_Y, scrollDivY);
    setProperty(VOODOO_TRACKPOINT_KEY, trackpoint);
    
    OSSafeReleaseNULL(deadzone);
    OSSafeReleaseNULL(buttonCnt);
    OSSafeReleaseNULL(multX);
    OSSafeReleaseNULL(multY);
    OSSafeReleaseNULL(divX);
    OSSafeReleaseNULL(divY);
    OSSafeReleaseNULL(scrollMultX);
    OSSafeReleaseNULL(scrollMultY);
    OSSafeReleaseNULL(scrollDivX);
    OSSafeReleaseNULL(scrollDivY);
    OSSafeReleaseNULL(trackpoint);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::setDevicePowerState( UInt32 whatToDo )
{
    if (otherBusInUse) {
        // SMBus/I2C is handling power management
        return;
    }
    
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            //
            // Disable touchpad (synchronous).
            //

            setTouchPadEnable( false ); // Disable stream mode

            if (!disableDeepSleep) {
                setModeByte(true); // Enable sleep
            }
            break;

        case kPS2C_EnableDevice:
            //
            // Must not issue any commands before the device has
            // completed its power-on self-test and calibration.
            //

            if (!disableDeepSleep) {
                IOSleep(wakedelay);
                setModeByte(false);
            }
            IOSleep(wakedelay);
            
            // Reset and enable the touchpad.
            initTouchPad();
            
            // Send extra kDP_Enable command
            IOSleep(wakedelay);
            setTouchPadEnable(true);
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
            if (enable == ignoreall && !otherBusInUse)
            {
                // save state, and update LED
                ignoreall = !enable;
                updateTouchpadLED();
            }
            break;
        }
            
        case kPS2M_resetTouchpad:
        {
            int* reqCode = (int*)argument;
            IOLog("VoodooPS2SynapticsTouchPad::kPS2M_resetTouchpad reqCode: %d\n", *reqCode);
            if (*reqCode == 1 && !otherBusInUse)
            {
                ignoreall = false;
                initTouchPad();
                IOSleep(wakedelay);
                setTouchPadEnable(true); // Send extra kDP_Enable
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
            keycode = pInfo->adbKeyCode;
            break;
        }
        case kPS2M_SMBusStart: {
            // Trackpad is being taken over by another driver
            
            // Queries/standing up before this point needs to be reset
            // Fixes issues with CSM/Fast Boot on HP laptops
            doHardwareReset();
            
            // Prevent any PS2 transactions, otherwise the trackpad can completely lock up from PS2 commands
            // This is called after ::start (specifically registerService()), so only prevent power management/reset msgs
            otherBusInUse = true;
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
    if (_extended_id.has_leds && !noled)
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
        request.commands[x].command = kPS2C_SendCommandAndCompareAck;
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
        
        propertyMatching(OSSymbol::withCString(kUSBHostMatchingPropertyInterfaceClass), OSNumber::withNumber(kUSBHIDInterfaceClass, 8), matchingDictionary);
        propertyMatching(OSSymbol::withCString(kUSBHostMatchingPropertyInterfaceSubClass), OSNumber::withNumber(kUSBHIDBootInterfaceSubClass, 8), matchingDictionary);
        propertyMatching(OSSymbol::withCString(kUSBHostMatchingPropertyInterfaceProtocol), OSNumber::withNumber(kHIDMouseInterfaceProtocol, 8), matchingDictionary);
        
        // Register for future services
        usb_hid_publish_notify = addMatchingNotification(gIOFirstPublishNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        usb_hid_terminate_notify = addMatchingNotification(gIOTerminatedNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        OSSafeReleaseNULL(matchingDictionary);
    }
    
    // Determine if we should listen for bluetooth mouse attach events as per configuration
    if (_processbluetoothmouse) {
        // Bluetooth HID devices
        OSDictionary* matchingDictionary = serviceMatching("IOBluetoothHIDDriver");
        propertyMatching(OSSymbol::withCString(kIOHIDVirtualHIDevice), kOSBooleanFalse, matchingDictionary);
        
        // Register for future services
        bluetooth_hid_publish_notify = addMatchingNotification(gIOFirstPublishNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        bluetooth_hid_terminate_notify = addMatchingNotification(gIOTerminatedNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        OSSafeReleaseNULL(matchingDictionary);
    }
}

void ApplePS2SynapticsTouchPad::unregisterHIDPointerNotifications()
{
    // Free device matching notifiers
    // remove() releases them
    if (usb_hid_publish_notify)
        usb_hid_publish_notify->remove();

    if (usb_hid_terminate_notify)
        usb_hid_terminate_notify->remove();

    if (bluetooth_hid_publish_notify)
        bluetooth_hid_publish_notify->remove();

    if (bluetooth_hid_terminate_notify)
        bluetooth_hid_terminate_notify->remove();

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

