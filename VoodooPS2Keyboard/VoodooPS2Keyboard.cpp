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

#include  <IOKit/IOService.h>

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IOTimerEventSource.h>

#include "VoodooPS2Controller.h"
#include "VoodooPS2Keyboard.h"
#include "AppleACPIPS2Nub.h"
#include <IOKit/hidsystem/ev_keymap.h>


// Constants for Info.plist settings

#define kSleepPressTime                     "SleepPressTime"
#define kHIDF12EjectDelay                   "HIDF12EjectDelay"
#define kRemapPrntScr                       "RemapPrntScr"
#define kNumLockSupport                     "NumLockSupport"
#define kNumLockOnAtBoot                    "NumLockOnAtBoot"
#define kSwapCapsLockLeftControl            "Swap capslock and left control"
#define kSwapCommandOption                  "Swap command and option"
#define kMakeApplicationKeyRightWindows     "Make Application key into right windows"
#define kMakeApplicationKeyAppleFN          "Make Application key into Apple Fn key"
#define kMakeRightModsHangulHanja           "Make right modifier keys into Hangul and Hanja"
#define kUseISOLayoutKeyboard               "Use ISO layout keyboard"

// =============================================================================
// ApplePS2Keyboard Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2Keyboard, IOService);

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

static bool parseRemap(const char *psz, UInt16 &scanFrom, UInt32& scanTo)
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
    if (NULL == psz)
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
    
    _remapPrntScr = false;
    _numLockSupport = false;
    _numLockOnAtBoot = false;
    _swapcommandoption = false;
    _sleepEjectTimer = 0;
    _cmdGate = 0;
    
    _f12ejectdelay = 250;   // default is 250 ms

    _ignoreCapsLedChange = false;

    // start out with all keys up
    bzero(_keyBitVector, sizeof(_keyBitVector));
    
    // make separate copy of ADB translation table.
    for (int i = 0; i < ADB_CONVERTER_LEN; i++) {
        _PS2ToHIDMap[i].usagePage = kHIDPage_KeyboardOrKeypad;
        _PS2ToHIDMap[i].usage = PS2ToHIDMapStock[i];
    }
    
    bcopy(ExtendedPS2ToHIDStockMap, &_PS2ToHIDMap[256], sizeof(ExtendedPS2ToHIDStockMap));
    
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
        loadCustomHIDMap(config, "Custom ADB Map");
    }
    
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
    
    //
    // Lock the controller during initialization
    //
    
    _device->lock();
    
    //
    // Reset and enable the keyboard.
    //

    initKeyboard();
    
//    //
//    // Set NumLock State to On (if specified).
//    //
//
//    if (_numLockOnAtBoot)
//        setNumLock(true);
	
    pWorkLoop->addEventSource(_sleepEjectTimer);
    pWorkLoop->addEventSource(_cmdGate);
    
    //
    // Construct HID Wrapper
    //
    _hidWrapper = OSTypeAlloc(VoodooPS2KeyboardHIDWrapper);
    if (_hidWrapper == nullptr ||
        !_hidWrapper->init() ||
        !_hidWrapper->attach(this) ||
        !_hidWrapper->start(this)) {
        IOLog("%s: HID Wrapper fail :(", getName());
        return nullptr;
    }
    
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
            UInt16 scanIn;
            UInt32 scanOut;
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

void ApplePS2Keyboard::loadCustomHIDMap(OSDictionary* dict, const char* name)
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
            UInt16 scanIn;
            UInt32 hidOut;
            if (!parseRemap(psz, scanIn, hidOut))
            {
                IOLog("VoodooPS2Keyboard: invalid custom ADB map entry: \"%s\"\n", psz);
                continue;
            }
            // must be normal scan code or extended, nothing else, adbOut is only a byte
            UInt8 exIn = scanIn >> 8;
            if (exIn != 0 && exIn != 0xe0)
            {
                IOLog("VoodooPS2Keyboard: scan code invalid for ADB map entry: \"%s\"\n", psz);
                continue;
            }
            // modify PS2 to ADB map per remap entry
            int index = (scanIn & 0xff) + (exIn == 0xe0 ? ADB_CONVERTER_EX_START : 0);
            assert(index < countof(_PS2ToHIDMap));
            _PS2ToHIDMap[index].usagePage = hidOut >> 16;
            _PS2ToHIDMap[index].usage = hidOut & 0xFFFF;
        }
    }
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
    
    //
    // Configure user preferences from Info.plist
    //
    OSBoolean* xml = OSDynamicCast(OSBoolean, dict->getObject(kSwapCapsLockLeftControl));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToHIDMap[0x3a].usage = PS2ToHIDMapStock[0x1d];
            _PS2ToHIDMap[0x1d].usage = PS2ToHIDMapStock[0x3a];
        } else {
            _PS2ToHIDMap[0x3a].usage  = PS2ToHIDMapStock[0x3a];
            _PS2ToHIDMap[0x1d].usage  = PS2ToHIDMapStock[0x1d];
        }
        setProperty(kSwapCapsLockLeftControl, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject(kSwapCommandOption));
    if (xml) {
        if (xml->isTrue()) {
            _swapcommandoption = true;
            _PS2ToHIDMap[0x38].usage  = kHIDUsage_KeyboardLeftGUI; // Left Alt -> Left GUI
            _PS2ToHIDMap[0x15b].usage = kHIDUsage_KeyboardLeftAlt; // Left GUI -> Left Alt
            _PS2ToHIDMap[0x138].usage = kHIDUsage_KeyboardRightGUI; // Right Alt -> Right GUI
            _PS2ToHIDMap[0x15c].usage = kHIDUsage_KeyboardRightAlt; // Right GUI -> Right Alt
        } else {
            _swapcommandoption = false;
            _PS2ToHIDMap[0x38].usage  = kHIDUsage_KeyboardLeftAlt;
            _PS2ToHIDMap[0x15b].usage = kHIDUsage_KeyboardLeftGUI;
            _PS2ToHIDMap[0x138].usage = kHIDUsage_KeyboardRightAlt;
            _PS2ToHIDMap[0x15c].usage = kHIDUsage_KeyboardRightGUI;
        }
        setProperty(kSwapCommandOption, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }

    if ((xml = OSDynamicCast(OSBoolean, dict->getObject(kRemapPrntScr))))
    {
        _remapPrntScr = xml->getValue();
        setProperty(kRemapPrntScr, _remapPrntScr);
    }
    
    if ((xml = OSDynamicCast(OSBoolean, dict->getObject(kNumLockSupport))))
    {
        _numLockSupport = xml->getValue();
        setProperty(kNumLockSupport, _numLockSupport);
    }
    
    if ((xml = OSDynamicCast(OSBoolean, dict->getObject(kNumLockOnAtBoot))))
    {
        _numLockOnAtBoot = xml->getValue();
        setProperty(kNumLockOnAtBoot, _numLockOnAtBoot);
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject(kMakeApplicationKeyRightWindows));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToHIDMap[0x15d].usage = _swapcommandoption ? kHIDUsage_KeyboardRightAlt : kHIDUsage_KeyboardRightGUI;
        }
        else {
            _PS2ToHIDMap[0x15d].usage = kHIDUsage_KeyboardApplication;
        }
        setProperty(kMakeApplicationKeyRightWindows, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    
    xml = OSDynamicCast(OSBoolean, dict->getObject(kMakeRightModsHangulHanja));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToHIDMap[0x138].usage = kHIDUsage_KeyboardLANG1;    // Right alt becomes Hangul
            _PS2ToHIDMap[0x11d].usage = kHIDUsage_KeyboardLANG2;    // Right control becomes Hanja
        }
        else {
            if (_swapcommandoption)
                _PS2ToHIDMap[0x138].usage = kHIDUsage_KeyboardRightGUI;
            else
                _PS2ToHIDMap[0x138].usage = kHIDUsage_KeyboardRightAlt;
            _PS2ToHIDMap[0x11d].usage = kHIDUsage_KeyboardRightControl;
        }
        setProperty(kMakeRightModsHangulHanja, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
    
    // ISO specific mapping to match ADB keyboards
    // This should really be done in the keymaps.
    xml = OSDynamicCast(OSBoolean, dict->getObject(kUseISOLayoutKeyboard));
    if (xml) {
        if (xml->isTrue()) {
            _PS2ToHIDMap[0x29].usage = kHIDUsage_KeyboardNonUSBackslash;
            _PS2ToHIDMap[0x56].usage = kHIDUsage_KeyboardGraveAccentAndTilde;
        }
        else {
            _PS2ToHIDMap[0x29].usage = kHIDUsage_KeyboardGraveAccentAndTilde;
            _PS2ToHIDMap[0x56].usage = kHIDUsage_KeyboardNonUSBackslash;
        }
        setProperty(kUseISOLayoutKeyboard, xml->isTrue() ? kOSBooleanTrue : kOSBooleanFalse);
    }
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
    
    if (_hidWrapper) {
        _hidWrapper->terminate();
        _hidWrapper->release();
    }
    
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
                        dispatchKeyboardEventWithPacket(packet);
                    }
                    if (3 == packet[0] || 4 == packet[0])
                    {
                        // code 3 and 4 indicate send both make and break
                        packet[0] -= 2;
                        clock_get_uptime((uint64_t*)(&packet[kPacketTimeOffset]));
                        dispatchKeyboardEventWithPacket(packet);
                        clock_get_uptime((uint64_t*)(&packet[kPacketTimeOffset]));
                        packet[1] |= 0x80; // break code
                        dispatchKeyboardEventWithPacket(packet);
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

        case kPS2K_notifyKeystroke:
        {
            if (argument) {
                PS2KeyInfo *keystroke = (PS2KeyInfo*)argument;
                if (!keystroke->eatKey) {
                    // the key is consumed
                    keystroke->eatKey = true;
//                    dispatchKeyboardEventX(keystroke->adbKeyCode, keystroke->goingDown, keystroke->time);
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
            // normal packet
            dispatchKeyboardEventWithPacket(packet);
        }
        else
        {
            // command/reset packet
            ////initKeyboard();
        }
        _ringBuffer.advanceTail(kPacketLength);
    }
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
            dispatchKeyboardEventX(kHIDPage_Consumer, kHIDUsage_Csmr_Eject, true, now_abs);
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
    
    unsigned keyCodeRaw = scanCode & ~kSC_UpBit;
    bool goingDown = !(scanCode & kSC_UpBit);
    unsigned keyCode;
    uint64_t now_abs = *(uint64_t*)(&packet[kPacketTimeOffset]);

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
            dispatchKeyboardEventX(_PS2ToHIDMap[scanCode], true, now_abs);
            clock_get_uptime(&now_abs);
            dispatchKeyboardEventX(_PS2ToHIDMap[scanCode], false, now_abs);
            return true;
        }
        
        // Allow PS2 -> PS2 map to work, look in normal part of the table
        keyCode = _PS2ToPS2Map[keyCodeRaw];
    }
    else
    {
        // allow PS2 -> PS2 map to work, look in extended part of the table
        keyCodeRaw += KBV_NUM_SCANCODES;
        keyCode = _PS2ToPS2Map[keyCodeRaw];
    }
    
    // tracking modifier key state
    if (UInt8 bit = (_PS2flags[keyCodeRaw] >> 8))
    {
        UInt16 mask = 1 << (bit-1);
        goingDown ? _PS2modifierState |= mask : _PS2modifierState &= ~mask;
    }

    // handle special cases
    switch (keyCode)
    {
        case 0x015f:    // sleep
            keyCode = 0;
            if (goingDown)
            {
                _timerFunc = kTimerSleep;
                if (!_maxsleeppresstime)
                    onSleepEjectTimer();
                else
                    setTimerTimeout(_sleepEjectTimer, (uint64_t)_maxsleeppresstime * 1000000);
            }
            else
            {
                cancelTimer(_sleepEjectTimer);
            }
            break;
    }
    
    // If keyboard input is disabled drop the key code..
    if (_disableInput && goingDown)
        keyCode=0;
    
    // We have a valid key event -- dispatch it to our superclass.
    
    // map scan code to Apple code
    const VoodooPS2HidElement &hidKeyCode = _PS2ToHIDMap[keyCode];
//    bool eatKey = false;
    
    // special cases
    if (hidKeyCode.usage == kHIDUsage_Csmr_Eject && hidKeyCode.usagePage == kHIDPage_Consumer) {
        if (0 == _PS2modifierState)
        {
            if (goingDown)
            {
//                eatKey = true;
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
    }
    
#ifdef DEBUG
    uint16_t debugCode = keyCode > KBV_NUM_SCANCODES ? (keyCode & 0xFF) | 0xe000 : keyCode;
    IOLog("%s: Sending key %x = Page %x Usage %x %s\n", getName(), debugCode, hidKeyCode.usagePage, hidKeyCode.usage, goingDown ? "down" : "up");
#endif
    
    // allow mouse/trackpad driver to have time of last keyboard activity
    // used to implement "PalmNoAction When Typing" and "OutsizeZoneNoAction When Typing"
//    PS2KeyInfo info;
//    info.time = now_ns;
//    info.adbKeyCode = hidKeyCode.usage;
//    info.goingDown = goingDown;
//    info.eatKey = eatKey;
//
//    _device->dispatchMessage(kPS2M_notifyKeyPressed, &info);

    // Convert ALT + Brightness to Keyboard Brightness
    if (hidKeyCode.usagePage == kHIDPage_AppleVendorTopCase && checkModifierState(kMaskLeftAlt)) {
        // Release left alt before sending brightness keys. macOS otherwise ignores the keypress
        VoodooPS2HidElement altElem = _PS2ToHIDMap[0x38];
        dispatchKeyboardEventX(altElem, false, now_abs);
        
        if (hidKeyCode.usage == kHIDUsage_AV_TopCase_BrightnessUp) {
            dispatchKeyboardEventX(kHIDPage_AppleVendorTopCase, kHIDUsage_AV_TopCase_IlluminationUp, goingDown, now_abs);
            dispatchKeyboardEventX(altElem, true, now_abs);
            return true;
        } else if (hidKeyCode.usage == kHIDUsage_AV_TopCase_BrightnessDown) {
            dispatchKeyboardEventX(kHIDPage_AppleVendorTopCase, kHIDUsage_AV_TopCase_IlluminationDown, goingDown, now_abs);
            dispatchKeyboardEventX(altElem, true, now_abs);
            return true;
        }
    }
    
    if (hidKeyCode.usage != 0 && hidKeyCode.usagePage != 0)
    {
        // dispatch to HID system
        if (goingDown || !(_PS2flags[keyCodeRaw] & kBreaklessKey))
            dispatchKeyboardEventX(hidKeyCode, goingDown, now_abs);
        if (goingDown && (_PS2flags[keyCodeRaw] & kBreaklessKey))
            dispatchKeyboardEventX(hidKeyCode, false, now_abs);
    }

    return true;
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

    PS2Request* request = _device->allocateRequest(2);

    // (set LEDs command)
    request->commands[0].command = kPS2C_SendCommandAndCompareAck;
    request->commands[0].inOrOut = kDP_SetKeyboardLEDs;
    request->commands[1].command = kPS2C_SendCommandAndCompareAck;
    request->commands[1].inOrOut = ledState;
    request->commandsCount = 2;
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
    TPS2Request<1> request;
    request.commands[0].command = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut = enable ? kDP_Enable : kDP_SetDefaults;
    request.commandsCount = 1;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
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

    TPS2Request<1> request;
    request.commands[0].command = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetDefaults;
    request.commandsCount = 1;
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

