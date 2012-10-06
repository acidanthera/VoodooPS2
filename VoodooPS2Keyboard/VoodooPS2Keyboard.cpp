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

#include <IOKit/assert.h>
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

#define super IOHIKeyboard

// get some keyboard id information from IOHIDFamily/IOHIDKeyboard.h and Gestalt.h
//#define APPLEPS2KEYBOARD_DEVICE_TYPE	205 // Generic ISO keyboard
#define APPLEPS2KEYBOARD_DEVICE_TYPE	3   // Unknown ANSI keyboard


OSDefineMetaClassAndStructors(ApplePS2Keyboard, IOHIKeyboard);

UInt32 ApplePS2Keyboard::deviceType()  {
    OSNumber    *xml_handlerID;
    UInt32      ret_id;

    if ( xml_handlerID = OSDynamicCast( OSNumber, getProperty("alt_handler_id")) )
        ret_id = xml_handlerID->unsigned32BitValue();
    else 
        ret_id = APPLEPS2KEYBOARD_DEVICE_TYPE;

    return ret_id; 
};

UInt32 ApplePS2Keyboard::interfaceID() { return NX_EVS_DEVICE_INTERFACE_ADB; };

UInt32 ApplePS2Keyboard::maxKeyCodes() { return NX_NUMKEYCODES; };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Keyboard::init(OSDictionary * properties)
{
    //
    // Initialize this object's minimal state.  This is invoked right after this
    // object is instantiated.
    //

    if (!super::init(properties))  return false;

    _device                    = 0;
    _extendCount               = 0;
    _interruptHandlerInstalled = false;
    _ledState                  = 0;

    for (int index = 0; index < KBV_NUNITS; index++)  _keyBitVector[index] = 0;

    //  This makes separate copy of ADB translation table.
    bcopy( PS2ToADBMap, _PS2ToADBMap, sizeof(UInt8) * ADB_CONVERTER_LEN);

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2Keyboard * ApplePS2Keyboard::probe(IOService * provider, SInt32 * score)
{
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

    // (free the request)
    success = (request->commandsCount <= 2); // Force to succeed?
    device->freeRequest(request);

    return (success) ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Keyboard::start(IOService * provider)
{
    OSBoolean *xml_swap_CAPSLOCK_CTRL;
    OSBoolean *xml_swap_ALT_WIN;
    OSBoolean *xml_use_right_modifier_into_HANGUL_HANJA;
    OSBoolean *xml_make_APP_into_RWIN;
    OSBoolean *xml_swap_GRAVE_EUROPE2;
    OSBoolean *xml_make_APP_into_AppleFN;
    
    //
    // The driver has been instructed to start.   This is called after a
    // successful attach.
    //

    if (!super::start(provider))  return false;

    //
    // Maintain a pointer to and retain the provider object.
    //

    _device = (ApplePS2KeyboardDevice *)provider;
    _device->retain();


    //
    // Configure user preferences from Info.plist
    //

    xml_swap_CAPSLOCK_CTRL = OSDynamicCast( OSBoolean, getProperty("Swap capslock and left control"));
    if (xml_swap_CAPSLOCK_CTRL) {
        if ( xml_swap_CAPSLOCK_CTRL->getValue()) {
            char temp = _PS2ToADBMap[0x3a];
            _PS2ToADBMap[0x3a] = _PS2ToADBMap[0x1d];
            _PS2ToADBMap[0x1d] = temp;
        }
    }

    xml_swap_ALT_WIN = OSDynamicCast( OSBoolean, getProperty("Swap command and option"));
    if (xml_swap_ALT_WIN) {
        if ( xml_swap_ALT_WIN->getValue()) {
            char temp = _PS2ToADBMap[0x38];
            _PS2ToADBMap[0x38] = _PS2ToADBMap[0x15b];
            _PS2ToADBMap[0x15b] = temp;

            temp = _PS2ToADBMap[0x138];
            _PS2ToADBMap[0x138] = _PS2ToADBMap[0x15c];
            _PS2ToADBMap[0x15c] = temp;
        }
    }

    xml_use_right_modifier_into_HANGUL_HANJA = OSDynamicCast( \
            OSBoolean, getProperty("Make right modifier keys into Hangul and Hanja"));
    if (xml_use_right_modifier_into_HANGUL_HANJA) {
        if ( xml_use_right_modifier_into_HANGUL_HANJA->getValue()) {
            _PS2ToADBMap[0x138] = _PS2ToADBMap[0xf2];    // Right alt becomes Hangul
            _PS2ToADBMap[0x11d] = _PS2ToADBMap[0xf1];    // Right control becomes Hanja
        }
    }

    xml_make_APP_into_RWIN = OSDynamicCast( OSBoolean, getProperty("Make Application key into right windows"));
    if (xml_make_APP_into_RWIN) {
        if ( xml_make_APP_into_RWIN->getValue())
            _PS2ToADBMap[0x15d] = _PS2ToADBMap[0x15c];
    }

    // not implemented yet.
    // Apple Fn key works well, but no combined key action was made.
    xml_make_APP_into_AppleFN = OSDynamicCast( OSBoolean, getProperty("Make Application key into Apple Fn key"));
    if (xml_make_APP_into_AppleFN) {
        if ( xml_make_APP_into_AppleFN->getValue()) {
            _PS2ToADBMap[0x15d] = 0x3f;
        }
    }

    // ISO specific mapping to match ADB keyboards
    // This should really be done in the keymaps.
    xml_swap_GRAVE_EUROPE2 = OSDynamicCast( OSBoolean, getProperty("Use ISO layout keyboard"));
    if (xml_swap_GRAVE_EUROPE2) {
        if ( xml_swap_GRAVE_EUROPE2->getValue()) {
            char temp = _PS2ToADBMap[0x29];             //Grave '~'
            _PS2ToADBMap[0x29] = _PS2ToADBMap[0x56];    //Europe2 '¤º'
            _PS2ToADBMap[0x56] = temp;
        }
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

    return true;
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
    // Release the pointer to the provider object.
    //

    _device->release();
    _device = 0;

    super::stop(provider);
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Keyboard::dispatchKeyboardEventWithScancode(UInt8 scanCode)
{
    //
    // Parses the given scan code, updating all necessary internal state, and
    // should a new key be detected, the key event is dispatched.
    //
    // Returns true if a key event was indeed dispatched.
    //

    unsigned int    keyCode;
    bool            goingDown;
    uint64_t        now;

    #ifdef DEBUG
    IOLog("%s: PS/2 scancode 0x%x\n", getName(), scanCode);
    #endif

    //
    // See if this scan code introduces an extended key sequence.  If so, note
    // it and then return.  Next time we get a key we'll finish the sequence.
    //

    if (scanCode == kSC_Extend) {
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
    if (scanCode == kSC_Pause) {
        _extendCount = 2;
        return false;
    }

    keyCode = scanCode & ~kSC_UpBit;

    //
    // Convert the scan code into a key code index.
    //
    // From "The Undocumented PC" chapter 8, The Keyboard System some
    // keyboard scan codes are single byte, some are multi-byte.
    // Scancodes from running showkey -s (under Linux) for extra keys on keyboard
    // Refer to the conversion table in defaultKeymapOfLength 
    // and the conversion table in ApplePS2ToADBMap.h.
    //
    if (_extendCount == 0) {
        // LANG1(Hangul) and LANG2(Hanja) make one event only when the key was pressed.
        // Make key-down and key-up event ADB event
        if (scanCode == 0xf2 || scanCode == 0xf1) {
                clock_get_uptime(&now);
                dispatchKeyboardEvent( PS2ToADBMap[scanCode], true, *((AbsoluteTime*)&now) );
                clock_get_uptime(&now);
                dispatchKeyboardEvent( PS2ToADBMap[scanCode], false, *((AbsoluteTime*)&now) );
                return true;
        }

        /* if you need ohter key control logic, use below block.
        switch (keyCode) {
            default:
        } */
    } else {
        _extendCount--;
        if (_extendCount)  return false;

        //
        // Convert certain extended codes on the PC keyboard into a key code index.
        //
        switch (keyCode) {
           case 0x5f:                          // sleep
                       keyCode = 0;
                       if (!(scanCode & kSC_UpBit)) {
                           IOPMrootDomain * rootDomain = getPMRootDomain();
                           if (rootDomain)
                               rootDomain->receivePowerNotification( kIOPMSleepNow );
                       }
                       break;			
				
			case 0x08: //samsung brightness up
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x90, true, *((AbsoluteTime*)&now) );
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x90, false, *((AbsoluteTime*)&now) );
				IOLog("Faking key release for brightness up\n");
						return false;
				
			case 0x06: //dell brightness up
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x90, true, *((AbsoluteTime*)&now) );
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x90, false, *((AbsoluteTime*)&now) );
				IOLog("Faking key release for brightness up\n");
				return false;
				
				
			case 0x05: //dell brightness down
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x91, true, *((AbsoluteTime*)&now) );
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x91, false, *((AbsoluteTime*)&now) );
				IOLog("Faking key release for brightness up\n");
				return false;
				
			case 0x09: //samsung brightness down
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x91, true, *((AbsoluteTime*)&now) );
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x91, false, *((AbsoluteTime*)&now) );
				IOLog("Faking key release for brightness up\n");
				return false;

/*				
			case 0x59: //acer brightness up
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x90, true, *((AbsoluteTime*)&now) );
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x90, false, *((AbsoluteTime*)&now) );
				IOLog("Faking key release for brightness up\n");
				_extendCount = 2;
				return true;
				
								
			case 0x6f: //acer brightness down
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x91, true, *((AbsoluteTime*)&now) );
				clock_get_uptime(&now);
				dispatchKeyboardEvent( 0x91, false, *((AbsoluteTime*)&now) );
				IOLog("Faking key release for brightness up\n");
				_extendCount = 2;
				return true;		
				
			case 0xd9: return false;
			case 0xef: return false;	
*/				
            case 0x2a: return false;            // header or trailer for PrintScreen
            default:
                break;
        }

        // extended code index start after the single code's
        keyCode |= ADB_CONVERTER_EX_START;
    }

    //
    // Update our key bit vector, which maintains the up/down status of all keys.
    //

    goingDown = !(scanCode & kSC_UpBit);

    if (goingDown) {
        //
        // Verify that this is not an autorepeated key -- discard it if it is.
        //

        if (KBV_IS_KEYDOWN(keyCode, _keyBitVector))  return false;

        KBV_KEYDOWN(keyCode, _keyBitVector);
    }
    else {
        KBV_KEYUP(keyCode, _keyBitVector);
    }

    //
    // We have a valid key event -- dispatch it to our superclass.
    //

    clock_get_uptime(&now);

    UInt8 adbKeyCode = _PS2ToADBMap[keyCode];

#ifdef DEBUG
    if (adbKeyCode == DEADKEY)
        IOLog("%s: Unknown ADB key for PS2 scancode: 0x%x\n", getName(), scanCode);
    else
        IOLog("%s: ADB key code 0x%x %s\n", getName(), adbKeyCode, goingDown?"down":"up");
#endif

    dispatchKeyboardEvent( adbKeyCode,
            /*direction*/ goingDown,
            /*timeStamp*/ *((AbsoluteTime*)&now) );
/*
	if (adbKeyCode==0x90)
	{
        if (keyCode == 0x108 || keyCode == 0x106) 
		{
			clock_get_uptime(&now);
			dispatchKeyboardEvent( 0x90, false, *((AbsoluteTime*)&now) );
			IOLog("Faking key release for brightness up\n");

		}
	}
	
	if (adbKeyCode==0x91)
	{
        if (keyCode == 0x109 || keyCode == 0x105) 
		{
			clock_get_uptime(&now);
			dispatchKeyboardEvent( 0x91, false, *((AbsoluteTime*)&now) );
			IOLog("Faking key release for brightness down\n");
		}
	}	
 */
//	IOLog("%s: PS/2 scancode 0x%x\n", getName(), keyCode);


	
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
