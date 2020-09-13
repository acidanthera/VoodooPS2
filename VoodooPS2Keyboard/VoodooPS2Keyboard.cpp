/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
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

// enable for keyboard debugging
#ifdef DEBUG_MSG
//#define DEBUG_VERBOSE
#define DEBUG_LITE
#endif

#include "LegacyIOService.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IOTimerEventSource.h>
#pragma clang diagnostic pop

#include "ApplePS2ToADBMap.h"
#include "VoodooPS2Controller.h"
#include "VoodooPS2Keyboard.h"
#include "ApplePS2ToADBMap.h"
#include "AppleACPIPS2Nub.h"
#include <IOKit/hidsystem/ev_keymap.h>
#include <Headers/kern_devinfo.hpp>


// Constants for Info.plist settings

#define kSleepPressTime                     "SleepPressTime"
#define kHIDFKeyMode                        "HIDFKeyMode"
#define kHIDF12EjectDelay                   "HIDF12EjectDelay"
#define kFunctionKeysStandard               "Function Keys Standard"
#define kFunctionKeysSpecial                "Function Keys Special"
#define kSwapCapsLockLeftControl            "Swap capslock and left control"
#define kSwapCommandOption                  "Swap command and option"
#define kMakeApplicationKeyRightWindows     "Make Application key into right windows"
#define kMakeApplicationKeyAppleFN          "Make Application key into Apple Fn key"
#define kMakeRightModsHangulHanja           "Make right modifier keys into Hangul and Hanja"
#define kUseISOLayoutKeyboard               "Use ISO layout keyboard"
#define kLogScanCodes                       "LogScanCodes"

#define kBrightnessHack                     "BrightnessHack"
#define kMacroInversion                     "Macro Inversion"
#define kMacroTranslation                   "Macro Translation"
#define kMaxMacroTime                       "MaximumMacroTime"

// Constants for brightness keys

#define kBrightnessDevice                   "BrightnessDevice"
#define kBrightnessKey                      "BrightnessKey"

// Definitions for Macro Inversion data format
//REVIEW: This should really be defined as some sort of structure
#define kIgnoreBytes            2 // first two bytes of macro data are ignored (always 0xffff)
#define kOutputBytes            2 // two bytes of Macro Inversion are used to specify output
#define kModifierBytes          4 // 4 bytes specify modifier key match criteria
#define kOutputBytesOffset      (kIgnoreBytes+0)
#define kModifierBytesOffset    (kIgnoreBytes+kOutputBytes+0)
#define kPrefixBytes            (kIgnoreBytes+kOutputBytes+kModifierBytes)
#define kSequenceBytesOffset    (kPrefixBytes+0)
#define kMinMacroInversion      (kPrefixBytes+2)

// Constants for other services to communicate with

#define kIOHIDSystem                        "IOHIDSystem"

// =============================================================================
// ApplePS2Keyboard Class Implementation
//

// get some keyboard id information from IOHIDFamily/IOHIDKeyboard.h and Gestalt.h
//#define APPLEPS2KEYBOARD_DEVICE_TYPE	205 // Generic ISO keyboard
#define APPLEPS2KEYBOARD_DEVICE_TYPE	3   // Unknown ANSI keyboard

OSDefineMetaClassAndStructors(ApplePS2Keyboard, IOHIKeyboard);

UInt32 ApplePS2Keyboard::deviceType()
{
    OSNumber    *xml_handlerID;
    UInt32      ret_id;

    if ( (xml_handlerID = OSDynamicCast( OSNumber, getProperty("alt_handler_id"))) )
        ret_id = xml_handlerID->unsigned32BitValue();
    else 
        ret_id = APPLEPS2KEYBOARD_DEVICE_TYPE;

    return ret_id; 
}

UInt32 ApplePS2Keyboard::interfaceID() { return NX_EVS_DEVICE_INTERFACE_ADB; }

UInt32 ApplePS2Keyboard::maxKeyCodes() { return NX_NUMKEYCODES; }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static const char* parseHex(const char *psz, char term1, char term2, unsigned& out)
{
    int n = 0;
    for (; 0 != *psz && term1 != *psz && term2 != *psz; ++psz)
    {
        n <<= 4;
        if (*psz >= '0' && *psz <= '9')
            n += *psz - '0';
        else if (*psz >= 'a' && *psz <= 'f')
            n += *psz - 'a' + 10;
        else if (*psz >= 'A' && *psz <= 'F')
            n += *psz - 'A' + 10;
        else
            return NULL;
    }
    out = n;
    return psz;
}

static bool parseRemap(const char *psz, UInt16 &scanFrom, UInt16& scanTo)
{
    // psz is of the form: "scanfrom=scanto", examples:
    //      non-extended:  "1d=3a"
    //      extended:      "e077=e017"
    // of course, extended can be mapped to non-extended or non-extended to extended
    
    unsigned n;
    psz = parseHex(psz, '=', 0, n);
    if (NULL == psz || *psz != '=' || n > 0xFFFF)
        return false;
    scanFrom = n;
    psz = parseHex(psz+1, '\n', ';', n);
    if (NULL == psz || n > 0xFFFF)
        return false;
    scanTo = n;
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Keyboard::init(OSDictionary * dict)
{
    //
    // Initialize this object's minimal state.  This is invoked right after this
    // object is instantiated.
    //
    
    if (!super::init(dict))
        return false;
    
    // initialize state
    _device                    = 0;
    _extendCount               = 0;
    _interruptHandlerInstalled = false;
    _ledState                  = 0;
    _lastdata = 0;
    
    _swapcommandoption = false;
    _sleepEjectTimer = 0;
    _cmdGate = 0;
    
    _fkeymode = 0;
    _fkeymodesupported = false;
    _keysStandard = 0;
    _keysSpecial = 0;
    _f12ejectdelay = 250;   // default is 250 ms

    // initialize ACPI support for brightness key
    _panel = 0;
    _panelNotified = false;
    _panelPrompt = false;
    _panelNotifiers = 0;

    // initialize ACPI support for keyboard backlight/screen brightness
    _provider = 0;
    _brightnessLevels = 0;
    _backlightLevels = 0;
    
    _logscancodes = 0;
    _brightnessHack = false;
    
    // initalize macro translation
    _macroInversion = 0;
    _macroTranslation = 0;
    _macroBuffer = 0;
    _macroCurrent = 0;
    _macroMax = 0;
    _macroMaxTime = 25000000ULL;
    _macroTimer = 0;

    _ignoreCapsLedChange = false;

    // start out with all keys up
    bzero(_keyBitVector, sizeof(_keyBitVector));
    
    // make separate copy of ADB translation table.
    bcopy(PS2ToADBMapStock, _PS2ToADBMapMapped, sizeof(_PS2ToADBMapMapped));
    
    // Setup the PS2 -> PS2 scan code mapper
    for (int i = 0; i < countof(_PS2ToPS2Map); i++)
    {
        // by default, each map entry is just itself (no mapping)
        // first half of map is normal scan codes, second half is extended scan codes (e0)
        _PS2ToPS2Map[i] = i;
    }
    bcopy(_PS2flagsStock, _PS2flags, sizeof(_PS2flags));
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2Keyboard* ApplePS2Keyboard::probe(IOService * provider, SInt32 * score)
{
    DEBUG_LOG("ApplePS2Keyboard::probe entered...\n");

    //
    // The driver has been instructed to verify the presence of the actual
    // hardware we represent. We are guaranteed by the controller that the
    // keyboard clock is enabled and the keyboard itself is disabled (thus
    // it won't send any asynchronous scan codes that may mess up the
    // responses expected by the commands we send it).  This is invoked
    // after the init.
    //

    if (!super::probe(provider, score))
        return 0;

    // find config specific to Platform Profile
    OSDictionary* list = OSDynamicCast(OSDictionary, getProperty(kPlatformProfile));
    ApplePS2Device* device = (ApplePS2Device*)provider;
    OSDictionary* config = device->getController()->makeConfigurationNode(list, "Keyboard");
    if (config)
    {
        // if DisableDevice is Yes, then do not load at all...
        OSBoolean* disable = OSDynamicCast(OSBoolean, config->getObject(kDisableDevice));
        if (disable && disable->isTrue())
        {
            config->release();
            return 0;
        }
#ifdef DEBUG
        // save configuration for later/diagnostics...
        setProperty(kMergedConfiguration, config);
#endif
    }

    //
    // Load settings specfic to the Platform Profile...
    //
    
    if (config)
    {
        // now load PS2 -> PS2 configuration data
        loadCustomPS2Map(OSDynamicCast(OSArray, config->getObject("Custom PS2 Map")));
        loadBreaklessPS2(config, "Breakless PS2");
        
        // now load PS2 -> ADB configuration data
        loadCustomADBMap(config, "Custom ADB Map");
        
        // determine if _fkeymode property should be handled in setParamProperties
        _keysStandard = OSDynamicCast(OSArray, config->getObject(kFunctionKeysStandard));
        _keysSpecial = OSDynamicCast(OSArray, config->getObject(kFunctionKeysSpecial));
        _fkeymodesupported = _keysStandard && _keysSpecial;
        if (_fkeymodesupported)
        {
            setProperty(kHIDFKeyMode, (uint64_t)0, 64);
            _keysStandard->retain();
            _keysSpecial->retain();
            loadCustomPS2Map(_keysSpecial);
        }
        else
        {
            _keysStandard = NULL;
            _keysSpecial = NULL;
        }
        
        // load custom macro data
        _macroTranslation = loadMacroData(config, kMacroTranslation);
        _macroInversion = loadMacroData(config, kMacroInversion);
        if (_macroInversion)
        {
            int max = 0;
            for (OSData** p = _macroInversion; *p; p++)
            {
                int length = (*p)->getLength()-kPrefixBytes;
                if (length > max)
                    max = length;
            }
            _macroBuffer = new UInt8[max*kPacketLength];
            _macroMax = max;
        }
    }
    
    // now copy to our PS2ToADBMap -- working copy...
    bcopy(_PS2ToADBMapMapped, _PS2ToADBMap, sizeof(_PS2ToADBMap));
    
    // populate rest of values via setParamProperties
    setParamPropertiesGated(config);
    OSSafeReleaseNULL(config);

    // Note: always return success for keyboard, so no need to do this!
    //  But we do it in the DEBUG build just for information's sake.
#ifdef DEBUG
    //
    // Check to see if the keyboard responds to a basic diagnostic echo.
    //

    // (diagnostic echo command)
    TPS2Request<2> request;
    request.commands[0].command = kPS2C_WriteDataPort;
    request.commands[0].inOrOut = kDP_TestKeyboardEcho;
    request.commands[1].command = kPS2C_ReadDataPort;
    request.commands[1].inOrOut = 0x00;
    request.commandsCount = 2;
    assert(request.commandsCount <= countof(request.commands));
    device->submitRequestAndBlock(&request);
    UInt8 result = request.commands[1].inOrOut;
    if (2 != request.commandsCount || (0x00 != result && kDP_TestKeyboardEcho != result && kSC_Acknowledge != result))
    {
        IOLog("%s: TestKeyboardEcho $EE failed: %d (%02x)\n", getName(), request.commandsCount, result);
        IOLog("%s: ApplePS2Keyboard::probe would normally return failure\n", getName());
    }
#endif
    
    DEBUG_LOG("ApplePS2Keyboard::probe leaving.\n");
    
    // Note: forced success regardless of "test keyboard echo"
    return this;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IORegistryEntry* ApplePS2Keyboard::getDevicebyAddress(IORegistryEntry *parent, int address) {
    IORegistryEntry* child = NULL;
    auto iter = parent->getChildIterator(gIODTPlane);
    if (iter) {
        IORegistryEntry* dev;
        int addr;
        while ((dev = (IORegistryEntry*)iter->getNextObject())) {
            auto location = dev->getLocation();
            // The device need to be present in ACPI scope and follow the naming convention ('A'-'Z', '_')
            auto name = dev->getName();
            if (location && name && name [0] <= '_' &&
                sscanf(dev->getLocation(), "%x", &addr) == 1 && addr == address) {
                child = dev;
                break;
            }
        }
    }
    OSSafeRelease(iter);
    return child;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOACPIPlatformDevice* ApplePS2Keyboard::getBrightnessPanel() {
    IOACPIPlatformDevice *panel = nullptr;

    auto info = DeviceInfo::create();

    auto getAcpiDevice = [](IORegistryEntry *dev) -> IOACPIPlatformDevice * {
        if (dev == nullptr)
            return nullptr;

        auto path = OSDynamicCast(OSString, dev->getProperty("acpi-path"));
        if (path != nullptr) {
            auto p = IORegistryEntry::fromPath(path->getCStringNoCopy());
            auto r = OSDynamicCast(IOACPIPlatformDevice, p);
            if (r) return r;
            OSSafeRelease(p);
        }
        return nullptr;
    };

    if (info) {
        if (info->videoBuiltin != nullptr)
            panel = getAcpiDevice(getDevicebyAddress(info->videoBuiltin, 0x400));

        if (panel == nullptr)
            for (size_t i = 0; panel == nullptr && i < info->videoExternal.size(); ++i)
                panel = getAcpiDevice(getDevicebyAddress(info->videoExternal[i].video, 0x110));

        DeviceInfo::deleter(info);
    }

    return panel;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Keyboard::start(IOService * provider)
{
    DEBUG_LOG("ApplePS2Keyboard::start entered...\n");

    setProperty(kDeliverNotifications, kOSBooleanTrue);

    setProperty(kDeliverNotifications, kOSBooleanTrue);
    //
    // The driver has been instructed to start.   This is called after a
    // successful attach.
    //

    if (!super::start(provider))
        return false;

    //
    // Maintain a pointer to and retain the provider object.
    //

    _device = (ApplePS2KeyboardDevice *)provider;
    _device->retain();
    
    //
    // Setup workloop with command gate for thread syncronization...
    //
    IOWorkLoop* pWorkLoop = getWorkLoop();
    _cmdGate = IOCommandGate::commandGate(this);
    if (!pWorkLoop || !_cmdGate)
    {
        _device->release();
        _device = 0;
        return false;
    }
    _sleepEjectTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2Keyboard::onSleepEjectTimer));
    if (!_sleepEjectTimer)
    {
        _cmdGate->release();
        _cmdGate = 0;
        _device->release();
        _device = 0;
        return false;
    }
    
    // get IOACPIPlatformDevice for built-in panel
    _panel = getBrightnessPanel();
    if (_panel != nullptr) {
        if ((_panelNotifiers = _panel->registerInterest(gIOGeneralInterest, _panelNotification, this)))
            setProperty(kBrightnessDevice, _panel->getName());
        else
            IOLog("ps2br: unable to register interest for GFX notifications\n");
    }

    // get IOACPIPlatformDevice for Device (PS2K)
    //REVIEW: should really look at the parent chain for IOACPIPlatformDevice instead.
    _provider = (IOACPIPlatformDevice*)IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K");

    //
    // get brightness levels for ACPI based brightness keys
    //
    
    OSObject* result = 0;
    if (_provider) do
    {
        // check for brightness methods
        if (kIOReturnSuccess != _provider->validateObject("KBCL") || kIOReturnSuccess != _provider->validateObject("KBCM") || kIOReturnSuccess != _provider->validateObject("KBQC"))
        {
            break;
        }
        // methods are there, so now try to collect brightness levels
        if (kIOReturnSuccess != _provider->evaluateObject("KBCL", &result))
        {
            DEBUG_LOG("ps2br: KBCL returned error\n");
            break;
        }
        OSArray* array = OSDynamicCast(OSArray, result);
        if (!array)
        {
            DEBUG_LOG("ps2br: KBCL returned non-array package\n");
            break;
        }
        int count = array->getCount();
        if (count < 4)
        {
            DEBUG_LOG("ps2br: KBCL returned invalid package\n");
            break;
        }
        _brightnessCount = count;
        _brightnessLevels = new int[_brightnessCount];
        if (!_brightnessLevels)
        {
            DEBUG_LOG("ps2br: _brightnessLevels new int[] failed\n");
            break;
        }
        for (int i = 0; i < _brightnessCount; i++)
        {
            OSNumber* num = OSDynamicCast(OSNumber, array->getObject(i));
            int brightness = num ? num->unsigned32BitValue() : 0;
            _brightnessLevels[i] = brightness;
        }
#ifdef DEBUG_VERBOSE
        DEBUG_LOG("ps2br: Brightness levels: { ");
        for (int i = 0; i < _brightnessCount; i++)
            DEBUG_LOG("%d, ", _brightnessLevels[i]);
        DEBUG_LOG("}\n");
#endif
        break;
    } while (false);
    
    OSSafeReleaseNULL(result);

    //
    // get keyboard backlight levels for ACPI based backlight keys
    //
    
    if (_provider) do
    {
        // check for brightness methods
        if (kIOReturnSuccess != _provider->validateObject("KKCL") || kIOReturnSuccess != _provider->validateObject("KKCM") || kIOReturnSuccess != _provider->validateObject("KKQC"))
        {
            DEBUG_LOG("ps2bl: KKCL, KKCM, KKQC methods not found in DSDT\n");
            break;
        }
        
        // methods are there, so now try to collect brightness levels
        if (kIOReturnSuccess != _provider->evaluateObject("KKCL", &result))
        {
            DEBUG_LOG("ps2bl: KKCL returned error\n");
            break;
        }
        OSArray* array = OSDynamicCast(OSArray, result);
        if (!array)
        {
            DEBUG_LOG("ps2bl: KKCL returned non-array package\n");
            break;
        }
        int count = array->getCount();
        if (count < 2)
        {
            DEBUG_LOG("ps2bl: KKCL returned invalid package\n");
            break;
        }
        _backlightCount = count;
        _backlightLevels = new int[_backlightCount];
        if (!_backlightLevels)
        {
            DEBUG_LOG("ps2bl: _backlightLevels new int[] failed\n");
            break;
        }
        for (int i = 0; i < _backlightCount; i++)
        {
            OSNumber* num = OSDynamicCast(OSNumber, array->getObject(i));
            int brightness = num ? num->unsigned32BitValue() : 0;
            _backlightLevels[i] = brightness;
        }
#ifdef DEBUG_VERBOSE
        DEBUG_LOG("ps2bl: Keyboard backlight levels: { ");
        for (int i = 0; i < _backlightCount; i++)
            DEBUG_LOG("%d, ", _backlightLevels[i]);
        DEBUG_LOG("}\n");
#endif
        break;
    } while (false);
    
    OSSafeReleaseNULL(result);
    
    //
    // Lock the controller during initialization
    //
    
    _device->lock();
    
    //
    // Reset and enable the keyboard.
    //

    initKeyboard();
	
    pWorkLoop->addEventSource(_sleepEjectTimer);
    pWorkLoop->addEventSource(_cmdGate);
    
    // _macroTimer is used in for macro inversion
    _macroTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2Keyboard::onMacroTimer));
    if (_macroTimer)
        pWorkLoop->addEventSource(_macroTimer);
    
    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //

    _device->installInterruptAction(this,
        OSMemberFunctionCast(PS2InterruptAction, this, &ApplePS2Keyboard::interruptOccurred),
        OSMemberFunctionCast(PS2PacketAction,this,&ApplePS2Keyboard::packetReady));
    _interruptHandlerInstalled = true;

    // now safe to allow other threads
    _device->unlock();
    
    //
    // Install our power control handler.
    //

    _device->installPowerControlAction( this,
            OSMemberFunctionCast(PS2PowerControlAction,this, &ApplePS2Keyboard::setDevicePowerState ));
    _powerControlHandlerInstalled = true;
    
    //
    // Tell ACPIPS2Nub that we are interested in ACPI notifications
    //
    //setProperty(kDeliverNotifications, true);

    DEBUG_LOG("ApplePS2Keyboard::start leaving.\n");
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::loadCustomPS2Map(OSArray* pArray)
{
    if (NULL != pArray)
    {
        int count = pArray->getCount();
        for (int i = 0; i < count; i++)
        {
            OSString* pString = OSDynamicCast(OSString, pArray->getObject(i));
            if (NULL == pString)
                continue;
            const char* psz = pString->getCStringNoCopy();
            // check for comment
            if (';' == *psz)
                continue;
            // otherwise, try to parse it
            UInt16 scanIn, scanOut;
            if (!parseRemap(psz, scanIn, scanOut))
            {
                IOLog("VoodooPS2Keyboard: invalid custom PS2 map entry: \"%s\"\n", psz);
                continue;
            }
            // must be normal scan code or extended, nothing else
            UInt8 exIn = scanIn >> 8;
            UInt8 exOut = scanOut >> 8;
            if ((exIn != 0 && exIn != 0xe0) || (exOut != 0 && exOut != 0xe0))
            {
                IOLog("VoodooPS2Keyboard: scan code invalid for PS2 map entry: \"%s\"\n", psz);
                continue;
            }
            // modify PS2 to PS2 map per remap entry
            int index = (scanIn & 0xff) + (exIn == 0xe0 ? KBV_NUM_SCANCODES : 0);
            assert(index < countof(_PS2ToPS2Map));
            _PS2ToPS2Map[index] = (scanOut & 0xff) + (exOut == 0xe0 ? KBV_NUM_SCANCODES : 0);
        }
    }
}

void ApplePS2Keyboard::loadBreaklessPS2(OSDictionary* dict, const char* name)
{
    OSArray* pArray = OSDynamicCast(OSArray, dict->getObject(name));
    if (NULL != pArray)
    {
        int count = pArray->getCount();
        for (int i = 0; i < count; i++)
        {
            OSString* pString = OSDynamicCast(OSString, pArray->getObject(i));
            if (NULL == pString)
                continue;
            const char* psz = pString->getCStringNoCopy();
            // check for comment
            if (';' == *psz)
                continue;
            // otherwise, try to parse it
            unsigned scanIn;
            if (!parseHex(psz, '\n', ';', scanIn))
            {
                IOLog("VoodooPS2Keyboard: invalid breakless PS2 entry: \"%s\"\n", psz);
                continue;
            }
            // must be normal scan code or extended, nothing else
            UInt8 exIn = scanIn >> 8;
            if ((exIn != 0 && exIn != 0xe0))
            {
                IOLog("VoodooPS2Keyboard: scan code invalid for breakless PS2 entry: \"%s\"\n", psz);
                continue;
            }
            // modify PS2 to PS2 map per remap entry
            int index = (scanIn & 0xff) + (exIn == 0xe0 ? KBV_NUM_SCANCODES : 0);
            assert(index < countof(_PS2flags));
            _PS2flags[index] |= kBreaklessKey;
        }
    }
}

void ApplePS2Keyboard::loadCustomADBMap(OSDictionary* dict, const char* name)
{
    OSArray* pArray = OSDynamicCast(OSArray, dict->getObject(name));
    if (NULL != pArray)
    {
        int count = pArray->getCount();
        for (int i = 0; i < count; i++)
        {
            OSString* pString = OSDynamicCast(OSString, pArray->getObject(i));
            if (NULL == pString)
                continue;
            const char* psz = pString->getCStringNoCopy();
            // check for comment
            if (';' == *psz)
                continue;
            // otherwise, try to parse it
            UInt16 scanIn, adbOut;
            if (!parseRemap(psz, scanIn, adbOut))
            {
                IOLog("VoodooPS2Keyboard: invalid custom ADB map entry: \"%s\"\n", psz);
                continue;
            }
            // must be normal scan code or extended, nothing else, adbOut is only a byte
            UInt8 exIn = scanIn >> 8;
            if ((exIn != 0 && exIn != 0xe0) || adbOut > 0xFF)
            {
                IOLog("VoodooPS2Keyboard: scan code invalid for ADB map entry: \"%s\"\n", psz);
                continue;
            }
            // modify PS2 to ADB map per remap entry
            int index = (scanIn & 0xff) + (exIn == 0xe0 ? ADB_CONVERTER_EX_START : 0);
            assert(index < countof(_PS2ToADBMapMapped));
            _PS2ToADBMapMapped[index] = adbOut;
        }
    }
}

OSData** ApplePS2Keyboard::loadMacroData(OSDictionary* dict, const char* name)
{
    OSData** result = 0;
    OSArray* pArray = OSDynamicCast(OSArray, dict->getObject(name));
    if (NULL != pArray)
    {
        // count valid entries
        int total = 0;
        int count = pArray->getCount();
        for (int i = 0; i < count; i++)
        {
            if (OSData* pData = OSDynamicCast(OSData, pArray->getObject(i)))
            {
                int length = pData->getLength();
                if (length >= kMinMacroInversion && !(length & 0x01))
                {
                    const UInt8* p = static_cast<const UInt8*>(pData->getBytesNoCopy());
                    if (p[0] == 0xFF && p[1] == 0xFF)
                        total++;
                }
            }
        }
        if (total)
        {
            // store valid entries
            result = new OSData*[total+1];
            if (result)
            {
                bzero(result, sizeof(char*)*(total+1));
                int index = 0;
                for (int i = 0; i < count; i++)
                {
                    if (OSData* pData = OSDynamicCast(OSData, pArray->getObject(i)))
                    {
                        int length = pData->getLength();
                        if (length >= kMinMacroInversion && !(length & 0x01))
                        {
                            const UInt8* p = static_cast<const UInt8*>(pData->getBytesNoCopy());
                            if (p[0] == 0xFF && p[1] == 0xFF)
                            {
                                result[index++] = pData;
                                pData->retain();
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setParamPropertiesGated(OSDictionary * dict)
{
    if (NULL == dict)
        return;
    
//REVIEW: this code needs cleanup (should be table driven like mouse/trackpad)

    // get time before sleep button takes effect
	if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kSleepPressTime)))
    {
        _maxsleeppresstime = num->unsigned32BitValue();
        setProperty(kSleepPressTime, _maxsleeppresstime, 32);
    }
    // get time before eject button takes effect (no modifiers)
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kHIDF12EjectDelay)))
    {
        _f12ejectdelay = num->unsigned32BitValue();
        setProperty(kHIDF12EjectDelay, _f12ejectdelay, 32);
    }
    // get time between keys part of a macro "inversion"
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kMaxMacroTime)))
    {
        _macroMaxTime = num->unsigned64BitValue();
        setProperty(kMaxMacroTime, _macroMaxTime, 64);
    }
    
    if (_fkeymodesupported)
    {
        // get function key mode
        UInt32 oldfkeymode = _fkeymode;
        if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kHIDFKeyMode)))
        {
            _fkeymode = num->unsigned32BitValue();
            setProperty(kHIDFKeyMode, _fkeymode, 32);
        }
        if (oldfkeymode != _fkeymode)
        {
            OSArray* keys = _fkeymode ? _keysStandard : _keysSpecial;
            assert(keys);
            loadCustomPS2Map(keys);
        }
    }
    
    //
    // Configure user preferences from Info.plist
    //
    OSBoolean* xml = OSDynamicCast(OSBoolean, dict->getObject(kSwapCapsLockLeftControl));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToADBMap[0x3a]  = _PS2ToADBMapMapped[0x1d];
            _PS2ToADBMap[0x1d]  = _PS2ToADBMapMapped[0x3a];
        }
        else {
            _PS2ToADBMap[0x3a]  = _PS2ToADBMapMapped[0x3a];
            _PS2ToADBMap[0x1d]  = _PS2ToADBMapMapped[0x1d];
        }
        setProperty(kSwapCapsLockLeftControl, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject(kSwapCommandOption));
    if (xml) {
        if (xml->isTrue()) {
            _swapcommandoption = true;
            _PS2ToADBMap[0x38]  = _PS2ToADBMapMapped[0x15b];
            _PS2ToADBMap[0x15b] = _PS2ToADBMapMapped[0x38];
            _PS2ToADBMap[0x138] = _PS2ToADBMapMapped[0x15c];
            _PS2ToADBMap[0x15c] = _PS2ToADBMapMapped[0x138];
        }
        else {
            _swapcommandoption = false;
            _PS2ToADBMap[0x38]  = _PS2ToADBMapMapped[0x38];
            _PS2ToADBMap[0x15b] = _PS2ToADBMapMapped[0x15b];
            _PS2ToADBMap[0x138] = _PS2ToADBMapMapped[0x138];
            _PS2ToADBMap[0x15c] = _PS2ToADBMapMapped[0x15c];
        }
        setProperty(kSwapCommandOption, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }

    // special hack for HP Envy brightness
    xml = OSDynamicCast(OSBoolean, dict->getObject(kBrightnessHack));
    if (xml && xml->isTrue())
    {
        //REVIEW: should really read the key assignments via Info.plist instead of hardcoding to F2/F3
        _brightnessHack = true;
    }

    // these two options are mutually exclusive
    // kMakeApplicationKeyAppleFN is ignored if kMakeApplicationKeyRightWindows is set
    bool temp = false;
    xml = OSDynamicCast(OSBoolean, dict->getObject(kMakeApplicationKeyRightWindows));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToADBMap[0x15d] = _swapcommandoption ?  0x3d : 0x36;  // ADB = right-option/right-command
            temp = true;
        }
        else {
            _PS2ToADBMap[0x15d] = _PS2ToADBMapMapped[0x15d];
        }
        setProperty(kMakeApplicationKeyRightWindows, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    // not implemented yet (Note: maybe not true any more)
    // Apple Fn key works well, but no combined key action was made.
    xml = OSDynamicCast(OSBoolean, dict->getObject(kMakeApplicationKeyAppleFN));
    if (xml) {
        if (!temp) {
            if (xml->isTrue()) {
                _PS2ToADBMap[0x15d] = 0x3f; // ADB = AppleFN
            }
            else {
                _PS2ToADBMap[0x15d] = _PS2ToADBMapMapped[0x15d];
            }
        }
        setProperty(kMakeApplicationKeyAppleFN, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject(kMakeRightModsHangulHanja));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToADBMap[0x138] = _PS2ToADBMapMapped[0xf2];    // Right alt becomes Hangul
            _PS2ToADBMap[0x11d] = _PS2ToADBMapMapped[0xf1];    // Right control becomes Hanja
        }
        else {
            if (_swapcommandoption)
                _PS2ToADBMap[0x138] = _PS2ToADBMapMapped[0x15c];
            else
                _PS2ToADBMap[0x138] = _PS2ToADBMapMapped[0x138];
            _PS2ToADBMap[0x11d] = _PS2ToADBMapMapped[0x11d];
        }
        setProperty(kMakeRightModsHangulHanja, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    
    // ISO specific mapping to match ADB keyboards
    // This should really be done in the keymaps.
    xml = OSDynamicCast(OSBoolean, dict->getObject(kUseISOLayoutKeyboard));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToADBMap[0x29]  = _PS2ToADBMapMapped[0x56];     //Europe2 '��'
            _PS2ToADBMap[0x56]  = _PS2ToADBMapMapped[0x29];     //Grave '~'
        }
        else {
            _PS2ToADBMap[0x29]  = _PS2ToADBMapMapped[0x29];
            _PS2ToADBMap[0x56]  = _PS2ToADBMapMapped[0x56];
        }
        setProperty(kUseISOLayoutKeyboard, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kLogScanCodes))) {
        _logscancodes = num->unsigned32BitValue();
        setProperty(kLogScanCodes, num);
    }
}

IOReturn ApplePS2Keyboard::setParamProperties(OSDictionary *dict)
{
    ////IOReturn result = super::setParamProperties(dict);
    if (_cmdGate)
    {
        // syncronize through workloop...
        ////_cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Keyboard::setParamPropertiesGated), dict);
        setParamPropertiesGated(dict);
    }
    
    return super::setParamProperties(dict);
    ////return result;
}

IOReturn ApplePS2Keyboard::setProperties(OSObject *props)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, props);
    if (dict && _cmdGate)
    {
        // syncronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Keyboard::setParamPropertiesGated), dict);
    }
    
	return super::setProperties(props);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::stop(IOService * provider)
{
    //
    // The driver has been instructed to stop.  Note that we must break all
    // connections to other service objects now (ie. no registered actions,
    // no pointers and retains to objects, etc), if any.
    //

    assert(_device == provider);
    
    //
    // Disable the keyboard itself, so that it may stop reporting key events.
    //

    setKeyboardEnable(false);

    // free up the command gate
    IOWorkLoop* pWorkLoop = getWorkLoop();
    if (pWorkLoop)
    {
        if (_cmdGate)
        {
            pWorkLoop->removeEventSource(_cmdGate);
            _cmdGate->release();
            _cmdGate = 0;
        }
        if (_sleepEjectTimer)
        {
            pWorkLoop->removeEventSource(_sleepEjectTimer);
            _sleepEjectTimer->release();
            _sleepEjectTimer = 0;
        }
        if (_macroTimer)
        {
            pWorkLoop->removeEventSource(_macroTimer);
            _macroTimer->release();
            _macroTimer = 0;
        }
    }
    
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

    //
    // Release the pointer to the provider object.
    //

    OSSafeReleaseNULL(_device);

    //
    // Release ACPI provider for panel and PS2K ACPI device
    //
    if (_panel && _panelNotifiers)
        _panelNotifiers->remove();
    OSSafeReleaseNULL(_panel);
    OSSafeReleaseNULL(_provider);
    
    //
    // Release data related to screen brightness
    //
    if (_brightnessLevels)
    {
        delete[] _brightnessLevels;
        _brightnessLevels = 0;
    }
    
    //
    // Release data related to screen brightness
    //
    if (_backlightLevels)
    {
        delete[] _backlightLevels;
        _backlightLevels = 0;
    }

    OSSafeReleaseNULL(_keysStandard);
    OSSafeReleaseNULL(_keysSpecial);

    if (_macroInversion)
    {
        for (OSData** p = _macroInversion; *p; p++)
            (*p)->release();
        delete[] _macroInversion;
        _macroInversion = 0;
    }
    if (_macroTranslation)
    {
        for (OSData** p = _macroTranslation; *p; p++)
            (*p)->release();
        delete[] _macroTranslation;
        _macroTranslation = 0;
    }
    if (_macroBuffer)
    {
        delete[] _macroBuffer;
        _macroBuffer = 0;
    }

    super::stop(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2Keyboard::message(UInt32 type, IOService* provider, void* argument)
{
#ifdef DEBUG
    if (argument)
        IOLog("ApplePS2Keyboard::message: type=%x, provider=%p, argument=%p, argument=%04x, cmp=%x\n", type, provider, argument, *static_cast<UInt32*>(argument), kIOACPIMessageDeviceNotification);
    else
        IOLog("ApplePS2Keyboard::message: type=%x, provider=%p", type, provider);
#endif
    switch (type)
    {
        case kIOACPIMessageDeviceNotification:
            if (NULL != argument) {
                UInt32 arg = *static_cast<UInt32*>(argument);
                if ((arg & 0xFFFF0000) == 0)
                {
                    UInt8 packet[kPacketLength];
                    packet[0] = arg >> 8;
                    packet[1] = arg;
                    if (1 == packet[0] || 2 == packet[0])
                    {
                        // mark packet with timestamp
                        clock_get_uptime((uint64_t*)(&packet[kPacketTimeOffset]));
                        if (!_macroInversion || !invertMacros(packet))
                        {
                            // normal packet
                            dispatchKeyboardEventWithPacket(packet);
                        }
                    }
                    if (3 == packet[0] || 4 == packet[0])
                    {
                        // code 3 and 4 indicate send both make and break
                        packet[0] -= 2;
                        clock_get_uptime((uint64_t*)(&packet[kPacketTimeOffset]));
                        if (!_macroInversion || !invertMacros(packet))
                        {
                            // normal packet (make)
                            dispatchKeyboardEventWithPacket(packet);
                        }
                        clock_get_uptime((uint64_t*)(&packet[kPacketTimeOffset]));
                        packet[1] |= 0x80; // break code
                        if (!_macroInversion || !invertMacros(packet))
                        {
                            // normal packet (break)
                            dispatchKeyboardEventWithPacket(packet);
                        }
                    }
                }
            }
            break;

        case kPS2K_getKeyboardStatus:
        {
            if (argument) {
                bool* pResult = (bool*)argument;
                *pResult = !_disableInput;
            }
            break;
        }

        case kPS2K_setKeyboardStatus:
        {
            if (argument) {
                bool enable = *((bool*)argument);
                if (enable == _disableInput)
                {
                    _disableInput = !enable;
                }
            }
            break;
        }
    }
    return kIOReturnSuccess;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2InterruptResult ApplePS2Keyboard::interruptOccurred(UInt8 data)   // PS2InterruptAction
{
    ////IOLog("ps2interrupt: scanCode = %02x\n", data);
    ////uint64_t time;
    ////clock_get_uptime(&time);
    ////IOLog("ps2interrupt(%lld): scanCode = %02x\n", time, data);
    
    //
    // This will be invoked automatically from our device when asynchronous
    // keyboard data needs to be delivered.  Process the keyboard data.  Do
    // NOT send any BLOCKING commands to our device in this context.
    //
    
    UInt8* packet = _ringBuffer.head();
    
    // special case for $AA $00, spontaneous reset (usually due to static electricity)
    if (kSC_Reset == _lastdata && 0x00 == data)
    {
        IOLog("%s: Unexpected reset (%02x %02x) request from PS/2 controller.\n", getName(), _lastdata, data);
        
        // buffer a packet that will cause a reset in work loop
        packet[0] = 0x00;
        packet[1] = kSC_Reset;
        // mark packet with timestamp
        clock_get_uptime((uint64_t*)(&packet[kPacketTimeOffset]));
        _ringBuffer.advanceHead(kPacketLength);
        _extendCount = 0;
        return kPS2IR_packetReady;
    }
    _lastdata = data;
    
    // other data error conditions
    if (kSC_Acknowledge == data)
    {
        IOLog("%s: Unexpected acknowledge (%02x) from PS/2 controller.\n", getName(), data);
        return kPS2IR_packetBuffering;
    }
    if (kSC_Resend == data)
    {
        IOLog("%s: Unexpected resend (%02x) request from PS/2 controller.\n", getName(), data);
        return kPS2IR_packetBuffering;
    }
    
    //
    // See if this scan code introduces an extended key sequence.  If so, note
    // it and then return.  Next time we get a key we'll finish the sequence.
    //
    
    if (data == kSC_Extend)
    {
        _extendCount = 1;
        return kPS2IR_packetBuffering;
    }
    
    //
    // See if this scan code introduces an extended key sequence for the Pause
    // Key.  If so, note it and then return.  The next time we get a key, drop
    // it.  The next key we get after that finishes the Pause Key sequence.
    //
    // The sequence actually sent to us by the keyboard for the Pause Key is:
    //
    // 1. E1  Extended Sequence for Pause Key
    // 2. 1D  Useless Data, with Up Bit Cleared
    // 3. 45  Pause Key, with Up Bit Cleared
    // 4. E1  Extended Sequence for Pause Key
    // 5. 9D  Useless Data, with Up Bit Set
    // 6. C5  Pause Key, with Up Bit Set
    //
    // The reason items 4 through 6 are sent with the Pause Key is because the
    // keyboard hardware never generates a release code for the Pause Key and
    // the designers are being smart about it.  The sequence above translates
    // to this parser as two separate events, as it should be -- one down key
    // event and one up key event (for the Pause Key).
    //
    
    if (data == kSC_Pause)
    {
        _extendCount = 2;
        return kPS2IR_packetBuffering;
    }
    
    //
    // Otherwise it is a normal scan code, packetize it...
    //
    
    UInt8 extended = _extendCount;
    if (!_extendCount || 0 == --_extendCount)
    {
        // Update our key bit vector, which maintains the up/down status of all keys.
        unsigned keyCodeRaw =  (extended << 8) | (data & ~kSC_UpBit);
        if (!(_PS2flags[keyCodeRaw] & kBreaklessKey))
        {
            if (!(data & kSC_UpBit))
            {
                if (KBV_IS_KEYDOWN(keyCodeRaw))
                    return kPS2IR_packetBuffering;
                KBV_KEYDOWN(keyCodeRaw);
            }
            else
            {
                KBV_KEYUP(keyCodeRaw);
            }
        }
        // non-repeat make, or just break found, buffer it and dispatch
        packet[0] = extended + 1;  // packet[0] = 0 is special packet, so add one
        packet[1] = data;
        // mark packet with timestamp
        clock_get_uptime((uint64_t*)(&packet[kPacketTimeOffset]));
        _ringBuffer.advanceHead(kPacketLength);
        return kPS2IR_packetReady;
    }
    return kPS2IR_packetBuffering;
}

void ApplePS2Keyboard::packetReady()
{
    // empty the ring buffer, dispatching each packet...
    // each packet is always two bytes, for simplicity...
    while (_ringBuffer.count() >= kPacketLength)
    {
        UInt8* packet = _ringBuffer.tail();
        if (0x00 != packet[0])
        {
            if (!_macroInversion || !invertMacros(packet))
            {
                // normal packet
                dispatchKeyboardEventWithPacket(packet);
            }
        }
        else
        {
            // command/reset packet
            ////initKeyboard();
        }
        _ringBuffer.advanceTail(kPacketLength);
    }
}

bool ApplePS2Keyboard::compareMacro(const UInt8* buffer, const UInt8* data, int count)
{
    while (count--)
    {
        if (buffer[0] != data[0] || buffer[1] != data[1])
            return false;
        buffer += kPacketLength;
        data += kPacketKeyDataLength;
    }
    return true;
}

bool ApplePS2Keyboard::invertMacros(const UInt8* packet)
{
    assert(_macroInversion);
    
    if (!_macroTimer || !_macroBuffer)
        return false;

    if (_macroCurrent > 0)
    {
        // cancel macro conversion if packet arrives too late
        uint64_t now_ns;
        absolutetime_to_nanoseconds(*(uint64_t*)(&packet[kPacketTimeOffset]), &now_ns);
        uint64_t prev;
        absolutetime_to_nanoseconds(*(uint64_t*)(&_macroBuffer[(_macroCurrent-1)*kPacketLength+kPacketTimeOffset]), &prev);
        if (now_ns-prev > _macroMaxTime)
            dispatchInvertBuffer();
#if 0 // for testing min/max between macro segments
        uint64_t diff = now_ns-prev;
        static uint64_t diffmin = UINT64_MAX, diffmax = 0;
        if (diff > diffmax) diffmax = diff;
        if (diff < diffmin) diffmin = diff;
        IOLog("diffmin=%lld, diffmax=%lld\n", diffmin, diffmax);
#endif
    }
 
    // add current packet to macro buffer for comparison
    memcpy(_macroBuffer+_macroCurrent*kPacketLength, packet, kPacketLength);
    int buffered = _macroCurrent+1;
    int total = buffered*kPacketKeyDataLength;
    // compare against macro inversions
    for (OSData** p = _macroInversion; *p; p++)
    {
        int length = (*p)->getLength()-kPrefixBytes;
        if (total <= length)
        {
            const UInt8* data = static_cast<const UInt8*>((*p)->getBytesNoCopy());
            if (compareMacro(_macroBuffer, data+kSequenceBytesOffset, buffered))
            {
                if (total == length)
                {
                    // get modifier mask/compare from macro definition
                    UInt16 mask = (static_cast<UInt16>(data[kModifierBytesOffset+0]) << 8) + data[kModifierBytesOffset+1];
                    UInt16 compare = (static_cast<UInt16>(data[kModifierBytesOffset+2]) << 8) + data[kModifierBytesOffset+3];
                    if ((0xFFFF == compare && (_PS2modifierState & mask)) || ((_PS2modifierState & mask) == compare))
                    {
                        // exact match causes macro inversion
                        // grab bytes from macro definition
                        _macroBuffer[0] = data[kOutputBytesOffset+0];
                        _macroBuffer[1] = data[kOutputBytesOffset+1];
                        // dispatch constructed packet (timestamp is stamp on first macro packet)
                        dispatchKeyboardEventWithPacket(_macroBuffer);
                        cancelTimer(_macroTimer);
                        _macroCurrent = 0;
                        return true;
                    }
                }
                else
                {
                    // partial match, keep waiting for full match
                    cancelTimer(_macroTimer);
                    setTimerTimeout(_macroTimer, _macroMaxTime);
                    _macroCurrent++;
                    return true;
                }
            }
        }
    }
    // no match, so... empty macro buffer that may have been existing...
    if (_macroCurrent > 0)
        dispatchInvertBuffer();
    
    return false;
}

void ApplePS2Keyboard::onMacroTimer()
{
    DEBUG_LOG("ApplePS2Keyboard::onMacroTimer\n");

    // timers have a very high priority, packets may have been placed in the
    // input queue already.

    // by calling packetReady, these packets are processed before dealing with
    // the timer (the packets may have arrived before the timer)
    packetReady();

    // after all packets have been processed, ok to check for time expiration
    if (_macroCurrent > 0)
    {
        uint64_t now_abs, now_ns;
        clock_get_uptime(&now_abs);
        absolutetime_to_nanoseconds(now_abs, &now_ns);
        uint64_t prev;
        absolutetime_to_nanoseconds(*(uint64_t*)(&_macroBuffer[(_macroCurrent-1)*kPacketLength+kPacketTimeOffset]), &prev);
        if (now_ns-prev > _macroMaxTime)
            dispatchInvertBuffer();
    }
}

void ApplePS2Keyboard::dispatchInvertBuffer()
{
    UInt8* packet = _macroBuffer;
    for (int i = 0; i < _macroCurrent; i++)
    {
        // dispatch constructed packet
        dispatchKeyboardEventWithPacket(packet);
        packet += kPacketLength;
    }
    _macroCurrent = 0;
    cancelTimer(_macroTimer);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// Note: attempted brightness through ACPI methods, but it didn't work.
//
// I think because Probook 4530s does some funny in things in its
// ACPI brightness methods.
//
// Just keeping it here in case someone wants to try with theirs.

void ApplePS2Keyboard::modifyScreenBrightness(int adbKeyCode, bool goingDown)
{
    assert(_provider);
    assert(_brightnessLevels);
    
    // get current brightness level
    UInt32 result;
    if (kIOReturnSuccess != _provider->evaluateInteger("KBQC", &result))
    {
        DEBUG_LOG("ps2br: KBQC returned error\n");
        return;
    }
    int current = result;
#ifdef DEBUG_VERBOSE
    if (goingDown)
        DEBUG_LOG("ps2br: Current brightness: %d\n", current);
#endif
    // calculate new brightness level, find current in table >= entry in table
    // note first two entries in table are ac-power/battery
    int index = 2;
    while (index < _brightnessCount)
    {
        if (_brightnessLevels[index] >= current)
            break;
        ++index;
    }
    // move to next or previous
    index += (adbKeyCode == 0x90 ? +1 : -1);
    if (index >= _brightnessCount)
        index = _brightnessCount - 1;
    if (index < 2)
        index = 2;
#ifdef DEBUG_VERBOSE
    if (goingDown)
        DEBUG_LOG("ps2br: setting brightness %d\n", _brightnessLevels[index]);
#endif
    OSNumber* num = OSNumber::withNumber(_brightnessLevels[index], 32);
    if (!num)
    {
        DEBUG_LOG("ps2br: OSNumber::withNumber failed\n");
        return;
    }
    if (goingDown && kIOReturnSuccess != _provider->evaluateObject("KBCM", NULL, (OSObject**)&num, 1))
    {
        DEBUG_LOG("ps2br: KBCM returned error\n");
    }
    num->release();
}

//
// Note: trying for ACPI backlight control for ASUS notebooks
//
// This one did work, so leaving this in.  It is done in a generic way such that
// it can be used on more than just ASUS laptops, provided you can figure out
// how to implememnt the KKQC, KKCM, and KKCL methods.
//

void ApplePS2Keyboard::modifyKeyboardBacklight(int keyCode, bool goingDown)
{
    assert(_provider);
    assert(_backlightLevels);
    
    // get current brightness level
    UInt32 result;
    if (kIOReturnSuccess != _provider->evaluateInteger("KKQC", &result))
    {
        DEBUG_LOG("ps2bl: KKQC returned error\n");
        return;
    }
    int current = result;
#ifdef DEBUG_VERBOSE
    if (goingDown)
        DEBUG_LOG("ps2bl: Current keyboard backlight: %d\n", current);
#endif
    // calculate new brightness level, find current in table >= entry in table
    // note first two entries in table are ac-power/battery
    int index = 0;
    while (index < _backlightCount)
    {
        if (_backlightLevels[index] >= current)
            break;
        ++index;
    }
    // move to next or previous
    index += (keyCode == 0x4e ? +1 : -1);
    if (index >= _backlightCount)
        index = _backlightCount - 1;
    if (index < 0)
        index = 0;
#ifdef DEBUG_VERBOSE
    if (goingDown)
        DEBUG_LOG("ps2bl: setting keyboard backlight %d\n", _backlightLevels[index]);
#endif
    OSNumber* num = OSNumber::withNumber(_backlightLevels[index], 32);
    if (!num)
    {
        DEBUG_LOG("ps2bl: OSNumber::withNumber failed\n");
        return;
    }
    if (goingDown && kIOReturnSuccess != _provider->evaluateObject("KKCM", NULL, (OSObject**)&num, 1))
    {
        DEBUG_LOG("ps2bl: KKCM returned error\n");
    }
    num->release();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::onSleepEjectTimer()
{
    switch (_timerFunc)
    {
        case kTimerSleep:
        {
            IOPMrootDomain* rootDomain = getPMRootDomain();
            if (NULL != rootDomain)
                rootDomain->receivePowerNotification(kIOPMSleepNow);
            break;
        }

        case kTimerEject:
        {
            uint64_t now_abs;
            clock_get_uptime(&now_abs);
            dispatchKeyboardEventX(0x92, true, now_abs);
            break;
        }
    }
}

bool ApplePS2Keyboard::dispatchKeyboardEventWithPacket(const UInt8* packet)
{
    // Parses the given scan code, updating all necessary internal state, and
    // should a new key be detected, the key event is dispatched.
    //
    // Returns true if a key event was indeed dispatched.

    UInt8 extended = packet[0] - 1;
    UInt8 scanCode = packet[1];

#ifdef DEBUG_VERBOSE
    DEBUG_LOG("%s: PS/2 scancode %s 0x%x\n", getName(),  extended ? "extended" : "", scanCode);
#endif
    
    unsigned keyCodeRaw = scanCode & ~kSC_UpBit;
    bool goingDown = !(scanCode & kSC_UpBit);
    unsigned keyCode;
    uint64_t now_abs = *(uint64_t*)(&packet[kPacketTimeOffset]);
    uint64_t now_ns;
    absolutetime_to_nanoseconds(now_abs, &now_ns);

    //
    // Convert the scan code into a key code index.
    //
    // From "The Undocumented PC" chapter 8, The Keyboard System some
    // keyboard scan codes are single byte, some are multi-byte.
    // Scancodes from running showkey -s (under Linux) for extra keys on keyboard
    // Refer to the conversion table in defaultKeymapOfLength 
    // and the conversion table in ApplePS2ToADBMap.h.
    //
    if (!extended)
    {
        // LANG1(Hangul) and LANG2(Hanja) make one event only when the key was pressed.
        // Make key-down and key-up event ADB event
        if (scanCode == 0xf2 || scanCode == 0xf1)
        {
            clock_get_uptime(&now_abs);
            dispatchKeyboardEventX(_PS2ToADBMap[scanCode], true, now_abs);
            clock_get_uptime(&now_abs);
            dispatchKeyboardEventX(_PS2ToADBMap[scanCode], false, now_abs);
            return true;
        }
        
        // Allow PS2 -> PS2 map to work, look in normal part of the table
        keyCode = _PS2ToPS2Map[keyCodeRaw];
        
#ifdef DEBUG_VERBOSE
        if (keyCode != keyCodeRaw)
            DEBUG_LOG("%s: keycode translated from=0x%02x to=0x%04x\n", getName(), keyCodeRaw, keyCode);
#endif
    }
    else
    {
        // allow PS2 -> PS2 map to work, look in extended part of the table
        keyCodeRaw += KBV_NUM_SCANCODES;
        keyCode = _PS2ToPS2Map[keyCodeRaw];
        
#ifdef DEBUG_VERBOSE
        if (keyCode != keyCodeRaw)
            DEBUG_LOG("%s: keycode translated from=0xe0%02x to=0x%04x\n", getName(), keyCodeRaw, keyCode);
#endif
        // handle special cases
        switch (keyCodeRaw)
        {
            case 0x012a: // header or trailer for PrintScreen
                return false;
        }
    }
    
    // tracking modifier key state
    if (UInt8 bit = (_PS2flags[keyCodeRaw] >> 8))
    {
        UInt16 mask = 1 << (bit-1);
        goingDown ? _PS2modifierState |= mask : _PS2modifierState &= ~mask;
    }

    // codes e0f0 through e0ff can be used to call back into ACPI methods on this device
    if (keyCode >= 0x01f0 && keyCode <= 0x01ff && _provider != NULL)
    {
        // evaluate RKA[0-F] for these keys
        char method[5] = "RKAx";
        char n = keyCode - 0x01f0;
        method[3] = n < 10 ? n + '0' : n - 10 + 'A';
        if (OSNumber* num = OSNumber::withNumber(goingDown, 32))
        {
            // call ACPI RKAx(Arg0=goingDown)
            _provider->evaluateObject(method, NULL, (OSObject**)&num, 1);
            num->release();
        }
    }

    // handle special cases
    switch (keyCode)
    {
        case 0x4e:  // Numpad+
        case 0x4a:  // Numpad-
            if (_backlightLevels && checkModifierState(kMaskLeftControl|kMaskLeftAlt))
            {
                // Ctrl+Alt+Numpad(+/-) => use to manipulate keyboard backlight
                modifyKeyboardBacklight(keyCode, goingDown);
                keyCode = 0;
            }
            else if (_brightnessHack && checkModifierState(kMaskLeftControl|kMaskLeftShift))
            {
                // Ctrl+Shift+NumPad(+/0) => manipulate brightness (special hack for HP Envy)
                // Fn+F2 generates e0 ab and so does Fn+F3 (we will null those out in ps2 map)
                static unsigned keys[] = { 0x2a, 0x1d };
                // if Option key is down don't pull up on the Shift keys
                int start = checkModifierState(kMaskLeftWindows) ? 1 : 0;
                for (int i = start; i < countof(keys); i++)
                    if (KBV_IS_KEYDOWN(keys[i]))
                        dispatchKeyboardEventX(_PS2ToADBMap[keys[i]], false, now_abs);
                dispatchKeyboardEventX(keyCode == 0x4e ? 0x90 : 0x91, goingDown, now_abs);
                for (int i = start; i < countof(keys); i++)
                    if (KBV_IS_KEYDOWN(keys[i]))
                        dispatchKeyboardEventX(_PS2ToADBMap[keys[i]], true, now_abs);
                keyCode = 0;
            }
            break;
            
        case 0x0153:    // delete
            // check for Ctrl+Alt+Delete? (three finger salute)
            if (checkModifierState(kMaskLeftControl|kMaskLeftAlt))
            {
                keyCode = 0;
                if (!goingDown)
                {
                    // Note: If OS X thinks the Command and Control keys are down at the time of
                    //  receiving an ADB 0x7f (power button), it will unconditionaly and unsafely
                    //  reboot the computer, much like the old PC/AT Ctrl+Alt+Delete!
                    // That's why we make sure Control (0x3b) and Alt (0x37) are up!!
                    dispatchKeyboardEventX(0x37, false, now_abs);
                    dispatchKeyboardEventX(0x3b, false, now_abs);
                    dispatchKeyboardEventX(0x7f, true, now_abs);
                    dispatchKeyboardEventX(0x7f, false, now_abs);
                }
            }
            break;
                
        case 0x015f:    // sleep
            keyCode = 0;
            if (goingDown)
            {
                _timerFunc = kTimerSleep;
                if (_fkeymode || !_maxsleeppresstime)
                    onSleepEjectTimer();
                else
                    setTimerTimeout(_sleepEjectTimer, (uint64_t)_maxsleeppresstime * 1000000);
            }
            else
            {
                cancelTimer(_sleepEjectTimer);
            }
            break;

        //REVIEW: this is getting a bit ugly
        case 0x0128:    // alternate that cannot fnkeys toggle (discrete trackpad toggle)
        case 0x0054:    // SysRq (PrntScr when combined with Alt modifier -left or right-)
        {
            // PrntScr is handled specially by some keyboard devices.
            // See: 5.19 on https://www.win.tue.nl/~aeb/linux/kbd/scancodes-5.html#mtek
#ifdef DEBUG
            UInt16 debug_originalModifiers = _PS2modifierState;
#endif
            // Force Alt (Left) key to be down when receiving this keycode, dont rely on KB firmware
            _PS2modifierState &= ~kMaskRightAlt;
            _PS2modifierState |= kMaskLeftAlt;
            keyCode = 0x0137; // Rewrite keycode
            
#ifdef DEBUG
            IOLog("VoodooPS2Keyboard: special PrntScr: modifiersBefore=%#.4X  modifiersAfter=%#.4X\n", debug_originalModifiers, _PS2modifierState);
#endif
            
            // Fall to the original PrntScr handling case
        }
        case 0x0137:    // prt sc/sys rq
        {
            /* Supported Voodoo PrntScr Key combinations:
               PrntScr            Enable/Disable touchpad
               Windows+PrntScr    Enable/Disable touchpad+keyboard
               Ctrl+Alt+PrntScr   Reset and enable touchpad
               Shift+PrntScr      Send SysRq scancode to the kernel
             
             Notes:
                - Alt+Windows combo seems to be masked out by some keyboard devices and dont produce any ScanCode.
                  Dont rely on it.
            */
            
            unsigned origKeyCode = keyCode;
            keyCode = 0; // Handle all these keycode variants internally
            
#ifdef DEBUG
            bool debug_control = checkModifierStateAny(kMaskLeftControl|kMaskRightControl);
            bool debug_alt = checkModifierStateAny(kMaskLeftAlt|kMaskRightAlt);
            bool debug_shift = checkModifierStateAny(kMaskLeftShift|kMaskRightShift);
            bool debug_windows = checkModifierStateAny(kMaskLeftWindows|kMaskRightWindows);
            
            IOLog("VoodooPS2Keyboard: PrtScr:: goingDown=%s  control=%s  alt=%s  shift=%s  windows=%s  modifiers=%d\n",
                  goingDown ? "Yes" : "No",
                  debug_control ? "Yes" : "No",
                  debug_alt ? "Yes" : "No",
                  debug_shift ? "Yes" : "No",
                  debug_windows ? "Yes" : "No",
                  _PS2modifierState);
#endif
            
            if (!goingDown)
                break;
            
            if (checkModifierStateAny(kMaskLeftControl|kMaskRightControl))
            {
                // Shift is ignored from this point onwards
                if (checkModifierStateAny(kMaskLeftAlt|kMaskRightAlt))
                {
                    // Ctrl+Alt+PrntScr
                    IOLog("VoodooPS2Keyboard: Sending RESET signal to touchpad."); // Dont wrap into a DEBUG compilation condition since this should be a workaroung to be used on faulty states only
                    int val = 1;
                    _device->dispatchMessage(kPS2M_resetTouchpad, &val); // Reset touchpad
                }
            }
            else
            {
                if (checkModifierState(kMaskLeftShift) || checkModifierState(kMaskRightShift))
                {
                    // Shift+PrntScr, no other modifiers present
#ifdef DEBUG
                    IOLog("VoodooPS2Keyboard: Sending SysRq virtual scancode 0x58");
#endif
                    dispatchKeyboardEventX(0x58, true, now_abs); // Send SysRq to the kernel
                    dispatchKeyboardEventX(0x58, false, now_abs);
                }
                else
                {
                    if (checkModifierStateAny(kMaskRightShift|kMaskLeftShift|kMaskRightAlt|kMaskLeftAlt))
                        break; // Eat combinations where Ctrl is Up and Alt or Shift are Down (!Ctrl+[Alt|Shift])
                    
                    bool enabled;
                    if (checkModifierStateAny(kMaskLeftWindows|kMaskRightWindows))
                    {
                        // Windows+PrntScr
                        // Disable keyboard input along with the touchpad using Windows(Option)+prtsc, useful for 2-in-1 applications.
#ifdef DEBUG
                        IOLog("VoodooPS2Keyboard: Toggling keyboard+Touchpad enabled state.");
#endif
                        enabled = _disableInput;
                        _disableInput = !_disableInput;
                        _device->dispatchMessage(kPS2M_setDisableTouchpad, &enabled); // Sync Keyboard and Touchpad enabled states
                    }
                    else
                    {
                        // No other modifiers pressed down
#ifdef DEBUG
                        IOLog("VoodooPS2Keyboard: Toggling Touchpad enabled state.");
#endif
                        // Touchpad enable/disable: get current enabled status, and toggle it
                        _device->dispatchMessage(kPS2M_getDisableTouchpad, &enabled);
                        enabled = !enabled;
                        _device->dispatchMessage(kPS2M_setDisableTouchpad, &enabled);
                    }
                }
                
                break;
            }
            
            if (origKeyCode != 0x0137)
                break; // do not fall through for 0x0128
            // fall through
        }
        case 0x0127:    // alternate for fnkeys toggle (discrete fnkeys toggle)
            keyCode = 0;
            if (!goingDown)
                break;
            if (_fkeymodesupported)
            {
                // modify HIDFKeyMode via IOService... IOHIDSystem
                if (IOService* service = IOService::waitForMatchingService(serviceMatching(kIOHIDSystem), 0))
                {
                    const OSObject* num = OSNumber::withNumber(!_fkeymode, 32);
                    const OSString* key = OSString::withCString(kHIDFKeyMode);
                    if (num && key)
                    {
                        if (OSDictionary* dict = OSDictionary::withObjects(&num, &key, 1))
                        {
                            service->setProperties(dict);
                            dict->release();
                        }
                    }
                    OSSafeReleaseNULL(num);
                    OSSafeReleaseNULL(key);
                    service->release();
                }
            }
            break;
#ifdef DEBUG
        default:
            IOLog("VoodooPS2Keyboard: Unhandled keycode: %#.4X\n", keyCode);
#endif
    }
    
    // If keyboard input is disabled drop the key code..
    if (_disableInput && goingDown)
        keyCode=0;
    
#ifdef DEBUG
    // allow hold Alt+numpad keys to type in arbitrary ADB key code
    static int genADB = -1;
    if (goingDown && checkModifierState(kMaskLeftAlt) &&
        ((keyCodeRaw >= 0x47 && keyCodeRaw <= 0x52 && keyCodeRaw != 0x4e && keyCodeRaw != 0x4a) ||
        (keyCodeRaw >= 0x02 && keyCodeRaw <= 0x0B)))
    {
        // map numpad scan codes to digits
        static int map1[0x52-0x47+1] = { 7, 8, 9, -1, 4, 5, 6, -1, 1, 2, 3, 0 };
        static int map2[0x0B-0x02+1] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
        if (-1 == genADB)
            genADB = 0;
        int digit = keyCodeRaw >= 0x47 ? map1[keyCodeRaw-0x47] : map2[keyCodeRaw-0x02];
        if (-1 != digit)
            genADB = genADB * 10 + digit;
        DEBUG_LOG("%s: genADB = %d\n", getName(), genADB);
        keyCode = 0;    // eat it
    }
#endif
    
    // We have a valid key event -- dispatch it to our superclass.
    
    // map scan code to Apple code
    UInt8 adbKeyCode = _PS2ToADBMap[keyCode];
    bool eatKey = false;
    
    // special cases
    switch (adbKeyCode)
    {
        case BRIGHTNESS_UP:
        case BRIGHTNESS_DOWN:
            if (_panelNotified) {
                eatKey = true;
                if (!_panelPrompt) {
                    _panelPrompt = true;
                    IOLog("%s: Already got brightness key from GFX device, please revert DSDT modification.\n", getName());
                }
            } else if (!_panel && !_panelPrompt) {
                _panelPrompt = true;
                IOLog("%s: Unrecognized GFX device, please consider report your case.\n", getName());
            }
            break;
        case 0x90:
        case 0x91:
            if (_brightnessLevels)
            {
                modifyScreenBrightness(adbKeyCode, goingDown);
                adbKeyCode = DEADKEY;
            }
            break;
        case 0x92: // eject
            if (0 == _PS2modifierState)
            {
                if (goingDown)
                {
                    eatKey = true;
                    _timerFunc = kTimerEject;
                    if (!_f12ejectdelay)
                        onSleepEjectTimer();
                    else
                        setTimerTimeout(_sleepEjectTimer, (uint64_t)_f12ejectdelay * 1000000);
                }
                else
                {
                    cancelTimer(_sleepEjectTimer);
                }
            }
            break;
    }

#ifdef DEBUG_VERBOSE
    if (adbKeyCode == DEADKEY && 0 != keyCode)
        DEBUG_LOG("%s: Unknown ADB key for PS2 scancode: 0x%x\n", getName(), scanCode);
    else
        DEBUG_LOG("%s: ADB key code 0x%x %s\n", getName(), adbKeyCode, goingDown?"down":"up");
#endif
    
#ifdef DEBUG_LITE
    int logscancodes = 2;
#else
    int logscancodes = _logscancodes;
#endif
    if (logscancodes==2 || (logscancodes==1 && goingDown))
    {
        if (keyCode == keyCodeRaw)
            IOLog("%s: sending key %x=%x %s\n", getName(), keyCode > KBV_NUM_SCANCODES ? (keyCode & 0xFF) | 0xe000 : keyCode, adbKeyCode, goingDown?"down":"up");
        else
            IOLog("%s: sending key %x=%x, %x=%x %s\n", getName(), keyCodeRaw > KBV_NUM_SCANCODES ? (keyCodeRaw & 0xFF) | 0xe000 : keyCodeRaw, keyCode > KBV_NUM_SCANCODES ? (keyCode & 0xFF) | 0xe000 : keyCode, keyCode > KBV_NUM_SCANCODES ? (keyCode & 0xFF) | 0xe000 : keyCode, adbKeyCode, goingDown?"down":"up");
    }
    
    // allow mouse/trackpad driver to have time of last keyboard activity
    // used to implement "PalmNoAction When Typing" and "OutsizeZoneNoAction When Typing"
    PS2KeyInfo info;
    info.time = now_ns;
    info.adbKeyCode = adbKeyCode;
    info.goingDown = goingDown;
    info.eatKey = eatKey;

    _device->dispatchMessage(kPS2M_notifyKeyPressed, &info);

    //REVIEW: work around for caps lock bug on Sierra 10.12...
    if (adbKeyCode == 0x39 && version_major >= 16)
    {
        // Mojave 10.14 only needs a part of the workaround:
        // https://github.com/RehabMan/OS-X-Voodoo-PS2-Controller/issues/142#issuecomment-529813842
        if (goingDown || version_major >= 18)
        {
            DEBUG_LOG("%s: Caps Lock goingDown: 0x%x\n", getName(), goingDown);
            _ignoreCapsLedChange = true;
            clock_get_uptime(&now_abs);
            dispatchKeyboardEventX(adbKeyCode, true, now_abs);
            clock_get_uptime(&now_abs);
            dispatchKeyboardEventX(adbKeyCode, false, now_abs);
        }
        if (goingDown && version_major < 18) {
            static bool firsttime = true;
            if (!firsttime)
            {
                clock_get_uptime(&now_abs);
                dispatchKeyboardEventX(adbKeyCode, true, now_abs);
                clock_get_uptime(&now_abs);
                dispatchKeyboardEventX(adbKeyCode, false, now_abs);
            }
            firsttime = false;
        }
        return true;
    }

    if (keyCode && !info.eatKey)
    {
        // dispatch to HID system
        if (goingDown || !(_PS2flags[keyCodeRaw] & kBreaklessKey))
            dispatchKeyboardEventX(adbKeyCode, goingDown, now_abs);
        if (goingDown && (_PS2flags[keyCodeRaw] & kBreaklessKey))
            dispatchKeyboardEventX(adbKeyCode, false, now_abs);
    }
    
#ifdef DEBUG
    if (0x38 == keyCode && !goingDown && -1 != genADB) // Alt going up
    {
        // dispatch typed adb code
        dispatchKeyboardEventX(genADB, true, now_abs);
        dispatchKeyboardEventX(genADB, false, now_abs);
        DEBUG_LOG("%s: sending typed ADB code 0x%x\n", getName(), genADB);
        genADB = -1;
    }
#endif

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2Keyboard::_panelNotification(void *target, void *refCon, UInt32 messageType, IOService *provider, void *messageArgument, vm_size_t argSize) {
    if (messageType == kIOACPIMessageDeviceNotification) {
        if (NULL == target) {
            DEBUG_LOG("%s kIOACPIMessageDeviceNotification target is null\n", provider->getName());
            return kIOReturnError;
        }

        auto self = OSDynamicCast(ApplePS2Keyboard, reinterpret_cast<OSMetaClassBase*>(target));
        if (NULL == self) {
            DEBUG_LOG("%s kIOACPIMessageDeviceNotification target is not a ApplePS2Keyboard\n", provider->getName());
            return kIOReturnError;
        }

        if (NULL != messageArgument) {
            uint64_t now_abs;
            UInt32 arg = *static_cast<UInt32*>(messageArgument);
            switch (arg) {
                case kIOACPIMessageBrightnessUp:
                    clock_get_uptime(&now_abs);
                    self->dispatchKeyboardEventX(BRIGHTNESS_UP, true, now_abs);
                    clock_get_uptime(&now_abs);
                    self->dispatchKeyboardEventX(BRIGHTNESS_UP, false, now_abs);
                    DEBUG_LOG("%s ACPI brightness up\n", self->getName());
                    break;

                case kIOACPIMessageBrightnessDown:
                    clock_get_uptime(&now_abs);
                    self->dispatchKeyboardEventX(BRIGHTNESS_DOWN, true, now_abs);
                    clock_get_uptime(&now_abs);
                    self->dispatchKeyboardEventX(BRIGHTNESS_DOWN, false, now_abs);
                    DEBUG_LOG("%s ACPI brightness down\n", self->getName());
                    break;

                case kIOACPIMessageBrightnessCycle:
                case kIOACPIMessageBrightnessZero:
                case kIOACPIMessageBrightnessOff:
                    DEBUG_LOG("%s ACPI brightness operation 0x%02x not implemented\n", self->getName(), *((UInt32 *) messageArgument));
                    return kIOReturnSuccess;

                default:
                    DEBUG_LOG("%s unknown ACPI notification 0x%04x\n", self->getName(), *((UInt32 *) messageArgument));
                    return kIOReturnSuccess;
            }
            if (!self->_panelNotified) {
                self->_panelNotified = true;
                self->setProperty(kBrightnessKey, "ACPI");
            }
        } else {
            DEBUG_LOG("%s %s received unknown kIOACPIMessageDeviceNotification\n", self->getName(), provider->getName());
        }
    } else {
        DEBUG_LOG("%s received %08X\n", provider->getName(), messageType);
    }
    return kIOReturnSuccess;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setAlphaLockFeedback(bool locked)
{
    //
    // Set the keyboard LEDs to reflect the state of alpha (caps) lock.
    //
    // It is safe to issue this request from the interrupt/completion context.
    //


    DEBUG_LOG("%s: setAlphaLockFeedback locked:0x%x ignore: 0x%x\n", getName(), locked, _ignoreCapsLedChange);
    if (_ignoreCapsLedChange)
    {
        _ignoreCapsLedChange = false;
        return;
    }
    _ledState = locked ? (_ledState | kLED_CapsLock):(_ledState & ~kLED_CapsLock);
    setLEDs(_ledState);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setNumLockFeedback(bool locked)
{
    //
    // Set the keyboard LEDs to reflect the state of num lock.
    //
    // It is safe to issue this request from the interrupt/completion context.
    //

    _ledState = locked ? (_ledState | kLED_NumLock):(_ledState & ~kLED_NumLock);
    setLEDs(_ledState);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setLEDs(UInt8 ledState)
{
    //
    // Asynchronously instructs the controller to set the keyboard LED state.
    //
    // It is safe to issue this request from the interrupt/completion context.
    //

    PS2Request* request = _device->allocateRequest(4);

    // (set LEDs command)
    request->commands[0].command = kPS2C_WriteDataPort;
    request->commands[0].inOrOut = kDP_SetKeyboardLEDs;
    request->commands[1].command = kPS2C_ReadDataPortAndCompare;
    request->commands[1].inOrOut = kSC_Acknowledge;
    request->commands[2].command = kPS2C_WriteDataPort;
    request->commands[2].inOrOut = ledState;
    request->commands[3].command = kPS2C_ReadDataPortAndCompare;
    request->commands[3].inOrOut = kSC_Acknowledge;
    request->commandsCount = 4;
    _device->submitRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setKeyboardEnable(bool enable)
{
    //
    // Instructs the keyboard to start or stop the reporting of key events.
    // Be aware that while the keyboard is enabled, asynchronous key events
    // may arrive in the middle of command sequences sent to the controller,
    // and may get confused for expected command responses.
    //
    // It is safe to issue this request from the interrupt/completion context.
    //

    // (keyboard enable/disable command)
    TPS2Request<2> request;
    request.commands[0].command = kPS2C_WriteDataPort;
    request.commands[0].inOrOut = enable ? kDP_Enable : kDP_SetDefaultsAndDisable;
    request.commands[1].command = kPS2C_ReadDataPortAndCompare;
    request.commands[1].inOrOut = kSC_Acknowledge;
    request.commandsCount = 2;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

const unsigned char * ApplePS2Keyboard::defaultKeymapOfLength(UInt32 * length)
{
    //
    // Keymap data borrowed and modified from IOHIDFamily/IOHIDKeyboard.cpp
    // references  http://www.xfree.org/current/dumpkeymap.1.html
    //             http://www.tamasoft.co.jp/en/general-info/unicode.html
    //
    static const unsigned char appleUSAKeyMap[] = {
        0x00,0x00, // use byte unit.
       
        
        // modifier definition
        0x0b,   //Number of modifier keys.
        // ( modifier   , num of keys, ADB keycodes... )
        // ( 0x00       , 0x01       , 0x39            )
        //0x00,0x01,0x39, //NX_MODIFIERKEY_ALPHALOCK, uses one byte, ADB keycode is 0x39
        //13,0x01,0x39,//NX_MODIFIERKEY_ALPHALOCK_STATELESS
        NX_MODIFIERKEY_SHIFT,       0x01,0x38,
        NX_MODIFIERKEY_CONTROL,     0x01,0x3b,
        NX_MODIFIERKEY_ALTERNATE,   0x01,0x3a,
        NX_MODIFIERKEY_COMMAND,     0x01,0x37,
        NX_MODIFIERKEY_NUMERICPAD,  0x15,0x52,0x41,0x4c,0x53,0x54,0x55,0x45,0x58,0x57,0x56,0x5b,0x5c,0x43,0x4b,0x51,0x7b,0x7d,0x7e,0x7c,0x4e,0x59,
        NX_MODIFIERKEY_HELP,        0x01,0x72,
        NX_MODIFIERKEY_SECONDARYFN, 0x01,0x3f, // Apple's Fn key
        NX_MODIFIERKEY_RSHIFT,      0x01,0x3c,
        NX_MODIFIERKEY_RCONTROL,    0x01,0x3e,
        NX_MODIFIERKEY_RALTERNATE,  0x01,0x3d,
        NX_MODIFIERKEY_RCOMMAND,    0x01,0x36,
        
        
        // ADB virtual key definitions
        0xa2, // number of key definitions
        // ( modifier mask           , generated character{char_set,char_code}...         )
        // ( 0x0d[has 3bit modifiers], {0x00,0x3c}, {0x00,0x3e}, ... total 2^3 characters )
        0x0d,0x00,0x61,0x00,0x41,0x00,0x01,0x00,0x01,0x00,0xca,0x00,0xc7,0x00,0x01,0x00,0x01, //00 A
        0x0d,0x00,0x73,0x00,0x53,0x00,0x13,0x00,0x13,0x00,0xfb,0x00,0xa7,0x00,0x13,0x00,0x13, //01 S
        0x0d,0x00,0x64,0x00,0x44,0x00,0x04,0x00,0x04,0x01,0x44,0x01,0xb6,0x00,0x04,0x00,0x04, //02 D
        0x0d,0x00,0x66,0x00,0x46,0x00,0x06,0x00,0x06,0x00,0xa6,0x01,0xac,0x00,0x06,0x00,0x06, //03 F
        0x0d,0x00,0x68,0x00,0x48,0x00,0x08,0x00,0x08,0x00,0xe3,0x00,0xeb,0x00,0x00,0x18,0x00, //04 H
        0x0d,0x00,0x67,0x00,0x47,0x00,0x07,0x00,0x07,0x00,0xf1,0x00,0xe1,0x00,0x07,0x00,0x07, //05 G
        0x0d,0x00,0x7a,0x00,0x5a,0x00,0x1a,0x00,0x1a,0x00,0xcf,0x01,0x57,0x00,0x1a,0x00,0x1a, //06 Z
        0x0d,0x00,0x78,0x00,0x58,0x00,0x18,0x00,0x18,0x01,0xb4,0x01,0xce,0x00,0x18,0x00,0x18, //07 X
        0x0d,0x00,0x63,0x00,0x43,0x00,0x03,0x00,0x03,0x01,0xe3,0x01,0xd3,0x00,0x03,0x00,0x03, //08 C
        0x0d,0x00,0x76,0x00,0x56,0x00,0x16,0x00,0x16,0x01,0xd6,0x01,0xe0,0x00,0x16,0x00,0x16, //09 V
        0x02,0x00,0x3c,0x00,0x3e, //0a NON-US-BACKSLASH on ANSI and JIS keyboards GRAVE on ISO
        0x0d,0x00,0x62,0x00,0x42,0x00,0x02,0x00,0x02,0x01,0xe5,0x01,0xf2,0x00,0x02,0x00,0x02, //0b B
        0x0d,0x00,0x71,0x00,0x51,0x00,0x11,0x00,0x11,0x00,0xfa,0x00,0xea,0x00,0x11,0x00,0x11, //0c Q
        0x0d,0x00,0x77,0x00,0x57,0x00,0x17,0x00,0x17,0x01,0xc8,0x01,0xc7,0x00,0x17,0x00,0x17, //0d W
        0x0d,0x00,0x65,0x00,0x45,0x00,0x05,0x00,0x05,0x00,0xc2,0x00,0xc5,0x00,0x05,0x00,0x05, //0e E
        0x0d,0x00,0x72,0x00,0x52,0x00,0x12,0x00,0x12,0x01,0xe2,0x01,0xd2,0x00,0x12,0x00,0x12, //0f R
        0x0d,0x00,0x79,0x00,0x59,0x00,0x19,0x00,0x19,0x00,0xa5,0x01,0xdb,0x00,0x19,0x00,0x19, //10 Y
        0x0d,0x00,0x74,0x00,0x54,0x00,0x14,0x00,0x14,0x01,0xe4,0x01,0xd4,0x00,0x14,0x00,0x14, //11 T
        0x0a,0x00,0x31,0x00,0x21,0x01,0xad,0x00,0xa1, //12 1
        0x0e,0x00,0x32,0x00,0x40,0x00,0x32,0x00,0x00,0x00,0xb2,0x00,0xb3,0x00,0x00,0x00,0x00, //13 2
        0x0a,0x00,0x33,0x00,0x23,0x00,0xa3,0x01,0xba, //14 3
        0x0a,0x00,0x34,0x00,0x24,0x00,0xa2,0x00,0xa8, //15 4
        0x0e,0x00,0x36,0x00,0x5e,0x00,0x36,0x00,0x1e,0x00,0xb6,0x00,0xc3,0x00,0x1e,0x00,0x1e, //16 6
        0x0a,0x00,0x35,0x00,0x25,0x01,0xa5,0x00,0xbd, //17 5
        0x0a,0x00,0x3d,0x00,0x2b,0x01,0xb9,0x01,0xb1, //18 EQUALS
        0x0a,0x00,0x39,0x00,0x28,0x00,0xac,0x00,0xab, //19 9
        0x0a,0x00,0x37,0x00,0x26,0x01,0xb0,0x01,0xab, //1a 7
        0x0e,0x00,0x2d,0x00,0x5f,0x00,0x1f,0x00,0x1f,0x00,0xb1,0x00,0xd0,0x00,0x1f,0x00,0x1f, //1b MINUS
        0x0a,0x00,0x38,0x00,0x2a,0x00,0xb7,0x00,0xb4, //1c 8
        0x0a,0x00,0x30,0x00,0x29,0x00,0xad,0x00,0xbb, //1d 0
        0x0e,0x00,0x5d,0x00,0x7d,0x00,0x1d,0x00,0x1d,0x00,0x27,0x00,0xba,0x00,0x1d,0x00,0x1d, //1e RIGHTBRACKET
        0x0d,0x00,0x6f,0x00,0x4f,0x00,0x0f,0x00,0x0f,0x00,0xf9,0x00,0xe9,0x00,0x0f,0x00,0x0f, //1f O
        0x0d,0x00,0x75,0x00,0x55,0x00,0x15,0x00,0x15,0x00,0xc8,0x00,0xcd,0x00,0x15,0x00,0x15, //20 U
        0x0e,0x00,0x5b,0x00,0x7b,0x00,0x1b,0x00,0x1b,0x00,0x60,0x00,0xaa,0x00,0x1b,0x00,0x1b, //21 LEFTBRACKET
        0x0d,0x00,0x69,0x00,0x49,0x00,0x09,0x00,0x09,0x00,0xc1,0x00,0xf5,0x00,0x09,0x00,0x09, //22 I
        0x0d,0x00,0x70,0x00,0x50,0x00,0x10,0x00,0x10,0x01,0x70,0x01,0x50,0x00,0x10,0x00,0x10, //23 P
        0x10,0x00,0x0d,0x00,0x03, //24 RETURN
        0x0d,0x00,0x6c,0x00,0x4c,0x00,0x0c,0x00,0x0c,0x00,0xf8,0x00,0xe8,0x00,0x0c,0x00,0x0c, //25 L
        0x0d,0x00,0x6a,0x00,0x4a,0x00,0x0a,0x00,0x0a,0x00,0xc6,0x00,0xae,0x00,0x0a,0x00,0x0a, //26 J
        0x0a,0x00,0x27,0x00,0x22,0x00,0xa9,0x01,0xae, //27 APOSTROPHE
        0x0d,0x00,0x6b,0x00,0x4b,0x00,0x0b,0x00,0x0b,0x00,0xce,0x00,0xaf,0x00,0x0b,0x00,0x0b, //28 K
        0x0a,0x00,0x3b,0x00,0x3a,0x01,0xb2,0x01,0xa2, //29 SEMICOLON
        0x0e,0x00,0x5c,0x00,0x7c,0x00,0x1c,0x00,0x1c,0x00,0xe3,0x00,0xeb,0x00,0x1c,0x00,0x1c, //2a BACKSLASH
        0x0a,0x00,0x2c,0x00,0x3c,0x00,0xcb,0x01,0xa3, //2b COMMA
        0x0a,0x00,0x2f,0x00,0x3f,0x01,0xb8,0x00,0xbf, //2c SLASH
        0x0d,0x00,0x6e,0x00,0x4e,0x00,0x0e,0x00,0x0e,0x00,0xc4,0x01,0xaf,0x00,0x0e,0x00,0x0e, //2d N
        0x0d,0x00,0x6d,0x00,0x4d,0x00,0x0d,0x00,0x0d,0x01,0x6d,0x01,0xd8,0x00,0x0d,0x00,0x0d, //2e M
        0x0a,0x00,0x2e,0x00,0x3e,0x00,0xbc,0x01,0xb3, //2f PERIOD
        0x02,0x00,0x09,0x00,0x19, //30 TAB
        0x0c,0x00,0x20,0x00,0x00,0x00,0x80,0x00,0x00, //31 SPACE
        0x0a,0x00,0x60,0x00,0x7e,0x00,0x60,0x01,0xbb, //32 GRAVE on ANSI and JIS keyboards, NON-US-BACKSLASH on ISO
        0x02,0x00,0x7f,0x00,0x08, //33 BACKSPACE
        0xff, //34 PLAY/PAUSE
        0x02,0x00,0x1b,0x00,0x7e, //35 ESCAPE
        0xff, //36 RGUI
        0xff, //37 LGUI
        0xff, //38 LSHIFT
        0xff, //39 CAPSLOCK
        0xff, //3a LALT
        0xff, //3b LCTRL
        0xff, //3c RSHIFT
        0xff, //3d RALT
        0xff, //3e RCTRL
        0xff, //3f Apple Fn key
        0x00,0xfe,0x36, //40 F17
        0x00,0x00,0x2e, //41 KEYPAD_PERIOD
        0xff, //42 NEXT TRACK or FAST
        0x00,0x00,0x2a, //43 KEYPAD_MULTIPLY
        0xff, //44
        0x00,0x00,0x2b, //45 KEYPAD_PLUS
        0xff, //46
        0x00,0x00,0x1b, //47 CLEAR
        0xff, //48 VOLUME UP
        0xff, //49 VOLUME DOWN
        0xff, //4a MUTE
        0x0e,0x00,0x2f,0x00,0x5c,0x00,0x2f,0x00,0x1c,0x00,0x2f,0x00,0x5c,0x00,0x00,0x0a,0x00, //4b KEYPAD_DIVIDE
        0x00,0x00,0x0d,  //4c Apple Fn + Return = ENTER //XX03
        0xff, //4d PREVIOUS TRACK or REWIND
        0x00,0x00,0x2d, //4e KEYPAD_MINUS
        0x00,0xfe,0x37, //4f F18
        0x00,0xfe,0x38, //50 F19
        0x0e,0x00,0x3d,0x00,0x7c,0x00,0x3d,0x00,0x1c,0x00,0x3d,0x00,0x7c,0x00,0x00,0x18,0x46, //51 KEYPAD_EQUALS
        0x00,0x00,0x30, //52 KEYPAD_0
        0x00,0x00,0x31, //53 KEYPAD_1
        0x00,0x00,0x32, //54 KEYPAD_2
        0x00,0x00,0x33, //55 KEYPAD_3
        0x00,0x00,0x34, //56 KEYPAD_4
        0x00,0x00,0x35, //57 KEYPAD_5
        0x00,0x00,0x36, //58 KEYPAD_6
        0x00,0x00,0x37, //59 KEYPAD_7
        0x00,0xfe,0x39, //5a F20
        0x00,0x00,0x38, //5b KEYPAD_8
        0x00,0x00,0x39, //5c KEYPAD_9
        0xff,           //0x02,0x00,0xa5,0x00,0x7c, //5d JIS JAPANESE YEN
        0xff,           //0x00,0x00,0x5f, //5e JIS JAPANESE RO
        0xff,           //0x00,0x00,0x2c, //5f KEYPAD_COMMA, JIS only
        0x00,0xfe,0x24, //60 F5
        0x00,0xfe,0x25, //61 F6
        0x00,0xfe,0x26, //62 F7
        0x00,0xfe,0x22, //63 F3
        0x00,0xfe,0x27, //64 F8
        0x00,0xfe,0x28, //65 F9
        0xff, //66 JIS JAPANESE EISU, KOREAN HANJA
        0x00,0xfe,0x2a, //67 F11
        0xff, //68 JIS JAPANESE KANA, KOREAN HANGUL
        0x00,0xfe,0x32, //69 F13
        0x00,0xfe,0x35, //6a F16
        0x00,0xfe,0x33, //6b F14
        0xff, //6c
        0x00,0xfe,0x29, //6d F10
        0xff, //6e
        0x00,0xfe,0x2b, //6f F12
        0xff, //70
        0x00,0xfe,0x34, //71 F15
        0xff, //72 HELP
        0x00,0xfe,0x2e, //73 HOME
        0x00,0xfe,0x30, //74 PAGEUP
        0x00,0xfe,0x2d, //75 DELETE
        0x00,0xfe,0x23, //76 F4
        0x00,0xfe,0x2f, //77 END
        0x00,0xfe,0x21, //78 F2
        0x00,0xfe,0x31, //79 PAGEDOWN
        0x00,0xfe,0x20, //7a F1
        0x00,0x01,0xac, //7b LEFT ARROW
        0x00,0x01,0xae, //7c RIGHT ARROW
        0x00,0x01,0xaf, //7d DOWN ARROW
        0x00,0x01,0xad, //7e UP ARROW
        0x00,0x00,0x00, //7f POWER
        0x00,0x00,0x00, 
        0x00,0x00,0x00, //81 Spotlight
        0x00,0x00,0x00, //82 Dashboard
        0x00,0x00,0x00, //83 Launchpad
        0x00,0x00,0x00, 
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00, //90 Main Brightness Up
        0x00,0x00,0x00, //91 Main Brightness Down
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00,
        0x00,0x00,0x00, // a0 Exposes All
        0x00,0x00,0x00, // a1 Expose Desktop
        
        
        // key sequence definition
        // I tested some key sequence "Command + Shift + '['", but it doesn't work well.
        // No one sequence was used on key deff table now.
        0x11, // number of of sequence definitions
        // ( num of keys, generated sequence characters(char_set,char_code)... )
        // ( 0x02       , {0xff,0x04}, {0x00,0x31},                            )
        0x02,0xff,0x04,0x00,0x31, // Command + '1'
        0x02,0xff,0x04,0x00,0x32, // Command + '2'
        0x02,0xff,0x04,0x00,0x33, // Command + '3'
        0x02,0xff,0x04,0x00,0x34, // Command + '4'
        0x02,0xff,0x04,0x00,0x35, // Command + '5'
        0x02,0xff,0x04,0x00,0x36, // Command + '6'
        0x02,0xff,0x04,0x00,0x37, // Command + '7'
        0x02,0xff,0x04,0x00,0x38, // Command + '8'
        0x02,0xff,0x04,0x00,0x39, // Command + '9'
        0x02,0xff,0x04,0x00,0x30, // Command + '0'
        0x02,0xff,0x04,0x00,0x2d, // Command + '-'
        0x02,0xff,0x04,0x00,0x3d, // Command + '='
        0x02,0xff,0x04,0x00,0x70, // Command + 'p'
        0x02,0xff,0x04,0x00,0x5d, // Command + ']'
        0x02,0xff,0x04,0x00,0x5b, // Command + '['
        0x03,0xff,0x04,0xff,0x01,0x00,0x5b, // Command + Shift + '['
        0x03,0xff,0x04,0xff,0x01,0x00,0x5d, // Command + shift + ']'

       
        // special key definition
        0x0e, // number of special keys
        // ( NX_KEYTYPE,        Virtual ADB code )
        NX_KEYTYPE_CAPS_LOCK,   0x39,
        NX_KEYTYPE_HELP,        0x72,
        NX_POWER_KEY,           0x7f,
        NX_KEYTYPE_MUTE,        0x4a,
        NX_KEYTYPE_SOUND_UP,    0x48,
        NX_KEYTYPE_SOUND_DOWN,  0x49,
        // remove arrow keys as special keys. They are generating double up/down scroll events
        // in both carbon and coco apps.
        //NX_UP_ARROW_KEY,        0x7e,       // ADB is 3e raw, 7e virtual (KMAP)
        //NX_DOWN_ARROW_KEY,      0x7d,       // ADB is 0x3d raw, 7d virtual
        NX_KEYTYPE_NUM_LOCK,    0x47,       // ADB combines with CLEAR key for numlock
        NX_KEYTYPE_VIDMIRROR,   0x70,
        NX_KEYTYPE_PLAY,        0x34,
        NX_KEYTYPE_NEXT,        0x42,       // if this event repeated, act as NX_KEYTYPE_FAST
        NX_KEYTYPE_PREVIOUS,    0x4d,        // if this event repeated, act as NX_KEYTYPE_REWIND
		NX_KEYTYPE_BRIGHTNESS_UP, 0x90,
		NX_KEYTYPE_BRIGHTNESS_DOWN,	0x91,
        NX_KEYTYPE_EJECT,       0x92,
    };
 
    *length = sizeof(appleUSAKeyMap);
    return appleUSAKeyMap;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setDevicePowerState( UInt32 whatToDo )
{
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            //
            // Disable keyboard.
            //
            setKeyboardEnable( false );
            break;

        case kPS2C_EnableDevice:
            //
            // Enable keyboard and restore state.
            //
            initKeyboard();
            break;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::initKeyboard()
{
    //
    // Reset the keyboard to its default state.
    //

    TPS2Request<2> request;
    request.commands[0].command = kPS2C_WriteDataPort;
    request.commands[0].inOrOut = kDP_SetDefaults;
    request.commands[1].command = kPS2C_ReadDataPortAndCompare;
    request.commands[1].inOrOut = kSC_Acknowledge;
    request.commandsCount = 2;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    // look for any keys that are down (just in case the reset happened with keys down)
    // for each key that is down, dispatch a key up for it
    UInt8 packet[kPacketLength];
    for (int scanCode = 0; scanCode < KBV_NUM_KEYCODES; scanCode++)
    {
        if (KBV_IS_KEYDOWN(scanCode))
        {
            packet[0] = scanCode < KBV_NUM_SCANCODES ? 1 : 2;
            packet[1] = scanCode | kSC_UpBit;
            dispatchKeyboardEventWithPacket(packet);
        }
    }
    
    // start out with all keys up
    bzero(_keyBitVector, sizeof(_keyBitVector));
    _PS2modifierState = 0;
    
    //
    // Initialize the keyboard LED state.
    //

    setLEDs(_ledState);
    
    //
    // Reset state of packet/keystroke buffer
    //
    
    _extendCount = 0;
    _ringBuffer.reset();

    //
    // Finally, we enable the keyboard itself, so that it may start reporting
    // key events.
    //
    
    setKeyboardEnable(true);
    
    //
    // Enable keyboard Kscan -> scan code translation mode.
    //
    _device->setCommandByte(kCB_TranslateMode, 0);
}

