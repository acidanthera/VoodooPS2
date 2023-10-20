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

#ifndef _APPLEPS2TOADBMAP_H
#define _APPLEPS2TOADBMAP_H

#include <IOKit/hid/IOHIDUsageTables.h>
#include <IOKit/hid/AppleHIDUsageTables.h>

#define PROBOOK

#define DEADKEY                 0x00
#define DEADKEY_EXTENDED        { 0, 0 }

#define BRIGHTNESS_DOWN { kHIDPage_AppleVendorTopCase, kHIDUsage_AV_TopCase_BrightnessDown }
#define BRIGHTNESS_UP   { kHIDPage_AppleVendorTopCase, kHIDUsage_AV_TopCase_BrightnessUp   }

#define ADB_CONVERTER_LEN       256     // 0x00~0xff : normal key , 0x100~0x1ff : extended key
#define ADB_CONVERTER_EX_START  256

// PS/2 scancode reference : USB HID to PS/2 Scan Code Translation Table PS/2 Set 1 columns
// http://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/translate.pdf
static const UInt8 PS2ToHIDMapStock[ADB_CONVERTER_LEN] =
{
/*  HID                            AT  ANSI Key-Legend
    ======================== */
    DEADKEY,                    // 00
    kHIDUsage_KeyboardEscape,   // 01  Escape
    kHIDUsage_Keyboard1,        // 02  1!
    kHIDUsage_Keyboard2,        // 03  2@
    kHIDUsage_Keyboard3,        // 04  3#
    kHIDUsage_Keyboard4,        // 05  4$
    kHIDUsage_Keyboard5,        // 06  5%
    kHIDUsage_Keyboard6,        // 07  6^
    kHIDUsage_Keyboard7,        // 08  7&
    kHIDUsage_Keyboard8,        // 09  8*
    kHIDUsage_Keyboard9,        // 0a  9(
    kHIDUsage_Keyboard0,        // 0b  0)
    kHIDUsage_KeyboardHyphen,   // 0c  -_
    kHIDUsage_KeyboardEqualSign,// 0d  =+
    kHIDUsage_KeyboardDeleteOrBackspace, // 0e  Backspace
    kHIDUsage_KeyboardTab,      // 0f  Tab
    kHIDUsage_KeyboardQ,        // 10  qQ
    kHIDUsage_KeyboardW,        // 11  wW
    kHIDUsage_KeyboardE,        // 12  eE
    kHIDUsage_KeyboardR,        // 13  rR
    kHIDUsage_KeyboardT,        // 14  tT
    kHIDUsage_KeyboardY,        // 15  yY
    kHIDUsage_KeyboardU,        // 16  uU
    kHIDUsage_KeyboardI,        // 17  iI
    kHIDUsage_KeyboardO,        // 18  oO
    kHIDUsage_KeyboardP,        // 19  pP
    kHIDUsage_KeyboardOpenBracket,   // 1a  [{
    kHIDUsage_KeyboardCloseBracket,  // 1b  ]}
    kHIDUsage_KeyboardReturnOrEnter, // 1c  Return
    kHIDUsage_KeyboardLeftControl,   // 1d  Left Control
    kHIDUsage_KeyboardA,        // 1e  aA
    kHIDUsage_KeyboardS,        // 1f  sS
    kHIDUsage_KeyboardD,        // 20  dD
    kHIDUsage_KeyboardF,        // 21  fF
    kHIDUsage_KeyboardG,        // 22  gG
    kHIDUsage_KeyboardH,        // 23  hH
    kHIDUsage_KeyboardJ,        // 24  jJ
    kHIDUsage_KeyboardK,        // 25  kK
    kHIDUsage_KeyboardL,        // 26  lL
    kHIDUsage_KeyboardSemicolon,// 27  ;:
    kHIDUsage_KeyboardQuote,    // 28  '"
    kHIDUsage_KeyboardGraveAccentAndTilde,  // 29  `~
    kHIDUsage_KeyboardLeftShift,            // 2a  Left Shift
    kHIDUsage_KeyboardBackslash,            // 2b  \| , Europe 1(ISO)
    kHIDUsage_KeyboardZ,        // 2c  zZ
    kHIDUsage_KeyboardX,        // 2d  xX
    kHIDUsage_KeyboardC,        // 2e  cC
    kHIDUsage_KeyboardV,        // 2f  vV
    kHIDUsage_KeyboardB,        // 30  bB
    kHIDUsage_KeyboardN,        // 31  nN
    kHIDUsage_KeyboardM,        // 32  mM
    kHIDUsage_KeyboardComma,    // 33  ,<
    kHIDUsage_KeyboardPeriod,   // 34  .>
    kHIDUsage_KeyboardSlash,    // 35  /?
    kHIDUsage_KeyboardRightShift,  // 36  Right Shift
    kHIDUsage_KeypadAsterisk,   // 37  Keypad *
    kHIDUsage_KeyboardLeftAlt,  // 38  Left Alt
    kHIDUsage_KeyboardSpacebar, // 39  Space
    kHIDUsage_KeyboardCapsLock, // 3a  Caps Lock
    kHIDUsage_KeyboardF1,       // 3b  F1
    kHIDUsage_KeyboardF2,       // 3c  F2
    kHIDUsage_KeyboardF3,       // 3d  F3
    kHIDUsage_KeyboardF4,       // 3e  F4
    kHIDUsage_KeyboardF5,       // 3f  F5
    kHIDUsage_KeyboardF6,       // 40  F6
    kHIDUsage_KeyboardF7,       // 41  F7
    kHIDUsage_KeyboardF8,       // 42  F8
    kHIDUsage_KeyboardF9,       // 43  F9
    kHIDUsage_KeyboardF10,      // 44  F10
    kHIDUsage_KeypadNumLock,    // 45  Num Lock
    kHIDUsage_KeyboardScrollLock,  // 46  Scroll Lock
    kHIDUsage_Keypad7,          // 47  Keypad 7 Home
    kHIDUsage_Keypad8,          // 48  Keypad 8 Up
    kHIDUsage_Keypad9,          // 49  Keypad 9 PageUp
    kHIDUsage_KeypadHyphen,     // 4a  Keypad -
    kHIDUsage_Keypad4,          // 4b  Keypad 4 Left
    kHIDUsage_Keypad5,          // 4c  Keypad 5
    kHIDUsage_Keypad6,          // 4d  Keypad 6 Right
    kHIDUsage_KeypadPlus,       // 4e  Keypad +
    kHIDUsage_Keypad1,          // 4f  Keypad 1 End
    kHIDUsage_Keypad2,          // 50  Keypad 2 Down
    kHIDUsage_Keypad3,          // 51  Keypad 3 PageDn
    kHIDUsage_Keypad0,          // 52  Keypad 0 Insert
    kHIDUsage_KeypadPeriod,     // 53  Keypad . Delete
    kHIDUsage_KeyboardSysReqOrAttention,  // 54  SysReq
    0x46,   // 55
    kHIDUsage_KeyboardNonUSBackslash,  // 56  Europe 2(ISO)
    kHIDUsage_KeyboardF11,      // 57  F11
    kHIDUsage_KeyboardF12,      // 58  F12
    kHIDUsage_KeypadEqualSign,  // 59  Keypad =
    DEADKEY,// 5a
    DEADKEY,// 5b
    kHIDUsage_KeyboardInternational6,  // 5c  Keyboard Int'l 6 (PC9800 Keypad , )
    DEADKEY,// 5d
    DEADKEY,// 5e
    DEADKEY,// 5f
    DEADKEY,// 60
    DEADKEY,// 61
    DEADKEY,// 62
    DEADKEY,// 63
    kHIDUsage_KeyboardF13,      // 64  F13
    kHIDUsage_KeyboardF14,      // 65  F14
    kHIDUsage_KeyboardF15,      // 66  F15
    kHIDUsage_KeyboardF16,      // 67  F16
    kHIDUsage_KeyboardF17,      // 68  F17
    kHIDUsage_KeyboardF18,      // 69  F18
    kHIDUsage_KeyboardF19,      // 6a  F19
    kHIDUsage_KeyboardF20,      // 6b  F20
    kHIDUsage_KeyboardF21,      // 6c  F21
    kHIDUsage_KeyboardF22,      // 6d  F22
    kHIDUsage_KeyboardF23,      // 6e  F23
    DEADKEY,// 6f
    kHIDUsage_KeyboardInternational2,  // 70  Keyboard Intl'2 (Japanese Katakana/Hiragana)
    DEADKEY,// 71
    DEADKEY,// 72
    kHIDUsage_KeyboardInternational1,  // 73  Keyboard Int'l 1 (Japanese Ro)
    DEADKEY,// 74
    DEADKEY,// 75
    kHIDUsage_KeyboardF24,      // 76  F24 , Keyboard Lang 5 (Japanese Zenkaku/Hankaku)
    kHIDUsage_KeyboardLANG4,    // 77  Keyboard Lang 4 (Japanese Hiragana)
    kHIDUsage_KeyboardLANG3,    // 78  Keyboard Lang 3 (Japanese Katakana)
    kHIDUsage_KeyboardInternational4,  // 79  Keyboard Int'l 4 (Japanese Henkan)
    DEADKEY,// 7a
    kHIDUsage_KeyboardInternational5,  // 7b  Keyboard Int'l 5 (Japanese Muhenkan)
    DEADKEY,// 7c
    kHIDUsage_KeyboardInternational3,  // 7d  Keyboard Int'l 3 (Japanese Yen)
    kHIDUsage_KeypadComma,      // 7e  Keypad , (Brazilian Keypad .)
    DEADKEY,// 7f 
    DEADKEY,// 80 
    DEADKEY,// 81 
    DEADKEY,// 82 
    DEADKEY,// 83 
    DEADKEY,// 84 
    DEADKEY,// 85 
    DEADKEY,// 86 
    DEADKEY,// 87 
    DEADKEY,// 88 
    DEADKEY,// 89 
    DEADKEY,// 8a 
    DEADKEY,// 8b 
    DEADKEY,// 8c 
    DEADKEY,// 8d 
    DEADKEY,// 8e 
    DEADKEY,// 8f 
    DEADKEY,// 90 
    DEADKEY,// 91 
    DEADKEY,// 92 
    DEADKEY,// 93 
    DEADKEY,// 94 
    DEADKEY,// 95 
    DEADKEY,// 96 
    DEADKEY,// 97 
    DEADKEY,// 98 
    DEADKEY,// 99 
    DEADKEY,// 9a 
    DEADKEY,// 9b 
    DEADKEY,// 9c 
    DEADKEY,// 9d 
    DEADKEY,// 9e 
    DEADKEY,// 9f 
    DEADKEY,// a0 
    DEADKEY,// a1 
    DEADKEY,// a2 
    DEADKEY,// a3 
    DEADKEY,// a4 
    DEADKEY,// a5 
    DEADKEY,// a6 
    DEADKEY,// a7 
    DEADKEY,// a8 
    DEADKEY,// a9 
    DEADKEY,// aa 
    DEADKEY,// ab 
    DEADKEY,// ac 
    DEADKEY,// ad 
    DEADKEY,// ae 
    DEADKEY,// af 
    DEADKEY,// b0 
    DEADKEY,// b1 
    DEADKEY,// b2 
    DEADKEY,// b3 
    DEADKEY,// b4 
    DEADKEY,// b5 
    DEADKEY,// b6 
    DEADKEY,// b7 
    DEADKEY,// b8 
    DEADKEY,// b9 
    DEADKEY,// ba 
    DEADKEY,// bb 
    DEADKEY,// bc 
    DEADKEY,// bd 
    DEADKEY,// be 
    DEADKEY,// bf 
    DEADKEY,// c0 
    DEADKEY,// c1 
    DEADKEY,// c2 
    DEADKEY,// c3 
    DEADKEY,// c4 
    DEADKEY,// c5 
    DEADKEY,// c6 
    DEADKEY,// c7 
    DEADKEY,// c8 
    DEADKEY,// c9 
    DEADKEY,// ca 
    DEADKEY,// cb 
    DEADKEY,// cc 
    DEADKEY,// cd 
    DEADKEY,// ce 
    DEADKEY,// cf 
    DEADKEY,// d0 
    DEADKEY,// d1 
    DEADKEY,// d2 
    DEADKEY,// d3 
    DEADKEY,// d4 
    DEADKEY,// d5 
    DEADKEY,// d6 
    DEADKEY,// d7 
    DEADKEY,// d8 
    DEADKEY,// d9 
    DEADKEY,// da 
    DEADKEY,// db 
    DEADKEY,// dc 
    DEADKEY,// dd 
    DEADKEY,// de 
    DEADKEY,// df 
    DEADKEY,// e0 
    DEADKEY,// e1 
    DEADKEY,// e2 
    DEADKEY,// e3 
    DEADKEY,// e4 
    DEADKEY,// e5 
    DEADKEY,// e6 
    DEADKEY,// e7 
    DEADKEY,// e8 
    DEADKEY,// e9 
    DEADKEY,// ea 
    DEADKEY,// eb 
    DEADKEY,// ec 
    DEADKEY,// ed 
    DEADKEY,// ee 
    DEADKEY,// ef 
    DEADKEY,// f0 
    kHIDUsage_KeyboardLANG2,    // f1*  Keyboard Lang 2 (Korean Hanja)
    kHIDUsage_KeyboardLANG1,    // f2*  Keyboard Lang 1 (Korean Hangul)
    DEADKEY,// f3 
    DEADKEY,// f4 
    DEADKEY,// f5 
    DEADKEY,// f6 
    DEADKEY,// f7 
    DEADKEY,// f8 
    DEADKEY,// f9 
    DEADKEY,// fa 
    DEADKEY,// fb 
    DEADKEY,// fc 
    DEADKEY,// fd 
    DEADKEY,// fe 
    DEADKEY,// ff
};

struct VoodooPS2HidElement {
    uint16_t usagePage;
    uint16_t usage;
};

#define KEY(a) { kHIDPage_KeyboardOrKeypad, (a)}
#define MEDIA(a) { kHIDPage_Consumer, (a)}

static const VoodooPS2HidElement ExtendedPS2ToHIDStockMap[ADB_CONVERTER_LEN] {
    DEADKEY_EXTENDED,// e0 00
    DEADKEY_EXTENDED,// e0 01
    DEADKEY_EXTENDED,// e0 02
    DEADKEY_EXTENDED,// e0 03
    DEADKEY_EXTENDED,// e0 04
    BRIGHTNESS_DOWN, // e0 05 dell down
    BRIGHTNESS_UP,   // e0 06 dell up
    DEADKEY_EXTENDED,// e0 07
#ifndef PROBOOK
    BRIGHTNESS_UP,      // e0 08 samsung up
    BRIGHTNESS_DOWN,    // e0 09 samsung down
#else
    DEADKEY_EXTENDED,// e0 08
    { kHIDPage_AppleVendorKeyboard, kHIDUsage_AppleVendorKeyboard_Launchpad },   // e0 09 Launchpad (hp Fn+F6)
#endif
    { kHIDPage_AppleVendorKeyboard, kHIDUsage_AppleVendorKeyboard_Expose_All },   // e0 0a Mission Control (hp Fn+F5)
    DEADKEY_EXTENDED,// e0 0b
    DEADKEY_EXTENDED,// e0 0c
    DEADKEY_EXTENDED,// e0 0d
    DEADKEY_EXTENDED,// e0 0e
    DEADKEY_EXTENDED,// e0 0f
    MEDIA ( kHIDUsage_Csmr_ScanPreviousTrack ),   // e0 10  Scan Previous Track (hp Fn+F10)
    DEADKEY_EXTENDED,// e0 11
    BRIGHTNESS_DOWN,    // e0 12 hp down (Fn+F2)
    DEADKEY_EXTENDED,// e0 13
    DEADKEY_EXTENDED,// e0 14
    DEADKEY_EXTENDED,// e0 15
    DEADKEY_EXTENDED,// e0 16
    BRIGHTNESS_UP,      // e0 17 hp up (Fn+F3)
    DEADKEY_EXTENDED,// e0 18
    MEDIA ( kHIDUsage_Csmr_ScanNextTrack ),   // e0 19  Scan Next Track (hp Fn+F12)
    DEADKEY_EXTENDED,// e0 1a
    DEADKEY_EXTENDED,// e0 1b
    KEY( kHIDUsage_KeypadEnter ),   // e0 1c  Keypad Enter
    KEY( kHIDUsage_KeyboardRightControl ),   // e0 1d  Right Control
    DEADKEY_EXTENDED,// e0 1e
    DEADKEY_EXTENDED,// e0 1f
    MEDIA ( kHIDUsage_Csmr_Mute ),   // e0 20  Mute (hp Fn+F7)
    MEDIA ( kHIDUsage_Csmr_ALCalculator ),// e0 21  Calculator
    MEDIA ( kHIDUsage_Csmr_PlayOrPause ),   // e0 22  Play/Pause (hp Fn+F11)
    DEADKEY_EXTENDED,// e0 23
    MEDIA ( kHIDUsage_Csmr_Stop ),// e0 24  Stop
    DEADKEY_EXTENDED,// e0 25
    DEADKEY_EXTENDED,// e0 26
    DEADKEY_EXTENDED,// e0 27
    DEADKEY_EXTENDED,// e0 28
    DEADKEY_EXTENDED,// e0 29
    DEADKEY_EXTENDED,// e0 2a
    DEADKEY_EXTENDED,// e0 2b
    DEADKEY_EXTENDED,// e0 2c
    DEADKEY_EXTENDED,// e0 2d
    MEDIA ( kHIDUsage_Csmr_VolumeDecrement ),   // e0 2e  Volume Down (hp Fn+F8)
    DEADKEY_EXTENDED,// e0 2f
    MEDIA ( kHIDUsage_Csmr_VolumeIncrement ),   // e0 30  Volume Up (hp Fn+F9)
    DEADKEY_EXTENDED,// e0 31
    MEDIA ( kHIDUsage_Csmr_ACHome ),// e0 32  WWW Home
    DEADKEY_EXTENDED,// e0 33
    DEADKEY_EXTENDED,// e0 34
    KEY ( kHIDUsage_KeypadSlash ),          // e0 35  Keypad /
    DEADKEY_EXTENDED,// e0 36
    KEY ( kHIDUsage_KeyboardPrintScreen ),  // e0 37  Print Screen
    KEY ( kHIDUsage_KeyboardRightAlt ),     // e0 38  Right Alt
    DEADKEY_EXTENDED,// e0 39
    DEADKEY_EXTENDED,// e0 3a
    DEADKEY_EXTENDED,// e0 3b
    DEADKEY_EXTENDED,// e0 3c
    DEADKEY_EXTENDED,// e0 3d
    DEADKEY_EXTENDED,// e0 3e
    DEADKEY_EXTENDED,// e0 3f
    DEADKEY_EXTENDED,// e0 40
    DEADKEY_EXTENDED,// e0 41
    DEADKEY_EXTENDED,// e0 42
    DEADKEY_EXTENDED,// e0 43
    DEADKEY_EXTENDED,// e0 44
    KEY ( kHIDUsage_KeyboardPause ),    // e0 45* Pause
    KEY ( kHIDUsage_KeyboardPause ),    // e0 46* Break(Ctrl-Pause)
    KEY ( kHIDUsage_KeyboardHome ),   // e0 47  Home
    KEY ( kHIDUsage_KeyboardUpArrow ),   // e0 48  Up Arrow
    KEY ( kHIDUsage_KeyboardPageUp ),   // e0 49  Page Up
    DEADKEY_EXTENDED,// e0 4a
    KEY ( kHIDUsage_KeyboardLeftArrow ),   // e0 4b  Left Arrow
    DEADKEY_EXTENDED,// e0 4c
    KEY ( kHIDUsage_KeyboardRightArrow ),   // e0 4d  Right Arrow
    BRIGHTNESS_UP,      // e0 4e acer up
    KEY ( kHIDUsage_KeyboardEnd ),   // e0 4f  End
    KEY ( kHIDUsage_KeyboardDownArrow ),   // e0 50  Down Arrow
    KEY ( kHIDUsage_KeyboardPageDown ),   // e0 51  Page Down
    MEDIA ( kHIDUsage_Csmr_Eject ),   // e0 52  Insert = Eject
    KEY ( kHIDUsage_KeyboardDeleteForward ),   // e0 53  Delete
    DEADKEY_EXTENDED,// e0 54
    DEADKEY_EXTENDED,// e0 55
    DEADKEY_EXTENDED,// e0 56
    DEADKEY_EXTENDED,// e0 57
    DEADKEY_EXTENDED,// e0 58
    BRIGHTNESS_UP,      // e0 59 acer up for my acer
    DEADKEY_EXTENDED,// e0 5a
    KEY ( kHIDUsage_KeyboardLeftGUI ),   // e0 5b  Left GUI(Windows)
    KEY ( kHIDUsage_KeyboardRightGUI ),   // e0 5c  Right GUI(Windows)
    KEY ( kHIDUsage_KeyboardApplication ),   // e0 5d  App( Windows context menu key )
    { kHIDPage_GenericDesktop, kHIDUsage_GD_SystemPowerDown },   // e0 5e  System Power / Keyboard Power
    { kHIDPage_GenericDesktop, kHIDUsage_GD_SystemSleep },// e0 5f  System Sleep (hp Fn+F1)
    DEADKEY_EXTENDED,// e0 60
    DEADKEY_EXTENDED,// e0 61
    DEADKEY_EXTENDED,// e0 62
    { kHIDPage_GenericDesktop, kHIDUsage_GD_SystemWakeUp },// e0 63  System Wake
    DEADKEY_EXTENDED,// e0 64
    MEDIA ( kHIDUsage_Csmr_ACSearch ),// e0 65  WWW Search
    MEDIA ( kHIDUsage_Csmr_ACBookmarks ),// e0 66  WWW Favorites
    MEDIA ( kHIDUsage_Csmr_ACRefresh ),// e0 67  WWW Refresh
    MEDIA ( kHIDUsage_Csmr_ACStop ),// e0 68  WWW Stop
    MEDIA ( kHIDUsage_Csmr_ACForward ),// e0 69  WWW Forward
    MEDIA ( kHIDUsage_Csmr_ACBack ),// e0 6a  WWW Back
    DEADKEY_EXTENDED,// e0 6b  My Computer
    MEDIA ( kHIDUsage_Csmr_ALEmailReader ),// e0 6c  Mail
    DEADKEY_EXTENDED,// e0 6d  Media Select
#ifndef PROBOOK
    BRIGHTNESS_UP,      // e0 6e acer up
    BRIGHTNESS_DOWN,    // e0 6f acer down
#else
    MEDIA ( 0x00 ),   // e0 6e  Video Mirror = hp Fn+F4
    DEADKEY_EXTENDED,// e0 6f  Fn+Home
#endif
    DEADKEY_EXTENDED,// e0 70
    DEADKEY_EXTENDED,// e0 71
    DEADKEY_EXTENDED,// e0 72
    DEADKEY_EXTENDED,// e0 73
    DEADKEY_EXTENDED,// e0 74
    DEADKEY_EXTENDED,// e0 75
    DEADKEY_EXTENDED,// e0 76
#ifndef PROBOOK
    BRIGHTNESS_DOWN,    // e0 77 lg down
    BRIGHTNESS_UP,      // e0 78 lg up
#else
    DEADKEY_EXTENDED,// e0 77
    DEADKEY_EXTENDED,// e0 78 WiFi on/off button on HP ProBook
#endif
    DEADKEY_EXTENDED,// e0 79
    DEADKEY_EXTENDED,// e0 7a
    DEADKEY_EXTENDED,// e0 7b
    DEADKEY_EXTENDED,// e0 7c
    DEADKEY_EXTENDED,// e0 7d
    DEADKEY_EXTENDED,// e0 7e
    DEADKEY_EXTENDED,// e0 7f
    DEADKEY_EXTENDED,// e0 80
    DEADKEY_EXTENDED,// e0 81
    DEADKEY_EXTENDED,// e0 82
    DEADKEY_EXTENDED,// e0 83
    DEADKEY_EXTENDED,// e0 84
    DEADKEY_EXTENDED,// e0 85
    DEADKEY_EXTENDED,// e0 86
    DEADKEY_EXTENDED,// e0 87
    DEADKEY_EXTENDED,// e0 88
    DEADKEY_EXTENDED,// e0 89
    DEADKEY_EXTENDED,// e0 8a
    DEADKEY_EXTENDED,// e0 8b
    DEADKEY_EXTENDED,// e0 8c
    DEADKEY_EXTENDED,// e0 8d
    DEADKEY_EXTENDED,// e0 8e
    DEADKEY_EXTENDED,// e0 8f
    DEADKEY_EXTENDED,// e0 90
    DEADKEY_EXTENDED,// e0 91
    DEADKEY_EXTENDED,// e0 92
    DEADKEY_EXTENDED,// e0 93
    DEADKEY_EXTENDED,// e0 94
    DEADKEY_EXTENDED,// e0 95
    DEADKEY_EXTENDED,// e0 96
    DEADKEY_EXTENDED,// e0 97
    DEADKEY_EXTENDED,// e0 98
    DEADKEY_EXTENDED,// e0 99
    DEADKEY_EXTENDED,// e0 9a
    DEADKEY_EXTENDED,// e0 9b
    DEADKEY_EXTENDED,// e0 9c
    DEADKEY_EXTENDED,// e0 9d
    DEADKEY_EXTENDED,// e0 9e
    DEADKEY_EXTENDED,// e0 9f
    DEADKEY_EXTENDED,// e0 a0
    DEADKEY_EXTENDED,// e0 a1
    DEADKEY_EXTENDED,// e0 a2
    DEADKEY_EXTENDED,// e0 a3
    DEADKEY_EXTENDED,// e0 a4
    DEADKEY_EXTENDED,// e0 a5
    DEADKEY_EXTENDED,// e0 a6
    DEADKEY_EXTENDED,// e0 a7
    DEADKEY_EXTENDED,// e0 a8
    DEADKEY_EXTENDED,// e0 a9
    DEADKEY_EXTENDED,// e0 aa
    DEADKEY_EXTENDED,// e0 ab
    DEADKEY_EXTENDED,// e0 ac
    DEADKEY_EXTENDED,// e0 ad
    DEADKEY_EXTENDED,// e0 ae
    DEADKEY_EXTENDED,// e0 af
    DEADKEY_EXTENDED,// e0 b0
    DEADKEY_EXTENDED,// e0 b1
    DEADKEY_EXTENDED,// e0 b2
    DEADKEY_EXTENDED,// e0 b3
    DEADKEY_EXTENDED,// e0 b4
    DEADKEY_EXTENDED,// e0 b5
    DEADKEY_EXTENDED,// e0 b6
    DEADKEY_EXTENDED,// e0 b7
    DEADKEY_EXTENDED,// e0 b8
    DEADKEY_EXTENDED,// e0 b9
    DEADKEY_EXTENDED,// e0 ba
    DEADKEY_EXTENDED,// e0 bb
    DEADKEY_EXTENDED,// e0 bc
    DEADKEY_EXTENDED,// e0 bd
    DEADKEY_EXTENDED,// e0 be
    DEADKEY_EXTENDED,// e0 bf
    DEADKEY_EXTENDED,// e0 c0
    DEADKEY_EXTENDED,// e0 c1
    DEADKEY_EXTENDED,// e0 c2
    DEADKEY_EXTENDED,// e0 c3
    DEADKEY_EXTENDED,// e0 c4
    DEADKEY_EXTENDED,// e0 c5
    DEADKEY_EXTENDED,// e0 c6
    DEADKEY_EXTENDED,// e0 c7
    DEADKEY_EXTENDED,// e0 c8
    DEADKEY_EXTENDED,// e0 c9
    DEADKEY_EXTENDED,// e0 ca
    DEADKEY_EXTENDED,// e0 cb
    DEADKEY_EXTENDED,// e0 cc
    DEADKEY_EXTENDED,// e0 cd
    DEADKEY_EXTENDED,// e0 ce
    DEADKEY_EXTENDED,// e0 cf
    DEADKEY_EXTENDED,// e0 d0
    DEADKEY_EXTENDED,// e0 d1
    DEADKEY_EXTENDED,// e0 d2
    DEADKEY_EXTENDED,// e0 d3
    DEADKEY_EXTENDED,// e0 d4
    DEADKEY_EXTENDED,// e0 d5
    DEADKEY_EXTENDED,// e0 d6
    DEADKEY_EXTENDED,// e0 d7
    DEADKEY_EXTENDED,// e0 d8
    DEADKEY_EXTENDED,// e0 d9
    DEADKEY_EXTENDED,// e0 da
    DEADKEY_EXTENDED,// e0 db
    DEADKEY_EXTENDED,// e0 dc
    DEADKEY_EXTENDED,// e0 dd
    DEADKEY_EXTENDED,// e0 de
    DEADKEY_EXTENDED,// e0 df
    DEADKEY_EXTENDED,// e0 e0
    DEADKEY_EXTENDED,// e0 e1
    DEADKEY_EXTENDED,// e0 e2
    DEADKEY_EXTENDED,// e0 e3
    DEADKEY_EXTENDED,// e0 e4
    DEADKEY_EXTENDED,// e0 e5
    DEADKEY_EXTENDED,// e0 e6
    DEADKEY_EXTENDED,// e0 e7
    DEADKEY_EXTENDED,// e0 e8
    DEADKEY_EXTENDED,// e0 e9
    DEADKEY_EXTENDED,// e0 ea
    DEADKEY_EXTENDED,// e0 eb
    DEADKEY_EXTENDED,// e0 ec
    DEADKEY_EXTENDED,// e0 ed
    DEADKEY_EXTENDED,// e0 ee
    DEADKEY_EXTENDED,// e0 ef
    DEADKEY_EXTENDED,// e0 f0 // Note: codes e0f0 through e0ff are reserved for ACPI callback
    DEADKEY_EXTENDED,// e0 f1
    DEADKEY_EXTENDED,// e0 f2
    DEADKEY_EXTENDED,// e0 f3
    DEADKEY_EXTENDED,// e0 f4
    DEADKEY_EXTENDED,// e0 f5
    DEADKEY_EXTENDED,// e0 f6
    DEADKEY_EXTENDED,// e0 f7
    DEADKEY_EXTENDED,// e0 f8
    DEADKEY_EXTENDED,// e0 f9
    DEADKEY_EXTENDED,// e0 fa
    DEADKEY_EXTENDED,// e0 fb
    DEADKEY_EXTENDED,// e0 fc
    DEADKEY_EXTENDED,// e0 fd
    DEADKEY_EXTENDED,// e0 fe
    DEADKEY_EXTENDED // e0 ff // End reserved
};

///////////////////////////////////////////////////////////////////////////////////
//
//
// high-byte of flags are (bit number + 1) for modifier key tracking
//  1: left control
//  2: right control
//  3: left shift
//  4: right shift
//  5: left alt
//  6: right alt
//  7: left windows
//  8: right windows
//  9: left Fn (e0 63 on Lenovo u430)
// 10: windows context menu (usually on right)
//
// low-byte is used for other purposes
//  bit 0: breakless bit (set by "PS2 Breakless"
//

#define kMaskLeftControl    0x0001
#define kMaskRightControl   0x0002
#define kMaskLeftShift      0x0004
#define kMaskRightShift     0x0008
#define kMaskLeftAlt        0x0010
#define kMaskRightAlt       0x0020
#define kMaskLeftWindows    0x0040
#define kMaskRightWindows   0x0080
#define kMaskLeftFn         0x0100
#define kMaskWindowsContext 0x0200

static const UInt16 _PS2flagsStock[256*2] =
{
    // flags/modifier key        AT  ANSI Key-Legend
    0x00,   // 00
    0x00,   // 01  Escape
    0x00,   // 02  1!
    0x00,   // 03  2@
    0x00,   // 04  3#
    0x00,   // 05  4$
    0x00,   // 06  5%
    0x00,   // 07  6^
    0x00,   // 08  7&
    0x00,   // 09  8*
    0x00,   // 0a  9(
    0x00,   // 0b  0)
    0x00,   // 0c  -_
    0x00,   // 0d  =+
    0x00,   // 0e  Backspace
    0x00,   // 0f  Tab
    0x00,   // 10  qQ
    0x00,   // 11  wW
    0x00,   // 12  eE
    0x00,   // 13  rR
    0x00,   // 14  tT
    0x00,   // 15  yY
    0x00,   // 16  uU
    0x00,   // 17  iI
    0x00,   // 18  oO
    0x00,   // 19  pP
    0x00,   // 1a  [{
    0x00,   // 1b  ]}
    0x00,   // 1c  Return
    0x0100, // 1d  Left Control
    0x00,   // 1e  aA
    0x00,   // 1f  sS
    0x00,   // 20  dD
    0x00,   // 21  fF
    0x00,   // 22  gG
    0x00,   // 23  hH
    0x00,   // 24  jJ
    0x00,   // 25  kK
    0x00,   // 26  lL
    0x00,   // 27  ;:
    0x00,   // 28  '"
    0x00,   // 29  `~
    0x0300, // 2a  Left Shift
    0x00,   // 2b  \| , Europe 1(ISO)
    0x00,   // 2c  zZ
    0x00,   // 2d  xX
    0x00,   // 2e  cC
    0x00,   // 2f  vV
    0x00,   // 30  bB
    0x00,   // 31  nN
    0x00,   // 32  mM
    0x00,   // 33  ,<
    0x00,   // 34  .>
    0x00,   // 35  /?
    0x0400, // 36  Right Shift
    0x00,   // 37  Keypad *
    0x0500, // 38  Left Alt
    0x00,   // 39  Space
    0x00,   // 3a  Caps Lock
    0x00,   // 3b  F1
    0x00,   // 3c  F2
    0x00,   // 3d  F3
    0x00,   // 3e  F4
    0x00,   // 3f  F5
    0x00,   // 40  F6
    0x00,   // 41  F7
    0x00,   // 42  F8
    0x00,   // 43  F9
    0x00,   // 44  F10
    0x00,   // 45  Num Lock
    0x00,   // 46  Scroll Lock
    0x00,   // 47  Keypad 7 Home
    0x00,   // 48  Keypad 8 Up
    0x00,   // 49  Keypad 9 PageUp
    0x00,   // 4a  Keypad -
    0x00,   // 4b  Keypad 4 Left
    0x00,   // 4c  Keypad 5
    0x00,   // 4d  Keypad 6 Right
    0x00,   // 4e  Keypad +
    0x00,   // 4f  Keypad 1 End
    0x00,   // 50  Keypad 2 Down
    0x00,   // 51  Keypad 3 PageDn
    0x00,   // 52  Keypad 0 Insert
    0x00,   // 53  Keypad . Delete
    0x00,   // 54  SysReq
    0x00,   // 55
    0x00,   // 56  Europe 2(ISO)
    0x00,   // 57  F11
    0x00,   // 58  F12
    0x00,   // 59  Keypad =
    0x00,   // 5a
    0x00,   // 5b
    0x00,   // 5c  Keyboard Int'l 6 (PC9800 Keypad , )
    0x00,   // 5d
    0x00,   // 5e
    0x00,   // 5f
    0x00,   // 60
    0x00,   // 61
    0x00,   // 62
    0x00,   // 63
    0x00,   // 64  F13
    0x00,   // 65  F14
    0x00,   // 66  F15
    0x00,   // 67  F16
    0x00,   // 68  F17
    0x00,   // 69  F18
    0x00,   // 6a  F19
    0x00,   // 6b  F20
    0x00,   // 6c  F21
    0x00,   // 6d  F22
    0x00,   // 6e  F23
    0x00,   // 6f
    0x00,   // 70  Keyboard Intl'2 (Japanese Katakana/Hiragana)
    0x00,   // 71
    0x00,   // 72
    0x00,   // 73  Keyboard Int'l 1 (Japanese Ro)
    0x00,   // 74
    0x00,   // 75
    0x00,   // 76  F24 , Keyboard Lang 5 (Japanese Zenkaku/Hankaku)
    0x00,   // 77  Keyboard Lang 4 (Japanese Hiragana)
    0x00,   // 78  Keyboard Lang 3 (Japanese Katakana)
    0x00,   // 79  Keyboard Int'l 4 (Japanese Henkan)
    0x00,   // 7a
    0x00,   // 7b  Keyboard Int'l 5 (Japanese Muhenkan)
    0x00,   // 7c
    0x00,   // 7d  Keyboard Int'l 3 (Japanese Yen)
    0x00,   // 7e  Keypad , (Brazilian Keypad .)
    0x00,   // 7f
    0x00,   // 80
    0x00,   // 81
    0x00,   // 82
    0x00,   // 83
    0x00,   // 84
    0x00,   // 85
    0x00,   // 86
    0x00,   // 87
    0x00,   // 88
    0x00,   // 89
    0x00,   // 8a
    0x00,   // 8b
    0x00,   // 8c
    0x00,   // 8d
    0x00,   // 8e
    0x00,   // 8f
    0x00,   // 90
    0x00,   // 91
    0x00,   // 92
    0x00,   // 93
    0x00,   // 94
    0x00,   // 95
    0x00,   // 96
    0x00,   // 97
    0x00,   // 98
    0x00,   // 99
    0x00,   // 9a
    0x00,   // 9b
    0x00,   // 9c
    0x00,   // 9d
    0x00,   // 9e
    0x00,   // 9f
    0x00,   // a0
    0x00,   // a1
    0x00,   // a2
    0x00,   // a3
    0x00,   // a4
    0x00,   // a5
    0x00,   // a6
    0x00,   // a7
    0x00,   // a8
    0x00,   // a9
    0x00,   // aa
    0x00,   // ab
    0x00,   // ac
    0x00,   // ad
    0x00,   // ae
    0x00,   // af
    0x00,   // b0
    0x00,   // b1
    0x00,   // b2
    0x00,   // b3
    0x00,   // b4
    0x00,   // b5
    0x00,   // b6
    0x00,   // b7
    0x00,   // b8
    0x00,   // b9
    0x00,   // ba
    0x00,   // bb
    0x00,   // bc
    0x00,   // bd
    0x00,   // be
    0x00,   // bf
    0x00,   // c0
    0x00,   // c1
    0x00,   // c2
    0x00,   // c3
    0x00,   // c4
    0x00,   // c5
    0x00,   // c6
    0x00,   // c7
    0x00,   // c8
    0x00,   // c9
    0x00,   // ca
    0x00,   // cb
    0x00,   // cc
    0x00,   // cd
    0x00,   // ce
    0x00,   // cf
    0x00,   // d0
    0x00,   // d1
    0x00,   // d2
    0x00,   // d3
    0x00,   // d4
    0x00,   // d5
    0x00,   // d6
    0x00,   // d7
    0x00,   // d8
    0x00,   // d9
    0x00,   // da
    0x00,   // db
    0x00,   // dc
    0x00,   // dd
    0x00,   // de
    0x00,   // df
    0x00,   // e0
    0x00,   // e1
    0x00,   // e2
    0x00,   // e3
    0x00,   // e4
    0x00,   // e5
    0x00,   // e6
    0x00,   // e7
    0x00,   // e8
    0x00,   // e9
    0x00,   // ea
    0x00,   // eb
    0x00,   // ec
    0x00,   // ed
    0x00,   // ee
    0x00,   // ef
    0x00,   // f0
    0x00,   // f1*  Keyboard Lang 2 (Korean Hanja)
    0x00,   // f2*  Keyboard Lang 1 (Korean Hangul)
    0x00,   // f3
    0x00,   // f4
    0x00,   // f5
    0x00,   // f6
    0x00,   // f7
    0x00,   // f8
    0x00,   // f9
    0x00,   // fa
    0x00,   // fb
    0x00,   // fc
    0x00,   // fd
    0x00,   // fe
    0x00,   // ff
    0x00,   // e0 00
    0x00,   // e0 01
    0x00,   // e0 02
    0x00,   // e0 03
    0x00,   // e0 04
    0x00,   // e0 05 dell down
    0x00,   // e0 06 dell up
    0x00,   // e0 07
#ifndef PROBOOK
    0x00,   // e0 08 samsung up
    0x00,   // e0 09 samsung down
#else
    0x00,   // e0 08
    0x00,   // e0 09 Launchpad (hp Fn+F6)
#endif
    0x00,   // e0 0a Mission Control (hp Fn+F5)
    0x00,   // e0 0b
    0x00,   // e0 0c
    0x00,   // e0 0d
    0x00,   // e0 0e
    0x00,   // e0 0f
    0x00,   // e0 10  Scan Previous Track (hp Fn+F10)
    0x00,   // e0 11
    0x00,   // e0 12 hp down (Fn+F2)
    0x00,   // e0 13
    0x00,   // e0 14
    0x00,   // e0 15
    0x00,   // e0 16
    0x00,   // e0 17 hp up (Fn+F3)
    0x00,   // e0 18
    0x00,   // e0 19  Scan Next Track (hp Fn+F12)
    0x00,   // e0 1a
    0x00,   // e0 1b
    0x00,   // e0 1c  Keypad Enter
    0x0200, // e0 1d  Right Control
    0x00,   // e0 1e
    0x00,   // e0 1f
    0x00,   // e0 20  Mute (hp Fn+F7)
    0x00,   // e0 21  Calculator
    0x00,   // e0 22  Play/Pause (hp Fn+F11)
    0x00,   // e0 23
    0x00,   // e0 24  Stop
    0x00,   // e0 25
    0x00,   // e0 26
    0x00,   // e0 27  Fn+fkeys/fkeys toggle alternate (default Ctrl+e037)
    0x00,   // e0 28
    0x00,   // e0 29
    0x00,   // e0 2a
    0x00,   // e0 2b
    0x00,   // e0 2c
    0x00,   // e0 2d
    0x00,   // e0 2e  Volume Down (hp Fn+F8)
    0x00,   // e0 2f
    0x00,   // e0 30  Volume Up (hp Fn+F9)
    0x00,   // e0 31
    0x00,   // e0 32  WWW Home
    0x00,   // e0 33
    0x00,   // e0 34
    0x00,   // e0 35  Keypad /
    0x00,   // e0 36
    0x00,   // e0 37  Print Screen
    0x0600, // e0 38  Right Alt
    0x00,   // e0 39
    0x00,   // e0 3a
    0x00,   // e0 3b
    0x00,   // e0 3c
    0x00,   // e0 3d
    0x00,   // e0 3e
    0x00,   // e0 3f
    0x00,   // e0 40
    0x00,   // e0 41
    0x00,   // e0 42
    0x00,   // e0 43
    0x00,   // e0 44
    0x00,   // e0 45* Pause
    0x00,   // e0 46* Break(Ctrl-Pause)
    0x00,   // e0 47  Home
    0x00,   // e0 48  Up Arrow
    0x00,   // e0 49  Page Up
    0x00,   // e0 4a
    0x00,   // e0 4b  Left Arrow
    0x00,   // e0 4c
    0x00,   // e0 4d  Right Arrow
    0x00,   // e0 4e acer up
    0x00,   // e0 4f  End
    0x00,   // e0 50  Down Arrow
    0x00,   // e0 51  Page Down
    0x00,   // e0 52  Insert = Eject
    0x00,   // e0 53  Delete
    0x00,   // e0 54
    0x00,   // e0 55
    0x00,   // e0 56
    0x00,   // e0 57
    0x00,   // e0 58
    0x00,   // e0 59 acer up for my acer
    0x00,   // e0 5a
    0x0700, // e0 5b  Left GUI(Windows)
    0x0800, // e0 5c  Right GUI(Windows)
    0x0a00, // e0 5d  App( Windows context menu key )
    0x00,   // e0 5e  System Power / Keyboard Power
    0x00,   // e0 5f  System Sleep (hp Fn+F1)
    0x00,   // e0 60
    0x00,   // e0 61
    0x00,   // e0 62
    0x0900, // e0 63  System Wake (Fn on Lenovo u430)
    0x00,   // e0 64
    0x00,   // e0 65  WWW Search
    0x00,   // e0 66  WWW Favorites
    0x00,   // e0 67  WWW Refresh
    0x00,   // e0 68  WWW Stop
    0x00,   // e0 69  WWW Forward
    0x00,   // e0 6a  WWW Back
    0x00,   // e0 6b  My Computer
    0x00,   // e0 6c  Mail
    0x00,   // e0 6d  Media Select
#ifndef PROBOOK
    0x00,   // e0 6e acer up
    0x00,   // e0 6f acer down
#else
    0x00,   // e0 6e  Video Mirror = hp Fn+F4
    0x00,   // e0 6f  Fn+Home
#endif
    0x00,   // e0 70
    0x00,   // e0 71
    0x00,   // e0 72
    0x00,   // e0 73
    0x00,   // e0 74
    0x00,   // e0 75
    0x00,   // e0 76
#ifndef PROBOOK
    0x00,   // e0 77 lg down
    0x00,   // e0 78 lg up
#else
    0x00,   // e0 77
    0x00,   // e0 78 WiFi on/off button on HP ProBook
#endif
    0x00,   // e0 79
    0x00,   // e0 7a
    0x00,   // e0 7b
    0x00,   // e0 7c
    0x00,   // e0 7d
    0x00,   // e0 7e
    0x00,   // e0 7f
    0x00,   // e0 80
    0x00,   // e0 81
    0x00,   // e0 82
    0x00,   // e0 83
    0x00,   // e0 84
    0x00,   // e0 85
    0x00,   // e0 86
    0x00,   // e0 87
    0x00,   // e0 88
    0x00,   // e0 89
    0x00,   // e0 8a
    0x00,   // e0 8b
    0x00,   // e0 8c
    0x00,   // e0 8d
    0x00,   // e0 8e
    0x00,   // e0 8f
    0x00,   // e0 90
    0x00,   // e0 91
    0x00,   // e0 92
    0x00,   // e0 93
    0x00,   // e0 94
    0x00,   // e0 95
    0x00,   // e0 96
    0x00,   // e0 97
    0x00,   // e0 98
    0x00,   // e0 99
    0x00,   // e0 9a
    0x00,   // e0 9b
    0x00,   // e0 9c
    0x00,   // e0 9d
    0x00,   // e0 9e
    0x00,   // e0 9f
    0x00,   // e0 a0
    0x00,   // e0 a1
    0x00,   // e0 a2
    0x00,   // e0 a3
    0x00,   // e0 a4
    0x00,   // e0 a5
    0x00,   // e0 a6
    0x00,   // e0 a7
    0x00,   // e0 a8
    0x00,   // e0 a9
    0x00,   // e0 aa
    0x00,   // e0 ab
    0x00,   // e0 ac
    0x00,   // e0 ad
    0x00,   // e0 ae
    0x00,   // e0 af
    0x00,   // e0 b0
    0x00,   // e0 b1
    0x00,   // e0 b2
    0x00,   // e0 b3
    0x00,   // e0 b4
    0x00,   // e0 b5
    0x00,   // e0 b6
    0x00,   // e0 b7
    0x00,   // e0 b8
    0x00,   // e0 b9
    0x00,   // e0 ba
    0x00,   // e0 bb
    0x00,   // e0 bc
    0x00,   // e0 bd
    0x00,   // e0 be
    0x00,   // e0 bf
    0x00,   // e0 c0
    0x00,   // e0 c1
    0x00,   // e0 c2
    0x00,   // e0 c3
    0x00,   // e0 c4
    0x00,   // e0 c5
    0x00,   // e0 c6
    0x00,   // e0 c7
    0x00,   // e0 c8
    0x00,   // e0 c9
    0x00,   // e0 ca
    0x00,   // e0 cb
    0x00,   // e0 cc
    0x00,   // e0 cd
    0x00,   // e0 ce
    0x00,   // e0 cf
    0x00,   // e0 d0
    0x00,   // e0 d1
    0x00,   // e0 d2
    0x00,   // e0 d3
    0x00,   // e0 d4
    0x00,   // e0 d5
    0x00,   // e0 d6
    0x00,   // e0 d7
    0x00,   // e0 d8
    0x00,   // e0 d9
    0x00,   // e0 da
    0x00,   // e0 db
    0x00,   // e0 dc
    0x00,   // e0 dd
    0x00,   // e0 de
    0x00,   // e0 df
    0x00,   // e0 e0
    0x00,   // e0 e1
    0x00,   // e0 e2
    0x00,   // e0 e3
    0x00,   // e0 e4
    0x00,   // e0 e5
    0x00,   // e0 e6
    0x00,   // e0 e7
    0x00,   // e0 e8
    0x00,   // e0 e9
    0x00,   // e0 ea
    0x00,   // e0 eb
    0x00,   // e0 ec
    0x00,   // e0 ed
    0x00,   // e0 ee
    0x00,   // e0 ef
    0x00,   // e0 f0 // Note: codes e0f0 through e0ff are reserved for ACPI callback
    0x00,   // e0 f1
    0x00,   // e0 f2
    0x00,   // e0 f3
    0x00,   // e0 f4
    0x00,   // e0 f5
    0x00,   // e0 f6
    0x00,   // e0 f7
    0x00,   // e0 f8
    0x00,   // e0 f9
    0x00,   // e0 fa
    0x00,   // e0 fb
    0x00,   // e0 fc
    0x00,   // e0 fd
    0x00,   // e0 fe
    0x00,   // e0 ff // End reserved
};

#endif /* !_APPLEPS2TOADBMAP_H */
