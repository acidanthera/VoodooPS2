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
#include "VoodooPS2Mouse.h"

// =============================================================================
// ApplePS2Mouse Class Implementation
//

#define super IOHIPointing
OSDefineMetaClassAndStructors(ApplePS2Mouse, IOHIPointing);

UInt32 ApplePS2Mouse::deviceType()  { return NX_EVS_DEVICE_TYPE_MOUSE; };
UInt32 ApplePS2Mouse::interfaceID() { return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

IOItemCount ApplePS2Mouse::buttonCount() { return _buttonCount; };
IOFixed     ApplePS2Mouse::resolution()  { return _resolution; };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Mouse::init(OSDictionary * properties)
{
  //
  // Initialize this object's minimal state.  This is invoked right after this
  // object is instantiated.
  //
  OSNumber *num;
  OSBoolean *bl;

  if (!super::init(properties))  return false;

  _device                    = 0;
  _interruptHandlerInstalled = false;
  _packetByteCount           = 0;
  _packetLength              = kPacketLengthStandard;
  defres					 = (150) << 16; // (default is 150 dpi; 6 counts/mm)
  forceres					 = false;
  inverty					 = false;
  _type                      = kMouseTypeStandard;
  _buttonCount               = 3;
  _mouseInfoBytes            = (UInt32)-1;

  if (num=OSDynamicCast (OSNumber, properties->getObject ("DefaultResolution")))
	defres = num->unsigned32BitValue();
  if (bl=OSDynamicCast (OSBoolean, properties->getObject ("ForceDefaultResolution")))
	forceres=bl->isTrue();
  if (bl=OSDynamicCast (OSBoolean, properties->getObject ("InvertY")))
	inverty=bl->isTrue();

  _resolution                = defres;

	
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2Mouse * ApplePS2Mouse::probe(IOService * provider, SInt32 * score)
{ 

  //
  // The driver has been instructed to verify the presence of the actual
  // hardware we represent. We are guaranteed by the controller that the
  // mouse clock is enabled and the mouse itself is disabled (thus it
  // won't send any asynchronous mouse data that may mess up the
  // responses expected by the commands we send it).
  //

  ApplePS2MouseDevice * device  = (ApplePS2MouseDevice *)provider;
  PS2Request *          request = device->allocateRequest();
  bool                  success;

  if (!super::probe(provider, score))  return 0;

  //
  // Check to see if acknowledges are being received for commands to the mouse.
  //

  // (get information command)
  request->commands[0].command = kPS2C_WriteCommandPort;
  request->commands[0].inOrOut = kCP_TransmitToMouse;
  request->commands[1].command = kPS2C_WriteDataPort;
  request->commands[1].inOrOut = kDP_GetMouseInformation;
  request->commands[2].command = kPS2C_ReadDataPortAndCompare;
  request->commands[2].inOrOut = kSC_Acknowledge;
  request->commands[3].command = kPS2C_ReadDataPort;
  request->commands[3].inOrOut = 0;
  request->commands[4].command = kPS2C_ReadDataPort;
  request->commands[4].inOrOut = 0;
  request->commands[5].command = kPS2C_ReadDataPort;
  request->commands[5].inOrOut = 0;
  request->commandsCount = 6;
  device->submitRequestAndBlock(request);

  // (free the request)
  success = (request->commandsCount == 6);
  device->freeRequest(request);

  return (success) ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Mouse::start(IOService * provider)
{ 
  //
  // The driver has been instructed to start. This is called after a
  // successful probe and match.
  //

  if (!super::start(provider)) return false;

  //
  // Maintain a pointer to and retain the provider object.
  //

  _device = (ApplePS2MouseDevice *)provider;
  _device->retain();

  //
  // Reset and enable the mouse.
  //

  resetMouse();

  //
  // Install our driver's interrupt handler, for asynchronous data delivery.
  //

  _device->installInterruptAction(this,OSMemberFunctionCast
    (PS2InterruptAction,this,&ApplePS2Mouse::interruptOccurred));
  _interruptHandlerInstalled = true;

  //
  // Install our power control handler.
  //

  _device->installPowerControlAction( this,OSMemberFunctionCast
           (PS2PowerControlAction,this, &ApplePS2Mouse::setDevicePowerState) );
  _powerControlHandlerInstalled = true;

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::stop(IOService * provider)
{
  //
  // The driver has been instructed to stop.  Note that we must break all
  // connections to other service objects now (ie. no registered actions,
  // no pointers and retains to objects, etc), if any.
  //

  assert(_device == provider);

  //
  // Disable the mouse itself, so that it may stop reporting mouse events.
  //

  setMouseEnable(false);

  //
  // Disable the mouse clock and the mouse IRQ line.
  //

  setCommandByte(kCB_DisableMouseClock, kCB_EnableMouseIRQ);

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

void ApplePS2Mouse::resetMouse()
{
  PS2MouseId type;

  //
  // Reset the mouse to its default state.
  //

  PS2Request * request = _device->allocateRequest();
  if (request)
  {
    request->commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut = kDP_SetDefaults;
    request->commandsCount = 1;
    _device->submitRequestAndBlock(request);
    _device->freeRequest(request);
  }

  //
  // Obtain our mouse's resolution and sampling rate.
  //

  if (_mouseInfoBytes == (UInt32)-1)
  {
    _mouseInfoBytes = getMouseInformation();
	if (forceres)
		_resolution = defres;
	else
	  switch (_mouseInfoBytes & 0x00FF00)
	  {
		  case 0x0000: _resolution = (25)  << 16; break; //  25 dpi
		  case 0x0100: _resolution = (50)  << 16; break; //  50 dpi
		  case 0x0200: _resolution = (100) << 16; break; // 100 dpi
		  case 0x0300: _resolution = (200) << 16; break; // 200 dpi
		  default:     _resolution = (150) << 16; break; // 150 dpi
	  }
  }
  else
  {
    setMouseResolution(_mouseInfoBytes >> 8);
  }

  //
  // Enable the Intellimouse mode, should this be an Intellimouse.
  //

  if ( (type = setIntellimouseMode()) != kMouseTypeStandard )
  {
    _packetLength = kPacketLengthIntellimouse;
    _type         = type;

    if (_type == kMouseTypeIntellimouseExplorer)
      _buttonCount = 5;
    else
      _buttonCount = 3;

    //
    // Report the resolution of the scroll wheel. This property must
    // be present to enable acceleration for Z-axis movement.
    //
    setProperty(kIOHIDScrollResolutionKey, (10 << 16), 32);
  }
  else
  {
    _packetLength = kPacketLengthStandard;
    _type         = kMouseTypeStandard;
    _buttonCount  = 3;

    removeProperty(kIOHIDScrollResolutionKey);
  }

  _packetByteCount = 0;

  //
  // Enable the mouse clock (should already be so) and the mouse IRQ line.
  //

  setCommandByte(kCB_EnableMouseIRQ, kCB_DisableMouseClock);

  //
  // Finally, we enable the mouse itself, so that it may start reporting
  // mouse events.
  //

  setMouseEnable(true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::scheduleMouseReset()
{
  //
  // Request the mouse to stop. A 0xF5 command is issued.
  //

  setMouseEnable(false);

  //
  // Reset the mouse (synchronous).
  //

  resetMouse();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::interruptOccurred(UInt8 data)      // PS2InterruptAction
{
  //
  // This will be invoked automatically from our device when asynchronous mouse
  // needs to be delivered.  Process the mouse data.
  //
  // We ignore all bytes until we see the start of a packet, otherwise the mouse
  // packets may get out of sequence and things will get very confusing.
  //
  if (_packetByteCount == 0 && ((data == kSC_Acknowledge) || !(data & 0x08)))
  {
    IOLog("%s: Unexpected data from PS/2 controller.\n", getName());

    //
    // Reset the mouse when packet synchronization is lost. Limit the number
    // of consecutive resets to guard against flaky hardware.
    //

    if (_mouseResetCount < 5)
    {
        _mouseResetCount++;
        scheduleMouseReset();
    }
    return;
  }

  //
  // Add this byte to the packet buffer.  If the packet is complete, that is,
  // we have the three (or four) bytes, dispatch this packet for processing.
  //

  _packetBuffer[_packetByteCount++] = data;

  if (_packetByteCount == _packetLength)
  {
    dispatchRelativePointerEventWithPacket(_packetBuffer, _packetLength);
    _packetByteCount = 0;
    _mouseResetCount = 0;
  }
  else if (_packetByteCount == 2 && _packetBuffer[0] == 0xAA)
  {
    //
    // "0xAA 0x00" 2-byte packet is sent following a mouse hardware reset.
    // This can happen if the user removed and then inserted the same or a
    // different mouse to the mouse port. Reset the mouse and hope for the
    // best. KVM switches should not trigger this when switching stations.
    //

    scheduleMouseReset();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::dispatchRelativePointerEventWithPacket(UInt8 * packet,
                                                           UInt32  packetSize)
{
  //
  // Process the three byte mouse packet that was retreived from the mouse.
  // The format of the bytes is as follows:
  //
  // 7  6  5  4  3  2  1  0
  // YO XO YS XS 1  M  R  L
  // X7 X6 X5 X4 X3 X3 X1 X0
  // Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
  // Z7 Z6 Z5 Z4 Z3 Z2 Z1 Z0 <- fourth byte returned only for Intellimouse type
  //
  //  0  0 B5 B4 Z3 Z2 Z1 Z0 <- fourth byte for 5-button wheel mouse mode
  //

  UInt32       buttons = 0;
  SInt32       dx;
  SInt32       dy;
  SInt16       dz = 0;
  AbsoluteTime now;

  if ( (packet[0] & 0x1) ) buttons |= 0x1;  // left button   (bit 0 in packet)
  if ( (packet[0] & 0x2) ) buttons |= 0x2;  // right button  (bit 1 in packet)
  if ( (packet[0] & 0x4) ) buttons |= 0x4;  // middle button (bit 2 in packet)

  dx = ((packet[0] & 0x10) ? 0xffffff00 : 0 ) | packet[1];
  dy = -(((packet[0] & 0x20) ? 0xffffff00 : 0 ) | packet[2]);

  clock_get_uptime((uint64_t*) &now);

  if ( packetSize > 3 )
  {
    // Pull out fourth and fifth buttons.
    if (_type == kMouseTypeIntellimouseExplorer)
    {
      if (packet[3] & 0x10) buttons |= 0x8;  // fourth button (bit 4 in packet)
      if (packet[3] & 0x20) buttons |= 0x10; // fifth button  (bit 5 in packet)
    }
	dispatchRelativePointerEvent(dx, inverty?-dy:dy, buttons, now);

    //
    // We treat the 4th byte in the packet as a 8-bit signed Z value.
    // There are mice that can report only 4-bits for the Z data, and
    // use two of the remaining 4 bits for buttons 4 and 5. To enable
    // the 5-button mouse mode, the command sequence should be:
    //
    // setMouseSampleRate(200);
    // setMouseSampleRate(200);
    // setMouseSampleRate(80);
    //
    // Those devices will return 0x04 for the device ID.
    //
    // %%KCD - As it turns out, the valid range for scroll data from
    // PS2 mice is -8 to +7, thus the upper four bits are just a sign
    // bit.  If we just sign extend the lower four bits, the scroll
    // calculation works for normal scrollwheel mice and five button mice.
    dz = (SInt16)(((SInt8)(packet[3] << 4)) >> 4);
    if ( dz )
    {
      //
      // The Z counter is negative on an upwards scroll (away from the user),
      // and positive when scrolling downwards. Invert this before passing to
      // HID/CG.
      //
      dispatchScrollWheelEvent(-dz, 0, 0, now);
    }
  }
  else
  {
	  dispatchRelativePointerEvent(dx, inverty?-dy:dy, buttons, now);
  }

  return;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::setMouseEnable(bool enable)
{
  //
  // Instructs the mouse to start or stop the reporting of mouse events.
  // Be aware that while the mouse is enabled, asynchronous mouse events
  // may arrive in the middle of command sequences sent to the controller,
  // and may get confused for expected command responses.
  //
  // It is safe to issue this request from the interrupt/completion context.
  //

  PS2Request * request = _device->allocateRequest();

  // (mouse enable/disable command)
  request->commands[0].command = kPS2C_WriteCommandPort;
  request->commands[0].inOrOut = kCP_TransmitToMouse;
  request->commands[1].command = kPS2C_WriteDataPort;
  request->commands[1].inOrOut = (enable)?kDP_Enable:kDP_SetDefaultsAndDisable;
  request->commands[2].command = kPS2C_ReadDataPortAndCompare;
  request->commands[2].inOrOut = kSC_Acknowledge;
  request->commandsCount = 3;
  _device->submitRequestAndBlock(request);
  _device->freeRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::setMouseSampleRate(UInt8 sampleRate)
{
  //
  // Instructs the mouse to change its sampling rate to the given value, in
  // reports per second.
  //
  // It is safe to issue this request from the interrupt/completion context.
  //

  PS2Request * request = _device->allocateRequest();

  // (set mouse sample rate command)
  request->commands[0].command = kPS2C_WriteCommandPort;
  request->commands[0].inOrOut = kCP_TransmitToMouse;
  request->commands[1].command = kPS2C_WriteDataPort;
  request->commands[1].inOrOut = kDP_SetMouseSampleRate;
  request->commands[2].command = kPS2C_ReadDataPortAndCompare;
  request->commands[2].inOrOut = kSC_Acknowledge;
  request->commands[3].command = kPS2C_WriteCommandPort;
  request->commands[3].inOrOut = kCP_TransmitToMouse;
  request->commands[4].command = kPS2C_WriteDataPort;
  request->commands[4].inOrOut = sampleRate;
  request->commands[5].command = kPS2C_ReadDataPortAndCompare;
  request->commands[5].inOrOut = kSC_Acknowledge;
  request->commandsCount = 6;
  _device->submitRequest(request); // asynchronous, auto-free'd
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::setMouseResolution(UInt8 resolution)
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

  PS2Request * request = _device->allocateRequest();

  // (set mouse resolution command)
  request->commands[0].command = kPS2C_WriteCommandPort;
  request->commands[0].inOrOut = kCP_TransmitToMouse;
  request->commands[1].command = kPS2C_WriteDataPort;
  request->commands[1].inOrOut = kDP_SetMouseResolution;
  request->commands[2].command = kPS2C_ReadDataPortAndCompare;
  request->commands[2].inOrOut = kSC_Acknowledge;
  request->commands[3].command = kPS2C_WriteCommandPort;
  request->commands[3].inOrOut = kCP_TransmitToMouse;
  request->commands[4].command = kPS2C_WriteDataPort;
  request->commands[4].inOrOut = resolution;
  request->commands[5].command = kPS2C_ReadDataPortAndCompare;
  request->commands[5].inOrOut = kSC_Acknowledge;
  request->commandsCount = 6;
  _device->submitRequest(request); // asynchronous, auto-free'd
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2MouseId ApplePS2Mouse::setIntellimouseMode()
{
  //
  // Determines whether this mouse is a Microsoft Intellimouse, and if it is,
  // it enables it (the mouse will send 4 byte packets for mouse events from
  // then on). Returns true if the Intellimouse mode was succesfully enabled.
  //
  // Do NOT issue this request from the interrupt/completion context.
  //

  UInt8      mouseIDByte;
  PS2MouseId mouseID;

  //
  // Generate the special command sequence to enable the 'Intellimouse' mode.
  // The sequence is to set the sampling rate to 200, 100, then 80, at which
  // point the mouse will start sending 4 byte packets for mouse events and
  // return a mouse ID of 3.
  //

  setMouseSampleRate(200);
  setMouseSampleRate(100);
  setMouseSampleRate(80 );

  //
  // Pause before sending the next command after switching mouse mode.
  //

  IOSleep(50);

  //
  // Determine whether we have an Intellimouse by asking for the mouse's ID.
  //

  mouseIDByte = getMouseID();
  switch(mouseIDByte)
  {
    // %%KCD - I have a device that (incorrectly?) responds
    // with the Intellimouse Explorer mouse ID in response to the standard
    // Intellimouse device query.  It also seems to then go into
    // five button mode in that case, so look for that here.
    case kMouseTypeIntellimouseExplorer:
      mouseID = kMouseTypeIntellimouseExplorer;
      break;
    case kMouseTypeIntellimouse:
      mouseID = kMouseTypeIntellimouse;
      break;
    default:
      mouseID = kMouseTypeStandard;
      break;
  }

  if (mouseID == kMouseTypeIntellimouse)
  {
    // Try to enter 5 button mode if we're not there already.
    // Same code as above, more or less.
    setMouseSampleRate(200);
    setMouseSampleRate(200);
    setMouseSampleRate(80 );
    IOSleep(50);

    mouseIDByte = getMouseID();
    switch (mouseIDByte)
    {
      case kMouseTypeIntellimouseExplorer:
        mouseID = kMouseTypeIntellimouseExplorer;
        break;
      default:
        mouseID = kMouseTypeIntellimouse;
        break;
    }
  }

  //
  // Restore the original sampling rate, before we obliterated it.
  //

  setMouseSampleRate(_mouseInfoBytes & 0x0000FF);

  return mouseID;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt32 ApplePS2Mouse::getMouseInformation()
{
  //
  // Asks the mouse to transmit its three information bytes.  Should the
  // mouse not respond, a value of (UInt32)(-1) is returned.
  //
  // Do NOT issue this request from the interrupt/completion context.
  //

  PS2Request * request     = _device->allocateRequest();
  UInt32       returnValue = (UInt32)(-1);

  // (get information command)
  request->commands[0].command = kPS2C_WriteCommandPort;
  request->commands[0].inOrOut = kCP_TransmitToMouse;
  request->commands[1].command = kPS2C_WriteDataPort;
  request->commands[1].inOrOut = kDP_GetMouseInformation;
  request->commands[2].command = kPS2C_ReadDataPortAndCompare;
  request->commands[2].inOrOut = kSC_Acknowledge;
  request->commands[3].command = kPS2C_ReadDataPort;
  request->commands[3].inOrOut = 0;
  request->commands[4].command = kPS2C_ReadDataPort;
  request->commands[4].inOrOut = 0;
  request->commands[5].command = kPS2C_ReadDataPort;
  request->commands[5].inOrOut = 0;
  request->commandsCount = 6;
  _device->submitRequestAndBlock(request);

  if (request->commandsCount == 6) // success?
  {
    returnValue = ((UInt32)request->commands[3].inOrOut << 16) |
                  ((UInt32)request->commands[4].inOrOut << 8 ) |
                  ((UInt32)request->commands[5].inOrOut);
  }
  _device->freeRequest(request);
  
  return returnValue;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt8 ApplePS2Mouse::getMouseID()
{
  //
  // Asks the mouse to transmit its identification byte.  Should the mouse
  // not respond, a value of (UInt8)(-1) is returned.
  //
  // Note that some documentation on PS/2 mice implies that two identification
  // bytes are returned and not one.  This was proven to be false in my tests.
  //
  // Do NOT issue this request from the interrupt/completion context.
  //

  PS2Request * request     = _device->allocateRequest();
  UInt8        returnValue = (UInt8)(-1);

  // (get information command)
  request->commands[0].command = kPS2C_WriteCommandPort;
  request->commands[0].inOrOut = kCP_TransmitToMouse;
  request->commands[1].command = kPS2C_WriteDataPort;
  request->commands[1].inOrOut = kDP_GetId;
  request->commands[2].command = kPS2C_ReadDataPortAndCompare;
  request->commands[2].inOrOut = kSC_Acknowledge;
  request->commands[3].command = kPS2C_ReadDataPort;
  request->commands[3].inOrOut = 0;
  request->commandsCount = 4;
  _device->submitRequestAndBlock(request);

  if (request->commandsCount == 4) // success?
    returnValue = request->commands[3].inOrOut;

  _device->freeRequest(request);

  return returnValue;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::setCommandByte(UInt8 setBits, UInt8 clearBits)
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

void ApplePS2Mouse::setDevicePowerState( UInt32 whatToDo )
{
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:

            // Disable mouse (synchronous).
            setMouseEnable( false );
            break;

        case kPS2C_EnableDevice:
            
            // Enable mouse and restore state.
            resetMouse();
            break;
    }
}
