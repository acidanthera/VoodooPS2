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

#include <IOKit/IOService.h>

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/usb/IOUSBHostFamily.h>
#include <IOKit/usb/IOUSBHostHIDDevice.h>
#include <IOKit/bluetooth/BluetoothAssignedNumbers.h>
#include "VoodooPS2Controller.h"
#include "VoodooPS2Mouse.h"


// enable for mouse debugging
#ifdef DEBUG_MSG
#define DEBUG_VERBOSE
#endif

// =============================================================================
// ApplePS2Mouse Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2Mouse, IOHIPointing);

UInt32 ApplePS2Mouse::deviceType()  { return NX_EVS_DEVICE_TYPE_MOUSE; };
UInt32 ApplePS2Mouse::interfaceID() { return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

IOItemCount ApplePS2Mouse::buttonCount() { return _buttonCount; };
IOFixed     ApplePS2Mouse::resolution()  { return _resolution; };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Mouse::init(OSDictionary * dict)
{
  //
  // Initialize this object's minimal state.  This is invoked right after this
  // object is instantiated.
  //
    
  if (!super::init(dict))
      return false;


  // initialize state...
  _device                    = 0;
  _interruptHandlerInstalled = false;
  _packetByteCount           = 0;
  _lastdata                  = 0;
  _packetLength              = kPacketLengthStandard;
  defres					 = 150 << 16; // (default is 150 dpi; 6 counts/mm)
  forceres					 = false;
  mouseyinverter			 = 1;   // 1 for normal, -1 for inverting
  scrollyinverter            = 1;   // 1 for normal, -1 for inverting
  _type                      = kMouseTypeStandard;
  _buttonCount               = 3;
  _mouseInfoBytes            = (UInt32)-1;
  resmode                    = -1;
  forcesetres                = false;
  scrollres                  = 10;
  wakedelay                  = 1000;
  _cmdGate                   = 0;

  // state for middle button
  _buttonTimer = 0;
  _mbuttonstate = STATE_NOBUTTONS;
  _pendingbuttons = 0;
  _buttontime = 0;
  _maxmiddleclicktime = 100000000;

  // announce version
  extern kmod_info_t kmod_info;
  DEBUG_LOG("VoodooPS2Mouse: Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);

  return true;
}


void ApplePS2Mouse::setParamPropertiesGated(OSDictionary * config)
{
	if (NULL == config)
		return;
    
    const struct {const char *name; int *var;} int32vars[]={
        {"DefaultResolution",               &defres},
        {"ResolutionMode",                  &resmode},
        {"ScrollResolution",                &scrollres},
        {"MouseYInverter",                  &mouseyinverter},
        {"ScrollYInverter",                 &scrollyinverter},
        {"WakeDelay",                       &wakedelay},
        {"ButtonCount",                     &_buttonCount},
    };
    const struct {const char *name; int *var;} boolvars[]={
        {"ForceDefaultResolution",          &forceres},
        {"ForceSetResolution",              &forcesetres},
        {"FakeMiddleButton",                &_fakemiddlebutton},
    };
    const struct {const char* name; bool* var;} lowbitvars[]={
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
        {"MiddleClickTime",                 &_maxmiddleclicktime},
    };
    
    
    OSNumber *num;
    OSBoolean *bl;

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
    // lowbit config items
	for (int i = 0; i < countof(lowbitvars); i++)
		if ((num=OSDynamicCast (OSNumber,config->getObject(lowbitvars[i].name))))
        {
			*lowbitvars[i].var = (num->unsigned32BitValue()&0x1)?true:false;
            setProperty(lowbitvars[i].name, *lowbitvars[i].var ? 1 : 0, 32);
        }
    // 32-bit config items
    for (int i = 0; i < countof(int32vars);i++)
        if ((num=OSDynamicCast (OSNumber,config->getObject (int32vars[i].name))))
        {
            *int32vars[i].var = num->unsigned32BitValue();
            setProperty(int32vars[i].name, *int32vars[i].var, 32);
        }
    
    // convert to IOFixed format...
    defres <<= 16;
}

IOReturn ApplePS2Mouse::setParamProperties(OSDictionary* dict)
{
    ////IOReturn result = super::IOHIDevice::setParamProperties(dict);
    if (_cmdGate)
    {
        // syncronize through workloop...
        ////_cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Mouse::setParamPropertiesGated), dict);
        setParamPropertiesGated(dict);
    }
    
    return super::setParamProperties(dict);
    ////return result;
}

IOReturn ApplePS2Mouse::setProperties(OSObject *props)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, props);
    if (dict && _cmdGate)
    {
        // syncronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Mouse::setParamPropertiesGated), dict);
    }
    
	return super::setProperties(props);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2Mouse* ApplePS2Mouse::probe(IOService * provider, SInt32 * score)
{
  DEBUG_LOG("ApplePS2Mouse::probe entered...\n");
    
  //
  // The driver has been instructed to verify the presence of the actual
  // hardware we represent. We are guaranteed by the controller that the
  // mouse clock is enabled and the mouse itself is disabled (thus it
  // won't send any asynchronous mouse data that may mess up the
  // responses expected by the commands we send it).
  //

  if (!super::probe(provider, score))
      return 0;

  ApplePS2MouseDevice* device  = (ApplePS2MouseDevice*)provider;
    
  // find config specific to Platform Profile
  OSDictionary* list = OSDynamicCast(OSDictionary, getProperty(kPlatformProfile));
  OSDictionary* config = device->getController()->makeConfigurationNode(list, "Mouse");
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
    
    // load settings
    setParamPropertiesGated(config);
    OSSafeReleaseNULL(config);
  }

  //
  // Check to see if acknowledges are being received for commands to the mouse.
  //

  // (get information command)
  TPS2Request<5> request;
  request.commands[0].command = kPS2C_WriteDataPort;
  request.commands[0].inOrOut = kDP_GetMouseInformation;
  request.commands[1].command = kPS2C_ReadDataPortAndCompare;
  request.commands[1].inOrOut = kSC_Acknowledge;
  request.commands[2].command = kPS2C_ReadDataPort;
  request.commands[2].inOrOut = 0;
  request.commands[3].command = kPS2C_ReadDataPort;
  request.commands[3].inOrOut = 0;
  request.commands[4].command = kPS2C_ReadDataPort;
  request.commands[4].inOrOut = 0;
  request.commandsCount = 5;
  assert(request.commandsCount <= countof(request.commands));
  device->submitRequestAndBlock(&request);

  DEBUG_LOG("ApplePS2Mouse::probe leaving.\n");
  return 5 == request.commandsCount ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Mouse::start(IOService * provider)
{ 
  DEBUG_LOG("%s::start called\n", getName());
    
  //
  // The driver has been instructed to start. This is called after a
  // successful probe and match.
  //

  if (!super::start(provider))
      return false;

  //
  // Maintain a pointer to and retain the provider object.
  //

  _device = (ApplePS2MouseDevice *)provider;
  _device->retain();

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
    
  //
  // Reset and enable the mouse.
  //

  resetMouse();
	
  pWorkLoop->addEventSource(_cmdGate);

  //
  // Setup button timer event source
  //
  _buttonTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2Mouse::onButtonTimer));
  if (_buttonTimer)
	  pWorkLoop->addEventSource(_buttonTimer);

  //
  // Install our driver's interrupt handler, for asynchronous data delivery.
  //

  _device->installInterruptAction(this,
    OSMemberFunctionCast(PS2InterruptAction, this, &ApplePS2Mouse::interruptOccurred),
    OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2Mouse::packetReady));
  _interruptHandlerInstalled = true;

  // now safe to allow other threads
  _device->unlock();
    
  //
  // Install our power control handler.
  //

  _device->installPowerControlAction( this,OSMemberFunctionCast
           (PS2PowerControlAction,this, &ApplePS2Mouse::setDevicePowerState) );
  _powerControlHandlerInstalled = true;

  //
  // Request message registration for keyboard to trackpad communication
  //
  
  setProperty(kDeliverNotifications, true);

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
    if (_buttonTimer)
    {
      pWorkLoop->removeEventSource(_buttonTimer);
      _buttonTimer->release();
      _buttonTimer = 0;
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

void ApplePS2Mouse::resetMouse()
{
  DEBUG_LOG("%s::resetMouse called\n", getName());
    
  //
  // Reset the mouse to its default state.
  //

  // Contrary to what you might think kDP_SetDefaultsAndDisable does not set
  // relative mode (for trackpads), as kDP_SetDefaults does.  This is probably an
  // oversight/error in the naming of these constants.
  //
  // For the kDP_SetDefaults, there is no point in trying to read the ACK
  // ... it is just going to time out... and then later show up in the
  // input stream unexpectedly.
    
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteDataPort;
  request.commands[0].inOrOut = kDP_SetDefaults;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_GetMouseInformation;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_ReadDataPort;
  request.commands[3].inOrOut = 0;
  request.commands[4].command = kPS2C_ReadDataPort;
  request.commands[4].inOrOut = 0;
  request.commands[5].command = kPS2C_ReadDataPort;
  request.commands[5].inOrOut = 0;
  request.commandsCount = 6;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
  if (6 != request.commandsCount)
      DEBUG_LOG("%s: reset mouse sequence failed: %d\n", getName(), request.commandsCount);

  //
  // Obtain our mouse's resolution and sampling rate.
  //
    
  if (_mouseInfoBytes == (UInt32)-1)
  {
    // attempt switch to high resolution
    if (forcesetres && resmode != -1)
        setMouseResolution(resmode);
      
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
    DEBUG_LOG("%s: _resolution=0x%x\n", getName(), _resolution);
  }
  else
  {
    setMouseResolution((_mouseInfoBytes >> 8) & 3);
  }

  //
  // Enable the Intellimouse mode, should this be an Intellimouse.
  //

  _type = setIntellimouseMode();
  if (kMouseTypeStandard != _type)
  {
    _packetLength = kPacketLengthIntellimouse;
    if (kMouseTypeIntellimouseExplorer == _type)
      _buttonCount = 5;
    else
      _buttonCount = 3;

    //
    // Report the resolution of the scroll wheel. This property must
    // be present to enable acceleration for Z-axis movement.
    //
    setProperty(kIOHIDScrollResolutionKey, (scrollres << 16), 32);
    setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDMouseAccelerationType);
  }
  else
  {
    _packetLength = kPacketLengthStandard;

    removeProperty(kIOHIDScrollResolutionKey);
    removeProperty(kIOHIDScrollAccelerationTypeKey);
  }
  
  setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDMouseAccelerationType);

  // simulate three buttons with only two buttons if enabled
    
  if (2 == _buttonCount && _fakemiddlebutton && _buttonTimer)
     _buttonCount = 3;
    
  // initialize packet buffer
    
  _packetByteCount = 0;
  _ringBuffer.reset();

  //
  // Finally, we enable the mouse itself, so that it may start reporting
  // mouse events.
  //
    
  setMouseEnable(true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::initMouse()
{
  DEBUG_LOG("%s::initMouse called\n", getName());
    
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

PS2InterruptResult ApplePS2Mouse::interruptOccurred(UInt8 data)      // PS2InterruptAction
{
    //
    // This will be invoked automatically from our device when asynchronous mouse
    // needs to be delivered.  Process the mouse data.
    //
    
    UInt8* packet = _ringBuffer.head();
    
    // special case for $AA $00, spontaneous reset (usually due to static electricity)
    if (kSC_Reset == _lastdata && 0x00 == data)
    {
        IOLog("%s: Unexpected reset (%02x %02x) request from PS/2 controller\n", getName(), _lastdata, data);
        
        // spontaneous reset, device has announced with $AA $00, schedule a reset
        packet[0] = 0x00;
        packet[1] = kSC_Reset;
        _ringBuffer.advanceHead(kPacketLengthMax);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }
    _lastdata = data;
    
    // We ignore all bytes until we see the start of a packet, otherwise the mouse
    // packets may get out of sequence and things will get very confusing.
    //
    if (_packetByteCount == 0 && ((data == kSC_Acknowledge) || !(data & 0x08)))
    {
        IOLog("%s: Unexpected byte0 data (%02x) from PS/2 controller\n", getName(), data);
        
        //
        // Reset the mouse when packet synchronization is lost. Limit the number
        // of consecutive resets to guard against flaky hardware.
        //
        
        if (_mouseResetCount < 5)
        {
            _mouseResetCount++;
            packet[0] = 0x00;
            packet[1] = kSC_Acknowledge;
            _ringBuffer.advanceHead(kPacketLengthMax);
            return kPS2IR_packetReady;
        }
        return kPS2IR_packetBuffering;
    }
    
    //
    // Add this byte to the packet buffer.  If the packet is complete, that is,
    // we have the three (or four) bytes, dispatch this packet for processing.
    //
    
    packet[_packetByteCount++] = data;
    if (_packetByteCount == _packetLength)
    {
        _mouseResetCount = 0;
        _ringBuffer.advanceHead(kPacketLengthMax);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }
    return kPS2IR_packetBuffering;
}

void ApplePS2Mouse::packetReady()
{
    // empty the ring buffer, dispatching each packet...
    // all packets are kPacketLengthMax even if _packetLength is smaller, as they
    // are padded at interrupt time.
    while (_ringBuffer.count() >= kPacketLengthMax)
    {
        UInt8* packet = _ringBuffer.tail();
        if (0x00 != packet[0])
        {
            // normal packet with deltas
            dispatchRelativePointerEventWithPacket(_ringBuffer.tail(), _packetLength);
        }
        else
        {
            ////initMouse();
        }
        _ringBuffer.advanceTail(kPacketLengthMax);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::onButtonTimer(void)
{
	uint64_t now_abs;
	clock_get_uptime(&now_abs);
    
    middleButton(lastbuttons, now_abs, fromTimer);
}

UInt32 ApplePS2Mouse::middleButton(UInt32 buttons, uint64_t now_abs, MBComingFrom from)
{
    if (!_fakemiddlebutton || _buttonCount <= 2)
        return buttons;
    
    // cancel timer if we see input before timeout has fired, but after expired
    bool timeout = false;
    uint64_t now_ns;
    absolutetime_to_nanoseconds(now_abs, &now_ns);
    if (fromTimer == from || now_ns - _buttontime > _maxmiddleclicktime)
        timeout = true;
    
    //
    // A state machine to simulate middle buttons with two buttons pressed
    // together.
    //
    switch (_mbuttonstate)
    {
        // no buttons down, waiting for something to happen
        case STATE_NOBUTTONS:
            if (buttons & 0x4)
                _mbuttonstate = STATE_NOOP;
            else if (0x3 == buttons)
                _mbuttonstate = STATE_MIDDLE;
            else if (0x0 != buttons)
            {
                // only single button, so delay this for a bit
                _pendingbuttons = buttons;
                _buttontime = now_ns;
                setTimerTimeout(_buttonTimer, _maxmiddleclicktime);
                _mbuttonstate = STATE_WAIT4TWO;
            }
            break;
            
        // waiting for second button to come down or timeout
        case STATE_WAIT4TWO:
            if (!timeout && 0x3 == buttons)
            {
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                _mbuttonstate = STATE_MIDDLE;
            }
            else if (timeout || buttons != _pendingbuttons)
            {
                if (fromTimer == from || !(buttons & _pendingbuttons))
                    dispatchRelativePointerEventX(0, 0, buttons|_pendingbuttons, now_abs);
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                if (0x0 == buttons)
                    _mbuttonstate = STATE_NOBUTTONS;
                else
                    _mbuttonstate = STATE_NOOP;
            }
            break;
            
        // both buttons down and delivering middle button
        case STATE_MIDDLE:
            if (0x0 == buttons)
                _mbuttonstate = STATE_NOBUTTONS;
            else if (0x3 != (buttons & 0x3))
            {
                // only single button, so delay to see if we get to none
                _pendingbuttons = buttons;
                _buttontime = now_ns;
                setTimerTimeout(_buttonTimer, _maxmiddleclicktime);
                _mbuttonstate = STATE_WAIT4NONE;
            }
            break;
            
        // was middle button, but one button now up, waiting for second to go up
        case STATE_WAIT4NONE:
            if (!timeout && 0x0 == buttons)
            {
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                _mbuttonstate = STATE_NOBUTTONS;
            }
            else if (timeout || buttons != _pendingbuttons)
            {
                if (fromTimer == from)
                    dispatchRelativePointerEventX(0, 0, buttons|_pendingbuttons, now_abs);
                _pendingbuttons = 0;
                cancelTimer(_buttonTimer);
                if (0x0 == buttons)
                    _mbuttonstate = STATE_NOBUTTONS;
                else
                    _mbuttonstate = STATE_NOOP;
            }
            break;
            
        case STATE_NOOP:
            if (0x0 == buttons)
                _mbuttonstate = STATE_NOBUTTONS;
            break;
    }
    
    // modify buttons after new state set
    switch (_mbuttonstate)
    {
        case STATE_MIDDLE:
            buttons = 0x4;
            break;
            
        case STATE_WAIT4NONE:
        case STATE_WAIT4TWO:
            buttons &= ~0x3;
            break;
            
        case STATE_NOBUTTONS:
        case STATE_NOOP:
            break;
    }
    
    // return modified buttons
    return buttons;
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

  UInt32 buttons = packet[0] & 0x7;
  SInt32 dx = ((packet[0] & 0x10) ? 0xffffff00 : 0 ) | packet[1];
  SInt32 dy = -(((packet[0] & 0x20) ? 0xffffff00 : 0 ) | packet[2]);
  SInt16 dz = 0;

  uint64_t now_abs;
  clock_get_uptime(&now_abs);
  uint64_t now_ns;
  absolutetime_to_nanoseconds(now_abs, &now_ns);
    
  if ( packetSize > 3 )
  {
    // Pull out fourth and fifth buttons.
    if (_type == kMouseTypeIntellimouseExplorer)
    {
       buttons |= (packet[3] & 0x30) >> 1;
       //if (packet[3] & 0x10) buttons |= 0x8;  // fourth button (bit 4 in packet)
       //if (packet[3] & 0x20) buttons |= 0x10; // fifth button  (bit 5 in packet)
    }

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
  }

  buttons = middleButton(buttons, now_abs, fromMouse);
  lastbuttons = buttons;
    
  dispatchRelativePointerEventX(dx, mouseyinverter*dy, buttons, now_abs);
    
  if (dz)
  {
    //
    // The Z counter is negative on an upwards scroll (away from the user),
    // and positive when scrolling downwards. Invert this before passing to
    // HID/CG.
    //
    dispatchScrollWheelEventX(-scrollyinverter*dz, 0, 0, now_abs);
  }

#ifdef DEBUG_VERBOSE
  IOLog("ps2m: dx=%d, dy=%d, dz=%d, buttons=%d\n", dx, dy, dz, buttons);
#endif
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

  // (mouse enable/disable command)
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

void ApplePS2Mouse::setMouseSampleRate(UInt8 sampleRate)
{
  DEBUG_LOG("%s::setMouseSampleRate(0x%x)\n", getName(), sampleRate);
    
  //
  // Instructs the mouse to change its sampling rate to the given value, in
  // reports per second.
  //
  // It is safe to issue this request from the interrupt/completion context.
  //

  // (set mouse sample rate command)
  TPS2Request<4> request;
  request.commands[0].command = kPS2C_WriteDataPort;
  request.commands[0].inOrOut = kDP_SetMouseSampleRate;
  request.commands[1].command = kPS2C_ReadDataPortAndCompare;
  request.commands[1].inOrOut = kSC_Acknowledge;
  request.commands[2].command = kPS2C_WriteDataPort;
  request.commands[2].inOrOut = sampleRate;
  request.commands[3].command = kPS2C_ReadDataPortAndCompare;
  request.commands[3].inOrOut = kSC_Acknowledge;
  request.commandsCount = 4;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
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
    
  DEBUG_LOG("%s::setMouseResolution(0x%x)\n", getName(), resolution);

  // (set mouse resolution command)
  TPS2Request<4> request;
  request.commands[0].command = kPS2C_WriteDataPort;
  request.commands[0].inOrOut = kDP_SetMouseResolution;
  request.commands[1].command = kPS2C_ReadDataPortAndCompare;
  request.commands[1].inOrOut = kSC_Acknowledge;
  request.commands[2].command = kPS2C_WriteDataPort;
  request.commands[2].inOrOut = resolution;
  request.commands[3].command = kPS2C_ReadDataPortAndCompare;
  request.commands[3].inOrOut = kSC_Acknowledge;
  request.commandsCount = 4;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
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

  DEBUG_LOG("%s::setIntellimouseMode() returns 0x%x\n", getName(), mouseID);
    
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

  UInt32       returnValue = (UInt32)(-1);

  // (get information command)
  TPS2Request<5> request;
  request.commands[0].command = kPS2C_WriteDataPort;
  request.commands[0].inOrOut = kDP_GetMouseInformation;
  request.commands[1].command = kPS2C_ReadDataPortAndCompare;
  request.commands[1].inOrOut = kSC_Acknowledge;
  request.commands[2].command = kPS2C_ReadDataPort;
  request.commands[2].inOrOut = 0;
  request.commands[3].command = kPS2C_ReadDataPort;
  request.commands[3].inOrOut = 0;
  request.commands[4].command = kPS2C_ReadDataPort;
  request.commands[4].inOrOut = 0;
  request.commandsCount = 5;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);

  if (request.commandsCount == 5) // success?
  {
    returnValue = ((UInt32)request.commands[2].inOrOut << 16) |
                  ((UInt32)request.commands[3].inOrOut << 8 ) |
                  ((UInt32)request.commands[4].inOrOut);
  }
  
  DEBUG_LOG("%s::getMouseInformation() returns 0x%x\n", getName(), returnValue);
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

  UInt8        returnValue = (UInt8)(-1);

  // (get information command)
  TPS2Request<3> request;
  request.commands[0].command = kPS2C_WriteDataPort;
  request.commands[0].inOrOut = kDP_GetId;
  request.commands[1].command = kPS2C_ReadDataPortAndCompare;
  request.commands[1].inOrOut = kSC_Acknowledge;
  request.commands[2].command = kPS2C_ReadDataPort;
  request.commands[2].inOrOut = 0;
  request.commandsCount = 3;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);

  if (request.commandsCount == 3) // success?
    returnValue = request.commands[2].inOrOut;

  DEBUG_LOG("%s::getMouseID returns 0x%x\n", getName(), returnValue);
  return returnValue;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::setDevicePowerState( UInt32 whatToDo )
{
    DEBUG_LOG("%s::setDevicePowerState(0x%x)\n", getName(), whatToDo);
    
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            // Disable mouse (synchronous).
            setMouseEnable( false );
            break;

        case kPS2C_EnableDevice:
            // Allow time for device to initialize
            IOSleep(wakedelay);
            
            // Enable mouse and restore state.
            resetMouse();
            break;
    }
}
