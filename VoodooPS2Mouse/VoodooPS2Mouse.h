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

#ifndef _APPLEPS2MOUSE_H
#define _APPLEPS2MOUSE_H

#include "ApplePS2MouseDevice.h"
#include <IOKit/hidsystem/IOHIPointing.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Local Declarations
//

#define kPacketLengthMax          4
#define kPacketLengthStandard     3
#define kPacketLengthIntellimouse 4

typedef enum
{
  kMouseTypeStandard             = 0x00,
  kMouseTypeIntellimouse         = 0x03,
  kMouseTypeIntellimouseExplorer = 0x04
} PS2MouseId;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2Mouse Class Declaration
//

class EXPORT ApplePS2Mouse : public IOHIPointing
{
  typedef IOHIPointing super;
  OSDeclareDefaultStructors(ApplePS2Mouse);

private:
  ApplePS2MouseDevice * _device;
  bool                  _interruptHandlerInstalled;
  bool                  _powerControlHandlerInstalled;
  RingBuffer<UInt8, kPacketLengthMax*32> _ringBuffer;
  UInt32                _packetByteCount;
  UInt8                 _lastdata;
  UInt32                _packetLength;
  IOFixed               _resolution;                // (dots per inch)
  PS2MouseId            _type;
  int                   _buttonCount;
  UInt32                _mouseInfoBytes;
  UInt32                _mouseResetCount;
  IOCommandGate*        _cmdGate;
  int                   defres;
  int					forceres;
  int                   mouseyinverter;
  int                   scrollyinverter;
  int                   forcesetres;
  int32_t               resmode;
  int32_t               scrollres;
  int                   actliketrackpad;
  uint64_t              keytime;
  uint64_t              maxaftertyping;
  UInt32                buttonmask;
  bool                  outzone_wt, palm, palm_wt;
  bool                  scroll;
  bool                  ignoreall;
  bool                  ledpresent;
  int                   noled;
  int                   wakedelay;
  bool                  usb_mouse_stops_trackpad;
    
  int _processusbmouse;
  int _processbluetoothmouse;
    
  OSSet* attachedHIDPointerDevices;
    
  IONotifier* usb_hid_publish_notify;     // Notification when an USB mouse HID device is connected
  IONotifier* usb_hid_terminate_notify; // Notification when an USB mouse HID device is disconnected
    
  IONotifier* bluetooth_hid_publish_notify; // Notification when a bluetooth HID device is connected
  IONotifier* bluetooth_hid_terminate_notify; // Notification when a bluetooth HID device is disconnected
    
  // for middle button simulation
  enum mbuttonstate
  {
      STATE_NOBUTTONS,
      STATE_MIDDLE,
      STATE_WAIT4TWO,
      STATE_WAIT4NONE,
      STATE_NOOP,
  } _mbuttonstate;
  
  UInt32 lastbuttons;
  UInt32 _pendingbuttons;
  uint64_t _buttontime;
  IOTimerEventSource* _buttonTimer;
  uint64_t _maxmiddleclicktime;
  int _fakemiddlebutton;
    
  void onButtonTimer(void);
  enum MBComingFrom { fromTimer, fromMouse };
  UInt32 middleButton(UInt32 butttons, uint64_t now, MBComingFrom from);
   
  virtual void   dispatchRelativePointerEventWithPacket(UInt8 * packet,
                                                        UInt32  packetSize);
  virtual UInt8  getMouseID();
  virtual UInt32 getMouseInformation();
  virtual PS2MouseId setIntellimouseMode();
  virtual void   setMouseEnable(bool enable);
  virtual void   setMouseSampleRate(UInt8 sampleRate);
  virtual void   setMouseResolution(UInt8 resolution);
  virtual void   initMouse();
  virtual void   resetMouse();
  virtual void   setDevicePowerState(UInt32 whatToDo);
    
  void updateTouchpadLED();
  bool setTouchpadLED(UInt8 touchLED);
  bool getTouchPadData(UInt8 dataSelector, UInt8 buf3[]);
  void setParamPropertiesGated(OSDictionary * dict);
  void injectVersionDependentProperties(OSDictionary* dict);

    
  void registerHIDPointerNotifications();
  void unregisterHIDPointerNotifications();

  void notificationHIDAttachedHandlerGated(IOService * newService, IONotifier * notifier);
  bool notificationHIDAttachedHandler(void * refCon, IOService * newService, IONotifier * notifier);
protected:
  virtual IOItemCount buttonCount();
  virtual IOFixed     resolution();
  inline void dispatchRelativePointerEventX(int dx, int dy, UInt32 buttonState, uint64_t now)
    { dispatchRelativePointerEvent(dx, dy, buttonState, *(AbsoluteTime*)&now); }
  inline void dispatchScrollWheelEventX(short deltaAxis1, short deltaAxis2, short deltaAxis3, uint64_t now)
    { dispatchScrollWheelEvent(deltaAxis1, deltaAxis2, deltaAxis3, *(AbsoluteTime*)&now); }
  inline void setTimerTimeout(IOTimerEventSource* timer, uint64_t time)
    { timer->setTimeout(*(AbsoluteTime*)&time); }
  inline void cancelTimer(IOTimerEventSource* timer)
    { timer->cancelTimeout(); }
public:
  virtual bool init(OSDictionary * properties);
  virtual ApplePS2Mouse * probe(IOService * provider, SInt32 * score);

  virtual bool start(IOService * provider);
  virtual void stop(IOService * provider);

  virtual PS2InterruptResult interruptOccurred(UInt8 data);
  virtual void packetReady();

  virtual UInt32 deviceType();
  virtual UInt32 interfaceID();
    
  virtual IOReturn setParamProperties(OSDictionary * dict);
  virtual IOReturn setProperties (OSObject *props);
    
  virtual IOReturn message(UInt32 type, IOService* provider, void* argument);
};

#endif /* _APPLEPS2MOUSE_H */
