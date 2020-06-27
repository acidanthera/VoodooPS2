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

// generally one cannot IOLog from interrupt context, it eventually leads to kernel panic
// but it is useful sometimes
#ifdef PACKET_DEBUG
#define INTERRUPT_LOG(args...)  do { IOLog(args); } while (0)
#else
#define INTERRUPT_LOG(args...)  do { } while (0)
#endif

// enable for trackpad debugging
#ifdef DEBUG_MSG
#define DEBUG_VERBOSE
//#define PACKET_DEBUG
#endif

#define kTPDN "TPDN" // Trackpad Disable Notification

#include "LegacyIOService.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/usb/IOUSBHostFamily.h>
#include <IOKit/usb/IOUSBHostHIDDevice.h>
#include <IOKit/bluetooth/BluetoothAssignedNumbers.h>
#pragma clang diagnostic pop
#include "VoodooPS2Controller.h"
#include "VoodooPS2Elan.h"
#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"


#define kIOFBTransformKey               "IOFBTransform"

enum {
    // transforms
    kIOFBRotateFlags                    = 0x0000000f,

    kIOFBSwapAxes                       = 0x00000001,
    kIOFBInvertX                        = 0x00000002,
    kIOFBInvertY                        = 0x00000004,

    kIOFBRotate0                        = 0x00000000,
    kIOFBRotate90                       = kIOFBSwapAxes | kIOFBInvertX,
    kIOFBRotate180                      = kIOFBInvertX  | kIOFBInvertY,
    kIOFBRotate270                      = kIOFBSwapAxes | kIOFBInvertY
};

// =============================================================================
// ApplePS2Elan Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2Elan, IOHIPointing);

UInt32 ApplePS2Elan::deviceType()
{ return NX_EVS_DEVICE_TYPE_MOUSE; };

UInt32 ApplePS2Elan::interfaceID()
{ return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

#define abs(x) ((x) < 0 ? -(x) : (x))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Elan::init(OSDictionary * dict)
{
    //
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.
    //
    
    if (!super::init(dict))
        return false;

    // announce version
	extern kmod_info_t kmod_info;
    DEBUG_LOG("VoodooPS2Elan: Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);

	setProperty ("Revision", 24, 32);
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::injectVersionDependentProperties(OSDictionary *config)
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

ApplePS2Elan* ApplePS2Elan::probe(IOService * provider, SInt32 * score)
{
    DEBUG_LOG("ApplePS2Elan::probe entered...\n");
    
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
      setParamPropertiesGated(config);
      injectVersionDependentProperties(config);
      OSSafeReleaseNULL(config);
    }
    
    resetMouse();

    IOLog("VoodooPS2Elan: send magic knock to the device.\n");
    // send magic knock to the device
    if (elantechDetect()) {
        IOLog("VoodooPS2Elan: elantouchpad not detected\n");
        return NULL;
    }
    
    resetMouse();
    
    if (elantech_query_info())
    {
        IOLog("VoodooPS2Elan: query info failed\n");
        return NULL;
    }
    
    IOLog("VoodooPS2Elan: capabilities: %x %x %x\n", info.capabilities[0], info.capabilities[1], info.capabilities[2]);
    IOLog("VoodooPS2Elan: samples: %x %x %x\n", info.capabilities[0], info.capabilities[1], info.capabilities[2]);
    IOLog("VoodooPS2Elan: hw_version: %x\n", info.hw_version);
    IOLog("VoodooPS2Elan: fw_version: %x\n", info.fw_version);
    IOLog("VoodooPS2Elan: x_min: %d\n", info.x_min);
    IOLog("VoodooPS2Elan: y_min: %d\n", info.y_min);
    IOLog("VoodooPS2Elan: x_max: %d\n", info.x_max);
    IOLog("VoodooPS2Elan: y_max: %d\n", info.y_max);
    IOLog("VoodooPS2Elan: x_res: %d\n", info.x_res);
    IOLog("VoodooPS2Elan: y_res: %d\n", info.y_res);
    IOLog("VoodooPS2Elan: x_traces: %d\n", info.x_traces);
    IOLog("VoodooPS2Elan: y_traces: %d\n", info.y_traces);
    IOLog("VoodooPS2Elan: width: %d\n", info.width);
    IOLog("VoodooPS2Elan: bus: %d\n", info.bus);
    
    IOLog("VoodooPS2Elan: paritycheck: %d\n", info.paritycheck);
    IOLog("VoodooPS2Elan: jumpy_cursor: %d\n", info.jumpy_cursor);
    IOLog("VoodooPS2Elan: reports_pressure: %d\n", info.reports_pressure);
    IOLog("VoodooPS2Elan: crc_enabled: %d\n", info.crc_enabled);
    IOLog("VoodooPS2Elan: set_hw_resolution: %d\n", info.set_hw_resolution);
    IOLog("VoodooPS2Elan: has_trackpoint: %d\n", info.has_trackpoint);
    IOLog("VoodooPS2Elan: has_middle_button: %d\n", info.has_middle_button);
    
    IOLog("VoodooPS2Elan: elan touchpad detected. Probing finished.\n");
    
    _device = nullptr;
    
    return this;
}

bool ApplePS2Elan::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();

        return true;
    }

    return super::handleOpen(forClient, options, arg);
}

void ApplePS2Elan::handleClose(IOService *forClient, IOOptionBits options) {
    OSSafeReleaseNULL(voodooInputInstance);
    super::handleClose(forClient, options);
}

bool ApplePS2Elan::start(IOService* provider)
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

    char buf[128];
    snprintf(buf, sizeof(buf), "Elan v %d, fw: %x", info.hw_version, info.fw_version);
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
		_device = nullptr;
        return false;
    }
	    
    //
    // Lock the controller during initialization
    //
    
    _device->lock();
    
    attachedHIDPointerDevices = OSSet::withCapacity(1);
    registerHIDPointerNotifications();
    
    pWorkLoop->addEventSource(_cmdGate);
    
    elantech_setup_ps2();

    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //
    
    _device->installInterruptAction(this,
                                    OSMemberFunctionCast(PS2InterruptAction,this,&ApplePS2Elan::interruptOccurred),
                                    OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2Elan::packetReady));
    _interruptHandlerInstalled = true;
    
    Elantech_Touchpad_enable(true);
    
    // now safe to allow other threads
    _device->unlock();
    
    //
	// Install our power control handler.
	//
    
	_device->installPowerControlAction( this,
        OSMemberFunctionCast(PS2PowerControlAction, this, &ApplePS2Elan::setDevicePowerState) );
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
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::stop( IOService * provider )
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
    // Disable the mouse itself, so that it may stop reporting mouse events.
    //

    Elantech_Touchpad_enable(false);

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

void ApplePS2Elan::setParamPropertiesGated(OSDictionary * config)
{
	if (NULL == config)
		return;
    
	const struct {const char *name; int *var;} int32vars[]={
        {"WakeDelay",                       &wakedelay},
        {"ScrollResolution",                &_scrollresolution}
	};
	const struct {const char *name; int *var;} boolvars[]={
        {"ProcessUSBMouseStopsTrackpad",    &_processusbmouse},
        {"ProcessBluetoothMouseStopsTrackpad", &_processbluetoothmouse},
 	};
    const struct {const char* name; bool* var;} lowbitvars[]={
        {"USBMouseStopsTrackpad",           &usb_mouse_stops_trackpad},
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
    };
    
    // highrate?
	OSBoolean *bl;
	if ((bl=OSDynamicCast (OSBoolean, config->getObject ("UseHighRate"))))
    {
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

    // disable trackpad when USB mouse is plugged in and this functionality is requested
    if (attachedHIDPointerDevices && attachedHIDPointerDevices->getCount() > 0) {
        ignoreall = usb_mouse_stops_trackpad;
    }
}

IOReturn ApplePS2Elan::setParamProperties(OSDictionary* dict)
{
    ////IOReturn result = super::IOHIDevice::setParamProperties(dict);
    if (_cmdGate)
    {
        // syncronize through workloop...
        ////_cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Elan::setParamPropertiesGated), dict);
        setParamPropertiesGated(dict);
    }
    
    return super::setParamProperties(dict);
    ////return result;
}

IOReturn ApplePS2Elan::setProperties(OSObject *props)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, props);
    if (dict && _cmdGate)
    {
        // synchronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Elan::setParamPropertiesGated), dict);
    }
    
	return super::setProperties(props);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::setDevicePowerState( UInt32 whatToDo )
{
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            //
            // Disable touchpad (synchronous).
            //
            
            Elantech_Touchpad_enable(false);
            break;

        case kPS2C_EnableDevice:
            //
            // Must not issue any commands before the device has
            // completed its power-on self-test and calibration.
            //

            IOSleep(wakedelay);
            
            // Reset and enable the touchpad.
            // Clear packet buffer pointer to avoid issues caused by
            // stale packet fragments.
            //

            elantech_setup_ps2();
            
            _packetByteCount = 0;
            _ringBuffer.reset();
            
            _clickbuttons = 0;
            
            // clear state of control key cache
            _modifierdown = 0;
            Elantech_Touchpad_enable(true);
            break;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::registerHIDPointerNotifications()
{
    IOServiceMatchingNotificationHandler notificationHandler = OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this, &ApplePS2Elan::notificationHIDAttachedHandler);
    
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

void ApplePS2Elan::unregisterHIDPointerNotifications()
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

void ApplePS2Elan::notificationHIDAttachedHandlerGated(IOService * newService,
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
        }
    }
    
    if (notifier == usb_hid_terminate_notify || notifier == bluetooth_hid_terminate_notify) {
        if (usb_mouse_stops_trackpad && attachedHIDPointerDevices->getCount() == 0) {
            // No USB or bluetooth pointer devices attached, re-enable trackpad
            ignoreall = false;
        }
    }
}

bool ApplePS2Elan::notificationHIDAttachedHandler(void * refCon,
                                                               IOService * newService,
                                                               IONotifier * notifier)
{
    if (_cmdGate) { // defensive
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Elan::notificationHIDAttachedHandlerGated), newService, notifier);
    }

    return true;
}


/// elantech.c port

#define PSMOUSE_CMD_SETSCALE11    0x00e6
#define PSMOUSE_CMD_SETSCALE21    0x00e7
#define PSMOUSE_CMD_SETRES    0x10e8
#define PSMOUSE_CMD_GETINFO    0x03e9
#define PSMOUSE_CMD_SETSTREAM    0x00ea
#define PSMOUSE_CMD_SETPOLL    0x00f0
#define PSMOUSE_CMD_POLL    0x00eb    /* caller sets number of bytes to receive */
#define PSMOUSE_CMD_RESET_WRAP    0x00ec
#define PSMOUSE_CMD_GETID    0x02f2
#define PSMOUSE_CMD_SETRATE    0x10f3
#define PSMOUSE_CMD_ENABLE    0x00f4
#define PSMOUSE_CMD_DISABLE    0x00f5
#define PSMOUSE_CMD_RESET_DIS    0x00f6
#define PSMOUSE_CMD_RESET_BAT    0x02ff

template<int I>
int ApplePS2Elan::ps2_command(UInt8* params, unsigned int command)
{
    int rc = 0;

    TPS2Request<1 + I> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = command;
    for (int i = 0; i < I; ++i)
        request.commands[1 + i].command = kPS2C_ReadDataPort;
    request.commandsCount = 1 + I;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    for (int i = 0; i < I; ++i)
        params[i] = request.commands[i + 1].inOrOut;

    return request.commandsCount != 1 + I;
}

/*
 * A retrying version of ps2_command
 */
template<int I>
int ApplePS2Elan::elantech_ps2_command(unsigned char *param, int command)
{
    int rc;
    int tries = ETP_PS2_COMMAND_TRIES;

    do {
        rc = ps2_command<I>(param, command);
        if (rc == 0)
            break;
        tries--;
        IOLog("VoodooPS2Elan: retrying ps2 command 0x%02x (%d).\n",
                command, tries);
        IOSleep(ETP_PS2_COMMAND_DELAY);
    } while (tries > 0);

    if (rc)
        IOLog("VoodooPS2Elan: ps2 command 0x%02x failed.\n", command);

    return rc;
}

/*
 * ps2_sliced_command() sends an extended PS/2 command to the mouse
 * using sliced syntax, understood by advanced devices, such as Logitech
 * or Synaptics touchpads. The command is encoded as:
 * 0xE6 0xE8 rr 0xE8 ss 0xE8 tt 0xE8 uu where (rr*64)+(ss*16)+(tt*4)+uu
 * is the command.
 */

int ApplePS2Elan::ps2_sliced_command(UInt8 command)
{
    int j = 0;
    int retval;
    
    TPS2Request<> request;
    request.commands[j].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[j++].inOrOut = PSMOUSE_CMD_SETSCALE11;


    for (int i = 6; i >= 0; i -= 2) {
        UInt8 d = (command >> i) & 3;
        request.commands[j].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[j++].inOrOut = PSMOUSE_CMD_SETRES;
        
        request.commands[j].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[j++].inOrOut = d;
    }
    
    request.commandsCount = j;
    _device->submitRequestAndBlock(&request);
    
    return request.commandsCount != j;
}

/*
 * Send a Synaptics style sliced query command
 */
template<int I>
int ApplePS2Elan::synaptics_send_cmd(unsigned char c,
                unsigned char *param)
{
    if (ps2_sliced_command(c) ||
        ps2_command<I>(param, PSMOUSE_CMD_GETINFO)) {
            IOLog("VoodooPS2Elan: query 0x%02x failed.\n", c);
        return -1;
    }

    return 0;
}


/*
 * V3 and later support this fast command
 */
 template<int I>
int ApplePS2Elan::elantech_send_cmd(unsigned char c, unsigned char *param)
{
    if (ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
        ps2_command<0>(NULL, c) ||
        ps2_command<I>(param, PSMOUSE_CMD_GETINFO)) {
        IOLog("VoodooPS2Elan: query 0x%02x failed.\n", c);
        return -1;
    }

    return 0;
}

bool ApplePS2Elan::elantech_is_signature_valid(const unsigned char *param)
{
    static const unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10 };
    int i;

    if (param[0] == 0)
        return false;

    if (param[1] == 0)
        return true;

    /*
     * Some hw_version >= 4 models have a revision higher then 20. Meaning
     * that param[2] may be 10 or 20, skip the rates check for these.
     */
    if ((param[0] & 0x0f) >= 0x06 && (param[1] & 0xaf) == 0x0f &&
        param[2] < 40)
        return true;

    for (i = 0; i < sizeof(rates)/sizeof(*rates); i++)
        if (param[2] == rates[i])
            return false;

    return true;
}

/*
 * (value from firmware) * 10 + 790 = dpi
 * we also have to convert dpi to dots/mm (*10/254 to avoid floating point)
 */
static unsigned int elantech_convert_res(unsigned int val)
{
    return (val * 10 + 790) * 10 / 254;
}


int ApplePS2Elan::elantech_get_resolution_v4(unsigned int *x_res,
                      unsigned int *y_res,
                      unsigned int *bus)
{
    unsigned char param[3];

    if (elantech_send_cmd<3>(ETP_RESOLUTION_QUERY, param))
        return -1;

    *x_res = elantech_convert_res(param[1] & 0x0f);
    *y_res = elantech_convert_res((param[1] & 0xf0) >> 4);
    *bus = param[2];

    return 0;
}

template<int I>
int ApplePS2Elan::send_cmd(unsigned char c, unsigned char *param)
{
    if (info.hw_version >= 3)
        return elantech_send_cmd<I>(c, param);
    else
        return synaptics_send_cmd<I>(c, param);
}

/*
 * Use magic knock to detect Elantech touchpad
 */
int ApplePS2Elan::elantechDetect()
{
    unsigned char param[3];

    ps2_command<0>(NULL, PSMOUSE_CMD_RESET_DIS);
    
    if (ps2_command<0>( NULL, PSMOUSE_CMD_DISABLE) ||
        ps2_command<0>( NULL, PSMOUSE_CMD_SETSCALE11) ||
        ps2_command<0>( NULL, PSMOUSE_CMD_SETSCALE11) ||
        ps2_command<0>( NULL, PSMOUSE_CMD_SETSCALE11) ||
        ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
        IOLog("VoodooPS2Elan: sending Elantech magic knock failed.\n");
        return -1;
    }

    /*
     * Report this in case there are Elantech models that use a different
     * set of magic numbers
     */
    if (param[0] != 0x3c || param[1] != 0x03 ||
        (param[2] != 0xc8 && param[2] != 0x00)) {
        IOLog("VoodooPS2Elan: unexpected magic knock result 0x%02x, 0x%02x, 0x%02x.\n",
                param[0], param[1], param[2]);
        return -1;
    }

    /*
     * Query touchpad's firmware version and see if it reports known
     * value to avoid mis-detection. Logitech mice are known to respond
     * to Elantech magic knock and there might be more.
     */
    if (synaptics_send_cmd<3>(ETP_FW_VERSION_QUERY, param)) {
        IOLog("VoodooPS2Elan: failed to query firmware version.\n");
        return -1;
    }

    IOLog("VoodooPS2Elan: Elantech version query result 0x%02x, 0x%02x, 0x%02x.\n", param[0], param[1], param[2]);

    if (!elantech_is_signature_valid(param)) {
        IOLog("VoodooPS2Elan: Probably not a real Elantech touchpad. Aborting.\n");
        return -1;
    }

    return 0;
}

/*
 * determine hardware version and set some properties according to it.
 */
int ApplePS2Elan::elantech_set_properties()
{
    /* This represents the version of IC body. */
    int ver = (info.fw_version & 0x0f0000) >> 16;

    /* Early version of Elan touchpads doesn't obey the rule. */
    if (info.fw_version < 0x020030 || info.fw_version == 0x020600)
        info.hw_version = 1;
    else {
        switch (ver) {
        case 2:
        case 4:
                info.hw_version = 2;
            break;
        case 5:
                info.hw_version = 3;
            break;
        case 6 ... 15:
                info.hw_version = 4;
            break;
        default:
            return -1;
        }
    }

    /* decide which send_cmd we're gonna use early */
    // info.send_cmd = info.hw_version >= 3 ? elantech_send_cmd :
    //                     synaptics_send_cmd;
    // just use send_cmd
    
    /* Turn on packet checking by default */
    info.paritycheck = 1;

    /*
     * This firmware suffers from misreporting coordinates when
     * a touch action starts causing the mouse cursor or scrolled page
     * to jump. Enable a workaround.
     */
    info.jumpy_cursor =
    (info.fw_version == 0x020022 || info.fw_version == 0x020600);

    if (info.hw_version > 1) {
        /* For now show extra debug information */
        info.debug = 1;

        if (info.fw_version >= 0x020800)
            info.reports_pressure = true;
    }

    /*
     * The signatures of v3 and v4 packets change depending on the
     * value of this hardware flag.
     */
    info.crc_enabled = (info.fw_version & 0x4000) == 0x4000;

    /* Enable real hardware resolution on hw_version 3 ? */
    //info.set_hw_resolution = !dmi_check_system(no_hw_res_dmi_table);

    return 0;
}

int ApplePS2Elan::elantech_query_info()
{
    unsigned char param[3];
    unsigned char traces;

    /*
     * Do the version query again so we can store the result
     */
    if (synaptics_send_cmd<3>(ETP_FW_VERSION_QUERY, param)) {
        IOLog("VoodooPS2Elan: failed to query firmware version.\n");
        return -1;
    }
    
    info.fw_version = (param[0] << 16) | (param[1] << 8) | param[2];

    if (elantech_set_properties()) {
        IOLog("VoodooPS2Elan: unknown hardware version, aborting...\n");
        return -1;
    }
    IOLog("VoodooPS2Elan assuming hardware version %d (with firmware version 0x%02x%02x%02x)\n",
           info.hw_version, param[0], param[1], param[2]);

    if (send_cmd<3>(ETP_CAPABILITIES_QUERY,
                 info.capabilities)) {
        IOLog("VoodooPS2Elan: failed to query capabilities.\n");
        return -1;
    }
    IOLog("VoodooPS2Elan: Synaptics capabilities query result 0x%02x, 0x%02x, 0x%02x.\n",
           info.capabilities[0], info.capabilities[1],
           info.capabilities[2]);

    if (info.hw_version != 1) {
        if (send_cmd<3>(ETP_SAMPLE_QUERY, info.samples)) {
            IOLog("VoodooPS2Elan: failed to query sample data\n");
            return -1;
        }
        IOLog("VoodooPS2Elan: Elan sample query result %02x, %02x, %02x\n",
                      info.samples[0],
                      info.samples[1],
                      info.samples[2]);
    }

    if (info.samples[1] == 0x74 && info.hw_version == 0x03) {
        /*
         * This module has a bug which makes absolute mode
         * unusable, so let's abort so we'll be using standard
         * PS/2 protocol.
         */
        IOLog("VoodooPS2Elan: absolute mode broken, forcing standard PS/2 protocol\n");
        return -1;
    }

    /* The MSB indicates the presence of the trackpoint */
    info.has_trackpoint = (info.capabilities[0] & 0x80) == 0x80;

    info.x_res = 31;
    info.y_res = 31;
    if (info.hw_version == 4) {
        if (elantech_get_resolution_v4(&info.x_res,
                                       &info.y_res,
                                       &info.bus)) {
            IOLog("VoodooPS2Elan: failed to query resolution data.\n");
        }
    }

    /* query range information */
    switch (info.hw_version) {
    case 1:
            info.x_min = ETP_XMIN_V1;
            info.y_min = ETP_YMIN_V1;
            info.x_max = ETP_XMAX_V1;
            info.y_max = ETP_YMAX_V1;
        break;

    case 2:
            if (info.fw_version == 0x020800 ||
                info.fw_version == 0x020b00 ||
                info.fw_version == 0x020030) {
                info.x_min = ETP_XMIN_V2;
                info.y_min = ETP_YMIN_V2;
                info.x_max = ETP_XMAX_V2;
                info.y_max = ETP_YMAX_V2;
        } else {
            int i;
            int fixed_dpi;

            i = (info.fw_version > 0x020800 &&
                 info.fw_version < 0x020900) ? 1 : 2;

            if (send_cmd<3>(ETP_FW_ID_QUERY, param))
                return -1;

            fixed_dpi = param[1] & 0x10;

            if (((info.fw_version >> 16) == 0x14) && fixed_dpi) {
                if (send_cmd<3>(ETP_SAMPLE_QUERY, param))
                    return -1;

                info.x_max = (info.capabilities[1] - i) * param[1] / 2;
                info.y_max = (info.capabilities[2] - i) * param[2] / 2;
            } else if (info.fw_version == 0x040216) {
                info.x_max = 819;
                info.y_max = 405;
            } else if (info.fw_version == 0x040219 || info.fw_version == 0x040215) {
                info.x_max = 900;
                info.y_max = 500;
            } else {
                info.x_max = (info.capabilities[1] - i) * 64;
                info.y_max = (info.capabilities[2] - i) * 64;
            }
        }
        break;

    case 3:
            if (send_cmd<3>(ETP_FW_ID_QUERY, param))
            return -1;

            info.x_max = (0x0f & param[0]) << 8 | param[1];
            info.y_max = (0xf0 & param[0]) << 4 | param[2];
        break;

    case 4:
        if (send_cmd<3>(ETP_FW_ID_QUERY, param))
            return -1;

            info.x_max = (0x0f & param[0]) << 8 | param[1];
            info.y_max = (0xf0 & param[0]) << 4 | param[2];
            traces = info.capabilities[1];
            if ((traces < 2) || (traces > info.x_max))
            return -1;

            info.width = info.x_max / (traces - 1);

        /* column number of traces */
            info.x_traces = traces;

        /* row number of traces */
            traces = info.capabilities[2];
            if ((traces >= 2) && (traces <= info.y_max))
            info.y_traces = traces;

        break;
    }
    
    /* check for the middle button: DMI matching or new v4 firmwares */
    //info.has_middle_button = dmi_check_system(elantech_dmi_has_middle_button) ||
    //              (ETP_NEW_IC_SMBUS_HOST_NOTIFY(info.fw_version) &&
    //               !elantech_is_buttonpad(info));

    return 0;
}

void ApplePS2Elan::resetMouse()
{
    UInt8 params[2];
    ps2_command<2>(params, PSMOUSE_CMD_RESET_BAT);
    
    if (params[0] != 0xaa && params[1] != 0x00)
    {
        IOLog("VoodooPS2Elan: failed resetting.\n");
    }
}

/*
 * Send an Elantech style special command to read a value from a register
 */
int ApplePS2Elan::elantech_read_reg(unsigned char reg,
                unsigned char *val)
{
    unsigned char param[3];
    int rc = 0;

    if (reg < 0x07 || reg > 0x26)
        return -1;

    if (reg > 0x11 && reg < 0x20)
        return -1;

    switch (info.hw_version) {
    case 1:
        if (ps2_sliced_command(ETP_REGISTER_READ) ||
            ps2_sliced_command(reg) ||
            ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
            rc = -1;
        }
        break;

    case 2:
        if (elantech_ps2_command<0>( NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>( NULL, ETP_REGISTER_READ) ||
            elantech_ps2_command<0>( NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>( NULL, reg) ||
            elantech_ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
            rc = -1;
        }
        break;

    case 3 ... 4:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
            rc = -1;
        }
        break;
    }

    if (rc)
        IOLog("VoodooPS2Elan: failed to read register 0x%02x.\n", reg);
    else if (info.hw_version != 4)
        *val = param[0];
    else
        *val = param[1];

    return rc;
}

/*
 * Send an Elantech style special command to write a register with a value
 */
int ApplePS2Elan::elantech_write_reg(unsigned char reg, unsigned char val)
{
    int rc = 0;

    if (reg < 0x07 || reg > 0x26)
        return -1;

    if (reg > 0x11 && reg < 0x20)
        return -1;

    switch (info.hw_version) {
    case 1:
        if (ps2_sliced_command(ETP_REGISTER_WRITE) ||
            ps2_sliced_command(reg) ||
            ps2_sliced_command(val) ||
            ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;

    case 2:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_WRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, val) ||
            elantech_ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;

    case 3:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, val) ||
            elantech_ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;

    case 4:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, val) ||
            elantech_ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;
    }

    if (rc)
        IOLog("VoodooPS2Elan: failed to write register 0x%02x with value 0x%02x.\n",
                reg, val);

    return rc;
}

/*
 * Put the touchpad into absolute mode
 */
int ApplePS2Elan::elantech_set_absolute_mode()
{
    unsigned char val;
    int tries = ETP_READ_BACK_TRIES;
    int rc = 0;

    switch (info.hw_version) {
    case 1:
            etd.reg_10 = 0x16;
            etd.reg_11 = 0x8f;
            if (elantech_write_reg(0x10, etd.reg_10) ||
                elantech_write_reg(0x11, etd.reg_11)) {
            rc = -1;
        }
        break;

    case 2:
                    /* Windows driver values */
        etd.reg_10 = 0x54;
        etd.reg_11 = 0x88;    /* 0x8a */
        etd.reg_21 = 0x60;    /* 0x00 */
        if (elantech_write_reg(0x10, etd.reg_10) ||
            elantech_write_reg(0x11, etd.reg_11) ||
            elantech_write_reg(0x21, etd.reg_21)) {
            rc = -1;
        }
        break;

    case 3:
        if (info.set_hw_resolution)
            etd.reg_10 = 0x0b;
        else
            etd.reg_10 = 0x01;

        if (elantech_write_reg(0x10, etd.reg_10))
            rc = -1;

        break;

    case 4:
        etd.reg_07 = 0x01;
        if (elantech_write_reg(0x07, etd.reg_07))
            rc = -1;

        goto skip_readback_reg_10; /* v4 has no reg 0x10 to read */
    }

    if (rc == 0) {
        /*
         * Read back reg 0x10. For hardware version 1 we must make
         * sure the absolute mode bit is set. For hardware version 2
         * the touchpad is probably initializing and not ready until
         * we read back the value we just wrote.
         */
        do {
            rc = elantech_read_reg(0x10, &val);
            if (rc == 0)
                break;
            tries--;
            IOLog("VoodooPS2Elan: retrying read (%d).\n", tries);
            IOSleep(ETP_READ_BACK_DELAY);
        } while (tries > 0);

        if (rc) {
            IOLog("VoodooPS2Elan: failed to read back register 0x10.\n");
        } else if (info.hw_version == 1 &&
               !(val & ETP_R10_ABSOLUTE_MODE)) {
            IOLog("VoodooPS2Elan: touchpad refuses to switch to absolute mode.\n");
            rc = -1;
        }
    }

 skip_readback_reg_10:
    if (rc)
        IOLog("VoodooPS2Elan: failed to initialise registers.\n");

    return rc;
}

/*
 * Set the appropriate event bits for the input subsystem
 */
int ApplePS2Elan::elantech_set_input_params()
{
    unsigned int x_min = info.x_min, y_min = info.y_min,
             x_max = info.x_max, y_max = info.y_max,
             width = info.width;
    
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, info.x_max - info.x_min, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, info.y_max - info.y_min, 32);

    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, (info.x_max + 1) * 100 / info.x_res, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, (info.y_max + 1) * 100 / info.y_res, 32);

    setProperty("IOFBTransform", 0ull, 32);
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    registerService();
    
/*
    __set_bit(INPUT_PROP_POINTER, dev->propbit);
    __set_bit(EV_KEY, dev->evbit);
    __set_bit(EV_ABS, dev->evbit);
    __clear_bit(EV_REL, dev->evbit);

    __set_bit(BTN_LEFT, dev->keybit);
    if (info.has_middle_button)
        __set_bit(BTN_MIDDLE, dev->keybit);
    __set_bit(BTN_RIGHT, dev->keybit);

    __set_bit(BTN_TOUCH, dev->keybit);
    __set_bit(BTN_TOOL_FINGER, dev->keybit);
    __set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
    __set_bit(BTN_TOOL_TRIPLETAP, dev->keybit);

    switch (info.hw_version) {
    case 1:
        // Rocker button
        if (info.fw_version < 0x020000 &&
            (info.capabilities[0] & ETP_CAP_HAS_ROCKER)) {
            __set_bit(BTN_FORWARD, dev->keybit);
            __set_bit(BTN_BACK, dev->keybit);
        }
        input_set_abs_params(dev, ABS_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_Y, y_min, y_max, 0, 0);
        break;

    case 2:
        __set_bit(BTN_TOOL_QUADTAP, dev->keybit);
        __set_bit(INPUT_PROP_SEMI_MT, dev->propbit);
        // fall through
    case 3:
        if (info.hw_version == 3)
            elantech_set_buttonpad_prop(psmouse);
        input_set_abs_params(dev, ABS_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_Y, y_min, y_max, 0, 0);
        if (info.reports_pressure) {
            input_set_abs_params(dev, ABS_PRESSURE, ETP_PMIN_V2,
                         ETP_PMAX_V2, 0, 0);
            input_set_abs_params(dev, ABS_TOOL_WIDTH, ETP_WMIN_V2,
                         ETP_WMAX_V2, 0, 0);
        }
        input_mt_init_slots(dev, 2, INPUT_MT_SEMI_MT);
        input_set_abs_params(dev, ABS_MT_POSITION_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_MT_POSITION_Y, y_min, y_max, 0, 0);
        break;

    case 4:
        elantech_set_buttonpad_prop(psmouse);
        __set_bit(BTN_TOOL_QUADTAP, dev->keybit);
        // For X to recognize me as touchpad.
        input_set_abs_params(dev, ABS_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_Y, y_min, y_max, 0, 0);
        //
        // range of pressure and width is the same as v2,
        // report ABS_PRESSURE, ABS_TOOL_WIDTH for compatibility.
        //
        input_set_abs_params(dev, ABS_PRESSURE, ETP_PMIN_V2,
                     ETP_PMAX_V2, 0, 0);
        input_set_abs_params(dev, ABS_TOOL_WIDTH, ETP_WMIN_V2,
                     ETP_WMAX_V2, 0, 0);
        // Multitouch capable pad, up to 5 fingers.
        input_mt_init_slots(dev, ETP_MAX_FINGERS, 0);
        input_set_abs_params(dev, ABS_MT_POSITION_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_MT_POSITION_Y, y_min, y_max, 0, 0);
        input_set_abs_params(dev, ABS_MT_PRESSURE, ETP_PMIN_V2,
                     ETP_PMAX_V2, 0, 0);
        //
         //The firmware reports how many trace lines the finger spans,
         //convert to surface unit as Protocol-B requires.
         //
        input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR, 0,
                     ETP_WMAX_V2 * width, 0, 0);
        break;
    }

    input_abs_set_res(dev, ABS_X, info.x_res);
    input_abs_set_res(dev, ABS_Y, info.y_res);
    if (info.hw_version > 1) {
        input_abs_set_res(dev, ABS_MT_POSITION_X, info.x_res);
        input_abs_set_res(dev, ABS_MT_POSITION_Y, info.y_res);
    }
*/
    etd.y_max = y_max;
    etd.width = width;

    return 0;
}

/*
 * Initialize the touchpad and create sysfs entries
 */
int ApplePS2Elan::elantech_setup_ps2()
{
    int i;
    int error = -1;

    etd.parity[0] = 1;
    for (i = 1; i < 256; i++)
        etd.parity[i] = etd.parity[i & (i - 1)] ^ 1;

    if (elantech_set_absolute_mode()) {
        IOLog("VoodooPS2: failed to put touchpad into absolute mode.\n");
        return -1;
    }

    if (info.fw_version == 0x381f17) {
        //etd.original_set_rate = psmouse->set_rate;
        //psmouse->set_rate = elantech_set_rate_restore_reg_07;
    }

    if (elantech_set_input_params()) {
        IOLog("VoodooPS2: failed to query touchpad range.\n");
        return -1;
    }

    
    // set resolution and dpi
    TPS2Request<> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetDefaultsAndDisable;           // 0xF5, Disable data reporting
    request.commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut = kDP_SetMouseSampleRate;              // 0xF3
    request.commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut = 0x64 * 2;                                // 100 dpi
    request.commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut = kDP_SetMouseResolution;              // 0xE8
    request.commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut = 0x03;                                // 0x03 = 8 counts/mm
    request.commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut = kDP_SetMouseScaling1To1;             // 0xE6
    request.commands[6].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut = kDP_Enable;                          // 0xF4, Enable Data Reporting
    request.commandsCount = 7;
    _device->submitRequestAndBlock(&request);
    
    return 0;
}

PS2InterruptResult ApplePS2Elan::interruptOccurred(UInt8 data) {
    UInt8* packet = _ringBuffer.head();
    packet[_packetByteCount++] = data;

    //INTERRUPT_LOG("VoodooPS2Elan: Got packet %x\n", data);
    
    if (_packetByteCount == kPacketLengthMax)
    {
        _ringBuffer.advanceHead(kPacketLengthMax);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }

    return kPS2IR_packetBuffering;
}

int ApplePS2Elan::elantech_packet_check_v4()
{
    unsigned char *packet = _ringBuffer.tail();
    unsigned char packet_type = packet[3] & 0x03;
    unsigned int ic_version;
    bool sanity_check;
    
    INTERRUPT_LOG("VoodooPS2Elan: Packet dump (%04x, %04x, %04x, %04x, %04x, %04x)\n", packet[0], packet[1], packet[2], packet[3], packet[4], packet[5] );

    if (etd.tp_dev && (packet[3] & 0x0f) == 0x06)
        return PACKET_TRACKPOINT;

    /* This represents the version of IC body. */
    ic_version = (info.fw_version & 0x0f0000) >> 16;
    
    INTERRUPT_LOG("VoodooPS2Elan: icVersion(%d), crc(%d), samples[1](%d) \n", ic_version, info.crc_enabled, info.samples[1]);

    /*
     * Sanity check based on the constant bits of a packet.
     * The constant bits change depending on the value of
     * the hardware flag 'crc_enabled' and the version of
     * the IC body, but are the same for every packet,
     * regardless of the type.
     */
    if (info.crc_enabled)
        sanity_check = ((packet[3] & 0x08) == 0x00);
    else if (ic_version == 7 && info.samples[1] == 0x2A)
        sanity_check = ((packet[3] & 0x1c) == 0x10);
    else
        sanity_check = ((packet[0] & 0x08) == 0x00 &&
                (packet[3] & 0x1c) == 0x10);

    if (!sanity_check)
        return PACKET_UNKNOWN;

    switch (packet_type) {
    case 0:
        return PACKET_V4_STATUS;

    case 1:
        return PACKET_V4_HEAD;

    case 2:
        return PACKET_V4_MOTION;
    }

    return PACKET_UNKNOWN;
}

void ApplePS2Elan::processPacketStatusV4() {
    unsigned char *packet = _ringBuffer.tail();
    unsigned fingers;

    /* notify finger state change */
    fingers = packet[1] & 0x1f;
    int count = 0;
    for (int i = 0; i < ETP_MAX_FINGERS; i++) {
        if ((fingers & (1 << i)) == 0) {
            // finger has been lifted off the touchpad
            INTERRUPT_LOG("VoodooPS2Elan: %d finger has been lifted off the touchpad\n", i);
            virtualFinger[i].touch = false;
        }
        else
        {
            virtualFinger[i].touch = true;
            INTERRUPT_LOG("VoodooPS2Elan: %d finger has been touched the touchpad\n", i);
            count++;
        }
    }
    
    heldFingers = count;
    headPacketsCount = 0;
    
    if (count == 0)
        elantechInputSyncV4();
}

void ApplePS2Elan::processPacketHeadV4() {
    unsigned char *packet = _ringBuffer.tail();
    int id = ((packet[3] & 0xe0) >> 5) - 1;
    int pres, traces;

    headPacketsCount++;
    
    if (id < 0) {
        INTERRUPT_LOG("VoodooPS2Elan: invalid id, aborting\n");
        return;
    }

    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    uint64_t timestamp_ns;

    int x = ((packet[1] & 0x0f) << 8) | packet[2];
    int y = info.y_max - (((packet[4] & 0x0f) << 8) | packet[5]);

    pres = (packet[1] & 0xf0) | ((packet[4] & 0xf0) >> 4);
    traces = (packet[0] & 0xf0) >> 4;
    
    INTERRUPT_LOG("VoodooPS2Elan: pres: %d, traces: %d, width: %d\n", pres, traces, etd.width);

    //IOLog("VoodooPS2Elan: Pressure: %d\n", pres	);
    //IOLog("VoodooPS2Elan: Width: %d\n", traces);
    
    virtualFinger[id].button = (packet[0] & 1);
    virtualFinger[id].prev = virtualFinger[id].now;
    virtualFinger[id].now.pressure = pres;
    virtualFinger[id].now.width = pres / 4;// traces;
    //if (virtualFinger[id].now.width == 0)
    //    virtualFinger[id].now.width = 1;
        
    virtualFinger[id].now.x = x;
    virtualFinger[id].now.y = y;
    

    if (headPacketsCount == heldFingers)
    {
        headPacketsCount = 0;
        elantechInputSyncV4();
    }
}

void ApplePS2Elan::processPacketMotionV4() {
    unsigned char *packet = _ringBuffer.tail();
    int weight, delta_x1 = 0, delta_y1 = 0, delta_x2 = 0, delta_y2 = 0;
    int id, sid;

    id = ((packet[0] & 0xe0) >> 5) - 1;
    if (id < 0) {
        INTERRUPT_LOG("VoodooPS2Elan: invalid id, aborting\n");
        return;
    }
    
    sid = ((packet[3] & 0xe0) >> 5) - 1;
    weight = (packet[0] & 0x10) ? ETP_WEIGHT_VALUE : 1;
    /*
     * Motion packets give us the delta of x, y values of specific fingers,
     * but in two's complement. Let the compiler do the conversion for us.
     * Also _enlarge_ the numbers to int, in case of overflow.
     */
    delta_x1 = (signed char)packet[1];
    delta_y1 = (signed char)packet[2];
    delta_x2 = (signed char)packet[4];
    delta_y2 = (signed char)packet[5];
        
    virtualFinger[id].button = (packet[0] & 1);
    virtualFinger[id].prev = virtualFinger[id].now;
    virtualFinger[id].now.x += delta_x1 * weight;
    virtualFinger[id].now.y -= delta_y1 * weight;
    
    if (sid >= 0) {
        virtualFinger[sid].button = (packet[3] & 1);
        virtualFinger[sid].prev = virtualFinger[sid].now;
        virtualFinger[sid].now.x += delta_x2 * weight;
        virtualFinger[sid].now.y -= delta_y2 * weight;
    }
    
    elantechInputSyncV4();
}

void ApplePS2Elan::elantechInputSyncV4() {
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    
    int count = 0;
    for (int i = 0; i < 5; ++i){
        if (!virtualFinger[i].touch)
            continue;
        
        inputEvent.transducers[count].currentCoordinates = virtualFinger[i].now;
        inputEvent.transducers[count].previousCoordinates = virtualFinger[i].prev;
        
    
        inputEvent.transducers[count].isValid = true;
        inputEvent.transducers[count].isPhysicalButtonDown = virtualFinger[i].button;
        inputEvent.transducers[count].isTransducerActive = true;
        
        inputEvent.transducers[count].secondaryId = count;
        inputEvent.transducers[count].fingerType = MT2FingerType(count+2);//virtualFinger[i].fingerType;
        inputEvent.transducers[count].type = FINGER;
        
        // it looks like Elan PS2 pressure and width is very inaccurate
        // it is better to leave it that way
        inputEvent.transducers[count].supportsPressure = false;
        
        inputEvent.transducers[count].timestamp= timestamp;
        
        count++;
    }
    
    for (int i = count; i < VOODOO_INPUT_MAX_TRANSDUCERS; ++i)
    {
        inputEvent.transducers[i].isValid = false;
        inputEvent.transducers[i].isPhysicalButtonDown = false;
        inputEvent.transducers[i].isTransducerActive = false;
    }
 
    
    inputEvent.contact_count = count;
    inputEvent.timestamp = timestamp;
    
    if (voodooInputInstance)
    {
        IOLog("VoodooPS2Elan: STATE: %d fingers\n", inputEvent.contact_count);
        super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &inputEvent, sizeof(VoodooInputEvent));
    }
}


void ApplePS2Elan::elantechReportAbsoluteV4(int packetType)
{
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);

    inputEvent.timestamp = timestamp;

    switch (packetType) {
        case PACKET_V4_STATUS:
            INTERRUPT_LOG("VoodooPS2Elan: Got status packet\n");
            processPacketStatusV4();
            break;

        case PACKET_V4_HEAD:
            INTERRUPT_LOG("VoodooPS2Elan: Got head packet\n");
            processPacketHeadV4();
            break;

        case PACKET_V4_MOTION:
            INTERRUPT_LOG("VoodooPS2Elan: Got motion packet\n");
            processPacketMotionV4();
            break;
        case PACKET_UNKNOWN:
        default:
            /* impossible to get here */
            break;
    }
    
    if (voodooInputInstance) {
        if (changed)
        {
            VoodooInputDimensions d;
            d.min_x = info.x_min;
            d.max_x = info.x_max;
            d.min_y = info.y_min;
            d.max_y = info.y_max;
            super::messageClient(kIOMessageVoodooInputUpdateDimensionsMessage, voodooInputInstance, &d, sizeof(VoodooInputDimensions));

            changed = false;
        }
        
    }
    else
        INTERRUPT_LOG("VoodooPS2Elan: no voodooInputInstance\n");
}

void ApplePS2Elan::packetReady()
{
    INTERRUPT_LOG("VoodooPS2Elan: packet ready occurred\n");
    // empty the ring buffer, dispatching each packet...
    while (_ringBuffer.count() >= kPacketLength)
    {
        int packetType;
        switch (info.hw_version) {
            case 1:
            case 2:
                INTERRUPT_LOG("VoodooPS2Elan: packet ready occurred, but unsupported version\n");
                // V1 and V2 are ancient hardware, not going to implement right away
                break;
            case 3:
                INTERRUPT_LOG("VoodooPS2Elan: packet ready occurred, but unsupported version\n");
                break;
            case 4:
                packetType = elantech_packet_check_v4();

                INTERRUPT_LOG("VoodooPS2Elan: Packet Type %d\n", packetType);

                switch (packetType) {
                    case PACKET_UNKNOWN:
                        INTERRUPT_LOG("VoodooPS2Elan: Handling unknown mode\n");
                        break;

                    case PACKET_TRACKPOINT:
                        INTERRUPT_LOG("VoodooPS2Elan: Handling trackpoint mode\n");
                        break;
                    default:
                        INTERRUPT_LOG("VoodooPS2Elan: Handling absolute mode\n");
                        elantechReportAbsoluteV4(packetType);
                }
                break;
            default:
                INTERRUPT_LOG("VoodooPS2Elan: invalid packet received\n");
        }

        _ringBuffer.advanceTail(kPacketLength);
    }
}

void ApplePS2Elan::setMouseEnable(bool enable)
{
  //
  // Instructs the mouse to start or stop the reporting of mouse events.
  // Be aware that while the mouse is enabled, asynchronous mouse events
  // may arrive in the middle of command sequences sent to the controller,
  // and may get confused for expected command responses.
  //
  // It is safe to issue this request from the interrupt/completion context.
  //

  // (mouse enable/disable command)
  TPS2Request<3> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = enable ? kDP_Enable : kDP_SetDefaultsAndDisable;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commandsCount = 3;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::setMouseSampleRate(UInt8 sampleRate)
{
  DEBUG_LOG("%s::setMouseSampleRate(0x%x)\n", getName(), sampleRate);
    
  //
  // Instructs the mouse to change its sampling rate to the given value, in
  // reports per second.
  //
  // It is safe to issue this request from the interrupt/completion context.
  //

  // (set mouse sample rate command)
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_SetMouseSampleRate;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_WriteCommandPort;
  request.commands[3].inOrOut = kCP_TransmitToMouse;
  request.commands[4].command = kPS2C_WriteDataPort;
  request.commands[4].inOrOut = sampleRate;
  request.commands[5].command = kPS2C_ReadDataPortAndCompare;
  request.commands[5].inOrOut = kSC_Acknowledge;
  request.commandsCount = 6;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::setMouseResolution(UInt8 resolution)
{
  //
  // Instructs the mouse to change its resolution given the following
  // resolution codes:
  //
  // 0 =  25 dpi
  // 1 =  50 dpi
  // 2 = 100 dpi
  // 3 = 200 dpi
  //
  // It is safe to issue this request from the interrupt/completion context.
  //
    
  DEBUG_LOG("%s::setMouseResolution(0x%x)\n", getName(), resolution);

  // (set mouse resolution command)
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_SetMouseResolution;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_WriteCommandPort;
  request.commands[3].inOrOut = kCP_TransmitToMouse;
  request.commands[4].command = kPS2C_WriteDataPort;
  request.commands[4].inOrOut = resolution;
  request.commands[5].command = kPS2C_ReadDataPortAndCompare;
  request.commands[5].inOrOut = kSC_Acknowledge;
  request.commandsCount = 6;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
}

void ApplePS2Elan::Elantech_Touchpad_enable(bool enable )
{
    ps2_command<0>(NULL, (enable)?kDP_Enable:kDP_SetDefaultsAndDisable);
}
