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

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
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

  // find config specific to Platform Profile
  OSDictionary* list = OSDynamicCast(OSDictionary, dict->getObject(kPlatformProfile));
  OSDictionary* config = ApplePS2Controller::makeConfigurationNode(list);
  if (config)
  {
      // if DisableDevice is Yes, then do not load at all...
      OSBoolean* disable = OSDynamicCast(OSBoolean, config->getObject(kDisableDevice));
      if (disable && disable->isTrue())
      {
          config->release();
          return false;
      }
#ifdef DEBUG
      // save configuration for later/diagnostics...
      setProperty(kMergedConfiguration, config);
#endif
  }

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
  ignoreall                  = false;
  ledpresent                 = false;
  resmode                    = -1;
  forcesetres                = false;
  scrollres                  = 10;
  actliketrackpad            = false;
  keytime                    = 0;
  maxaftertyping             = 500000000;
  buttonmask                 = ~0;
  scroll                     = true;
  noled                      = false;
  wakedelay                  = 1000;
  usb_mouse_stops_trackpad   = true;
  mousecount                 = 0;
  _cmdGate = 0;
    
  // state for middle button
  _buttonTimer = 0;
  _mbuttonstate = STATE_NOBUTTONS;
  _pendingbuttons = 0;
  _buttontime = 0;
  _maxmiddleclicktime = 100000000;
 
  // load settings
  setParamPropertiesGated(config);
  OSSafeRelease(config);

  // remove some properties so system doesn't think it is a trackpad
  // this should cause "Product" = "Mouse" in ioreg.
  if (!actliketrackpad)
  {
    removeProperty("VendorID");
    removeProperty("ProductID");
    removeProperty("HIDPointerAccelerationType");
    removeProperty("HIDScrollAccelerationType");
    removeProperty("TrackpadScroll");
  }

  IOLog("VoodooPS2Mouse Version 1.8.9 loaded...\n");
	
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
        {"MouseCount",                      &mousecount},
        {"ButtonCount",                     &_buttonCount},
    };
    const struct {const char *name; int *var;} boolvars[]={
        {"ForceDefaultResolution",          &forceres},
        {"ForceSetResolution",              &forcesetres},
        {"ActLikeTrackpad",                 &actliketrackpad},
        {"DisableLEDUpdating",              &noled},
        {"FakeMiddleButton",                &_fakemiddlebutton},
    };
    const struct {const char* name; bool* var;} lowbitvars[]={
        {"TrackpadScroll",                  &scroll},
        {"OutsidezoneNoAction When Typing", &outzone_wt},
        {"PalmNoAction Permanent",          &palm},
        {"PalmNoAction When Typing",        &palm_wt},
        {"USBMouseStopsTrackpad",           &usb_mouse_stops_trackpad},
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
        {"MiddleClickTime",                 &_maxmiddleclicktime},
    };
    
    
    OSNumber *num;
    OSBoolean *bl;
    
    int oldmousecount = mousecount;
    bool old_usb_mouse_stops_trackpad = usb_mouse_stops_trackpad;
    
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
    
    // check for special terminating sequence from PS2Daemon
    if (-1 == mousecount)
    {
        // when system is shutting down/restarting we want to force LED off
        if (ledpresent && !noled)
            setTouchpadLED(0x10);
        mousecount = oldmousecount;
    }
    
    // disable trackpad when USB mouse is plugged in
    // check for mouse count changing...
    if ((oldmousecount != 0) != (mousecount != 0) || old_usb_mouse_stops_trackpad != usb_mouse_stops_trackpad)
    {
        // either last mouse removed or first mouse added
        ignoreall = (mousecount != 0) && usb_mouse_stops_trackpad;
        updateTouchpadLED();
    }
    
    // convert to IOFixed format...
    defres <<= 16;
}

IOReturn ApplePS2Mouse::setParamProperties(OSDictionary* dict)
{
    if (_cmdGate)
    {
        // syncronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Mouse::setParamPropertiesGated), dict);
    }
    
    return super::setParamProperties(dict);
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
    
  //
  // Check to see if acknowledges are being received for commands to the mouse.
  //

  // (get information command)
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
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
  device->submitRequestAndBlock(&request);

  DEBUG_LOG("ApplePS2Mouse::probe leaving.\n");
  return 6 == request.commandsCount ? this : 0;
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
    return false;
  }
  pWorkLoop->addEventSource(_cmdGate);
    
  //
  // Setup button timer event source
  //
  _buttonTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2Mouse::onButtonTimer));
  if (_buttonTimer)
      pWorkLoop->addEventSource(_buttonTimer);
    
  //
  // Lock the controller during initialization
  //
   
  _device->lock();
    
  //
  // Reset and enable the mouse.
  //

  resetMouse();

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
  // Install message hook for keyboard to trackpad communication
  //
  
  if (actliketrackpad)
  {
    _device->installMessageAction( this,
                                  OSMemberFunctionCast(PS2MessageAction, this, &ApplePS2Mouse::receiveMessage));
    _messageHandlerInstalled = true;
  }
    
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
  // Uinstall message handler.
  //
  if (_messageHandlerInstalled)
  {
    _device->uninstallMessageAction();
    _messageHandlerInstalled = false;
  }
    
  //
  // Release the pointer to the provider object.
  //

  OSSafeReleaseNULL(_device);;

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
    
  TPS2Request<8> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_SetDefaults;
  request.commands[2].command = kPS2C_WriteCommandPort;
  request.commands[2].inOrOut = kCP_TransmitToMouse;
  request.commands[3].command = kPS2C_WriteDataPort;
  request.commands[3].inOrOut = kDP_GetMouseInformation;
  request.commands[4].command = kPS2C_ReadDataPortAndCompare;
  request.commands[4].inOrOut = kSC_Acknowledge;
  request.commands[5].command = kPS2C_ReadDataPort;
  request.commands[5].inOrOut = 0;
  request.commands[6].command = kPS2C_ReadDataPort;
  request.commands[6].inOrOut = 0;
  request.commands[7].command = kPS2C_ReadDataPort;
  request.commands[7].inOrOut = 0;
  request.commandsCount = 8;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
  if (8 != request.commandsCount)
      DEBUG_LOG("%s: reset mouse sequence failed: %d\n", getName(), request.commandsCount);
  
  // Now deal with Synaptics specifics (ActLikeTrackpad trick)...
  ledpresent = false;
  do if (actliketrackpad && !noled)
  {
    // do Synaptics specific, but only if it is Synaptics device
    UInt8 buf3[3];
    if (!getTouchPadData(0x0, buf3) || (0x46 != buf3[1] && 0x47 != buf3[1]))
        break;
    // it is Synaptics, now test for LED capability...
    if (!getTouchPadData(0x2, buf3) || !(buf3[0] & 0x80))
        break;
    int nExtendedQueries = (buf3[0] & 0x70) >> 4;
    // check LED capability if query is supported
    if (nExtendedQueries >= 1 && getTouchPadData(0x9, buf3))
    {
        ledpresent = (buf3[0] >> 6) & 1;
        DEBUG_LOG("%s: ledpresent=%d\n", getName(), ledpresent);
    }
  } while (false);

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
    if (!actliketrackpad)
      setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDMouseAccelerationType);
    else
      setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
  }
  else
  {
    _packetLength = kPacketLengthStandard;

    removeProperty(kIOHIDScrollResolutionKey);
    removeProperty(kIOHIDScrollAccelerationTypeKey);
  }
  if (!actliketrackpad)
    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDMouseAccelerationType);
  else
    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);

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
    if (!_fakemiddlebutton || _buttonCount <= 2 || (ignoreall && fromMouse == from))
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
    if (!actliketrackpad || scroll)
       dz = (SInt16)(((SInt8)(packet[3] << 4)) >> 4);
  }

  buttons = middleButton(buttons, now_abs, fromMouse);
  lastbuttons = buttons;
    
  // ignore button 1 and 2 (could be simulated by trackpad) if just after typing
  if (palm_wt || outzone_wt)
  {
    if (now_ns-keytime <= maxaftertyping)
       buttonmask = ~(buttons & 0x3);
    else
       buttonmask = ~0;
    buttons &= buttonmask;
  }
    
  if (!ignoreall)
     dispatchRelativePointerEventX(dx, mouseyinverter*dy, buttons, now_abs);
    
  if ( dz && (!(palm_wt || outzone_wt) || now_ns-keytime > maxaftertyping))
  {
    //
    // The Z counter is negative on an upwards scroll (away from the user),
    // and positive when scrolling downwards. Invert this before passing to
    // HID/CG.
    //
    if (!ignoreall)
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
  TPS2Request<3> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = enable ? kDP_Enable : kDP_SetDefaultsAndDisable;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commandsCount = 3;
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
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_SetMouseSampleRate;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_WriteCommandPort;
  request.commands[3].inOrOut = kCP_TransmitToMouse;
  request.commands[4].command = kPS2C_WriteDataPort;
  request.commands[4].inOrOut = sampleRate;
  request.commands[5].command = kPS2C_ReadDataPortAndCompare;
  request.commands[5].inOrOut = kSC_Acknowledge;
  request.commandsCount = 6;
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
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_SetMouseResolution;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_WriteCommandPort;
  request.commands[3].inOrOut = kCP_TransmitToMouse;
  request.commands[4].command = kPS2C_WriteDataPort;
  request.commands[4].inOrOut = resolution;
  request.commands[5].command = kPS2C_ReadDataPortAndCompare;
  request.commands[5].inOrOut = kSC_Acknowledge;
  request.commandsCount = 6;
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
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
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

  if (request.commandsCount == 6) // success?
  {
    returnValue = ((UInt32)request.commands[3].inOrOut << 16) |
                  ((UInt32)request.commands[4].inOrOut << 8 ) |
                  ((UInt32)request.commands[5].inOrOut);
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
  TPS2Request<4> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_GetId;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_ReadDataPort;
  request.commands[3].inOrOut = 0;
  request.commandsCount = 4;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);

  if (request.commandsCount == 4) // success?
    returnValue = request.commands[3].inOrOut;

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

            // update touchpad LED after sleep
            updateTouchpadLED();
            break;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Mouse::receiveMessage(int message, void* data)
{
    //
    // Here is where we receive messages from the keyboard driver
    //
    // This allows for the keyboard driver to enable/disable the trackpad
    // when a certain keycode is pressed.
    //
    // It also allows the trackpad driver to learn the last time a key
    //  has been pressed, so it can implement various "ignore trackpad
    //  input while typing" options.
    //
    switch (message)
    {
        case kPS2M_getDisableTouchpad:
        {
            bool* pResult = (bool*)data;
            *pResult = !ignoreall;
            break;
        }
            
        case kPS2M_setDisableTouchpad:
        {
            bool enable = *((bool*)data);
            // ignoreall is true when trackpad has been disabled
            if (enable == ignoreall)
            {
                // save state, and update LED
                ignoreall = !enable;
                updateTouchpadLED();
            }
            break;
        }
            
        case kPS2M_notifyKeyPressed:
        {
            // just remember last time key pressed... this can be used in
            // interrupt handler to detect unintended input while typing
            PS2KeyInfo* pInfo = (PS2KeyInfo*)data;
            switch (pInfo->adbKeyCode)
            {
                    // don't store key time for modifier keys going down
                case 0x38:  // left shift
                case 0x3c:  // right shift
                case 0x3b:  // left control
                case 0x3e:  // right control
                case 0x3a:  // left alt (command)
                case 0x3d:  // right alt
                case 0x37:  // left windows (option)
                case 0x36:  // right windows
                    if (pInfo->goingDown)
                        break;
                default:
                    keytime = pInfo->time;
            }
            break;
        }
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// This code is specific to Synaptics Touchpads that have an LED indicator, but
//  it does no harm to Synaptics units that don't have an LED.
//
// Generally it is used to indicate that the touchpad has been made inactive.
//
// In the case of this package, we can disable the touchpad with both keyboard
//  and the touchpad itself.
//
// Linux sources were very useful in figuring this out...
// This patch to support HP Probook Synaptics LED in Linux was where found the
//  information:
// https://github.com/mmonaco/PKGBUILDs/blob/master/synaptics-led/synled.patch
//
// To quote from the email:
//
// From: Takashi Iwai <tiwai@suse.de>
// Date: Sun, 16 Sep 2012 14:19:41 -0600
// Subject: [PATCH] input: Add LED support to Synaptics device
//
// The new Synaptics devices have an LED on the top-left corner.
// This patch adds a new LED class device to control it.  It's created
// dynamically upon synaptics device probing.
//
// The LED is controlled via the command 0x0a with parameters 0x88 or 0x10.
// This seems only on/off control although other value might be accepted.
//
// The detection of the LED isn't clear yet.  It should have been the new
// capability bits that indicate the presence, but on real machines, it
// doesn't fit.  So, for the time being, the driver checks the product id
// in the ext capability bits and assumes that LED exists on the known
// devices.
//
// Signed-off-by: Takashi Iwai <tiwai@suse.de>
//

//REVIEW: this code copied from VoodooPS2SynapticsTouchPad.cpp
// would be nice to figure out how to share this code between the two kexts

void ApplePS2Mouse::updateTouchpadLED()
{
    if (ledpresent && !noled)
        setTouchpadLED(ignoreall ? 0x88 : 0x10);
}

bool ApplePS2Mouse::setTouchpadLED(UInt8 touchLED)
{
    TPS2Request<12> request;
    
    // send NOP before special command sequence
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetMouseScaling1To1;
    
    // 4 set resolution commands, each encode 2 data bits of LED level
    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_SetMouseResolution;
    request.commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut  = (touchLED >> 6) & 0x3;
    
    request.commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut  = kDP_SetMouseResolution;
    request.commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut  = (touchLED >> 4) & 0x3;
    
    request.commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut  = kDP_SetMouseResolution;
    request.commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut  = (touchLED >> 2) & 0x3;
    
    request.commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[7].inOrOut  = kDP_SetMouseResolution;
    request.commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[8].inOrOut  = (touchLED >> 0) & 0x3;
    
    // Set sample rate 10 (10 is command for setting LED)
    request.commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[9].inOrOut  = kDP_SetMouseSampleRate;
    request.commands[10].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[10].inOrOut = 10; // 0x0A command for setting LED
    
    // finally send NOP command to end the special sequence
    request.commands[11].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[11].inOrOut  = kDP_SetMouseScaling1To1;
    request.commandsCount = 12;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    return 12 == request.commandsCount;
}

bool ApplePS2Mouse::getTouchPadData(UInt8 dataSelector, UInt8 buf3[])
{
    TPS2Request<14> request;
    
    // Disable stream mode before the command sequence.
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
    
    // 4 set resolution commands, each encode 2 data bits.
    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_SetMouseResolution;
    request.commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut  = (dataSelector >> 6) & 0x3;
    
    request.commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut  = kDP_SetMouseResolution;
    request.commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut  = (dataSelector >> 4) & 0x3;
    
    request.commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut  = kDP_SetMouseResolution;
    request.commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut  = (dataSelector >> 2) & 0x3;
    
    request.commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[7].inOrOut  = kDP_SetMouseResolution;
    request.commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[8].inOrOut  = (dataSelector >> 0) & 0x3;
    
    // Read response bytes.
    request.commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[9].inOrOut  = kDP_GetMouseInformation;
    request.commands[10].command = kPS2C_ReadDataPort;
    request.commands[10].inOrOut = 0;
    request.commands[11].command = kPS2C_ReadDataPort;
    request.commands[11].inOrOut = 0;
    request.commands[12].command = kPS2C_ReadDataPort;
    request.commands[12].inOrOut = 0;
    request.commands[13].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[13].inOrOut = kDP_SetDefaultsAndDisable;
    request.commandsCount = 14;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    if (14 != request.commandsCount)
        return false;
    
    // store results
    buf3[0] = request.commands[10].inOrOut;
    buf3[1] = request.commands[11].inOrOut;
    buf3[2] = request.commands[12].inOrOut;
    
    return true;
}


