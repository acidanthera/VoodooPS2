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

#ifndef _APPLEPS2KEYBOARD_H
#define _APPLEPS2KEYBOARD_H

#include <libkern/c++/OSBoolean.h>
#include "../VoodooPS2Controller/ApplePS2KeyboardDevice.h"
#include "LegacyIOHIKeyboard.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IODeviceTreeSupport.h>
#pragma clang diagnostic pop

#include <IOKit/IOCommandGate.h>

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Definitions used to keep track of key state.   Key up/down state is tracked
// in a bit list.  Bits are set for key-down, and cleared for key-up.  The bit
// vector and macros for it's manipulation are defined here.
//

#define KBV_NUM_KEYCODES        512     // related with ADB_CONVERTER_LEN
#define KBV_BITS_PER_UNIT       32      // for UInt32
#define KBV_BITS_MASK           31
#define KBV_BITS_SHIFT          5       // 1<<5 == 32, for cheap divide
#define KBV_NUNITS ((KBV_NUM_KEYCODES + \
            (KBV_BITS_PER_UNIT-1))/KBV_BITS_PER_UNIT)

#define KBV_KEYDOWN(n) \
    (_keyBitVector)[((n)>>KBV_BITS_SHIFT)] |= (1 << ((n) & KBV_BITS_MASK))

#define KBV_KEYUP(n) \
    (_keyBitVector)[((n)>>KBV_BITS_SHIFT)] &= ~(1 << ((n) & KBV_BITS_MASK))

#define KBV_IS_KEYDOWN(n) \
    (((_keyBitVector)[((n)>>KBV_BITS_SHIFT)] & (1 << ((n) & KBV_BITS_MASK))) != 0)

#define KBV_NUM_SCANCODES       256

// Special bits for _PS2ToPS2Map

#define kBreaklessKey           0x01    // keys with this flag don't generate break codes

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2Keyboard Class Declaration
//

#define kPacketLength (2+6+8) // 2 bytes for key data, 6-bytes not used, 8 bytes for timestamp
#define kPacketKeyOffset 0
#define kPacketTimeOffset 8
#define kPacketKeyDataLength 2

class EXPORT ApplePS2Keyboard : public IOHIKeyboard
{
    typedef IOHIKeyboard super;
    OSDeclareDefaultStructors(ApplePS2Keyboard);

private:
    ApplePS2KeyboardDevice *    _device;
    UInt32                      _keyBitVector[KBV_NUNITS];
    UInt8                       _extendCount;
    RingBuffer<UInt8, kPacketLength*32> _ringBuffer;
    UInt8                       _lastdata;
    bool                        _interruptHandlerInstalled;
    bool                        _powerControlHandlerInstalled;
    UInt8                       _ledState;
    IOCommandGate*              _cmdGate;

    // for keyboard remapping
    UInt16                      _PS2modifierState;
    UInt16                      _PS2ToPS2Map[KBV_NUM_SCANCODES*2];
    UInt16                      _PS2flags[KBV_NUM_SCANCODES*2];
    UInt8                       _PS2ToADBMap[ADB_CONVERTER_LEN];
    UInt8                       _PS2ToADBMapMapped[ADB_CONVERTER_LEN];
    UInt32                      _fkeymode;
    bool                        _fkeymodesupported;
    OSArray*                    _keysStandard;
    OSArray*                    _keysSpecial;
    bool                        _swapcommandoption;
    int                         _logscancodes;
    UInt32                      _f12ejectdelay;
    enum { kTimerSleep, kTimerEject } _timerFunc;
    
    // dealing with sleep key delay
    IOTimerEventSource*         _sleepEjectTimer;
    UInt32                      _maxsleeppresstime;

    // ACPI support for panel brightness
    IOACPIPlatformDevice *      _panel;
    bool                        _panelNotified;
    bool                        _panelPrompt;
    IONotifier *                _panelNotifiers;

    IOACPIPlatformDevice *      _provider;
    int *                       _brightnessLevels;
    int                         _brightnessCount;

    // ACPI support for keyboard backlight
    int *                       _backlightLevels;
    int                         _backlightCount;
    
    // special hack for Envy brightness access, while retaining F2/F3 functionality
    bool                        _brightnessHack;
    
    // Toggle keyboard input along with touchpad when Windows+printscreen is pressed
    bool                        _disableInput;
    
    // macro processing
    OSData**                    _macroTranslation;
    OSData**                    _macroInversion;
    UInt8*                      _macroBuffer;
    int                         _macroMax;
    int                         _macroCurrent;
    uint64_t                    _macroMaxTime;
    IOTimerEventSource*         _macroTimer;
    
    // fix caps lock led
    bool                        _ignoreCapsLedChange;

    virtual bool dispatchKeyboardEventWithPacket(const UInt8* packet);
    virtual void setLEDs(UInt8 ledState);
    virtual void setKeyboardEnable(bool enable);
    virtual void initKeyboard();
    virtual void setDevicePowerState(UInt32 whatToDo);
    IORegistryEntry* getDevicebyAddress(IORegistryEntry *parent, int address);
    IOACPIPlatformDevice* getBrightnessPanel();
    static IOReturn _panelNotification(void *target, void *refCon, UInt32 messageType, IOService *provider, void *messageArgument, vm_size_t argSize);
    void modifyKeyboardBacklight(int adbKeyCode, bool goingDown);
    void modifyScreenBrightness(int adbKeyCode, bool goingDown);
    inline bool checkModifierState(UInt16 mask)
        { return mask == (_PS2modifierState & mask); }
    inline bool checkModifierStateAny(UInt16 mask)
        { return (_PS2modifierState & mask); }
    
    void loadCustomPS2Map(OSArray* pArray);
    void loadBreaklessPS2(OSDictionary* dict, const char* name);
    void loadCustomADBMap(OSDictionary* dict, const char* name);
    void setParamPropertiesGated(OSDictionary* dict);
    void onSleepEjectTimer(void);
    
    static OSData** loadMacroData(OSDictionary* dict, const char* name);
    static void freeMacroData(OSData** data);
    void onMacroTimer(void);
    bool invertMacros(const UInt8* packet);
    void dispatchInvertBuffer();
    static bool compareMacro(const UInt8* packet, const UInt8* data, int count);

protected:
    const unsigned char * defaultKeymapOfLength(UInt32 * length) override;
    void setAlphaLockFeedback(bool locked) override;
    void setNumLockFeedback(bool locked) override;
    UInt32 maxKeyCodes() override;
    inline void dispatchKeyboardEventX(unsigned int keyCode, bool goingDown, uint64_t time)
        { dispatchKeyboardEvent(keyCode, goingDown, *(AbsoluteTime*)&time); }
    inline void setTimerTimeout(IOTimerEventSource* timer, uint64_t time)
        { timer->setTimeout(*(AbsoluteTime*)&time); }
    inline void cancelTimer(IOTimerEventSource* timer)
        { timer->cancelTimeout(); }

public:
    bool init(OSDictionary * dict) override;
    ApplePS2Keyboard * probe(IOService * provider, SInt32 * score) override;

    bool start(IOService * provider) override;
    void stop(IOService * provider) override;

    virtual PS2InterruptResult interruptOccurred(UInt8 scanCode);
    virtual void packetReady();
    
    UInt32 deviceType() override;
    UInt32 interfaceID() override;
    
  	IOReturn setParamProperties(OSDictionary* dict) override;
    IOReturn setProperties (OSObject *props) override;
    
    IOReturn message(UInt32 type, IOService* provider, void* argument) override;
};

#endif /* _APPLEPS2KEYBOARD_H */
