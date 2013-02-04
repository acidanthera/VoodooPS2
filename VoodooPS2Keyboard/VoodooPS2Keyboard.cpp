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

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include "VoodooPS2Keyboard.h"
#include "ApplePS2ToADBMap.h"
#include <IOKit/hidsystem/ev_keymap.h>

// =============================================================================
// ApplePS2Keyboard Class Implementation
//

// get some keyboard id information from IOHIDFamily/IOHIDKeyboard.h and Gestalt.h
//#define APPLEPS2KEYBOARD_DEVICE_TYPE	205 // Generic ISO keyboard
#define APPLEPS2KEYBOARD_DEVICE_TYPE	3   // Unknown ANSI keyboard

#define super IOHIKeyboard
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

static bool parseAction(const char* psz, UInt16 dest[], int size)
{
    int i = 0;
    while (*psz && i < size)
    {
        unsigned n;
        psz = parseHex(psz, ' ', 0, n);
        if (!psz || *psz != ' ')
            goto error;
        ++psz;
        if (*psz != 'd' && *psz != 'u')
            goto error;
        dest[i++] = n | (*psz == 'u' ? 0x1000 : 0);
        ++psz;
        if (!*psz)
            break;
        if (*psz != ',')
            goto error;
        while (*++psz == ' ');
    }
    if (i >= size)
        goto error;
    
    dest[i] = 0;
    return true;
    
error:
    dest[0] = 0;
    return false;
}

#ifdef DEBUG
static void logKeySequence(const char* header, UInt16* pAction)
{
    IOLog("ApplePS2Keyboard: %s { ", header);
    for (; *pAction; ++pAction)
    {
        IOLog("%04x, ", *pAction);
    }
    IOLog("}\n");
}
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Keyboard::init(OSDictionary * dict)
{
    //
    // Initialize this object's minimal state.  This is invoked right after this
    // object is instantiated.
    //

    if (!super::init(dict))
        return false;

    _device                    = 0;
    _extendCount               = 0;
    _interruptHandlerInstalled = false;
    _ledState                  = 0;
    sleeppressedtime           = 0;
    
    _config = 0;
    _fkeymode = 0;

    // initialize ACPI support for keyboard backlight/screen brightness
    _provider = 0;
    _brightnessLevels = 0;
    _backlightLevels = 0;

    // start out with all keys up
    bzero(_keyBitVector, sizeof(_keyBitVector));

    // make separate copy of ADB translation table.
    bcopy( PS2ToADBMap, _PS2ToADBMap, sizeof(UInt8) * ADB_CONVERTER_LEN);
    
    // Setup the PS2 -> PS2 scan code mapper
    for (int i = 0; i < countof(_PS2ToPS2Map); i++)
    {
        // by default, each map entry is just itself (no mapping)
        // first half of map is normal scan codes, second half is extended scan codes (e0)
        _PS2ToPS2Map[i] = i;
    }
    
    // Setup default swipe actions
    //REVIEW: should put real defaults in here (same as plist)
    
    _actionSwipeDown[0] = 0;
    _actionSwipeLeft[0] = 0;
    _actionSwipeRight[0] = 0;
    _actionSwipeUp[0] = 0;
    
    //
    // Configure user preferences from Info.plist
    //
    OSBoolean* xml = OSDynamicCast(OSBoolean, dict->getObject("Swap capslock and left control"));
    if (xml && xml->getValue()) {
        char temp = _PS2ToADBMap[0x3a];
        _PS2ToADBMap[0x3a] = _PS2ToADBMap[0x1d];
        _PS2ToADBMap[0x1d] = temp;
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject("Swap command and option"));
    if (xml && xml->getValue()) {
        char temp = _PS2ToADBMap[0x38];
        _PS2ToADBMap[0x38] = _PS2ToADBMap[0x15b];
        _PS2ToADBMap[0x15b] = temp;
        
        temp = _PS2ToADBMap[0x138];
        _PS2ToADBMap[0x138] = _PS2ToADBMap[0x15c];
        _PS2ToADBMap[0x15c] = temp;
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject("Make Application key into right windows"));
    if (xml && xml->getValue()) {
        //REVIEW: was the original wrong?  need to check...
        //_PS2ToADBMap[0x15d] = _PS2ToADBMap[0x15c];
        _PS2ToADBMap[0x15d] = _PS2ToADBMap[0x15b];
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject("Make right modifier keys into Hangul and Hanja"));
    if (xml && xml->getValue()) {
        _PS2ToADBMap[0x138] = _PS2ToADBMap[0xf2];    // Right alt becomes Hangul
        _PS2ToADBMap[0x11d] = _PS2ToADBMap[0xf1];    // Right control becomes Hanja
    }
    
    // not implemented yet.
    // Apple Fn key works well, but no combined key action was made.
    xml = OSDynamicCast(OSBoolean, dict->getObject("Make Application key into Apple Fn key"));
    if (xml && xml->getValue()) {
        _PS2ToADBMap[0x15d] = 0x3f;
    }
    
    // ISO specific mapping to match ADB keyboards
    // This should really be done in the keymaps.
    xml = OSDynamicCast(OSBoolean, dict->getObject("Use ISO layout keyboard"));
    if (xml && xml->getValue()) {
        char temp = _PS2ToADBMap[0x29];             //Grave '~'
        _PS2ToADBMap[0x29] = _PS2ToADBMap[0x56];    //Europe2 '¤º'
        _PS2ToADBMap[0x56] = temp;
    }

    // now load PS2 -> PS2 configuration data
    loadCustomPS2Map(dict, "Custom PS2 Map");
    
    // now load PS2 -> ADB configuration data
    loadCustomADBMap(dict, "Custom ADB Map");
    
    // now load swipe Action configuration data
    OSString* str = OSDynamicCast(OSString, dict->getObject("ActionSwipeUp"));
    if (str)
        parseAction(str->getCStringNoCopy(), _actionSwipeUp, countof(_actionSwipeUp));
    
    str = OSDynamicCast(OSString, dict->getObject("ActionSwipeDown"));
    if (str)
        parseAction(str->getCStringNoCopy(), _actionSwipeDown, countof(_actionSwipeDown));
    
    str = OSDynamicCast(OSString, dict->getObject("ActionSwipeLeft"));
    if (str)
        parseAction(str->getCStringNoCopy(), _actionSwipeLeft, countof(_actionSwipeLeft));
    
    str = OSDynamicCast(OSString, dict->getObject("ActionSwipeRight"));
    if (str)
        parseAction(str->getCStringNoCopy(), _actionSwipeRight, countof(_actionSwipeRight));
    
#ifdef DEBUG
    logKeySequence("Swipe Up:", _actionSwipeUp);
    logKeySequence("Swipe Down:", _actionSwipeDown);
    logKeySequence("Swipe Left:", _actionSwipeLeft);
    logKeySequence("Swipe Right:", _actionSwipeRight);
#endif

    // determine if _fkeymode property should be handled in setParamProperties
    _fkeymodesupported = dict->getObject("Function Keys Standard") && dict->getObject("Function Keys Special");
    if (_fkeymodesupported)
        _fkeymode = -1;
    
    // save dictionary for later, and populate rest of values via setParamProperties
    _config = dict;
    _config->retain();
    setParamProperties(dict);

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2Keyboard * ApplePS2Keyboard::probe(IOService * provider, SInt32 * score)
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

    ApplePS2KeyboardDevice * device  = (ApplePS2KeyboardDevice *)provider;
    PS2Request *             request = device->allocateRequest();
    bool                     success;

    if (!super::probe(provider, score))  return 0;

    //
    // Check to see if the keyboard responds to a basic diagnostic echo.
    //

    // (diagnostic echo command)
    request->commands[0].command = kPS2C_WriteDataPort;
    request->commands[0].inOrOut = kDP_TestKeyboardEcho;
    request->commands[1].command = kPS2C_ReadDataPortAndCompare;
    request->commands[1].inOrOut = kDP_TestKeyboardEcho;
    request->commandsCount = 2;
    device->submitRequestAndBlock(request);

    //REVIEW: this looks like a force
    success = (request->commandsCount <= 2);
    
    // (free the request)
    device->freeRequest(request);

    DEBUG_LOG("ApplePS2Keyboard::probe leaving.\n");
    return (success) ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Keyboard::start(IOService * provider)
{
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
    
    // get IOACPIPlatformDevice for Device (PS2K)
    _provider = (IOACPIPlatformDevice*)IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K");
    if (_provider)
        _provider->retain();

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
        if (array->getCount() < 4)
        {
            DEBUG_LOG("ps2br: KBCL returned invalid package\n");
            break;
        }
        _brightnessCount = array->getCount();
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
        IOLog("ps2br: Brightness levels: { ");
        for (int i = 0; i < _brightnessCount; i++)
            IOLog("%d, ", _brightnessLevels[i]);
        IOLog("}\n");
#endif
        break;
    } while (false);
    
    if (result)
    {
        result->release();
        result = 0;
    }

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
        if (array->getCount() < 2)
        {
            DEBUG_LOG("ps2bl: KKCL returned invalid package\n");
            break;
        }
        _backlightCount = array->getCount();
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
        IOLog("ps2bl: Keyboard backlight levels: { ");
        for (int i = 0; i < _backlightCount; i++)
            IOLog("%d, ", _backlightLevels[i]);
        IOLog("}\n");
#endif
        break;
    } while (false);
    
    if (result)
    {
        result->release();
        result = 0;
    }
    
    //
    // Reset and enable the keyboard.
    //

    initKeyboard();

    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //

    _device->installInterruptAction(this,
            OSMemberFunctionCast(PS2InterruptAction,this,&ApplePS2Keyboard::interruptOccurred));
    _interruptHandlerInstalled = true;

    //
    // Install our power control handler.
    //

    _device->installPowerControlAction( this,
            OSMemberFunctionCast(PS2PowerControlAction,this, &ApplePS2Keyboard::setDevicePowerState ));
    _powerControlHandlerInstalled = true;
    
    //
    // Install our message handler.
    //
    _device->installMessageAction( this,
                                  OSMemberFunctionCast(PS2MessageAction, this, &ApplePS2Keyboard::receiveMessage));
    _messageHandlerInstalled = true;

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::loadCustomPS2Map(OSDictionary* dict, const char* name)
{
    OSArray* pArray = OSDynamicCast(OSArray, dict->getObject(name));
    if (NULL != pArray)
    {
        for (int i = 0; i < pArray->getCount(); i++)
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

void ApplePS2Keyboard::loadCustomADBMap(OSDictionary* dict, const char* name)
{
    OSArray* pArray = OSDynamicCast(OSArray, dict->getObject(name));
    if (NULL != pArray)
    {
        for (int i = 0; i < pArray->getCount(); i++)
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
            assert(index < countof(_PS2ToADBMap));
            _PS2ToADBMap[index] = adbOut;
        }
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2Keyboard::setParamProperties(OSDictionary * dict)
{
    if (NULL == dict)
        return 0;
    
    // get time before sleep button takes effect
    OSNumber* num;
	if ((num = OSDynamicCast(OSNumber, dict->getObject("SleepPressTime"))))
		maxsleeppresstime = (uint64_t)num->unsigned32BitValue() * (uint64_t)1000000;
    
    if (_fkeymodesupported)
    {
        // get function key mode
        UInt32 oldfkeymode = _fkeymode;
        if ((num = OSDynamicCast(OSNumber, dict->getObject("HIDFKeyMode"))))
        {
            _fkeymode = num->unsigned32BitValue();
            setProperty("HIDFKeyMode", _fkeymode, 32);
        }
        if (oldfkeymode != _fkeymode)
        {
            const char* name = _fkeymode ? "Function Keys Standard" : "Function Keys Special";
            if (_config)
                loadCustomPS2Map(_config, name);
        }
    }
    
    return super::setParamProperties(dict);
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

    //
    // Disable the keyboard clock and the keyboard IRQ line.
    //

    setCommandByte(kCB_DisableKeyboardClock, kCB_EnableKeyboardIRQ);

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
    // Uninstall the message handler.
    //
    
    if ( _messageHandlerInstalled ) _device->uninstallMessageAction();
    _messageHandlerInstalled = false;
    
    //
    // Release the pointer to the provider object.
    //

    if (_device)
    {
        _device->release();
        _device = 0;
    }

    //
    // Release ACPI provider for PS2K ACPI device
    //
    if (_provider)
    {
        _provider->release();
        _provider = 0;
    }
    
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
    
    super::stop(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::free()
{
    if (_config)
    {
        _config->release();
        _config = 0;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::interruptOccurred(UInt8 scanCode)   // PS2InterruptAction
{
    //
    // This will be invoked automatically from our device when asynchronous
    // keyboard data needs to be delivered.  Process the keyboard data.  Do
    // NOT send any BLOCKING commands to our device in this context.
    //

    if (scanCode == kSC_Acknowledge)
        IOLog("%s: Unexpected acknowledge from PS/2 controller.\n", getName());
    else if (scanCode == kSC_Resend)
        IOLog("%s: Unexpected resend request from PS/2 controller.\n", getName());
    else
        dispatchKeyboardEventWithScancode(scanCode);
}

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
        IOLog("ps2br: Current brightness: %d\n", current);
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

bool ApplePS2Keyboard::dispatchKeyboardEventWithScancode(UInt8 scanCode)
{
    // Parses the given scan code, updating all necessary internal state, and
    // should a new key be detected, the key event is dispatched.
    //
    // Returns true if a key event was indeed dispatched.

#ifdef DEBUG_VERBOSE
    DEBUG_LOG("%s: PS/2 scancode 0x%x\n", getName(), scanCode);
#endif

    //
    // See if this scan code introduces an extended key sequence.  If so, note
    // it and then return.  Next time we get a key we'll finish the sequence.
    //

    if (scanCode == kSC_Extend)
    {
        _extendCount = 1;
        return false;
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
    if (scanCode == kSC_Pause)
    {
        _extendCount = 2;
        return false;
    }

    unsigned keyCodeRaw = scanCode & ~kSC_UpBit;
    bool goingDown = !(scanCode & kSC_UpBit);
    unsigned keyCode;
    uint64_t now;
    clock_get_uptime(&now);

    //
    // Convert the scan code into a key code index.
    //
    // From "The Undocumented PC" chapter 8, The Keyboard System some
    // keyboard scan codes are single byte, some are multi-byte.
    // Scancodes from running showkey -s (under Linux) for extra keys on keyboard
    // Refer to the conversion table in defaultKeymapOfLength 
    // and the conversion table in ApplePS2ToADBMap.h.
    //
    if (_extendCount == 0)
    {
        // LANG1(Hangul) and LANG2(Hanja) make one event only when the key was pressed.
        // Make key-down and key-up event ADB event
        if (scanCode == 0xf2 || scanCode == 0xf1)
        {
            clock_get_uptime(&now);
            dispatchKeyboardEventX(PS2ToADBMap[scanCode], true, now);
            clock_get_uptime(&now);
            dispatchKeyboardEventX(PS2ToADBMap[scanCode], false, now);
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
        // ignore incoming scan codes until extend count is zero
        if (--_extendCount)
            return false;
        
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

    // handle special cases
    switch (keyCode)
    {
        case 0x4e:  // Numpad+
        case 0x4a:  // Numpad-
            if (_backlightLevels && KBV_IS_KEYDOWN(0x1d, _keyBitVector) && KBV_IS_KEYDOWN(0x38, _keyBitVector))
            {
                // Ctrl+Alt+Numpad(+/-) => use to manipulate keyboard backlight
                if (!KBV_IS_KEYDOWN(keyCode, _keyBitVector))
                    modifyKeyboardBacklight(keyCode, goingDown);
                keyCode = 0;
            }
            break;
            
        case 0x0153:    // delete
            // check for Ctrl+Alt+Delete? (three finger salute)
            if (KBV_IS_KEYDOWN(0x1d, _keyBitVector) && KBV_IS_KEYDOWN(0x38, _keyBitVector))
            {
                keyCode = 0;
                if (scanCode & kSC_UpBit)
                {
                    // Note: If OS X thinks the Command and Control keys are down at the time of
                    //  receiving an ADB 0x7f (power button), it will unconditionaly and unsafely
                    //  reboot the computer, much like the old PC/AT Ctrl+Alt+Delete!
                    // That's why we make sure Control (0x3b) and Alt (0x37) are up!!
                    dispatchKeyboardEventX(0x37, false, now);
                    dispatchKeyboardEventX(0x3b, false, now);
                    dispatchKeyboardEventX(0x7f, true, now);
                    dispatchKeyboardEventX(0x7f, false, now);
                }
            }
            break;
                
        case 0x015f:    // sleep
            // This code relies on the keyboard sending repeats...  If not, it won't
            // invoke sleep until after time has expired and we get the keyup!
            keyCode = 0;
            if (!KBV_IS_KEYDOWN(keyCodeRaw, _keyBitVector))
                sleeppressedtime = now;
            if (_fkeymode || now-sleeppressedtime >= maxsleeppresstime)
            {
                IOPMrootDomain* rootDomain = getPMRootDomain();
                if (NULL != rootDomain)
                    rootDomain->receivePowerNotification(kIOPMSleepNow);
            }
            break;
            
        case 0x0137:    // trackpad on/off
            keyCode = 0;
            if (!(scanCode & kSC_UpBit))
            {
                // get current enabled status, and toggle it
                bool enabled;
                _device->dispatchMouseMessage(kPS2M_getDisableTouchpad, &enabled);
                enabled = !enabled;
                _device->dispatchMouseMessage(kPS2M_setDisableTouchpad, &enabled);
            }
            break;
    }
        
    // Update our key bit vector, which maintains the up/down status of all keys.
    if (goingDown)
    {
        // discard if auto-repeated key
        if (KBV_IS_KEYDOWN(keyCodeRaw, _keyBitVector))
            return false;

        KBV_KEYDOWN(keyCodeRaw, _keyBitVector);
    }
    else
    {
        KBV_KEYUP(keyCodeRaw, _keyBitVector);
    }

#ifdef DEBUG
    // allow hold Alt+numpad keys to type in arbitrary ADB key code
    static int genADB = -1;
    if (KBV_IS_KEYDOWN(0x38, _keyBitVector) && keyCodeRaw >= 0x47 && keyCodeRaw <= 0x52 &&
        keyCodeRaw != 0x4e && keyCodeRaw != 0x4a)
    {
        if (!KBV_IS_KEYDOWN(keyCodeRaw, _keyBitVector))
        {
            // map numpad scan codes to digits
            static int map[0x52-0x47+1] = { 7, 8, 9, -1, 4, 5, 6, -1, 1, 2, 3, 0 };
            if (-1 == genADB)
                genADB = 0;
            int digit = map[keyCodeRaw-0x47];
            if (-1 != digit)
                genADB = genADB * 10 + digit;
            DEBUG_LOG("%s: genADB = %d\n", getName(), genADB);
        }
        keyCode = 0;    // eat it
    }
#endif
    
    // We have a valid key event -- dispatch it to our superclass.
    
    // map scan code to Apple code
    UInt8 adbKeyCode = _PS2ToADBMap[keyCode];
    
    // special cases
    switch (adbKeyCode)
    {
        case 0x90:
        case 0x91:
            if (_brightnessLevels)
            {
                modifyScreenBrightness(adbKeyCode, goingDown);
                adbKeyCode = DEADKEY;
            }
            break;
    }

#ifdef DEBUG_VERBOSE
    if (adbKeyCode == DEADKEY && 0 != keyCode)
        IOLog("%s: Unknown ADB key for PS2 scancode: 0x%x\n", getName(), scanCode);
    else
        IOLog("%s: ADB key code 0x%x %s\n", getName(), adbKeyCode, goingDown?"down":"up");
#endif
    
#ifdef DEBUG_LITE
    if (goingDown)
    {
        if (keyCode == keyCodeRaw)
            IOLog("%s: sending key %x=%x\n", getName(), keyCode > KBV_NUM_SCANCODES ? (keyCode & 0xFF) | 0xe000 : keyCode, adbKeyCode);
        else
            IOLog("%s: sending key %x=%x, %x=%x\n", getName(), keyCodeRaw > KBV_NUM_SCANCODES ? (keyCodeRaw & 0xFF) | 0xe000 : keyCodeRaw, keyCode > KBV_NUM_SCANCODES ? (keyCode & 0xFF) | 0xe000 : keyCode, keyCode > KBV_NUM_SCANCODES ? (keyCode & 0xFF) | 0xe000 : keyCode, adbKeyCode);
    }
#endif
    
    // allow mouse/trackpad driver to have time of last keyboard activity
    // used to implement "PalmNoAction When Typing" and "OutsizeZoneNoAction When Typing"
    PS2KeyInfo info;
    info.time = now;
    info.adbKeyCode = adbKeyCode;
    info.goingDown = goingDown;
    _device->dispatchMouseMessage(kPS2M_notifyKeyPressed, &info);

    // dispatch to HID system
    dispatchKeyboardEventX(adbKeyCode, goingDown, now);
    
#ifdef DEBUG
    if (0x38 == keyCode && !goingDown && -1 != genADB) // Alt going up
    {
        // dispatch typed adb code
        dispatchKeyboardEventX(genADB, true, now);
        dispatchKeyboardEventX(genADB, false, now);
        DEBUG_LOG("%s: sending typed ADB code 0x%x\n", getName(), genADB);
        genADB = -1;
    }
#endif

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::sendKeySequence(UInt16* pKeys)
{
    uint64_t now;
    clock_get_uptime(&now);
    
    for (; *pKeys; ++pKeys)
    {
        dispatchKeyboardEventX(*pKeys & 0xFF, *pKeys & 0x1000 ? false : true, now);
    }
}

void ApplePS2Keyboard::receiveMessage(int message, void* data)
{
    //
    // Here is where we receive messages from the mouse/trackpad driver
    //
    
    switch (message)
    {
        case kPS2M_swipeDown:
            DEBUG_LOG("ApplePS2Keyboard: Synaptic Trackpad call Swipe Down\n");
            sendKeySequence(_actionSwipeDown);
            break;
            
        case kPS2M_swipeLeft:
            DEBUG_LOG("ApplePS2Keyboard: Synaptic Trackpad call Swipe Left\n");
            sendKeySequence(_actionSwipeLeft);
			break;
            
		case kPS2M_swipeRight:
			DEBUG_LOG("ApplePS2Keyboard: Synaptic Trackpad call Swipe Right\n");
            sendKeySequence(_actionSwipeRight);
			break;
            
        case kPS2M_swipeUp:
			DEBUG_LOG("ApplePS2Keyboard: Synaptic Trackpad call Swipe Up\n");
            sendKeySequence(_actionSwipeUp);
            break;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setAlphaLockFeedback(bool locked)
{
    //
    // Set the keyboard LEDs to reflect the state of alpha (caps) lock.
    //
    // It is safe to issue this request from the interrupt/completion context.
    //

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

    PS2Request * request = _device->allocateRequest();

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
    _device->submitRequestAndBlock(request);
    _device->freeRequest(request);
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

    PS2Request * request = _device->allocateRequest();

    // (keyboard enable/disable command)
    request->commands[0].command = kPS2C_WriteDataPort;
    request->commands[0].inOrOut = (enable)?kDP_Enable:kDP_SetDefaultsAndDisable;
    request->commands[1].command = kPS2C_ReadDataPortAndCompare;
    request->commands[1].inOrOut = kSC_Acknowledge;
    request->commandsCount = 2;
    _device->submitRequestAndBlock(request);
    _device->freeRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Keyboard::setCommandByte(UInt8 setBits, UInt8 clearBits)
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
        // Repeat this loop if last command failed, that is, if the old command byte
        // was modified since we first read it.
        //

    } while (request->commandsCount != 4);  

    _device->freeRequest(request);
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
        0x0d, // number of special keys
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
		NX_KEYTYPE_BRIGHTNESS_DOWN,	0x91
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

            // Work around for auto repeat keyboard sometimes after
            //  wakeup from sleep
            // see: http://www.mydellmini.com/forum/general-mac-os-x-discussion/3553-fixed-zero-key-stack-after-wake-up.html
            // remove interrupt handler
            setCommandByte(kCB_DisableKeyboardClock, kCB_EnableKeyboardIRQ);
            if (_interruptHandlerInstalled)
            {
                _device->uninstallInterruptAction();
                _interruptHandlerInstalled = false;
            }
            break;

        case kPS2C_EnableDevice:
            // re-install interrupt handler
            _device->installInterruptAction(this,
                /*(PS2InterruptAction)&ApplePS2Keyboard::interruptOccurred*/
                OSMemberFunctionCast(PS2InterruptAction, this, &ApplePS2Keyboard::interruptOccurred));
            _interruptHandlerInstalled = true;

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

    PS2Request * request = _device->allocateRequest();
    if (request)
    {
        request->commands[0].command = kPS2C_WriteDataPort;
        request->commands[0].inOrOut = kDP_SetDefaults;
        request->commands[1].command = kPS2C_ReadDataPortAndCompare;
        request->commands[1].inOrOut = kSC_Acknowledge;
        request->commandsCount = 2;
        _device->submitRequestAndBlock(request);
        _device->freeRequest(request);
    }
    
    // start out with all keys up
    bzero(_keyBitVector, sizeof(_keyBitVector));
    
    //
    // Initialize the keyboard LED state.
    //

    setLEDs(_ledState);

    //
    // Enable the keyboard clock (should already be so), the keyboard IRQ line,
    // and the keyboard Kscan -> scan code translation mode.
    //

    setCommandByte(kCB_EnableKeyboardIRQ | kCB_TranslateMode,
            kCB_DisableKeyboardClock);

    //
    // Finally, we enable the keyboard itself, so that it may start reporting
    // key events.
    //
    
    setKeyboardEnable(true);
}

