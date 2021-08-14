/*
* Elan PS2 touchpad integration
*
* Mostly contains code ported from Linux
* https://github.com/torvalds/linux/blob/master/drivers/input/mouse/elantech.c
*
* Created by Bartosz Korczyński (@bandysc), Hiep Bao Le (@hieplpvip)
* Special thanks to Kishor Prins (@kprinssu), EMlyDinEsHMG and whole VoodooInput team
*/

// generally one cannot IOLog from interrupt context, it eventually leads to kernel panic
// but it is useful sometimes
#if 0
#define INTERRUPT_LOG(args...)  do { IOLog(args); } while (0)
#else
#define INTERRUPT_LOG(args...)  do { } while (0)
#endif

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/usb/IOUSBHostFamily.h>
#include <IOKit/usb/IOUSBHostHIDDevice.h>
#include <IOKit/bluetooth/BluetoothAssignedNumbers.h>
#include "VoodooPS2Controller.h"
#include "VoodooPS2Elan.h"
#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"

// =============================================================================
// ApplePS2Elan Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2Elan, IOHIPointing);

UInt32 ApplePS2Elan::deviceType()
{ return NX_EVS_DEVICE_TYPE_MOUSE; };

UInt32 ApplePS2Elan::interfaceID()
{ return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

bool ApplePS2Elan::init(OSDictionary *dict) {
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.

    if (!super::init(dict)) {
        return false;
    }

    // announce version
    extern kmod_info_t kmod_info;
    DEBUG_LOG("VoodooPS2Elan: Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);

    return true;
}

void ApplePS2Elan::injectVersionDependentProperties(OSDictionary *config) {
    // inject properties specific to the version of Darwin that is runnning...
    char buf[32];
    OSDictionary *dict = NULL;
    do {
        // check for "Darwin major.minor"
        snprintf(buf, sizeof(buf), "Darwin %d.%d", version_major, version_minor);
        if ((dict = OSDynamicCast(OSDictionary, config->getObject(buf)))) {
            break;
        }

        // check for "Darwin major.x"
        snprintf(buf, sizeof(buf), "Darwin %d.x", version_major);
        if ((dict = OSDynamicCast(OSDictionary, config->getObject(buf)))) {
            break;
        }

        // check for "Darwin 16+" (this is what is used currently, other formats are for future)
        if (version_major >= 16 && (dict = OSDynamicCast(OSDictionary, config->getObject("Darwin 16+")))) {
            break;
        }
    } while (0);

    if (dict) {
        // found version specific properties above, inject...
        if (OSCollectionIterator *iter = OSCollectionIterator::withCollection(dict)) {
            // Note: OSDictionary always contains OSSymbol*
            while (const OSSymbol *key = static_cast<const OSSymbol*>(iter->getNextObject())) {
                if (OSObject *value = dict->getObject(key)) {
                    setProperty(key, value);
                }
            }
            iter->release();
        }
    }
}

ApplePS2Elan *ApplePS2Elan::probe(IOService *provider, SInt32 *score) {
    DEBUG_LOG("ApplePS2Elan::probe entered...\n");

    // The driver has been instructed to verify the presence of the actual
    // hardware we represent. We are guaranteed by the controller that the
    // mouse clock is enabled and the mouse itself is disabled (thus it
    // won't send any asynchronous mouse data that may mess up the
    // responses expected by the commands we send it).

    if (!super::probe(provider, score)) {
        return 0;
    }

    _device = (ApplePS2MouseDevice*)provider;

    // find config specific to Platform Profile
    OSDictionary *list = OSDynamicCast(OSDictionary, getProperty(kPlatformProfile));
    OSDictionary *config = _device->getController()->makeConfigurationNode(list, "Elantech TouchPad");
    if (config) {
        // if DisableDevice is Yes, then do not load at all...
        OSBoolean *disable = OSDynamicCast(OSBoolean, config->getObject(kDisableDevice));
        if (disable && disable->isTrue()) {
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

    DEBUG_LOG("VoodooPS2Elan: send magic knock to the device.\n");
    // send magic knock to the device
    if (elantechDetect()) {
        DEBUG_LOG("VoodooPS2Elan: elan touchpad not detected\n");
        return NULL;
    }

    resetMouse();

    if (elantechQueryInfo()) {
        DEBUG_LOG("VoodooPS2Elan: query info failed\n");
        return NULL;
    }

    DEBUG_LOG("VoodooPS2Elan: capabilities: %x %x %x\n", info.capabilities[0], info.capabilities[1], info.capabilities[2]);
    DEBUG_LOG("VoodooPS2Elan: samples: %x %x %x\n", info.capabilities[0], info.capabilities[1], info.capabilities[2]);
    DEBUG_LOG("VoodooPS2Elan: hw_version: %x\n", info.hw_version);
    DEBUG_LOG("VoodooPS2Elan: fw_version: %x\n", info.fw_version);
    DEBUG_LOG("VoodooPS2Elan: x_min: %d\n", info.x_min);
    DEBUG_LOG("VoodooPS2Elan: y_min: %d\n", info.y_min);
    DEBUG_LOG("VoodooPS2Elan: x_max: %d\n", info.x_max);
    DEBUG_LOG("VoodooPS2Elan: y_max: %d\n", info.y_max);
    DEBUG_LOG("VoodooPS2Elan: x_res: %d\n", info.x_res);
    DEBUG_LOG("VoodooPS2Elan: y_res: %d\n", info.y_res);
    DEBUG_LOG("VoodooPS2Elan: x_traces: %d\n", info.x_traces);
    DEBUG_LOG("VoodooPS2Elan: y_traces: %d\n", info.y_traces);
    DEBUG_LOG("VoodooPS2Elan: width: %d\n", info.width);
    DEBUG_LOG("VoodooPS2Elan: bus: %d\n", info.bus);
    DEBUG_LOG("VoodooPS2Elan: paritycheck: %d\n", info.paritycheck);
    DEBUG_LOG("VoodooPS2Elan: jumpy_cursor: %d\n", info.jumpy_cursor);
    DEBUG_LOG("VoodooPS2Elan: reports_pressure: %d\n", info.reports_pressure);
    DEBUG_LOG("VoodooPS2Elan: crc_enabled: %d\n", info.crc_enabled);
    DEBUG_LOG("VoodooPS2Elan: set_hw_resolution: %d\n", info.set_hw_resolution);
    DEBUG_LOG("VoodooPS2Elan: has_trackpoint: %d\n", info.has_trackpoint);
    DEBUG_LOG("VoodooPS2Elan: has_middle_button: %d\n", info.has_middle_button);

    DEBUG_LOG("VoodooPS2Elan: elan touchpad detected. Probing finished.\n");

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

bool ApplePS2Elan::start(IOService *provider) {
    // The driver has been instructed to start. This is called after a
    // successful probe and match.

    if (!super::start(provider)) {
        return false;
    }

    // Maintain a pointer to and retain the provider object.
    _device = (ApplePS2MouseDevice *)provider;
    _device->retain();

    // Announce hardware properties.
    char buf[128];
    snprintf(buf, sizeof(buf), "Elan v %d, fw: %x, bus: %d", info.hw_version, info.fw_version, info.bus);
    setProperty("RM,TrackpadInfo", buf);

#ifdef DEBUG
    if (info.bus == ETP_BUS_PS2_ONLY) {
        setProperty("Bus", "ETP_BUS_PS2_ONLY");
    } else if (info.bus == ETP_BUS_SMB_ALERT_ONLY) {
        setProperty("Bus", "ETP_BUS_SMB_ALERT_ONLY");
    } else if (info.bus == ETP_BUS_SMB_HST_NTFY_ONLY) {
        setProperty("Bus", "ETP_BUS_SMB_HST_NTFY_ONLY");
    } else if (info.bus == ETP_BUS_PS2_SMB_ALERT) {
        setProperty("Bus", "ETP_BUS_PS2_SMB_ALERT");
    } else if (info.bus == ETP_BUS_PS2_SMB_HST_NTFY) {
        setProperty("Bus", "ETP_BUS_PS2_SMB_HST_NTFY");
    }

    if (info.bus == ETP_BUS_SMB_HST_NTFY_ONLY ||
        info.bus == ETP_BUS_PS2_SMB_HST_NTFY ||
        ETP_NEW_IC_SMBUS_HOST_NOTIFY(info.fw_version)) {
        setProperty("SMBus NOTE", "It looks like your touchpad is supported by VoodooSMBus kext, which gives better multitouch experience. We recommend you to try it.");
    } else if (info.bus == ETP_BUS_PS2_ONLY) {
        setProperty("SMBus NOTE", "It looks like your touchpad does not support SMBus protocol.");
    }
#endif

    // Advertise the current state of the tapping feature.
    //
    // Must add this property to let our superclass know that it should handle
    // trackpad acceleration settings from user space.  Without this, tracking
    // speed adjustments from the mouse prefs panel have no effect.
    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
    setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDTrackpadScrollAccelerationKey);
    setProperty(kIOHIDScrollResolutionKey, _scrollresolution << 16, 32);
    // added for Sierra precise scrolling (credit @usr-sse2)
    setProperty("HIDScrollResolutionX", _scrollresolution << 16, 32);
    setProperty("HIDScrollResolutionY", _scrollresolution << 16, 32);

    // Setup workloop with command gate for thread syncronization...
    IOWorkLoop *pWorkLoop = getWorkLoop();
    _cmdGate = IOCommandGate::commandGate(this);
    if (!pWorkLoop || !_cmdGate) {
        OSSafeReleaseNULL(_device);
        return false;
    }

    // Lock the controller during initialization
    _device->lock();

    attachedHIDPointerDevices = OSSet::withCapacity(1);
    registerHIDPointerNotifications();

    pWorkLoop->addEventSource(_cmdGate);

    elantechSetupPS2();

    // Install our driver's interrupt handler, for asynchronous data delivery.
    _device->installInterruptAction(this,
                                    OSMemberFunctionCast(PS2InterruptAction, this, &ApplePS2Elan::interruptOccurred),
                                    OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2Elan::packetReady));
    _interruptHandlerInstalled = true;

    // Enable the touchpad
    setTouchPadEnable(true);

    // Now it is safe to allow other threads
    _device->unlock();

    // Install our power control handler
    _device->installPowerControlAction(this, OSMemberFunctionCast(PS2PowerControlAction, this, &ApplePS2Elan::setDevicePowerState));
    _powerControlHandlerInstalled = true;

    // Request message registration for keyboard to trackpad communication
    //setProperty(kDeliverNotifications, true);

    return true;
}

void ApplePS2Elan::stop(IOService *provider) {
    DEBUG_LOG("%s: stop called\n", getName());

    // The driver has been instructed to stop. Note that we must break all
    // connections to other service objects now (ie. no registered actions,
    // no pointers and retains to objects, etc), if any.

    assert(_device == provider);

    unregisterHIDPointerNotifications();
    OSSafeReleaseNULL(attachedHIDPointerDevices);

    // Disable the touchpad
    setTouchPadEnable(false);

    // Release command gate
    IOWorkLoop *pWorkLoop = getWorkLoop();
    if (pWorkLoop) {
        if (_cmdGate) {
            pWorkLoop->removeEventSource(_cmdGate);
            OSSafeReleaseNULL(_cmdGate);
        }
    }

    // Uninstall the interrupt handler
    if (_interruptHandlerInstalled) {
        _device->uninstallInterruptAction();
        _interruptHandlerInstalled = false;
    }

    // Uninstall the power control handler
    if (_powerControlHandlerInstalled) {
        _device->uninstallPowerControlAction();
        _powerControlHandlerInstalled = false;
    }

    // Release the pointer to the provider object.
    OSSafeReleaseNULL(_device);

    super::stop(provider);
}

void ApplePS2Elan::setParamPropertiesGated(OSDictionary *config) {
    if (NULL == config) {
        return;
    }

    const struct {const char *name; int *var;} int32vars[] = {
        {"WakeDelay",                          &wakedelay},
        {"ScrollResolution",                   &_scrollresolution},
        {"TrackpointMultiplierX",              &_trackpointMultiplierX},
        {"TrackpointMultiplierY",              &_trackpointMultiplierY},
        {"TrackpointDividerX",                 &_trackpointDividerX},
        {"TrackpointDividerY",                 &_trackpointDividerY},
        {"MouseResolution",                    &_mouseResolution},
        {"MouseSampleRate",                    &_mouseSampleRate},
        {"ForceTouchMode",                     (int*)&_forceTouchMode},
    };

    const struct {const char *name; uint64_t *var;} int64vars[] = {
        {"QuietTimeAfterTyping",               &maxaftertyping},
    };

    const struct {const char *name; bool *var;} boolvars[] = {
        {"ProcessUSBMouseStopsTrackpad",       &_processusbmouse},
        {"ProcessBluetoothMouseStopsTrackpad", &_processbluetoothmouse},
        {"SetHwResolution",                    &_set_hw_resolution},
    };

    const struct {const char *name; bool *var;} lowbitvars[] = {
        {"USBMouseStopsTrackpad",              &usb_mouse_stops_trackpad},
    };

    OSBoolean *bl;
    OSNumber *num;

    // highrate?
    if ((bl = OSDynamicCast(OSBoolean, config->getObject("UseHighRate")))) {
        setProperty("UseHighRate", bl->isTrue());
    }

    // 32-bit config items
    for (int i = 0; i < countof(int32vars); i++) {
        if ((num = OSDynamicCast(OSNumber, config->getObject(int32vars[i].name)))) {
            *int32vars[i].var = num->unsigned32BitValue();
            setProperty(int32vars[i].name, *int32vars[i].var, 32);
        }
    }

    // 64-bit config items
    for (int i = 0; i < countof(int64vars); i++) {
        if ((num = OSDynamicCast(OSNumber, config->getObject(int64vars[i].name)))) {
            *int64vars[i].var = num->unsigned64BitValue();
            setProperty(int64vars[i].name, *int64vars[i].var, 64);
        }
    }

    // boolean config items
    for (int i = 0; i < countof(boolvars); i++) {
        if ((bl = OSDynamicCast(OSBoolean, config->getObject(boolvars[i].name)))) {
            *boolvars[i].var = bl->isTrue();
            setProperty(boolvars[i].name, *boolvars[i].var ? kOSBooleanTrue : kOSBooleanFalse);
        }
    }

    // lowbit config items
    for (int i = 0; i < countof(lowbitvars); i++) {
        if ((num = OSDynamicCast(OSNumber, config->getObject(lowbitvars[i].name)))) {
            *lowbitvars[i].var = (num->unsigned32BitValue() & 0x1) ? true : false;
            setProperty(lowbitvars[i].name, *lowbitvars[i].var ? 1 : 0, 32);
        } else if ((bl = OSDynamicCast(OSBoolean, config->getObject(lowbitvars[i].name)))) {
            // REVIEW: are these items ever carried in a boolean?
            *lowbitvars[i].var = bl->isTrue();
            setProperty(lowbitvars[i].name, *lowbitvars[i].var ? kOSBooleanTrue : kOSBooleanFalse);
        }
    }

    // disable trackpad when USB mouse is plugged in and this functionality is requested
    if (attachedHIDPointerDevices && attachedHIDPointerDevices->getCount() > 0) {
        ignoreall = usb_mouse_stops_trackpad;
    }
}

IOReturn ApplePS2Elan::setParamProperties(OSDictionary *dict) {
    if (_cmdGate) {
        // syncronize through workloop...
        //_cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Elan::setParamPropertiesGated), dict);
        setParamPropertiesGated(dict);
    }

    return super::setParamProperties(dict);
}

IOReturn ApplePS2Elan::setProperties(OSObject *props) {
    OSDictionary *dict = OSDynamicCast(OSDictionary, props);
    if (dict && _cmdGate) {
        // synchronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Elan::setParamPropertiesGated), dict);
    }

    return super::setProperties(props);
}

IOReturn ApplePS2Elan::message(UInt32 type, IOService* provider, void* argument) {
    // Here is where we receive messages from the keyboard driver
    //
    // This allows for the keyboard driver to enable/disable the trackpad
    // when a certain keycode is pressed.
    //
    // It also allows the trackpad driver to learn the last time a key
    // has been pressed, so it can implement various "ignore trackpad
    // input while typing" options.
    switch (type) {
        case kPS2M_getDisableTouchpad:
        {
            bool* pResult = (bool*)argument;
            *pResult = !ignoreall;
            break;
        }

        case kPS2M_setDisableTouchpad:
        {
            bool enable = *((bool*)argument);
            ignoreall = !enable;
            break;
        }

        case kPS2M_resetTouchpad:
        {
            int *reqCode = (int *)argument;
            DEBUG_LOG("VoodooPS2Elan::kPS2M_resetTouchpad reqCode: %d\n", *reqCode);
            if (*reqCode == 1) {
                setTouchPadEnable(false);
                IOSleep(wakedelay);

                ignoreall = false;
                _packetByteCount = 0;
                _ringBuffer.reset();

                resetMouse();
                elantechSetupPS2();
                setTouchPadEnable(true);
            }
            break;
        }

        case kPS2M_notifyKeyTime:
        {
            // just remember last time key pressed... this can be used in
            // interrupt handler to detect unintended input while typing
            keytime = *((uint64_t*)argument);
            break;
        }
    }

    return kIOReturnSuccess;
}

void ApplePS2Elan::setDevicePowerState(UInt32 whatToDo) {
    switch (whatToDo) {
        case kPS2C_DisableDevice:
            // Disable the touchpad
            setTouchPadEnable(false);
            break;

        case kPS2C_EnableDevice:
            // Must not issue any commands before the device has
            // completed its power-on self-test and calibration
            IOSleep(wakedelay);

            // Clear packet buffer pointer to avoid issues caused by stale packet fragments
            _packetByteCount = 0;
            _ringBuffer.reset();

            // Reset and enable the touchpad
            resetMouse();
            elantechSetupPS2();
            setTouchPadEnable(true);
            break;
    }
}

void ApplePS2Elan::registerHIDPointerNotifications() {
    IOServiceMatchingNotificationHandler notificationHandler = OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this, &ApplePS2Elan::notificationHIDAttachedHandler);

    // Determine if we should listen for USB mouse attach events as per configuration
    if (_processusbmouse) {
        // USB mouse HID description as per USB spec: http://www.usb.org/developers/hidpage/HID1_11.pdf
        OSDictionary *matchingDictionary = serviceMatching("IOUSBInterface");

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
        OSDictionary *matchingDictionary = serviceMatching("IOBluetoothHIDDriver");
        propertyMatching(OSSymbol::withCString(kIOHIDVirtualHIDevice), kOSBooleanFalse, matchingDictionary);

        // Register for future services
        bluetooth_hid_publish_notify = addMatchingNotification(gIOFirstPublishNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        bluetooth_hid_terminate_notify = addMatchingNotification(gIOTerminatedNotification, matchingDictionary, notificationHandler, this, NULL, 10000);
        OSSafeReleaseNULL(matchingDictionary);
    }
}

void ApplePS2Elan::unregisterHIDPointerNotifications() {
    // Free device matching notifiers
    // remove() releases them

    if (usb_hid_publish_notify) {
        usb_hid_publish_notify->remove();
    }

    if (usb_hid_terminate_notify) {
        usb_hid_terminate_notify->remove();
    }

    if (bluetooth_hid_publish_notify) {
        bluetooth_hid_publish_notify->remove();
    }

    if (bluetooth_hid_terminate_notify) {
        bluetooth_hid_terminate_notify->remove();
    }

    attachedHIDPointerDevices->flushCollection();
}

void ApplePS2Elan::notificationHIDAttachedHandlerGated(IOService *newService, IONotifier *notifier) {
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
        OSNumber *propDeviceClass = OSDynamicCast(OSNumber, newService->getProperty("ClassOfDevice"));

        if (propDeviceClass != NULL) {
            UInt32 classOfDevice = propDeviceClass->unsigned32BitValue();

            UInt32 deviceClassMajor = (classOfDevice & 0x1F00) >> 8;
            UInt32 deviceClassMinor = (classOfDevice & 0xFF) >> 2;

            if (deviceClassMajor == kBluetoothDeviceClassMajorPeripheral) { // Bluetooth peripheral devices
                UInt32 deviceClassMinor1 = (deviceClassMinor) & 0x30;
                UInt32 deviceClassMinor2 = (deviceClassMinor) & 0x0F;

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

bool ApplePS2Elan::notificationHIDAttachedHandler(void *refCon, IOService *newService, IONotifier *notifier) {
    if (_cmdGate) {
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Elan::notificationHIDAttachedHandlerGated), newService, notifier);
    }

    return true;
}

// elantech.c port

template<int I>
int ApplePS2Elan::ps2_command(UInt8 *params, unsigned int command) {
    TPS2Request<1 + I> request;
    request.commands[0].command = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut = command;
    for (int i = 0; i < I; i++) {
        request.commands[1 + i].command = kPS2C_ReadDataPort;
    }

    request.commandsCount = 1 + I;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);

    for (int i = 0; i < I; i++) {
        params[i] = request.commands[i + 1].inOrOut;
    }

    return request.commandsCount != 1 + I;
}

/*
 * A retrying version of ps2_command
 */
template<int I>
int ApplePS2Elan::elantech_ps2_command(unsigned char *param, int command) {
    int rc;
    int tries = ETP_PS2_COMMAND_TRIES;

    do {
        rc = ps2_command<I>(param, command);
        if (rc == 0) {
            break;
        }
        tries--;
        DEBUG_LOG("VoodooPS2Elan: retrying ps2 command 0x%02x (%d).\n", command, tries);
        IOSleep(ETP_PS2_COMMAND_DELAY);
    } while (tries > 0);

    if (rc) {
        DEBUG_LOG("VoodooPS2Elan: ps2 command 0x%02x failed.\n", command);
    }

    return rc;
}

/*
 * ps2_sliced_command() sends an extended PS/2 command to the mouse
 * using sliced syntax, understood by advanced devices, such as Logitech
 * or Synaptics touchpads. The command is encoded as:
 * 0xE6 0xE8 rr 0xE8 ss 0xE8 tt 0xE8 uu where (rr*64)+(ss*16)+(tt*4)+uu
 * is the command.
 */
int ApplePS2Elan::ps2_sliced_command(UInt8 command) {
    int j = 0;

    TPS2Request<> request;
    request.commands[j].command = kPS2C_SendCommandAndCompareAck;
    request.commands[j++].inOrOut = kDP_SetMouseScaling1To1;

    for (int i = 6; i >= 0; i -= 2) {
        UInt8 d = (command >> i) & 3;
        request.commands[j].command = kPS2C_SendCommandAndCompareAck;
        request.commands[j++].inOrOut = kDP_SetMouseResolution;

        request.commands[j].command = kPS2C_SendCommandAndCompareAck;
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
int ApplePS2Elan::synaptics_send_cmd(unsigned char c, unsigned char *param) {
    if (ps2_sliced_command(c) || ps2_command<I>(param, kDP_GetMouseInformation)) {
        DEBUG_LOG("VoodooPS2Elan: query 0x%02x failed.\n", c);
        return -1;
    }

    return 0;
}

/*
 * V3 and later support this fast command
 */
template<int I>
int ApplePS2Elan::elantech_send_cmd(unsigned char c, unsigned char *param) {
    if (ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
        ps2_command<0>(NULL, c) ||
        ps2_command<I>(param, kDP_GetMouseInformation)) {
        DEBUG_LOG("VoodooPS2Elan: query 0x%02x failed.\n", c);
        return -1;
    }

    return 0;
}

template<int I>
int ApplePS2Elan::send_cmd(unsigned char c, unsigned char *param) {
    if (info.hw_version >= 3) {
        return elantech_send_cmd<I>(c, param);
    } else {
        return synaptics_send_cmd<I>(c, param);
    }
}

bool ApplePS2Elan::elantech_is_signature_valid(const unsigned char *param) {
    static const unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10 };

    if (param[0] == 0) {
        return false;
    }

    if (param[1] == 0) {
        return true;
    }

    // Some hw_version >= 4 models have a revision higher then 20.
    // Meaning that param[2] may be 10 or 20, skip the rates check for these.
    if ((param[0] & 0x0f) >= 0x06 && (param[1] & 0xaf) == 0x0f && param[2] < 40) {
        return true;
    }

    for (int i = 0; i < sizeof(rates) / sizeof(*rates); i++) {
        if (param[2] == rates[i]) {
            return false;
        }
    }

    return true;
}

/*
 * (value from firmware) * 10 + 790 = dpi
 * we also have to convert dpi to dots/mm (*10/254 to avoid floating point)
 */
unsigned int ApplePS2Elan::elantech_convert_res(unsigned int val) {
    return (val * 10 + 790) * 10 / 254;
}

int ApplePS2Elan::elantech_get_resolution_v4(unsigned int *x_res, unsigned int *y_res, unsigned int *bus) {
    unsigned char param[3];

    if (elantech_send_cmd<3>(ETP_RESOLUTION_QUERY, param)) {
        return -1;
    }

    *x_res = elantech_convert_res(param[1] & 0x0f);
    *y_res = elantech_convert_res((param[1] & 0xf0) >> 4);
    *bus = param[2];

    return 0;
}

/*
 * Use magic knock to detect Elantech touchpad
 */
int ApplePS2Elan::elantechDetect() {
    unsigned char param[3];

    if (ps2_command<0>(NULL, kDP_SetDefaults) ||
        ps2_command<0>(NULL, kDP_SetDefaultsAndDisable) ||
        ps2_command<0>(NULL, kDP_SetMouseScaling1To1) ||
        ps2_command<0>(NULL, kDP_SetMouseScaling1To1) ||
        ps2_command<0>(NULL, kDP_SetMouseScaling1To1) ||
        ps2_command<3>(param, kDP_GetMouseInformation)) {
        DEBUG_LOG("VoodooPS2Elan: sending Elantech magic knock failed.\n");
        return -1;
    }

    // Report this in case there are Elantech models that use a different
    // set of magic numbers
    if (param[0] != 0x3c || param[1] != 0x03 || (param[2] != 0xc8 && param[2] != 0x00)) {
        DEBUG_LOG("VoodooPS2Elan: unexpected magic knock result 0x%02x, 0x%02x, 0x%02x.\n", param[0], param[1], param[2]);
        return -1;
    }

    // Query touchpad's firmware version and see if it reports known
    // value to avoid mis-detection. Logitech mice are known to respond
    // to Elantech magic knock and there might be more.
    if (synaptics_send_cmd<3>(ETP_FW_VERSION_QUERY, param)) {
        DEBUG_LOG("VoodooPS2Elan: failed to query firmware version.\n");
        return -1;
    }

    DEBUG_LOG("VoodooPS2Elan: Elantech version query result 0x%02x, 0x%02x, 0x%02x.\n", param[0], param[1], param[2]);

    if (!elantech_is_signature_valid(param)) {
        DEBUG_LOG("VoodooPS2Elan: Probably not a real Elantech touchpad. Aborting.\n");
        return -1;
    }

    return 0;
}

int ApplePS2Elan::elantechQueryInfo() {
    unsigned char param[3];
    unsigned char traces;

    // Do the version query again so we can store the result
    if (synaptics_send_cmd<3>(ETP_FW_VERSION_QUERY, param)) {
        DEBUG_LOG("VoodooPS2Elan: failed to query firmware version.\n");
        return -1;
    }

    info.fw_version = (param[0] << 16) | (param[1] << 8) | param[2];

    if (elantechSetProperties()) {
        DEBUG_LOG("VoodooPS2Elan: unknown hardware version, aborting...\n");
        return -1;
    }

    DEBUG_LOG("VoodooPS2Elan assuming hardware version %d (with firmware version 0x%02x%02x%02x)\n",
           info.hw_version, param[0], param[1], param[2]);

    if (send_cmd<3>(ETP_CAPABILITIES_QUERY, info.capabilities)) {
        DEBUG_LOG("VoodooPS2Elan: failed to query capabilities.\n");
        return -1;
    }

    DEBUG_LOG("VoodooPS2Elan: Elan capabilities query result 0x%02x, 0x%02x, 0x%02x.\n",
           info.capabilities[0], info.capabilities[1],
           info.capabilities[2]);

    if (info.hw_version != 1) {
        if (send_cmd<3>(ETP_SAMPLE_QUERY, info.samples)) {
            DEBUG_LOG("VoodooPS2Elan: failed to query sample data\n");
            return -1;
        }
        DEBUG_LOG("VoodooPS2Elan: Elan sample query result %02x, %02x, %02x\n",
                      info.samples[0],
                      info.samples[1],
                      info.samples[2]);
    }

    if (info.samples[1] == 0x74 && info.hw_version == 0x03) {
        // This module has a bug which makes absolute mode unusable,
        // so let's abort so we'll be using standard PS/2 protocol.
        DEBUG_LOG("VoodooPS2Elan: absolute mode broken, forcing standard PS/2 protocol\n");
        return -1;
    }

    // The MSB indicates the presence of the trackpoint
    info.has_trackpoint = (info.capabilities[0] & 0x80) == 0x80;

    info.x_res = 31;
    info.y_res = 31;
    if (info.hw_version == 4) {
        if (elantech_get_resolution_v4(&info.x_res, &info.y_res, &info.bus)) {
            DEBUG_LOG("VoodooPS2Elan: failed to query resolution data.\n");
        }
    }

    // query range information
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
                if (send_cmd<3>(ETP_FW_ID_QUERY, param)) {
                    return -1;
                }

                int i = (info.fw_version > 0x020800 && info.fw_version < 0x020900) ? 1 : 2;
                int fixed_dpi = param[1] & 0x10;

                if (((info.fw_version >> 16) == 0x14) && fixed_dpi) {
                    if (send_cmd<3>(ETP_SAMPLE_QUERY, param)) {
                        return -1;
                    }

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
            if (send_cmd<3>(ETP_FW_ID_QUERY, param)) {
                return -1;
            }

            info.x_max = (0x0f & param[0]) << 8 | param[1];
            info.y_max = (0xf0 & param[0]) << 4 | param[2];
            break;

        case 4:
            if (send_cmd<3>(ETP_FW_ID_QUERY, param)) {
                return -1;
            }

            info.x_max = (0x0f & param[0]) << 8 | param[1];
            info.y_max = (0xf0 & param[0]) << 4 | param[2];
            traces = info.capabilities[1];
            if ((traces < 2) || (traces > info.x_max)) {
                return -1;
            }

            info.width = info.x_max / (traces - 1);

            // column number of traces
            info.x_traces = traces;

            // row number of traces
            traces = info.capabilities[2];
            if ((traces >= 2) && (traces <= info.y_max)) {
                info.y_traces = traces;
            }

            break;
    }

    // check if device has buttonpad
    info.is_buttonpad = (info.fw_version & 0x001000) != 0;

    // check for the middle button
    info.has_middle_button = ETP_NEW_IC_SMBUS_HOST_NOTIFY(info.fw_version) && !info.is_buttonpad;

    return 0;
}

/*
 * determine hardware version and set some properties according to it.
 */
int ApplePS2Elan::elantechSetProperties() {
    // This represents the version of IC body
    int ver = (info.fw_version & 0x0f0000) >> 16;

    // Early version of Elan touchpads doesn't obey the rule
    if (info.fw_version < 0x020030 || info.fw_version == 0x020600) {
        info.hw_version = 1;
    } else {
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

    // Turn on packet checking by default
    info.paritycheck = 1;

    // This firmware suffers from misreporting coordinates when
    // a touch action starts causing the mouse cursor or scrolled page
    // to jump. Enable a workaround.
    info.jumpy_cursor = (info.fw_version == 0x020022 || info.fw_version == 0x020600);

    if (info.hw_version > 1) {
        // For now show extra debug information
        info.debug = 1;

        if (info.fw_version >= 0x020800) {
            info.reports_pressure = true;
        }
    }

    // The signatures of v3 and v4 packets change depending on the
    // value of this hardware flag.
    info.crc_enabled = (info.fw_version & 0x4000) == 0x4000;

    // Enable real hardware resolution on hw_version 3 ?
    info.set_hw_resolution = _set_hw_resolution;

    // Set packet length (4 for v1, 6 for v2 and newer)
    _packetLength = (info.hw_version == 1) ? 4 : 6;

    return 0;
}

/*
 * Set the appropriate event bits for the input subsystem
 */
int ApplePS2Elan::elantechSetInputParams() {
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, info.x_max - info.x_min, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, info.y_max - info.y_min, 32);

    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, (info.x_max - info.x_min + 1) * 100 / info.x_res, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, (info.y_max - info.y_min + 1) * 100 / info.y_res, 32);

    setProperty(VOODOO_INPUT_TRANSFORM_KEY, 0ull, 32);
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    registerService();

    return 0;
}

/*
 * Put the touchpad into absolute mode
 */
int ApplePS2Elan::elantechSetAbsoluteMode() {
    unsigned char val;
    int tries = ETP_READ_BACK_TRIES;
    int rc = 0;

    switch (info.hw_version) {
        case 1:
            etd.reg_10 = 0x16;
            etd.reg_11 = 0x8f;
            if (elantechWriteReg(0x10, etd.reg_10) ||
                elantechWriteReg(0x11, etd.reg_11)) {
                rc = -1;
            }
            break;

        case 2:
            // Windows driver values
            etd.reg_10 = 0x54;
            etd.reg_11 = 0x88;    // 0x8a
            etd.reg_21 = 0x60;    // 0x00
            if (elantechWriteReg(0x10, etd.reg_10) ||
                elantechWriteReg(0x11, etd.reg_11) ||
                elantechWriteReg(0x21, etd.reg_21)) {
                rc = -1;
            }
            break;

        case 3:
            if (info.set_hw_resolution) {
                etd.reg_10 = 0x0b;
            } else {
                etd.reg_10 = 0x01;
            }

            if (elantechWriteReg(0x10, etd.reg_10)) {
                rc = -1;
            }

            break;

        case 4:
            etd.reg_07 = 0x01;
            if (elantechWriteReg(0x07, etd.reg_07)) {
                rc = -1;
            }

            goto skip_readback_reg_10; // v4 has no reg 0x10 to read
    }

    if (rc == 0) {
        // Read back reg 0x10. For hardware version 1 we must make
        // sure the absolute mode bit is set. For hardware version 2
        // the touchpad is probably initializing and not ready until
        // we read back the value we just wrote.
        do {
            rc = elantechReadReg(0x10, &val);
            if (rc == 0) {
                break;
            }
            tries--;
            DEBUG_LOG("VoodooPS2Elan: retrying read (%d).\n", tries);
            IOSleep(ETP_READ_BACK_DELAY);
        } while (tries > 0);

        if (rc) {
            DEBUG_LOG("VoodooPS2Elan: failed to read back register 0x10.\n");
        } else if (info.hw_version == 1 && !(val & ETP_R10_ABSOLUTE_MODE)) {
            DEBUG_LOG("VoodooPS2Elan: touchpad refuses to switch to absolute mode.\n");
            rc = -1;
        }
    }

skip_readback_reg_10:
    if (rc) {
        DEBUG_LOG("VoodooPS2Elan: failed to initialise registers.\n");
    }

    return rc;
}

/*
 * Initialize the touchpad
 */
int ApplePS2Elan::elantechSetupPS2() {
    etd.parity[0] = 1;
    for (int i = 1; i < 256; i++)
        etd.parity[i] = etd.parity[i & (i - 1)] ^ 1;

    if (elantechSetAbsoluteMode()) {
        DEBUG_LOG("VoodooPS2: failed to put touchpad into absolute mode.\n");
        return -1;
    }

    /*
    if (info.fw_version == 0x381f17) {
        etd.original_set_rate = psmouse->set_rate;
        psmouse->set_rate = elantech_set_rate_restore_reg_07;
    }
     */

    if (elantechSetInputParams()) {
        DEBUG_LOG("VoodooPS2: failed to query touchpad range.\n");
        return -1;
    }

    // set resolution and dpi
    TPS2Request<> request;
    request.commands[0].command = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetDefaultsAndDisable;           // 0xF5, Disable data reporting
    request.commands[1].command = kPS2C_SendCommandAndCompareAck;
    request.commands[1].inOrOut = kDP_SetMouseSampleRate;              // 0xF3
    request.commands[2].command = kPS2C_SendCommandAndCompareAck;
    request.commands[2].inOrOut = _mouseSampleRate;                    // 200 dpi
    request.commands[3].command = kPS2C_SendCommandAndCompareAck;
    request.commands[3].inOrOut = kDP_SetMouseResolution;              // 0xE8
    request.commands[4].command = kPS2C_SendCommandAndCompareAck;
    request.commands[4].inOrOut = _mouseResolution;                    // 0x03 = 8 counts/mm
    request.commands[5].command = kPS2C_SendCommandAndCompareAck;
    request.commands[5].inOrOut = kDP_SetMouseScaling1To1;             // 0xE6
    request.commands[6].command = kPS2C_SendCommandAndCompareAck;
    request.commands[6].inOrOut = kDP_Enable;                          // 0xF4, Enable Data Reporting
    request.commandsCount = 7;
    _device->submitRequestAndBlock(&request);

    return 0;
}

/*
 * Send an Elantech style special command to read a value from a register
 */
int ApplePS2Elan::elantechReadReg(unsigned char reg, unsigned char *val) {
    unsigned char param[3] = {0, 0, 0};
    int rc = 0;

    if (reg < 0x07 || reg > 0x26) {
        return -1;
    }

    if (reg > 0x11 && reg < 0x20) {
        return -1;
    }

    switch (info.hw_version) {
        case 1:
            if (ps2_sliced_command(ETP_REGISTER_READ) ||
                ps2_sliced_command(reg) ||
                ps2_command<3>(param, kDP_GetMouseInformation)) {
                rc = -1;
            }
            break;

        case 2:
            if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantech_ps2_command<0>(NULL, ETP_REGISTER_READ) ||
                elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantech_ps2_command<0>(NULL, reg) ||
                elantech_ps2_command<3>(param, kDP_GetMouseInformation)) {
                rc = -1;
            }
            break;

        case 3 ... 4:
            if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
                elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantech_ps2_command<0>(NULL, reg) ||
                elantech_ps2_command<3>(param, kDP_GetMouseInformation)) {
                rc = -1;
            }
            break;
    }

    if (rc) {
        DEBUG_LOG("VoodooPS2Elan: failed to read register 0x%02x.\n", reg);
    } else if (info.hw_version != 4) {
        *val = param[0];
    } else {
        *val = param[1];
    }

    return rc;
}

/*
 * Send an Elantech style special command to write a register with a value
 */
int ApplePS2Elan::elantechWriteReg(unsigned char reg, unsigned char val) {
    int rc = 0;

    if (reg < 0x07 || reg > 0x26) {
        return -1;
    }

    if (reg > 0x11 && reg < 0x20) {
        return -1;
    }

    switch (info.hw_version) {
        case 1:
            if (ps2_sliced_command(ETP_REGISTER_WRITE) ||
                ps2_sliced_command(reg) ||
                ps2_sliced_command(val) ||
                ps2_command<0>(NULL, kDP_SetMouseScaling1To1)) {
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
                elantech_ps2_command<0>(NULL, kDP_SetMouseScaling1To1)) {
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
                elantech_ps2_command<0>(NULL, kDP_SetMouseScaling1To1)) {
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
                elantech_ps2_command<0>(NULL, kDP_SetMouseScaling1To1)) {
                rc = -1;
            }
            break;
    }

    if (rc) {
        DEBUG_LOG("VoodooPS2Elan: failed to write register 0x%02x with value 0x%02x.\n", reg, val);
    }

    return rc;
}

int ApplePS2Elan::elantechDebounceCheckV2() {
    // When we encounter packet that matches this exactly, it means the
    // hardware is in debounce status. Just ignore the whole packet.
    static const uint8_t debounce_packet[] = {
        0x84, 0xff, 0xff, 0x02, 0xff, 0xff
    };

    unsigned char *packet = _ringBuffer.tail();

    return !memcmp(packet, debounce_packet, sizeof(debounce_packet));
}

int ApplePS2Elan::elantechPacketCheckV1() {
    unsigned char *packet = _ringBuffer.tail();
    unsigned char p1, p2, p3;

    // Parity bits are placed differently
    if (info.fw_version < 0x020000) {
        // byte 0:  D   U  p1  p2   1  p3   R   L
        p1 = (packet[0] & 0x20) >> 5;
        p2 = (packet[0] & 0x10) >> 4;
    } else {
        // byte 0: n1  n0  p2  p1   1  p3   R   L
        p1 = (packet[0] & 0x10) >> 4;
        p2 = (packet[0] & 0x20) >> 5;
    }

    p3 = (packet[0] & 0x04) >> 2;

    return etd.parity[packet[1]] == p1 &&
           etd.parity[packet[2]] == p2 &&
           etd.parity[packet[3]] == p3;
}

int ApplePS2Elan::elantechPacketCheckV2() {
    unsigned char *packet = _ringBuffer.tail();

    // V2 hardware has two flavors. Older ones that do not report pressure,
    // and newer ones that reports pressure and width. With newer ones, all
    // packets (1, 2, 3 finger touch) have the same constant bits. With
    // older ones, 1/3 finger touch packets and 2 finger touch packets
    // have different constant bits.
    // With all three cases, if the constant bits are not exactly what I
    // expected, I consider them invalid.

    if (info.reports_pressure) {
        return (packet[0] & 0x0c) == 0x04 && (packet[3] & 0x0f) == 0x02;
    }

    if ((packet[0] & 0xc0) == 0x80) {
        return (packet[0] & 0x0c) == 0x0c && (packet[3] & 0x0e) == 0x08;
    }

    return (packet[0] & 0x3c) == 0x3c &&
           (packet[1] & 0xf0) == 0x00 &&
           (packet[3] & 0x3e) == 0x38 &&
           (packet[4] & 0xf0) == 0x00;
}

int ApplePS2Elan::elantechPacketCheckV3() {
    static const uint8_t debounce_packet[] = {
        0xc4, 0xff, 0xff, 0x02, 0xff, 0xff
    };

    unsigned char *packet = _ringBuffer.tail();

    // check debounce first, it has the same signature in byte 0
    // and byte 3 as PACKET_V3_HEAD.
    if (!memcmp(packet, debounce_packet, sizeof(debounce_packet))) {
        return PACKET_DEBOUNCE;
    }

    // If the hardware flag 'crc_enabled' is set the packets have different signatures.
    if (info.crc_enabled) {
        if ((packet[3] & 0x09) == 0x08) {
            return PACKET_V3_HEAD;
        }

        if ((packet[3] & 0x09) == 0x09) {
            return PACKET_V3_TAIL;
        }
    } else {
        if ((packet[0] & 0x0c) == 0x04 && (packet[3] & 0xcf) == 0x02) {
            return PACKET_V3_HEAD;
        }

        if ((packet[0] & 0x0c) == 0x0c && (packet[3] & 0xce) == 0x0c) {
            return PACKET_V3_TAIL;
        }

        if ((packet[3] & 0x0f) == 0x06) {
            return PACKET_TRACKPOINT;
        }
    }

    return PACKET_UNKNOWN;
}

int ApplePS2Elan::elantechPacketCheckV4() {
    unsigned char *packet = _ringBuffer.tail();
    unsigned char packet_type = packet[3] & 0x03;
    unsigned int ic_version;
    bool sanity_check;

    INTERRUPT_LOG("VoodooPS2Elan: Packet dump (%04x, %04x, %04x, %04x, %04x, %04x)\n", packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);

    if (info.has_trackpoint && (packet[3] & 0x0f) == 0x06) {
        return PACKET_TRACKPOINT;
    }

    // This represents the version of IC body.
    ic_version = (info.fw_version & 0x0f0000) >> 16;

    INTERRUPT_LOG("VoodooPS2Elan: icVersion(%d), crc(%d), samples[1](%d) \n", ic_version, info.crc_enabled, info.samples[1]);

    // Sanity check based on the constant bits of a packet.
    // The constant bits change depending on the value of
    // the hardware flag 'crc_enabled' and the version of
    // the IC body, but are the same for every packet,
    // regardless of the type.
    if (info.crc_enabled) {
        sanity_check = ((packet[3] & 0x08) == 0x00);
    } else if (ic_version == 7 && info.samples[1] == 0x2A) {
        sanity_check = ((packet[3] & 0x1c) == 0x10);
    } else {
        sanity_check = ((packet[0] & 0x08) == 0x00 && (packet[3] & 0x1c) == 0x10);
    }

    if (!sanity_check) {
        return PACKET_UNKNOWN;
    }

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

void ApplePS2Elan::elantechRescale(unsigned int x, unsigned int y) {
    bool needs_update = false;

    if (x > info.x_max) {
        info.x_max = x;
        needs_update = true;
    }

    if (y > info.y_max) {
        info.y_max = y;
        needs_update = true;
    }

    if (needs_update) {
        setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, info.x_max - info.x_min, 32);
        setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, info.y_max - info.y_min, 32);

        setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, (info.x_max - info.x_min + 1) * 100 / info.x_res, 32);
        setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, (info.y_max - info.y_min + 1) * 100 / info.y_res, 32);

        if (voodooInputInstance) {
            VoodooInputDimensions dims = {
                static_cast<SInt32>(info.x_min), static_cast<SInt32>(info.x_max),
                static_cast<SInt32>(info.y_min), static_cast<SInt32>(info.y_max)
            };

            super::messageClient(kIOMessageVoodooInputUpdateDimensionsMessage, voodooInputInstance, &dims, sizeof(dims));
        }
    }
}

void ApplePS2Elan::elantechReportAbsoluteV1() {
    unsigned char *packet = _ringBuffer.tail();
    unsigned int fingers = 0, x = 0, y = 0;

    if (info.fw_version < 0x020000) {
        // byte 0:  D   U  p1  p2   1  p3   R   L
        // byte 1:  f   0  th  tw  x9  x8  y9  y8
        fingers = ((packet[1] & 0x80) >> 7) + ((packet[1] & 0x30) >> 4);
    } else {
        // byte 0: n1  n0  p2  p1   1  p3   R   L
        // byte 1:  0   0   0   0  x9  x8  y9  y8
        fingers = (packet[0] & 0xc0) >> 6;
    }

    if (info.jumpy_cursor) {
        if (fingers != 1) {
            etd.single_finger_reports = 0;
        } else if (etd.single_finger_reports < 2) {
            // Discard first 2 reports of one finger, bogus
            etd.single_finger_reports++;
            INTERRUPT_LOG("VoodooPS2Elan: discarding packet\n");
            return;
        }
    }

    // byte 2: x7  x6  x5  x4  x3  x2  x1  x0
    // byte 3: y7  y6  y5  y4  y3  y2  y1  y0
    x = ((packet[1] & 0x0c) << 6) | packet[2];
    y = info.y_max - (((packet[1] & 0x03) << 8) | packet[3]);

    virtualFinger[0].touch = false;
    virtualFinger[1].touch = false;
    virtualFinger[2].touch = false;

    leftButton = packet[0] & 0x01;
    rightButton = packet[0] & 0x02;

    if (fingers == 1) {
        virtualFinger[0].touch = true;
        virtualFinger[0].button = packet[0] & 0x03;
        virtualFinger[0].prev = virtualFinger[0].now;
        virtualFinger[0].now.x = x;
        virtualFinger[0].now.y = y;
        if (lastFingers != 1) {
            virtualFinger[0].prev = virtualFinger[0].now;
        }
    }

    if (fingers == 2) {
        virtualFinger[0].touch = virtualFinger[1].touch = true;
        virtualFinger[0].button = virtualFinger[1].button = packet[0] & 0x03;
        virtualFinger[0].prev = virtualFinger[0].now;
        virtualFinger[1].prev = virtualFinger[1].now;

        int h = 100;
        int dy = (int)(sin30deg * h);
        int dx = (int)(cos30deg * h);

        virtualFinger[0].now.x = x;
        virtualFinger[0].now.y = y - h;

        virtualFinger[1].now.x = x + dx;
        virtualFinger[1].now.y = y + dy;

        if (lastFingers != 2) {
            virtualFinger[0].prev = virtualFinger[0].now;
            virtualFinger[1].prev = virtualFinger[1].now;
        }
    }

    if (fingers == 3) {
        virtualFinger[0].touch = virtualFinger[1].touch = virtualFinger[2].touch = true;
        virtualFinger[0].button = virtualFinger[1].button = virtualFinger[2].button = packet[0] & 0x03;
        virtualFinger[0].prev = virtualFinger[0].now;
        virtualFinger[1].prev = virtualFinger[1].now;
        virtualFinger[2].prev = virtualFinger[2].now;

        int h = 100;
        int dy = (int)(sin30deg * h);
        int dx = (int)(cos30deg * h);

        virtualFinger[0].now.x = x;
        virtualFinger[0].now.y = y - h;

        virtualFinger[1].now.x = x - dx;
        virtualFinger[1].now.y = y + dy;

        virtualFinger[2].now.x = x + dx;
        virtualFinger[2].now.y = y + dy;

        if (lastFingers != 3) {
            virtualFinger[0].prev = virtualFinger[0].now;
            virtualFinger[1].prev = virtualFinger[1].now;
            virtualFinger[2].prev = virtualFinger[2].now;
        }
    }

    lastFingers = fingers;
    sendTouchData();
}

void ApplePS2Elan::elantechReportAbsoluteV2() {
    unsigned char *packet = _ringBuffer.tail();
    unsigned int fingers = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    // byte 0: n1  n0   .   .   .   .   R   L
    fingers = (packet[0] & 0xc0) >> 6;

    switch (fingers) {
        case 3:
        case 1:
            // byte 1:  .   .   .   .  x11 x10 x9  x8
            // byte 2: x7  x6  x5  x4  x4  x2  x1  x0
            x1 = ((packet[1] & 0x0f) << 8) | packet[2];

            // byte 4:  .   .   .   .  y11 y10 y9  y8
            // byte 5: y7  y6  y5  y4  y3  y2  y1  y0
            y1 = info.y_max - (((packet[4] & 0x0f) << 8) | packet[5]);

            // pressure: (packet[1] & 0xf0) | ((packet[4] & 0xf0) >> 4);
            // finger width: ((packet[0] & 0x30) >> 2) | ((packet[3] & 0x30) >> 4);
            break;

        case 2:
            // The coordinate of each finger is reported separately
            // with a lower resolution for two finger touches:

            // byte 0:  .   .  ay8 ax8  .   .   .   .
            // byte 1: ax7 ax6 ax5 ax4 ax3 ax2 ax1 ax0
            x1 = (((packet[0] & 0x10) << 4) | packet[1]) << 2;

            // byte 2: ay7 ay6 ay5 ay4 ay3 ay2 ay1 ay0
            y1 = info.y_max - ((((packet[0] & 0x20) << 3) | packet[2]) << 2);

            // byte 3:  .   .  by8 bx8  .   .   .   .
            // byte 4: bx7 bx6 bx5 bx4 bx3 bx2 bx1 bx0
            x2 = (((packet[3] & 0x10) << 4) | packet[4]) << 2;

            // byte 5: by7 by8 by5 by4 by3 by2 by1 by0
            y2 = info.y_max - ((((packet[3] & 0x20) << 3) | packet[5]) << 2);
            break;
    }

    virtualFinger[0].touch = false;
    virtualFinger[1].touch = false;
    virtualFinger[2].touch = false;

    leftButton = packet[0] & 0x01;
    rightButton = packet[0] & 0x02;

    if (fingers == 1 || fingers == 2) {
        virtualFinger[0].touch = true;
        virtualFinger[0].button = packet[0] & 0x03;
        virtualFinger[0].prev = virtualFinger[0].now;
        virtualFinger[0].now.x = x1;
        virtualFinger[0].now.y = y1;
        if (lastFingers != 1 && lastFingers != 2) {
            virtualFinger[0].prev = virtualFinger[0].now;
        }
    }

    if (fingers == 2) {
        virtualFinger[1].touch = true;
        virtualFinger[1].button = packet[0] & 0x03;
        virtualFinger[1].prev = virtualFinger[1].now;
        virtualFinger[1].now.x = x2;
        virtualFinger[1].now.y = y2;
        if (lastFingers != 2) {
            virtualFinger[1].prev = virtualFinger[1].now;
        }
    }

    if (fingers == 3) {
        virtualFinger[0].touch = virtualFinger[1].touch = virtualFinger[2].touch = true;
        virtualFinger[0].button = virtualFinger[1].button = virtualFinger[2].button = packet[0] & 0x03;
        virtualFinger[0].prev = virtualFinger[0].now;
        virtualFinger[1].prev = virtualFinger[1].now;
        virtualFinger[2].prev = virtualFinger[2].now;

        int h = 100;
        int dy = (int)(sin30deg * h);
        int dx = (int)(cos30deg * h);

        virtualFinger[0].now.x = x1;
        virtualFinger[0].now.y = y1 - h;

        virtualFinger[1].now.x = x1 - dx;
        virtualFinger[1].now.y = y1 + dy;

        virtualFinger[2].now.x = x1 + dx;
        virtualFinger[2].now.y = y1 + dy;

        if (lastFingers != 3) {
            virtualFinger[0].prev = virtualFinger[0].now;
            virtualFinger[1].prev = virtualFinger[1].now;
            virtualFinger[2].prev = virtualFinger[2].now;
        }
    }

    lastFingers = fingers;
    sendTouchData();
}

void ApplePS2Elan::elantechReportAbsoluteV3(int packetType) {
    unsigned char *packet = _ringBuffer.tail();
    unsigned int fingers = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    
    // byte 0: n1  n0   .   .   .   .   R   L
    fingers = (packet[0] & 0xc0) >> 6;

    INTERRUPT_LOG("report abs v3 type %d finger %u x %d y %d btn %d (%02x %02x %02x %02x %02x %02x)\n", packetType, fingers,
                  ((packet[1] & 0x0f) << 8) | packet[2],
                  (((packet[4] & 0x0f) << 8) | packet[5]),
                  packet[0] & 0x03, packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);

    switch (fingers) {
        case 3:
        case 1:
            // byte 1:  .   .   .   .  x11 x10 x9  x8
            // byte 2: x7  x6  x5  x4  x4  x2  x1  x0
            x1 = ((packet[1] & 0x0f) << 8) | packet[2];

            // byte 4:  .   .   .   .  y11 y10 y9  y8
            // byte 5: y7  y6  y5  y4  y3  y2  y1  y0
            y1 = (((packet[4] & 0x0f) << 8) | packet[5]);
            elantechRescale(x1, y1);
            y1 = info.y_max - y1;
            break;

        case 2:
            if (packetType == PACKET_V3_HEAD) {
                // byte 1:   .    .    .    .  ax11 ax10 ax9  ax8
                // byte 2: ax7  ax6  ax5  ax4  ax3  ax2  ax1  ax0
                etd.mt[0].x = ((packet[1] & 0x0f) << 8) | packet[2];

                // byte 4:   .    .    .    .  ay11 ay10 ay9  ay8
                // byte 5: ay7  ay6  ay5  ay4  ay3  ay2  ay1  ay0
                etd.mt[0].y = info.y_max - (((packet[4] & 0x0f) << 8) | packet[5]);

                // wait for next packet
                return;
            }

            // packet_type == PACKET_V3_TAIL
            x1 = etd.mt[0].x;
            y1 = etd.mt[0].y;
            x2 = ((packet[1] & 0x0f) << 8) | packet[2];
            y2 = (((packet[4] & 0x0f) << 8) | packet[5]);
            elantechRescale(x2, y2);
            y2 = info.y_max - y2;
            break;
    }

    // pressure: (packet[1] & 0xf0) | ((packet[4] & 0xf0) >> 4);
    // finger width: ((packet[0] & 0x30) >> 2) | ((packet[3] & 0x30) >> 4);

    virtualFinger[0].touch = false;
    virtualFinger[1].touch = false;
    virtualFinger[2].touch = false;

    leftButton = packet[0] & 0x01;
    rightButton = packet[0] & 0x02;

    if (fingers == 1 || fingers == 2) {
        virtualFinger[0].touch = true;
        virtualFinger[0].button = packet[0] & 0x03;
        virtualFinger[0].prev = virtualFinger[0].now;
        virtualFinger[0].now.x = x1;
        virtualFinger[0].now.y = y1;
        if (lastFingers != 1 && lastFingers != 2) {
            virtualFinger[0].prev = virtualFinger[0].now;
        }
    }

    if (fingers == 2) {
        virtualFinger[1].touch = true;
        virtualFinger[1].button = packet[0] & 0x03;
        virtualFinger[1].prev = virtualFinger[1].now;
        virtualFinger[1].now.x = x2;
        virtualFinger[1].now.y = y2;
        if (lastFingers != 2) {
            virtualFinger[1].prev = virtualFinger[1].now;
        }
    }

    if (fingers == 3) {
        virtualFinger[0].touch = virtualFinger[1].touch = virtualFinger[2].touch = true;
        virtualFinger[0].button = virtualFinger[1].button = virtualFinger[2].button = packet[0] & 0x03;
        virtualFinger[0].prev = virtualFinger[0].now;
        virtualFinger[1].prev = virtualFinger[1].now;
        virtualFinger[2].prev = virtualFinger[2].now;

        int h = 100;
        int dy = (int)(sin30deg * h);
        int dx = (int)(cos30deg * h);

        virtualFinger[0].now.x = x1;
        virtualFinger[0].now.y = y1 - h;

        virtualFinger[1].now.x = x1 - dx;
        virtualFinger[1].now.y = y1 + dy;

        virtualFinger[2].now.x = x1 + dx;
        virtualFinger[2].now.y = y1 + dy;

        if (lastFingers != 3) {
            virtualFinger[0].prev = virtualFinger[0].now;
            virtualFinger[1].prev = virtualFinger[1].now;
            virtualFinger[2].prev = virtualFinger[2].now;
        }
    }

    lastFingers = fingers;
    sendTouchData();
}

void ApplePS2Elan::elantechReportAbsoluteV4(int packetType) {
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
            // impossible to get here
            break;
    }
}

void ApplePS2Elan::elantechReportTrackpoint() {
    // byte 0:   0   0  sx  sy   0   M   R   L
    // byte 1: ~sx   0   0   0   0   0   0   0
    // byte 2: ~sy   0   0   0   0   0   0   0
    // byte 3:   0   0 ~sy ~sx   0   1   1   0
    // byte 4:  x7  x6  x5  x4  x3  x2  x1  x0
    // byte 5:  y7  y6  y5  y4  y3  y2  y1  y0
    //
    // x and y are written in two's complement spread
    // over 9 bits with sx/sy the relative top bit and
    // x7..x0 and y7..y0 the lower bits.
    // ~sx is the inverse of sx, ~sy is the inverse of sy.
    // The sign of y is opposite to what the input driver
    // expects for a relative movement

    UInt32 *t = (UInt32 *)_ringBuffer.tail();
    UInt32 signature = *t & ~7U;
    if (signature != 0x06000030U &&
        signature != 0x16008020U &&
        signature != 0x26800010U &&
        signature != 0x36808000U) {
        INTERRUPT_LOG("VoodooPS2Elan: unexpected trackpoint packet skipped\n");
        return;
    }

    unsigned char *packet = _ringBuffer.tail();

    int trackpointLeftButton = packet[0] & 0x1;
    int trackpointRightButton = packet[0] & 0x2;
    int trackpointMiddleButton = packet[0] & 0x4;

    int dx = packet[4] - (int)((packet[1] ^ 0x80) << 1);
    int dy = (int)((packet[2] ^ 0x80) << 1) - packet[5];

    dx = dx * _trackpointMultiplierX / _trackpointDividerX;
    dy = dy * _trackpointMultiplierY / _trackpointDividerY;

    // enable trackpoint scroll mode when middle button was pressed and the trackpoint moved
    if (trackpointMiddleButton == 4 && (dx != 0 || dy != 0)) {
        trackpointScrolling = true;
    }

    // disable trackpoint scrolling mode when middle button is released
    if (trackpointScrolling && trackpointMiddleButton == 0) {
        trackpointScrolling = false;
    }

    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);

    // remember last time trackpoint was used. this can be used in
    // interrupt handler to detect unintended input
    uint64_t timestamp_ns;
    absolutetime_to_nanoseconds(timestamp, &timestamp_ns);
    keytime = timestamp_ns;

    if (trackpointScrolling) {
        dispatchScrollWheelEvent(dx, dy, 0, timestamp);
    } else {
        dispatchRelativePointerEvent(dx, dy, trackpointRightButton | trackpointLeftButton | trackpointMiddleButton, timestamp);
    }
}

void ApplePS2Elan::processPacketStatusV4() {
    unsigned char *packet = _ringBuffer.tail();
    unsigned fingers;
    leftButton = packet[0] & 0x1;
    rightButton = packet[0] & 0x2;

    // notify finger state change
    fingers = packet[1] & 0x1f;
    int count = 0;
    for (int i = 0; i < ETP_MAX_FINGERS; i++) {
        if ((fingers & (1 << i)) == 0) {
            // finger has been lifted off the touchpad
            INTERRUPT_LOG("VoodooPS2Elan: %d finger has been lifted off the touchpad\n", i);
            virtualFinger[i].touch = false;
        } else {
            virtualFinger[i].touch = true;
            INTERRUPT_LOG("VoodooPS2Elan: %d finger has been touched the touchpad\n", i);
            count++;
        }
    }

    heldFingers = count;

    headPacketsCount = 0;

    // if count > 0, we wait for HEAD packets to report so that we report all fingers at once.
    // if count == 0, we have to report the fact fingers are taken off, because there won't be any HEAD packets
    if (count == 0) {
        sendTouchData();
    }
}

void ApplePS2Elan::processPacketHeadV4() {
    unsigned char *packet = _ringBuffer.tail();

    leftButton = packet[0] & 0x1;
    rightButton = packet[0] & 0x2;

    int id = ((packet[3] & 0xe0) >> 5) - 1;
    int pres, traces;

    headPacketsCount++;

    if (id < 0) {
        INTERRUPT_LOG("VoodooPS2Elan: invalid id, aborting\n");
        return;
    }

    int x = ((packet[1] & 0x0f) << 8) | packet[2];
    int y = info.y_max - (((packet[4] & 0x0f) << 8) | packet[5]);

    pres = (packet[1] & 0xf0) | ((packet[4] & 0xf0) >> 4);
    traces = (packet[0] & 0xf0) >> 4;

    INTERRUPT_LOG("VoodooPS2Elan: pres: %d, traces: %d, width: %d\n", pres, traces, etd.width);

    virtualFinger[id].button = (packet[0] & 0x3);
    virtualFinger[id].prev = virtualFinger[id].now;
    virtualFinger[id].pressure = pres;
    virtualFinger[id].width = traces;

    virtualFinger[id].now.x = x;
    virtualFinger[id].now.y = y;

    if (headPacketsCount == heldFingers) {
        headPacketsCount = 0;
        sendTouchData();
    }
}

void ApplePS2Elan::processPacketMotionV4() {
    unsigned char *packet = _ringBuffer.tail();
    int weight, delta_x1 = 0, delta_y1 = 0, delta_x2 = 0, delta_y2 = 0;
    int id, sid;

    leftButton = packet[0] & 0x1;
    rightButton = packet[0] & 0x2;

    id = ((packet[0] & 0xe0) >> 5) - 1;
    if (id < 0) {
        INTERRUPT_LOG("VoodooPS2Elan: invalid id, aborting\n");
        return;
    }

    sid = ((packet[3] & 0xe0) >> 5) - 1;
    weight = (packet[0] & 0x10) ? ETP_WEIGHT_VALUE : 1;

    // Motion packets give us the delta of x, y values of specific fingers,
    // but in two's complement. Let the compiler do the conversion for us.
    // Also _enlarge_ the numbers to int, in case of overflow.
    delta_x1 = (signed char)packet[1];
    delta_y1 = (signed char)packet[2];
    delta_x2 = (signed char)packet[4];
    delta_y2 = (signed char)packet[5];

    virtualFinger[id].button = (packet[0] & 0x3);
    virtualFinger[id].prev = virtualFinger[id].now;
    virtualFinger[id].now.x += delta_x1 * weight;
    virtualFinger[id].now.y -= delta_y1 * weight;

    if (sid >= 0) {
        virtualFinger[sid].button = (packet[0] & 0x3);
        virtualFinger[sid].prev = virtualFinger[sid].now;
        virtualFinger[sid].now.x += delta_x2 * weight;
        virtualFinger[sid].now.y -= delta_y2 * weight;
    }

    sendTouchData();
}

MT2FingerType ApplePS2Elan::GetBestFingerType(int i) {
    switch (i) {
        case 0: return kMT2FingerTypeIndexFinger;
        case 1: return kMT2FingerTypeMiddleFinger;
        case 2: return kMT2FingerTypeRingFinger;
        case 3: return kMT2FingerTypeThumb;
        case 4: return kMT2FingerTypeLittleFinger;

        default:
            break;
    }
    return kMT2FingerTypeIndexFinger;
}

void ApplePS2Elan::sendTouchData() {
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    uint64_t timestamp_ns;
    absolutetime_to_nanoseconds(timestamp, &timestamp_ns);

    // Ignore input for specified time after keyboard/trackpoint usage
    if (timestamp_ns - keytime < maxaftertyping) {
        return;
    }

    static_assert(VOODOO_INPUT_MAX_TRANSDUCERS >= ETP_MAX_FINGERS, "Trackpad supports too many fingers");

    int transducers_count = 0;
    for (int i = 0; i < ETP_MAX_FINGERS; i++) {
        const auto &state = virtualFinger[i];
        if (!state.touch) {
            continue;
        }

        auto &transducer = inputEvent.transducers[transducers_count];

        transducer.currentCoordinates = state.now;
        transducer.previousCoordinates = state.prev;
        transducer.timestamp = timestamp;

        transducer.isValid = true;
        transducer.isPhysicalButtonDown = info.is_buttonpad && state.button;
        transducer.isTransducerActive = true;

        transducer.secondaryId = i;
        transducer.fingerType = GetBestFingerType(transducers_count);
        transducer.type = FINGER;

        // it looks like Elan PS2 pressure and width is very inaccurate
        // it is better to leave it that way
        transducer.supportsPressure = false;

        // Force Touch emulation
        // Physical button is translated into force touch instead of click
        if (_forceTouchMode == FORCE_TOUCH_BUTTON && transducer.isPhysicalButtonDown) {
            transducer.supportsPressure = true;
            transducer.isPhysicalButtonDown = false;
            transducer.currentCoordinates.pressure = 255;
            transducer.currentCoordinates.width = 10;
        }

        transducers_count++;
    }

    // set the thumb to improve 4F pinch and spread gesture and cross-screen dragging
    if (transducers_count >= 4) {
        // simple thumb detection: find the lowest finger touch in the vertical direction
        // note: the origin is top left corner, so lower finger means higher y coordinate
        UInt32 maxY = 0;
        int newThumbIndex = 0;
        int currentThumbIndex = 0;
        for (int i = 0; i < transducers_count; i++) {
            if (inputEvent.transducers[i].currentCoordinates.y > maxY) {
                maxY = inputEvent.transducers[i].currentCoordinates.y;
                newThumbIndex = i;
            }
            if (inputEvent.transducers[i].fingerType == kMT2FingerTypeThumb) {
                currentThumbIndex = i;
            }
        }
        inputEvent.transducers[currentThumbIndex].fingerType = inputEvent.transducers[newThumbIndex].fingerType;
        inputEvent.transducers[newThumbIndex].fingerType = kMT2FingerTypeThumb;
    }

    for (int i = transducers_count; i < VOODOO_INPUT_MAX_TRANSDUCERS; i++) {
        inputEvent.transducers[i].isValid = false;
        inputEvent.transducers[i].isPhysicalButtonDown = false;
        inputEvent.transducers[i].isTransducerActive = false;
    }

    inputEvent.contact_count = transducers_count;
    inputEvent.timestamp = timestamp;

    if (voodooInputInstance) {
        super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &inputEvent, sizeof(VoodooInputEvent));
    }

    if (!info.is_buttonpad) {
        if (transducers_count == 0) {
            UInt32 buttons = leftButton | rightButton;
            dispatchRelativePointerEvent(0, 0, buttons, timestamp);
        } else {
            UInt32 buttons = 0;
            bool send = false;
            if (lastLeftButton != leftButton) {
                buttons |= leftButton;
                send = true;
            }
            if (lastRightButton != rightButton) {
                buttons |= rightButton;
                send = true;
            }
            if (send) {
                dispatchRelativePointerEvent(0, 0, buttons, timestamp);
            }
        }

        lastLeftButton = leftButton;
        lastRightButton = rightButton;
    }
}

PS2InterruptResult ApplePS2Elan::interruptOccurred(UInt8 data) {
    UInt8 *packet = _ringBuffer.head();
    packet[_packetByteCount++] = data;

    if (_packetByteCount == _packetLength) {
        _ringBuffer.advanceHead(_packetLength);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }

    return kPS2IR_packetBuffering;
}

void ApplePS2Elan::packetReady() {
    INTERRUPT_LOG("VoodooPS2Elan: packet ready occurred\n");
    // empty the ring buffer, dispatching each packet...
    while (_ringBuffer.count() >= _packetLength) {
        if (ignoreall) {
            _ringBuffer.advanceTail(_packetLength);
            continue;
        }

        int packetType;
        switch (info.hw_version) {
            case 1:
                if (info.paritycheck && !elantechPacketCheckV1()) {
                    // ignore invalid packet
                    INTERRUPT_LOG("VoodooPS2Elan: invalid packet received\n");
                    break;
                }

                INTERRUPT_LOG("VoodooPS2Elan: Handling absolute mode\n");
                elantechReportAbsoluteV1();
                break;

            case 2:
                if (elantechDebounceCheckV2()) {
                    // ignore debounce
                    break;
                }

                if (info.paritycheck && !elantechPacketCheckV2()) {
                    // ignore invalid packet
                    INTERRUPT_LOG("VoodooPS2Elan: invalid packet received\n");
                    break;
                }

                INTERRUPT_LOG("VoodooPS2Elan: Handling absolute mode\n");
                elantechReportAbsoluteV2();
                break;

            case 3:
                packetType = elantechPacketCheckV3();
                INTERRUPT_LOG("VoodooPS2Elan: Packet Type %d\n", packetType);

                switch (packetType) {
                    case PACKET_UNKNOWN:
                        INTERRUPT_LOG("VoodooPS2Elan: invalid packet received\n");
                        break;

                    case PACKET_DEBOUNCE:
                        // ignore debounce
                        break;

                    case PACKET_TRACKPOINT:
                        INTERRUPT_LOG("VoodooPS2Elan: Handling trackpoint packet\n");
                        elantechReportTrackpoint();
                        break;

                    default:
                        INTERRUPT_LOG("VoodooPS2Elan: Handling absolute mode\n");
                        elantechReportAbsoluteV3(packetType);
                        break;
                }
                break;

            case 4:
                packetType = elantechPacketCheckV4();
                INTERRUPT_LOG("VoodooPS2Elan: Packet Type %d\n", packetType);

                switch (packetType) {
                    case PACKET_UNKNOWN:
                        INTERRUPT_LOG("VoodooPS2Elan: invalid packet received\n");
                        break;

                    case PACKET_TRACKPOINT:
                        INTERRUPT_LOG("VoodooPS2Elan: Handling trackpoint packet\n");
                        elantechReportTrackpoint();
                        break;

                    default:
                        INTERRUPT_LOG("VoodooPS2Elan: Handling absolute mode\n");
                        elantechReportAbsoluteV4(packetType);
                        break;
                }
                break;

            default:
                INTERRUPT_LOG("VoodooPS2Elan: invalid packet received\n");
        }

        _ringBuffer.advanceTail(_packetLength);
    }
}

void ApplePS2Elan::resetMouse() {
    UInt8 params[2];
    ps2_command<2>(params, kDP_Reset);

    if (params[0] != 0xaa && params[1] != 0x00) {
        DEBUG_LOG("VoodooPS2Elan: failed resetting.\n");
    }
}

void ApplePS2Elan::setTouchPadEnable(bool enable) {
    ps2_command<0>(NULL, enable ? kDP_Enable : kDP_SetDefaultsAndDisable);
}
