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

#define DISABLE_CLOCKS_IRQS_BEFORE_SLEEP 1
#define FULL_INIT_AFTER_WAKE 1

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "ApplePS2KeyboardDevice.h"
#include "ApplePS2MouseDevice.h"
#include "VoodooPS2Controller.h"

//REVIEW: avoids problem with Xcode 5.1.0 where -dead_strip eliminates these required symbols
#include <libkern/OSKextLib.h>
void* _org_rehabman_dontstrip_[] =
{
    (void*)&OSKextGetCurrentIdentifier,
    (void*)&OSKextGetCurrentLoadTag,
    (void*)&OSKextGetCurrentVersionString,
};

enum {
    kPS2PowerStateSleep  = 0,
    kPS2PowerStateDoze   = 1,
    kPS2PowerStateNormal = 2,
    kPS2PowerStateCount
};

static const IOPMPowerState PS2PowerStateArray[ kPS2PowerStateCount ] =
{
    { 1,0,0,0,0,0,0,0,0,0,0,0 },
    { 1,kIOPMDeviceUsable, kIOPMDoze, kIOPMDoze, 0,0,0,0,0,0,0,0 },
    { 1,kIOPMDeviceUsable, IOPMPowerOn, IOPMPowerOn, 0,0,0,0,0,0,0,0 }
};

// =============================================================================
// Interrupt-Time Support Functions
//

//static
void ApplePS2Controller::interruptHandlerMouse(OSObject*, void* refCon, IOService*, int)
{
  ApplePS2Controller* me = (ApplePS2Controller*)refCon;
  if (me->_ignoreInterrupts)
    return;
    
  //
  // Wake our workloop to service the interrupt.    This is an edge-triggered
  // interrupt, so returning from this routine without clearing the interrupt
  // condition is perfectly normal.
  //
#if HANDLE_INTERRUPT_DATA_LATER
  me->_interruptSourceMouse->interruptOccurred(0, 0, 0);
#else
  me->handleInterrupt(kDT_Mouse);
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//static
void ApplePS2Controller::interruptHandlerKeyboard(OSObject*, void* refCon, IOService*, int)
{
  ApplePS2Controller* me = (ApplePS2Controller*)refCon;
  if (me->_ignoreInterrupts)
    return;
    
#if DEBUGGER_SUPPORT
  //
  // The keyboard interrupt handler reads in the pending scan code and stores
  // it on our internal queue; should it completes a debugger escape sequence,
  // we jump to the debugger function immediately.
  //

  UInt8 key;
  UInt8 status;
  int   state;

  // Lock out the keyboard interrupt handler [redundant here] and claim
  // exclusive access to the internal keyboard queue.

  me->lockController(&state);

  // Verify that data is available on the controller's input port.

  if ( ((status = inb(kCommandPort)) & kOutputReady) )
  {
    // Verify that the data is keyboard data, otherwise call mouse handler.
    // This case should never really happen, but if it does, we handle it.

    if ( (status & kMouseData) )
    {
      interruptHandlerMouse(0, refCon, 0, 0);
    }
    else
    {
      // Retrieve the keyboard data on the controller's input port.

      IODelay(kDataDelay);
      key = inb(kDataPort);

      // Call the debugger-key-sequence checking code (if a debugger sequence
      // completes, the debugger function will be invoked immediately within
      // doEscape).  The doEscape call may insist that we drop the scan code
      // we just received in some cases (a true return) -- we don't question
      // it's judgement and comply. No escape check if debugging is disabled.

      if (me->_debuggingEnabled == false || me->doEscape(key) == false)
        me->enqueueKeyboardData(key);

      // In all cases, we wake up our workloop to service the interrupt data.
      me->_interruptSourceKeyboard->interruptOccurred(0, 0, 0);
    }
  }

  // Remove the lockout on the keyboard interrupt handler [ineffective here]
  // and release our exclusive access to the internal keyboard queue.

  me->unlockController(state);
#else
  //
  // Wake our workloop to service the interrupt.    This is an edge-triggered
  // interrupt, so returning from this routine without clearing the interrupt
  // condition is perfectly normal.
  //
#if HANDLE_INTERRUPT_DATA_LATER
  me->_interruptSourceKeyboard->interruptOccurred(0, 0, 0);
#else
  me->handleInterrupt(kDT_Keyboard);
#endif

#endif //DEBUGGER_SUPPORT
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#if WATCHDOG_TIMER

void ApplePS2Controller::onWatchdogTimer()
{
    if (!_ignoreInterrupts)
        handleInterrupt(kDT_Watchdog);
    _watchdogTimer->setTimeoutMS(kWatchdogTimerInterval);
}

#endif // WATCHDOG_TIMER

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#if !HANDLE_INTERRUPT_DATA_LATER

void ApplePS2Controller::handleInterrupt(PS2DeviceType deviceType)
{
    ////IOLog("%s:handleInterrupt(%s)\n", getName(), deviceType == kDT_Keyboard ? "kDT_Keyboard" : deviceType == kDT_Watchdog ? "kDT_Watchdog" : "kDT_Mouse");

    // Loop only while there is data currently on the input stream.
    
    bool wakeMouse = false;
    bool wakeKeyboard = false;
    while (1)
    {
        // while getting status and reading the port, no interrupts...
        bool enable = ml_set_interrupts_enabled(false);
        IODelay(kDataDelay);
        UInt8 status = inb(kCommandPort);
        if (!(status & kOutputReady))
        {
            // no data available, so break out and return
            ml_set_interrupts_enabled(enable);
            break;
        }
        
#if WATCHDOG_TIMER
        // do not process mouse data in watchdog timer
        if (deviceType == kDT_Watchdog && (status & kMouseData))
        {
            ml_set_interrupts_enabled(enable);
            break;
        }
#endif
        
        // read the data
        IODelay(kDataDelay);
        UInt8 data = inb(kDataPort);
        
        // now ok for interrupts, we have read status, and found data...
        // (it does not matter [too much] if keyboard data is delivered out of order)
        ml_set_interrupts_enabled(enable);
        
#if WATCHDOG_TIMER
        //REVIEW: remove this debug eventually...
        if (deviceType == kDT_Watchdog)
            IOLog("%s:handleInterrupt(kDT_Watchdog): %s = %02x\n", getName(), status & kMouseData ? "mouse" : "keyboard", data);
#endif
        if (status & kMouseData)
        {
            // Dispatch the data to the mouse driver.
            if (kPS2IR_packetReady == _dispatchDriverInterrupt(kDT_Mouse, data))
                wakeMouse = true;
        }
        else
        {
            // Dispatch the data to the keyboard driver.
            if (kPS2IR_packetReady == _dispatchDriverInterrupt(kDT_Keyboard, data))
                wakeKeyboard = true;
        }
    } // while (forever)
    
    // wake up workloop based mouse interrupt source if needed
    if (wakeMouse)
        _interruptSourceMouse->interruptOccurred(0, 0, 0);
    // wake up workloop based keyboard interrupt source if needed
    if (wakeKeyboard)
        _interruptSourceKeyboard->interruptOccurred(0, 0, 0);
}

#else // HANDLE_INTERRUPT_DATA_LATER

void ApplePS2Controller::handleInterrupt(PS2DeviceType deviceType)
{
    ////IOLog("%s:handleInterrupt(%s)\n", getName(), deviceType == kDT_Keyboard ? "kDT_Keyboard" : deviceType == kDT_Watchdog ? "kDT_Watchdog" : "kDT_Mouse");
    
    // Loop only while there is data currently on the input stream.
    
    UInt8 status;
    IODelay(kDataDelay);
    while ((status = inb(kCommandPort)) & kOutputReady)
    {
#if WATCHDOG_TIMER
        if (deviceType == kDT_Watchdog && (status & kMouseData))
            break;
#endif
        
        IODelay(kDataDelay);
        UInt8 data = inb(kDataPort);
#if WATCHDOG_TIMER
        //REVIEW: remove this debug eventually...
        if (deviceType == kDT_Watchdog)
            IOLog("%s:handleInterrupt(kDT_Watchdog): %s = %02x\n", getName(), status & kMouseData ? "mouse" : "keyboard", data);
#endif
        dispatchDriverInterrupt(status & kMouseData ? kDT_Mouse : kDT_Keyboard, data);
        IODelay(kDataDelay);
    }
}

#endif // HANDLE_INTERRUPT_DATA_LATER

// =============================================================================
// ApplePS2Controller Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2Controller, IOService);

bool ApplePS2Controller::init(OSDictionary* dict)
{
  if (!super::init(dict))
      return false;

#if 0
  IOLog("PS2Request: %lu\n", sizeof(PS2Request));
  IOLog("TPS2Request<0>: %lu\n", sizeof(TPS2Request<0>));
  IOLog("TPS2Request<1>: %lu\n", sizeof(TPS2Request<1>));
  IOLog("offsetof(PS2Request,commands): %lu\n", offsetof(PS2Request, commands));
  IOLog("offsetof(TPS2Request<0>,commands): %lu\n", offsetof(TPS2Request<0>, commands));
  IOLog("offsetof(TPS2Request<1>,commands): %lu\n", offsetof(TPS2Request<1>, commands));
#endif
  // verify that compiler is working correctly wrt PS2Request/TPS2Request
  if (sizeof(PS2Request) != sizeof(TPS2Request<0>))
  {
      IOLog("ApplePS2Controller::init: PS2Request size mismatch (%lu != %lu)\n",
            sizeof(PS2Request), sizeof(TPS2Request<0>));
      return false;
  }
  if (offsetof(PS2Request,commands) != offsetof(TPS2Request<>,commands))
  {
      IOLog("ApplePS2Controller::init: PS2Request.commands offset mismatch (%lu != %lu)\n",
            offsetof(PS2Request,commands), offsetof(PS2Request,commands));
      return false;
  }

  //
  // Initialize minimal state.
  //
  _cmdbyteLock = IOLockAlloc();
  if (!_cmdbyteLock)
      return false;

  _workLoop                = 0;

  _interruptSourceKeyboard = 0;
  _interruptSourceMouse    = 0;
  _interruptTargetKeyboard = 0;
  _interruptTargetMouse    = 0;
  _interruptActionKeyboard = NULL;
  _interruptActionMouse    = NULL;
  _packetActionKeyboard    = NULL;
  _packetActionMouse       = NULL;
  _interruptInstalledKeyboard = false;
  _interruptInstalledMouse    = false;
  _ignoreInterrupts = 0;
  _ignoreOutOfOrder = 0;
    
  _powerControlTargetKeyboard = 0;
  _powerControlTargetMouse = 0;
  _powerControlActionKeyboard = 0;
  _powerControlActionMouse = 0;
  _powerControlInstalledKeyboard = false;
  _powerControlInstalledMouse = false;
    
  _messageTargetKeyboard = 0;
  _messageTargetMouse = 0;
  _messageActionKeyboard = 0;
  _messageActionMouse = 0;
  _messageInstalledKeyboard = false;
  _messageInstalledMouse = false;

  _mouseDevice    = 0;
  _keyboardDevice = 0;
  
  _suppressTimeout = false;

#ifdef NEWIRQ
  _newIRQLayout = false;	// turbo
#endif
    
  _wakedelay = 10;
  _mouseWakeFirst = false;
  _cmdGate = 0;
    
  _requestQueueLock = 0;

#if WATCHDOG_TIMER
  _watchdogTimer = 0;
#endif
  _rmcfCache = 0;
    
  queue_init(&_requestQueue);

  _currentPowerState = kPS2PowerStateNormal;
  
#if DEBUGGER_SUPPORT
  _extendedState = false;
  _modifierState = 0x00;
  _debuggingEnabled = false;

  _keyboardQueueAlloc = NULL;
  queue_init(&_keyboardQueue);
  queue_init(&_keyboardQueueUnused);

  _controllerLock = IOSimpleLockAlloc();
  if (!_controllerLock) return false;
#endif //DEBUGGER_SUPPORT
    
  return true;
}

ApplePS2Controller* ApplePS2Controller::probe(IOService* provider, SInt32* probeScore)
{
    if (!super::probe(provider, probeScore))
        return 0;

    // find config specific to Platform Profile
    OSDictionary* list = OSDynamicCast(OSDictionary, getProperty(kPlatformProfile));
    OSDictionary* config = makeConfigurationNode(list, "Controller");
#ifdef DEBUG
    if (config)
        setProperty(kMergedConfiguration, config);
#endif
    setPropertiesGated(config);
    OSSafeReleaseNULL(config);

    return this;
}

void ApplePS2Controller::free(void)
{
    if (_cmdbyteLock)
    {
        IOLockFree(_cmdbyteLock);
        _cmdbyteLock = 0;
    }
#if DEBUGGER_SUPPORT
    if (_controllerLock)
    {
        IOSimpleLockFree(_controllerLock);
        _controllerLock = 0;
    }
#endif
    super::free();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2Controller::setPropertiesGated(OSObject* props)
{
    OSDictionary* dict = OSDynamicCast(OSDictionary, props);
    if (!dict)
        return kIOReturnSuccess;
    
    // get wakedelay
	if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject("WakeDelay")))
    {
		_wakedelay = (int)num->unsigned32BitValue();
        setProperty("WakeDelay", _wakedelay, 32);
    }
    // get mouseWakeFirst
    if (OSBoolean* flag = OSDynamicCast(OSBoolean, dict->getObject("MouseWakeFirst")))
    {
        _mouseWakeFirst = flag->isTrue();
        setProperty("MouseWakeFirst", _mouseWakeFirst);
    }
    return kIOReturnSuccess;
}

IOReturn ApplePS2Controller::setProperties(OSObject* props)
{
    if (_cmdGate)
    {
        // syncronize through workloop...
        IOReturn result = _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Controller::setPropertiesGated), props);
        if (kIOReturnSuccess != result)
            return result;
    }
    return kIOReturnSuccess;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::resetController(void)
{
    _suppressTimeout = true;
    UInt8 commandByte;
    
    // Disable keyboard and mouse
    writeCommandPort(kCP_DisableKeyboardClock);
    writeCommandPort(kCP_DisableMouseClock);
    // Flush any data
    while ( inb(kCommandPort) & kOutputReady )
    {
        IODelay(kDataDelay);
        inb(kDataPort);
        IODelay(kDataDelay);
    }
    writeCommandPort(kCP_EnableMouseClock);
    writeCommandPort(kCP_EnableKeyboardClock);
    // Read current command
    writeCommandPort(kCP_GetCommandByte);
    commandByte = readDataPort(kDT_Keyboard);
    DEBUG_LOG("%s: initial commandByte = %02x\n", getName(), commandByte);
    // Issue Test Controller to try to reset device
    writeCommandPort(kCP_TestController);
    readDataPort(kDT_Keyboard);
    readDataPort(kDT_Mouse);
    // Issue Test Keyboard Port to try to reset device
    writeCommandPort(kCP_TestKeyboardPort);
    readDataPort(kDT_Keyboard);
    // Issue Test Mouse Port to try to reset device
    writeCommandPort(kCP_TestMousePort);
    readDataPort(kDT_Mouse);
    _suppressTimeout = false;
    
    //
    // Initialize the mouse and keyboard hardware to a known state --  the IRQs
    // are disabled (don't want interrupts), the clock line is enabled (want to
    // be able to send commands), and the device itself is disabled (don't want
    // asynchronous data arrival for key/mouse events).  We call the read/write
    // port routines directly, since no other thread will conflict with us.
    //
    commandByte &= ~(kCB_EnableKeyboardIRQ | kCB_EnableMouseIRQ | kCB_DisableMouseClock | kCB_DisableMouseClock);
    ////commandByte |= kCB_EnableKeyboardIRQ | kCB_EnableMouseIRQ;
    commandByte |= kCB_TranslateMode;
    writeCommandPort(kCP_SetCommandByte);
    writeDataPort(commandByte);
    DEBUG_LOG("%s: new commandByte = %02x\n", getName(), commandByte);
    
    writeDataPort(kDP_SetDefaultsAndDisable);
    readDataPort(kDT_Keyboard);       // (discard acknowledge; success irrelevant)
    
    writeCommandPort(kCP_TransmitToMouse);
    writeDataPort(kDP_SetDefaultsAndDisable);
    readDataPort(kDT_Mouse);          // (discard acknowledge; success irrelevant)
    
    //
    // Clear out garbage in the controller's input streams, before starting up
    // the work loop.
    //
    
    while ( inb(kCommandPort) & kOutputReady )
    {
        IODelay(kDataDelay);
        inb(kDataPort);
        IODelay(kDataDelay);
    }
}

// -- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Controller::start(IOService * provider)
{
  DEBUG_LOG("ApplePS2Controller::start entered...\n");
    
  // place version/build info in ioreg properties RM,Build and RM,Version
  extern kmod_info_t kmod_info;
  char buf[128];
  snprintf(buf, sizeof(buf), "%s %s", kmod_info.name, kmod_info.version);
  setProperty("RM,Version", buf);
#ifdef DEBUG
  setProperty("RM,Build", "Debug-" LOGNAME);
#else
  setProperty("RM,Build", "Release-" LOGNAME);
#endif

 //
 // The driver has been instructed to start.  Allocate all our resources.
 //
 if (!super::start(provider))
     return false;

#if DEBUGGER_SUPPORT
  // Enable special key sequence to enter debugger if debug boot-arg was set.
  int debugFlag = 0;
#ifdef TIGER
  PE_parse_boot_arg("debug", &debugFlag);
#else
  PE_parse_boot_argn("debug", &debugFlag, sizeof(debugFlag));
#endif
  if (debugFlag) _debuggingEnabled = true;

  _keyboardQueueAlloc = new KeyboardQueueElement[kKeyboardQueueSize];
  if (!_keyboardQueueAlloc)  goto fail;

  // Add the allocated keyboard queue entries to "unused" queue.
  for (int index = 0; index < kKeyboardQueueSize; index++)
    queue_enter(&_keyboardQueueUnused, &_keyboardQueueAlloc[index],
                KeyboardQueueElement *, chain);
#endif //DEBUGGER_SUPPORT
  // Note: I don't think this newIRQLayout thing is used at all
  // -- our provider is PS2Nub and the PS2 nub we use does not set this flag
  // -- in addition it only supports the LEGACY interrupt specifiers
  // #ifdef to eliminate for now...
#ifdef NEWIRQ
  if (provider->getProperty("newIRQLayout")) {	// turbo
   IOLog("Using new IRQ layout 0,1\n");
   _newIRQLayout = true;
  }
#endif
    
  //
  // Reset and clean the 8042 keyboard/mouse controller.
  //
    
  resetController();

  //
  // Use a spin lock to protect the client async request queue.
  //

  _requestQueueLock = IOLockAlloc();
  if (!_requestQueueLock) goto fail;

  //
  // Initialize our work loop, our command gate, and our interrupt event
  // sources.  The work loop can accept requests after this step.
  //

  _workLoop                = IOWorkLoop::workLoop();
#if HANDLE_INTERRUPT_DATA_LATER
  _interruptSourceMouse    = IOInterruptEventSource::interruptEventSource( this,
	OSMemberFunctionCast(IOInterruptEventAction, this, &ApplePS2Controller::interruptOccurred));
  _interruptSourceKeyboard = IOInterruptEventSource::interruptEventSource( this,
    OSMemberFunctionCast(IOInterruptEventAction, this, &ApplePS2Controller::interruptOccurred));
#else
  _interruptSourceMouse    = IOInterruptEventSource::interruptEventSource( this,
    OSMemberFunctionCast(IOInterruptEventAction, this, &ApplePS2Controller::packetReadyMouse));
  _interruptSourceKeyboard = IOInterruptEventSource::interruptEventSource( this,
    OSMemberFunctionCast(IOInterruptEventAction, this, &ApplePS2Controller::packetReadyKeyboard));
#endif
  _interruptSourceQueue    = IOInterruptEventSource::interruptEventSource( this,
			OSMemberFunctionCast(IOInterruptEventAction, this, &ApplePS2Controller::processRequestQueue));
  _cmdGate = IOCommandGate::commandGate(this);
#if WATCHDOG_TIMER
  _watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &ApplePS2Controller::onWatchdogTimer));
  if (!_watchdogTimer)
    goto fail;
#endif
    
  if ( !_workLoop                ||
       !_interruptSourceMouse    ||
       !_interruptSourceKeyboard ||
       !_interruptSourceQueue    ||
       !_cmdGate)  goto fail;

  if ( _workLoop->addEventSource(_interruptSourceQueue) != kIOReturnSuccess )
    goto fail;
  if ( _workLoop->addEventSource(_cmdGate) != kIOReturnSuccess )
    goto fail;
    
#if WATCHDOG_TIMER
  if ( _workLoop->addEventSource(_watchdogTimer) != kIOReturnSuccess )
    goto fail;
  _watchdogTimer->setTimeoutMS(kWatchdogTimerInterval);
#endif
  _interruptSourceQueue->enable();

  //
  // Since there is a calling path from the PS/2 driver stack to power
  // management for activity tickles.  We must create a thread callout
  // to handle power state changes from PM to avoid a deadlock.
  //

  _powerChangeThreadCall = thread_call_allocate( 
                           (thread_call_func_t)  setPowerStateCallout,
                           (thread_call_param_t) this );
  if ( !_powerChangeThreadCall )
    goto fail;

  //
  // Initialize our PM superclass variables and register as the power
  // controlling driver.
  //

  PMinit();

  registerPowerDriver( this, (IOPMPowerState *) PS2PowerStateArray,
                       kPS2PowerStateCount );

  //
  // Insert ourselves into the PM tree.
  //

  provider->joinPMtree(this);
    
  //
  // Create the keyboard nub and the mouse nub. The keyboard and mouse drivers
  // will query these nubs to determine the existence of the keyboard or mouse,
  // and should they exist, will attach themselves to the nub as clients.
  //
    
  _keyboardDevice = OSTypeAlloc(ApplePS2KeyboardDevice);
  if ( !_keyboardDevice               ||
       !_keyboardDevice->init()       ||
       !_keyboardDevice->attach(this) )
  {
	  OSSafeReleaseNULL(_keyboardDevice);
	  OSSafeReleaseNULL(_interruptSourceKeyboard);
  }
    
  _mouseDevice = OSTypeAlloc(ApplePS2MouseDevice);
  if ( !_mouseDevice               ||
       !_mouseDevice->init()       ||
       !_mouseDevice->attach(this) )
  {
	  OSSafeReleaseNULL(_mouseDevice);
	  OSSafeReleaseNULL(_interruptSourceMouse);
  }
	   
  if (_keyboardDevice)
	_keyboardDevice->registerService();
  if (_mouseDevice)
	_mouseDevice->registerService();
    
  registerService();

  DEBUG_LOG("ApplePS2Controller::start leaving.\n");
  return true; // success

fail:
  stop(provider);
    
  DEBUG_LOG("ApplePS2Controller::start leaving(fail).\n");
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::stop(IOService * provider)
{
  //
  // The driver has been instructed to stop.  Note that we must break all
  // connections to other service objects now (ie. no registered actions,
  // no pointers and retains to objects, etc), if any.
  //

  // Ensure that the interrupt handlers have been uninstalled (ie. no clients).
  assert(!_interruptInstalledKeyboard);
  assert(!_interruptInstalledMouse);
  assert(!_powerControlInstalledKeyboard);
  assert(!_powerControlInstalledMouse);

  // Free the nubs we created.
  OSSafeReleaseNULL(_keyboardDevice);
  OSSafeReleaseNULL(_mouseDevice);

  // Free the event/interrupt sources.
  OSSafeReleaseNULL(_interruptSourceKeyboard);
  OSSafeReleaseNULL(_interruptSourceMouse);
  OSSafeReleaseNULL(_interruptSourceQueue);
  OSSafeReleaseNULL(_cmdGate);
#if WATCHDOG_TIMER
  OSSafeReleaseNULL(_watchdogTimer);
#endif
    
  // Free the work loop.
  OSSafeReleaseNULL(_workLoop);

  // Free the RMCF configuration cache
  OSSafeReleaseNULL(_rmcfCache);

  // Free the request queue lock and empty out the request queue.
  if (_requestQueueLock)
  {
    _hardwareOffline = true;
    processRequestQueue(0, 0);
    IOLockFree(_requestQueueLock);
    _requestQueueLock = 0;
  }

  // Free the power management thread call.
  if (_powerChangeThreadCall)
  {
    thread_call_free(_powerChangeThreadCall);
    _powerChangeThreadCall = 0;
  }

  // Detach from power management plane.
  PMstop();

#if DEBUGGER_SUPPORT
  // Free the keyboard queue allocation space (after disabling interrupt).
  if (_keyboardQueueAlloc)
  {
    delete[] _keyboardQueueAlloc;
    _keyboardQueueAlloc = 0;
  }
#endif //DEBUGGER_SUPPORT

  super::stop(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOWorkLoop * ApplePS2Controller::getWorkLoop() const
{
    return _workLoop;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::installInterruptAction(PS2DeviceType      deviceType,
                                                OSObject *         target, 
                                                PS2InterruptAction interruptAction,
                                                PS2PacketAction    packetAction)
{
  //
  // Install the keyboard or mouse interrupt handler.
  //
  // This method assumes only one possible mouse and only one possible
  // keyboard client (ie. callers), and assumes two distinct interrupt
  // handlers for each, hence needs no protection against races.
  //

  // Is it the keyboard or the mouse interrupt handler that was requested?
  // We only install it if it is currently uninstalled.

  if (deviceType == kDT_Keyboard && !_interruptInstalledKeyboard  && _interruptSourceKeyboard)
  {
    target->retain();
    _interruptTargetKeyboard = target;
    _interruptActionKeyboard = interruptAction;
    _packetActionKeyboard = packetAction;
    _workLoop->addEventSource(_interruptSourceKeyboard);
    DEBUG_LOG("%s: setCommandByte for keyboard interrupt install\n", getName());
    setCommandByte(kCB_EnableKeyboardIRQ, 0);
#ifdef NEWIRQ
    if (_newIRQLayout)
    {		// turbo
     getProvider()->registerInterrupt(0,0, interruptHandlerKeyboard, this);
     getProvider()->enableInterrupt(0);
    } else
#endif
    {
     getProvider()->registerInterrupt(kIRQ_Keyboard,0, interruptHandlerKeyboard, this);
     getProvider()->enableInterrupt(kIRQ_Keyboard);
    }
    
    _interruptInstalledKeyboard = true;
  }
  else if (deviceType == kDT_Mouse && !_interruptInstalledMouse && _interruptSourceMouse)
  {
    target->retain();
    _interruptTargetMouse = target;
    _interruptActionMouse = interruptAction;
    _packetActionMouse = packetAction;
    _workLoop->addEventSource(_interruptSourceMouse);
    DEBUG_LOG("%s: setCommandByte for mouse interrupt install\n", getName());
    setCommandByte(kCB_EnableMouseIRQ, 0);
#ifdef NEWIRQ
    if (_newIRQLayout)
    {		// turbo
     getProvider()->registerInterrupt(1, 0, interruptHandlerMouse, this);
     getProvider()->enableInterrupt(1);
    } else
#endif
    {
     getProvider()->registerInterrupt(kIRQ_Mouse, 0, interruptHandlerMouse, this);
     getProvider()->enableInterrupt(kIRQ_Mouse);
    }

    _interruptInstalledMouse = true;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::uninstallInterruptAction(PS2DeviceType deviceType)
{
  //
  // Uninstall the keyboard or mouse interrupt handler.
  //
  // This method assumes only one possible mouse and only one possible
  // keyboard client (ie. callers), and assumes two distinct interrupt
  // handlers for each, hence needs no protection against races.
  //

  // Is it the keyboard or the mouse interrupt handler that was requested?
  // We only install it if it is currently uninstalled.

  if (deviceType == kDT_Keyboard && _interruptInstalledKeyboard)
  {
    setCommandByte(0, kCB_EnableKeyboardIRQ);
#ifdef NEWIRQ
    getProvider()->disableInterrupt(0);
    getProvider()->unregisterInterrupt(0);
#else
    getProvider()->disableInterrupt(kIRQ_Keyboard);
    getProvider()->unregisterInterrupt(kIRQ_Keyboard);
#endif
    _workLoop->removeEventSource(_interruptSourceKeyboard);
    _interruptInstalledKeyboard = false;
    _interruptActionKeyboard = NULL;
    _packetActionKeyboard = NULL;
    _interruptTargetKeyboard->release();
    _interruptTargetKeyboard = 0;
  }

  else if (deviceType == kDT_Mouse && _interruptInstalledMouse)
  {
    setCommandByte(0, kCB_EnableMouseIRQ);
#ifdef NEWIRQ
    getProvider()->disableInterrupt(1);
    getProvider()->unregisterInterrupt(1);
#else
    getProvider()->disableInterrupt(kIRQ_Mouse);
    getProvider()->unregisterInterrupt(kIRQ_Mouse);
#endif
    _workLoop->removeEventSource(_interruptSourceMouse);
    _interruptInstalledMouse = false;
    _interruptActionMouse = NULL;
    _packetActionMouse = NULL;
    _interruptTargetMouse->release();
    _interruptTargetMouse = 0;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2Request * ApplePS2Controller::allocateRequest(int max)
{
  //
  // Allocate a request structure.  Blocks until successful.
  // Most of request structure is guaranteed to be zeroed.
  //
    
  assert(max > 0);
    
  return new(max) PS2Request;
}

EXPORT PS2Request::PS2Request()
{
  commandsCount = 0;
  completionTarget = 0;
  completionAction = 0;
  completionParam = 0;

#ifdef DEBUG
  // These items do not need to be initialized, but it might make it easier to
  // debug if they start at zero.
  chain.prev = 0;
  chain.next = 0;
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::freeRequest(PS2Request * request)
{
  //
  // Deallocate a request structure.
  //

  delete request;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt8 ApplePS2Controller::setCommandByte(UInt8 setBits, UInt8 clearBits)
{
    TPS2Request<1> request;
    request.commands[0].command = kPS2C_ModifyCommandByte;
    request.commands[0].setBits = setBits;
    request.commands[0].clearBits = clearBits;
    request.commandsCount = 1;
    assert(request.commandsCount <= countof(request.commands));
    _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Controller::setCommandByteGated), &request);
    return request.commands[0].oldBits;
}

void ApplePS2Controller::setCommandByteGated(PS2Request* request)
{
    UInt8 setBits = request->commands[0].setBits;
    UInt8 clearBits = request->commands[0].clearBits;
    ++_ignoreInterrupts;
    writeCommandPort(kCP_GetCommandByte);
    UInt8 oldCommandByte = readDataPort(kDT_Keyboard);
    --_ignoreInterrupts;
    DEBUG_LOG("%s: oldCommandByte = %02x\n", getName(), oldCommandByte);
    UInt8 newCommandByte = (oldCommandByte | setBits) & ~clearBits;
    if (oldCommandByte != newCommandByte)
    {
        DEBUG_LOG("%s: newCommandByte = %02x\n", getName(), newCommandByte);
        writeCommandPort(kCP_SetCommandByte);
        writeDataPort(newCommandByte);
    }
    request->commands[0].oldBits = oldCommandByte;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Controller::submitRequest(PS2Request * request)
{
  //
  // Submit the request to the controller for processing, asynchronously.
  //

  IOLockLock(_requestQueueLock);
  queue_enter(&_requestQueue, request, PS2Request *, chain);
  IOLockUnlock(_requestQueueLock);

  _interruptSourceQueue->interruptOccurred(0, 0, 0);

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::submitRequestAndBlock(PS2Request * request)
{
    _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &ApplePS2Controller::submitRequestAndBlockGated), request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::submitRequestAndBlockGated(PS2Request* request)
{
    processRequestQueue(0, 0);
    processRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#if HANDLE_INTERRUPT_DATA_LATER
void ApplePS2Controller::interruptOccurred(IOInterruptEventSource* source, int)
{                                                      // IOInterruptEventAction
  //
  // Our work loop has informed us of an interrupt, that is, asynchronous
  // data has arrived on our input stream.  Read the data and dispatch it
  // to the appropriate driver.
  //
  // This method should only be called from our single-threaded work loop.
  //

  if (_hardwareOffline)
  {
    // Toss any asynchronous data received. The interrupt event source may
    // have been signalled before the PS/2 port was offline.
    return;
  }

#if DEBUGGER_SUPPORT
  UInt8 status;
  int state;
  lockController(&state);              // (lock out interrupt + access to queue)
  while (1)
  {
    // See if data is available on the keyboard input stream (off queue);
    // we do not read keyboard data from the real data port if it should
    // be available. 

    if (dequeueKeyboardData(&status))
    {
      unlockController(state);
      dispatchDriverInterrupt(kDT_Keyboard, status);
      lockController(&state);
    }

    // See if data is available on the mouse input stream (off real port).

    else if ( (inb(kCommandPort) & (kOutputReady | kMouseData)) ==
                                   (kOutputReady | kMouseData))
    {
      unlockController(state);
      IODelay(kDataDelay);
      dispatchDriverInterrupt(kDT_Mouse, inb(kDataPort));
      lockController(&state);
    }
    else break; // out of loop
  }
  unlockController(state);      // (release interrupt lockout + access to queue)
#else
  handleInterrupt(source == _interruptSourceKeyboard ? kDT_Keyboard : kDT_Mouse);
#endif // DEBUGGER_SUPPORT
}
#endif // HANDLE_INTERRUPT_DATA_LATER

#if !HANDLE_INTERRUPT_DATA_LATER
void ApplePS2Controller::packetReadyKeyboard(IOInterruptEventSource *, int)
{
    // a complete packet has arrived for the keyboard and has signaled the workloop
    // -- dispatch it to the installed keyboard packet handler
    if (_interruptInstalledKeyboard)
        (*_packetActionKeyboard)(_interruptTargetKeyboard);
}

void ApplePS2Controller::packetReadyMouse(IOInterruptEventSource *, int)
{
    // a complete packet has arrived for the mouse and has signaled the workloop
    // -- dispatch it to the installed mouse packet handler
    if (_interruptInstalledMouse)
        (*_packetActionMouse)(_interruptTargetMouse);
}
#endif // !HANDLE_INTERRUPT_DATA_LATER

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2InterruptResult ApplePS2Controller::_dispatchDriverInterrupt(PS2DeviceType deviceType, UInt8 data)
{
    PS2InterruptResult result = kPS2IR_packetBuffering;
    if (kDT_Mouse == deviceType && _interruptInstalledMouse)
    {
        // Dispatch the data to the mouse driver.
        result = (*_interruptActionMouse)(_interruptTargetMouse, data);
    }
    else if (kDT_Keyboard == deviceType && _interruptInstalledKeyboard)
    {
        // Dispatch the data to the keyboard driver.
        result = (*_interruptActionKeyboard)(_interruptTargetKeyboard, data);
    }
    return result;
}

void ApplePS2Controller::dispatchDriverInterrupt(PS2DeviceType deviceType, UInt8 data)
{
    PS2InterruptResult result = _dispatchDriverInterrupt(deviceType, data);
    if (kPS2IR_packetReady == result)
    {
#if HANDLE_INTERRUPT_DATA_LATER
        if (kDT_Mouse == deviceType)
            (*_packetActionMouse)(_interruptTargetMouse);
        else if (kDT_Keyboard == deviceType)
            (*_packetActionKeyboard)(_interruptTargetKeyboard);
#else
        if (kDT_Mouse == deviceType)
            _interruptSourceMouse->interruptOccurred(0, 0, 0);
        else if (kDT_Keyboard == deviceType)
            _interruptSourceKeyboard->interruptOccurred(0, 0, 0);
#endif
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::processRequest(PS2Request * request)
{
  //
  // Our work loop has informed us of a request submission. Process
  // the request.  Note that this code "figures out" when the mouse
  // input stream should be read over the keyboard input stream.
  //
  // This method should only be called from our single-threaded work loop.
  //

  UInt8         byte;
  PS2DeviceType deviceMode      = kDT_Keyboard;
  bool          failed          = false;
  bool          transmitToMouse = false;
  unsigned      index;

  if (_hardwareOffline)
  {
    failed = true;
    index  = 0;
    goto hardware_offline;
  }

  // Don't handle interrupts during this process.  We want to read the
  // data by polling for it here.
    
  ++_ignoreInterrupts;

  // Process each of the commands in the list.

  for (index = 0; index < request->commandsCount; index++)
  {
    switch (request->commands[index].command)
    {
      case kPS2C_ReadDataPort:
        request->commands[index].inOrOut = readDataPort(deviceMode);
        break;

      case kPS2C_ReadDataPortAndCompare:
#if OUT_OF_ORDER_DATA_CORRECTION_FEATURE
        byte = readDataPort(deviceMode, request->commands[index].inOrOut);
#else 
        byte = readDataPort(deviceMode);
#endif
        failed = (byte != request->commands[index].inOrOut);
        request->commands[index].inOrOut = byte;
        break;

      case kPS2C_WriteDataPort:
        writeDataPort(request->commands[index].inOrOut);
        if (transmitToMouse)     // next reads from mouse input stream
        {
          deviceMode      = kDT_Mouse;
          transmitToMouse = false;
        }
        else
        {
           deviceMode   = kDT_Keyboard;
        }
        break;

      case kPS2C_WriteCommandPort:
        writeCommandPort(request->commands[index].inOrOut);
        if (request->commands[index].inOrOut == kCP_TransmitToMouse)
          transmitToMouse = true; // preparing to transmit data to mouse
        break;

      //
      // Send a composite mouse command that is equivalent to the following
      // (frequently used) command sequence:
      //
      // 1. kPS2C_WriteCommandPort( kCP_TransmitToMouse )
      // 2. kPS2C_WriteDataPort( command )
      // 3. kPS2C_ReadDataPortAndCompare( kSC_Acknowledge )
      //

      case kPS2C_SendMouseCommandAndCompareAck:
        writeCommandPort(kCP_TransmitToMouse);
        writeDataPort(request->commands[index].inOrOut);
        deviceMode = kDT_Mouse;
#if OUT_OF_ORDER_DATA_CORRECTION_FEATURE
        byte = readDataPort(kDT_Mouse, kSC_Acknowledge);
#else 
        byte = readDataPort(kDT_Mouse);
#endif
        failed = (byte != kSC_Acknowledge);
        break;
            
      case kPS2C_ReadMouseDataPort:
        deviceMode= kDT_Mouse;
        request->commands[index].inOrOut = readDataPort(deviceMode);
        break;
            
      case kPS2C_ReadMouseDataPortAndCompare:
        deviceMode= kDT_Mouse;
#if OUT_OF_ORDER_DATA_CORRECTION_FEATURE
        byte = readDataPort(deviceMode, request->commands[index].inOrOut);
#else
        byte = readDataPort(deviceMode);
#endif
        failed = (byte != request->commands[index].inOrOut);
        break;
            
      case kPS2C_FlushDataPort:
        request->commands[index].inOrOut32 = 0;
        while ( inb(kCommandPort) & kOutputReady )
        {
            ++request->commands[index].inOrOut32;
            IODelay(kDataDelay);
            inb(kDataPort);
            IODelay(kDataDelay);
        }
        break;
      
      case kPS2C_SleepMS:
        IOSleep(request->commands[index].inOrOut32);
        break;
            
      case kPS2C_ModifyCommandByte:
        writeCommandPort(kCP_GetCommandByte);
        UInt8 commandByte = readDataPort(kDT_Keyboard);
        writeCommandPort(kCP_SetCommandByte);
        writeDataPort((commandByte | request->commands[index].setBits) & ~request->commands[index].clearBits);
        request->commands[index].oldBits = commandByte;
        break;
    }

    if (failed) break;
  }
    
  // Now it is ok to process interrupts normally.
    
  --_ignoreInterrupts;
    
hardware_offline:

  // If a command failed and stopped the request processing, store its
  // index into the commandsCount field.

  if (failed) request->commandsCount = index;

  // Invoke the completion routine, if one was supplied.

  if (request->completionTarget != kStackCompletionTarget && request->completionTarget && request->completionAction)
  {
    (*request->completionAction)(request->completionTarget,
                                 request->completionParam);
  }
  else
  {
    if (request->completionTarget != kStackCompletionTarget)
      freeRequest(request);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::processRequestQueue(IOInterruptEventSource *, int)
{
  queue_head_t localQueue;

  // Transfer queued (async) requests to a local queue.

  IOLockLock(_requestQueueLock);

  if (!queue_empty(&_requestQueue))
  {
    queue_assign(&localQueue, &_requestQueue, PS2Request *, chain);
    queue_init(&_requestQueue);
  }
  else queue_init(&localQueue);

  IOLockUnlock(_requestQueueLock);

  // Process each request in order.

  while (!queue_empty(&localQueue))
  {
    PS2Request * request;
    queue_remove_first(&localQueue, request, PS2Request *, chain);
    processRequest(request);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt8 ApplePS2Controller::readDataPort(PS2DeviceType deviceType)
{
  //
  // Blocks until keyboard or mouse data is available from the controller
  // and returns that data. Note, if mouse data is requested but keyboard
  // data is what is available,  the data is delivered to the appropriate
  // driver interrupt routine immediately (effectively, the request is
  // "preempted" temporarily).
  //
  // There is a built-in timeout for this command of (timeoutCounter X
  // kDataDelay) microseconds, approximately. 
  //
  // This method should only be called from our single-threaded work loop.
  //

  UInt8  readByte;
  UInt8  status;
  UInt32 timeoutCounter = 10000;    // (timeoutCounter * kDataDelay = 70 ms)

  while (1)
  {
#if DEBUGGER_SUPPORT
    int state;
    lockController(&state);            // (lock out interrupt + access to queue)
    if (deviceType == kDT_Keyboard && dequeueKeyboardData(&readByte))
    {
      unlockController(state);
      return readByte;
    }
#endif //DEBUGGER_SUPPORT

    //
    // Wait for the controller's output buffer to become ready.
    //

    while (timeoutCounter && !((status = inb(kCommandPort)) & kOutputReady))
    {
      timeoutCounter--;
      IODelay(kDataDelay);
    }

    //
    // If we timed out, something went awfully wrong; return a fake value.
    //

    if (timeoutCounter == 0)
    {
#if DEBUGGER_SUPPORT
      unlockController(state);  // (release interrupt lockout + access to queue)
#endif //DEBUGGER_SUPPORT

	  if (!_suppressTimeout)
		IOLog("%s: Timed out on %s input stream.\n", getName(),
                          (deviceType == kDT_Keyboard) ? "keyboard" : "mouse");
      return 0;
    }

    //
    // For older machines, it is necessary to wait a while after the controller
    // has asserted the output buffer bit before reading the data port. No more
    // data will be available if this wait is not performed.
    //

    IODelay(kDataDelay);

    //
    // Read in the data.  We return the data, however, only if it arrived on
    // the requested input stream.
    //

    readByte = inb(kDataPort);

#if DEBUGGER_SUPPORT
    unlockController(state);    // (release interrupt lockout + access to queue)
#endif //DEBUGGER_SUPPORT

	if (_suppressTimeout)		// startup mode w/o interrupts
		return readByte;

    if ( (status & kMouseData) )
    {
      if (deviceType == kDT_Mouse)  return readByte;
    }
    else
    {
      if (deviceType == kDT_Keyboard)  return readByte;
    }

    //
    // The data we just received is for the other input stream, not the one
    // that was requested, so dispatch other device's interrupt handler.
    //

    dispatchDriverInterrupt((deviceType==kDT_Keyboard)?kDT_Mouse:kDT_Keyboard,
                            readByte);
  } // while (forever)
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#if OUT_OF_ORDER_DATA_CORRECTION_FEATURE

UInt8 ApplePS2Controller::readDataPort(PS2DeviceType deviceType,
                                       UInt8         expectedByte)
{
  //
  // Blocks until keyboard or mouse data is available from the controller
  // and returns that data. Note, if mouse data is requested but keyboard
  // data is what is available,  the data is delivered to the appropriate
  // driver interrupt routine immediately (effectively, the request is
  // "preempted" temporarily).
  //
  // There is a built-in timeout for this command of (timeoutCounter X
  // kDataDelay) microseconds, approximately. 
  //
  // This method should only be called from our single-threaded work loop.
  //
  // This version of readDataPort does exactly the same as the original,
  // except that if the value that should be read from the (appropriate)
  // input stream is not what is expected, we make these assumptions:
  //
  // (a) the data byte we did get was  "asynchronous" data being sent by
  //     the device, which has not figured out that it has to respond to
  //     the command we just sent to it.
  // (b) that the real  "expected" response will be the next byte in the
  //     stream;   so what we do is put aside the first byte we read and
  //     wait for the next byte; if it's the expected value, we dispatch
  //     the first byte we read to the driver's interrupt handler,  then
  //     return the expected byte. The caller will have never known that
  //     asynchronous data arrived at a very bad time.
  // (c) that the real "expected" response will arrive within (kDataDelay
  //     X timeoutCounter) microseconds from the time the call is made.
  //

  UInt8  firstByte     = 0;
  bool   firstByteHeld = false;
  UInt8  readByte;
  bool   requestedStream;
  UInt8  status;
  UInt32 timeoutCounter = 10000;    // (timeoutCounter * kDataDelay = 70 ms)

  while (1)
  {
#if DEBUGGER_SUPPORT
    int state;
    lockController(&state);            // (lock out interrupt + access to queue)
    if (deviceType == kDT_Keyboard && dequeueKeyboardData(&readByte))
    {
      requestedStream = true;
      goto skipForwardToY;
    }
#endif //DEBUGGER_SUPPORT

    //
    // Wait for the controller's output buffer to become ready.
    //

    while (timeoutCounter && !((status = inb(kCommandPort)) & kOutputReady))
    {
      timeoutCounter--;
      IODelay(kDataDelay);
    }

    //
    // If we timed out, we return the first byte we read, unless THIS IS the
    // first byte we are trying to read,  then something went awfully wrong
    // and we return a fake value rather than lock up the controller longer.
    //

    if (timeoutCounter == 0)
    {
#if DEBUGGER_SUPPORT
      unlockController(state);  // (release interrupt lockout + access to queue)
#endif //DEBUGGER_SUPPORT

      if (firstByteHeld)  return firstByte;

      IOLog("%s: Timed out on %s input stream.\n", getName(),
                          (deviceType == kDT_Keyboard) ? "keyboard" : "mouse");
      return 0;
    }

    //
    // For older machines, it is necessary to wait a while after the controller
    // has asserted the output buffer bit before reading the data port. No more
    // data will be available if this wait is not performed.
    //

    IODelay(kDataDelay);

    //
    // Read in the data.  We process the data, however, only if it arrived on
    // the requested input stream.
    //

    readByte        = inb(kDataPort);
    requestedStream = false;

    if ( (status & kMouseData) )
    {
      if (deviceType == kDT_Mouse)  requestedStream = true;
    }
    else
    {
      if (deviceType == kDT_Keyboard)  requestedStream = true;
    }

#if DEBUGGER_SUPPORT
skipForwardToY:
    unlockController(state);    // (release interrupt lockout + access to queue)
#endif //DEBUGGER_SUPPORT

    if (requestedStream)
    {
      if (readByte == expectedByte)
      {
        if (firstByteHeld == false)
        {
          //
          // Normal case.  Return first byte received.
          //

          return readByte;
        }
        else
        {
          //
          // Our assumption was correct.  The second byte matched.  Dispatch
          // the first byte to the interrupt handler, and return the second.
          //

          if (!_ignoreOutOfOrder)
            dispatchDriverInterrupt(deviceType, firstByte);
          return readByte;
        }
      }
      else // (readByte does not match expectedByte)
      {
        if (firstByteHeld == false)
        {
          //
          // The first byte was received, and does not match the byte we are
          // expecting.  Put it aside for the moment.
          //

          firstByteHeld = true;
          firstByte     = readByte;
        }
        else if (readByte != expectedByte)
        {
          //
          // The second byte mismatched as well.  I have yet to see this case
          // occur [Dan], however I do think it's plausible.  No error logged.
          //

          if (!_ignoreOutOfOrder)
            dispatchDriverInterrupt(deviceType, readByte);
          return firstByte;
        }
      }
    }
    else
    {
      //
      // The data we just received is for the other input stream, not ours,
      // so dispatch appropriate interrupt handler.
      //

      if (!_ignoreOutOfOrder)
        dispatchDriverInterrupt(deviceType == kDT_Keyboard ? kDT_Mouse : kDT_Keyboard, readByte);
    }
  } // while (forever)
}

#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::writeDataPort(UInt8 byte)
{
  //
  // Block until room in the controller's input buffer is available, then
  // write the given byte to the Data Port.
  //
  // This method should only be dispatched from our single-threaded work loop.
  //

  while (inb(kCommandPort) & kInputBusy)
      IODelay(kDataDelay);
  IODelay(kDataDelay);
  outb(kDataPort, byte);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::writeCommandPort(UInt8 byte)
{
  //
  // Block until room in the controller's input buffer is available, then
  // write the given byte to the Command Port.
  //
  // This method should only be dispatched from our single-threaded work loop.
  //

  while (inb(kCommandPort) & kInputBusy)
      IODelay(kDataDelay);
  IODelay(kDataDelay);
  outb(kCommandPort, byte);
}

// =============================================================================
// Escape-Key Processing Stuff Localized Here (eg. Mini-Monitor)
//

#if DEBUGGER_SUPPORT

#define kModifierShiftLeft    0x01
#define kModifierShiftRight   0x02
#define kModifierCtrlLeft     0x04
#define kModifierCtrlRight    0x08
#define kModifierAltLeft      0x10
#define kModifierAltRight     0x20
#define kModifierWindowsLeft  0x40
#define kModifierWindowsRight 0x80

#define kModifierShiftMask    (kModifierShiftLeft   | kModifierShiftRight  )
#define kModifierCtrlMask     (kModifierCtrlLeft    | kModifierCtrlRight   )
#define kModifierAltMask      (kModifierAltLeft     | kModifierAltRight    )
#define kModifierWindowsMask  (kModifierWindowsLeft | kModifierWindowsRight)

bool ApplePS2Controller::doEscape(UInt8 scancode)
{
  static struct
  {
    UInt8  scancode;
    UInt8  extended;
    UInt16 modifier;
  } modifierTable[] = { { kSC_Alt,          false, kModifierAltLeft      },
                         { kSC_Alt,          true,  kModifierAltRight     },
                         { kSC_Ctrl,         false, kModifierCtrlLeft     },
                         { kSC_Ctrl,         true,  kModifierCtrlRight    },
                         { kSC_ShiftLeft,    false, kModifierShiftLeft    },
                         { kSC_ShiftRight,   false, kModifierShiftRight   },
                         { kSC_WindowsLeft,  true,  kModifierWindowsLeft  },
                         { kSC_WindowsRight, true,  kModifierWindowsRight },
                         { 0,                0,   0                     } };

  UInt32 index;
  bool   releaseModifiers = false;
  bool   upBit            = (scancode & kSC_UpBit) ? true : false;

  //
  // See if this is an extened scancode sequence.
  //

  if (scancode == kSC_Extend)
  {
    _extendedState = true;
    return false;
  }

  //
  // Update the modifier state, if applicable.
  //

  scancode &= ~kSC_UpBit;

  for (index = 0; modifierTable[index].scancode; index++)
  {
    if ( modifierTable[index].scancode == scancode &&
         modifierTable[index].extended == _extendedState )
    {
      if (upBit)  _modifierState &= ~modifierTable[index].modifier;
      else        _modifierState |=  modifierTable[index].modifier;

      _extendedState = false;
      return false;
    }
  } 

  //
  // Call the debugger function, if applicable.
  //

  if (scancode == kSC_Delete)    // (both extended and non-extended scancodes)
  {
    if ( _modifierState == (kModifierAltLeft | kModifierAltRight) )
    {
      // Disable the mouse by forcing the clock line low.

      while (inb(kCommandPort) & kInputBusy)
          IODelay(kDataDelay);
      IODelay(kDataDelay);
      outb(kCommandPort, kCP_DisableMouseClock);

      // Call the debugger function.

      Debugger("Programmer Key");

      // Re-enable the mouse by making the clock line active.

      while (inb(kCommandPort) & kInputBusy)
          IODelay(kDataDelay);
      IODelay(kDataDelay);
      outb(kCommandPort, kCP_EnableMouseClock);

      releaseModifiers = true;
    }
  }

  //
  // Release all the modifier keys that were down before the debugger
  // function was called  (assumption is that they are no longer held
  // down after the debugger function returns).
  //

  if (releaseModifiers)
  {
    for (index = 0; modifierTable[index].scancode; index++)
    {
      if ( _modifierState & modifierTable[index].modifier )
      {
        if (modifierTable[index].extended)  enqueueKeyboardData(kSC_Extend);
        enqueueKeyboardData(modifierTable[index].scancode | kSC_UpBit);
      }
    }
    _modifierState = 0x00;
  }

  //
  // Update all other state and return status.
  //

  _extendedState = false;
  return (releaseModifiers);
}

void ApplePS2Controller::enqueueKeyboardData(UInt8 key)
{
  //
  // Enqueue the supplied keyboard data onto our internal queues.  The
  // controller must already be locked. 
  //

  KeyboardQueueElement * element;

  // Obtain an unused keyboard data element. 
  if (!queue_empty(&_keyboardQueueUnused))
  {
    queue_remove_first(&_keyboardQueueUnused,
                       element, KeyboardQueueElement *, chain);

    // Store the new keyboard data element on the queue. 
    element->data = key; 
    queue_enter(&_keyboardQueue, element, KeyboardQueueElement *, chain); 
  }
}

bool ApplePS2Controller::dequeueKeyboardData(UInt8 * key)
{
  //
  // Dequeue keyboard data from our internal queues, if the queue is not
  // empty.  Should the queue be empty, false is returned.  The controller
  // must already be locked. 
  //

  KeyboardQueueElement * element;

  // Obtain an unused keyboard data element.
  if (!queue_empty(&_keyboardQueue))
  {
    queue_remove_first(&_keyboardQueue, element, KeyboardQueueElement *, chain);
    *key = element->data;

    // Place the unused keyboard data element onto the unused queue.
    queue_enter(&_keyboardQueueUnused, element, KeyboardQueueElement *, chain);

    return true;
  }
  return false;
}

void ApplePS2Controller::unlockController(int state)
{
  IOSimpleLockUnlockEnableInterrupt(_controllerLock, state);
}

void ApplePS2Controller::lockController(int * state)
{
  *state = IOSimpleLockLockDisableInterrupt(_controllerLock);
}

#endif //DEBUGGER_SUPPORT

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
// Power Management support.
//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2Controller::setPowerState( unsigned long powerStateOrdinal,
                                            IOService *   policyMaker)
{
  IOReturn result = IOPMAckImplied;

  //
  // Prevent the object from being freed while a call is pending.
  // If thread_call_enter() returns TRUE, indicating that a call
  // is already pending, then the extra retain is dropped.
  //

  retain();
  if ( thread_call_enter1( _powerChangeThreadCall,
                           (void *) powerStateOrdinal ) == TRUE )
  {
    release();
  }
  result = 5000000;  // 5 seconds before acknowledgement timeout

  return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::setPowerStateCallout( thread_call_param_t param0,
                                               thread_call_param_t param1 )
{
  ApplePS2Controller * me = (ApplePS2Controller *) param0;
  assert(me);

  if (me->_workLoop)
  {
    me->_workLoop->runAction( /* Action */ setPowerStateAction,
                              /* target */ me,
                              /*   arg0 */ param1 );
  }

  me->release();  // drop the retain from setPowerState()
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2Controller::setPowerStateAction( OSObject * target,
                                                  void * arg0, void * arg1,
                                                  void * arg2, void * arg3 )
{
  ApplePS2Controller * me = (ApplePS2Controller *) target;
	
#ifdef __LP64__
	UInt32       powerState = (UInt32)(UInt64)arg0;
#else	
	UInt32       powerState = (UInt32) arg0;
#endif	
	
  me->setPowerStateGated( powerState );

  return kIOReturnSuccess;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::setPowerStateGated( UInt32 powerState )
{
  if ( _currentPowerState != powerState )
  {
    switch ( powerState )
    {
      case kPS2PowerStateSleep:

        //
        // 1. Make sure clocks are enabled, but IRQ lines held low.
        //
        
        ++_ignoreInterrupts;
        DEBUG_LOG("%s: setCommandByte for sleep 1\n", getName());
        setCommandByte(0, kCB_EnableKeyboardIRQ | kCB_EnableMouseIRQ);
        
        // 2. Notify clients about the state change. Clients can issue
        //    synchronous requests thanks to the recursive lock.
        //    First Mouse, then Keyboard.
            
        dispatchDriverPowerControl( kPS2C_DisableDevice, kDT_Mouse );
        dispatchDriverPowerControl( kPS2C_DisableDevice, kDT_Keyboard );

        // 3. Freeze the request queue and drop all data received over
        //    the PS/2 port.

        _hardwareOffline = true;

        // 4. Disable the PS/2 port.

#if DISABLE_CLOCKS_IRQS_BEFORE_SLEEP
        // This will cause some machines to turn on the LCD after the
        // ACPI display driver has turned it off. With a real display
        // driver present, this block of code can be uncommented (?).

        DEBUG_LOG("%s: setCommandByte for sleep 2\n", getName());
        setCommandByte(kCB_DisableKeyboardClock | kCB_DisableMouseClock, 0);
#endif // DISABLE_CLOCKS_IRQS_BEFORE_SLEEP
        break;

      case kPS2PowerStateDoze:
      case kPS2PowerStateNormal:

        if ( _currentPowerState != kPS2PowerStateSleep )
        {
          // Transitions between doze and normal power states
          // require no action, since both are working states.
          break;
        }
            
        if (_wakedelay)
            IOSleep(_wakedelay);
            
#if FULL_INIT_AFTER_WAKE
        //
        // Reset and clean the 8042 keyboard/mouse controller.
        //
        
        resetController();
            
#endif // FULL_INIT_AFTER_WAKE
            

        //
        // Transition from Sleep state to Working state in 4 stages.
        //

        // 1. Enable the PS/2 port -- but just the clocks

        DEBUG_LOG("%s: setCommandByte for wake 1\n", getName());
        setCommandByte(0, kCB_DisableKeyboardClock | kCB_DisableMouseClock | kCB_EnableKeyboardIRQ | kCB_EnableMouseIRQ);

        // 2. Unblock the request queue and wake up all driver threads
        //    that were blocked by submitRequest().

        _hardwareOffline = false;

        // 3. Notify clients about the state change: Keyboard, then Mouse.
        //   (This ordering is also part of the fix for ProBook 4x40s trackpad wake issue)
        //    The ordering can be reversed from normal by setting MouseWakeFirst=true

        if (!_mouseWakeFirst)
        {
            dispatchDriverPowerControl( kPS2C_EnableDevice, kDT_Keyboard );
            dispatchDriverPowerControl( kPS2C_EnableDevice, kDT_Mouse );
        }
        else
        {
            dispatchDriverPowerControl( kPS2C_EnableDevice, kDT_Mouse );
            dispatchDriverPowerControl( kPS2C_EnableDevice, kDT_Keyboard );
        }

        // 4. Now safe to enable the IRQs...
            
        DEBUG_LOG("%s: setCommandByte for wake 2\n", getName());
        setCommandByte(kCB_EnableKeyboardIRQ | kCB_EnableMouseIRQ | kCB_SystemFlag, 0);
        --_ignoreInterrupts;
        break;

      default:
        IOLog("%s: bad power state %ld\n", getName(), (long)powerState);
        break;
    }

    _currentPowerState = powerState;
  }

  //
  // Acknowledge the power change before the power management timeout
  // expires.
  //

  acknowledgeSetPowerState();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::dispatchDriverPowerControl( UInt32 whatToDo, PS2DeviceType deviceType )
{
  if (kDT_Mouse == deviceType && _powerControlInstalledMouse)
    (*_powerControlActionMouse)(_powerControlTargetMouse, whatToDo);

  if (kDT_Keyboard == deviceType && _powerControlInstalledKeyboard)
    (*_powerControlActionKeyboard)(_powerControlTargetKeyboard, whatToDo);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::installPowerControlAction(
                                          PS2DeviceType         deviceType,
                                          OSObject *            target, 
                                          PS2PowerControlAction action )
{
  if ( deviceType == kDT_Keyboard && _powerControlInstalledKeyboard == false )
  {
    target->retain();
    _powerControlTargetKeyboard = target;
    _powerControlActionKeyboard = action;
    _powerControlInstalledKeyboard = true;
  }
  else if ( deviceType == kDT_Mouse && _powerControlInstalledMouse == false )
  {
    target->retain();
    _powerControlTargetMouse = target;
    _powerControlActionMouse = action;
    _powerControlInstalledMouse = true;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::uninstallPowerControlAction( PS2DeviceType deviceType )
{
  if ( deviceType == kDT_Keyboard && _powerControlInstalledKeyboard == true )
  {
    _powerControlInstalledKeyboard = false;
    _powerControlActionKeyboard = NULL;
    _powerControlTargetKeyboard->release();
    _powerControlTargetKeyboard = 0;
  }
  else if ( deviceType == kDT_Mouse && _powerControlInstalledMouse == true )
  {
    _powerControlInstalledMouse = false;
    _powerControlActionMouse = NULL;
    _powerControlTargetMouse->release();
    _powerControlTargetMouse = 0;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::installMessageAction(
                                              PS2DeviceType deviceType,
                                              OSObject *target, PS2MessageAction action)
{
    if (deviceType == kDT_Keyboard && !_messageInstalledKeyboard)
    {
        target->retain();
        _messageTargetKeyboard = target;
        _messageActionKeyboard = action;
        _messageInstalledKeyboard = true;
    }
    else if (deviceType == kDT_Mouse && !_messageInstalledMouse)
    {
        target->retain();
        _messageTargetMouse = target;
        _messageActionMouse = action;
        _messageInstalledMouse = true;
    }
}

void ApplePS2Controller::uninstallMessageAction(PS2DeviceType deviceType)
{
    if (deviceType == kDT_Keyboard && _messageInstalledKeyboard)
    {
        _messageInstalledKeyboard = false;
        _messageActionKeyboard = NULL;
        _messageTargetKeyboard->release();
        _messageTargetKeyboard = NULL;
    }
    else if (deviceType == kDT_Mouse && _messageInstalledMouse)
    {
        _messageInstalledMouse = false;
        _messageActionMouse = NULL;
        _messageTargetMouse->release();
        _messageTargetMouse = NULL;
    }
}

void ApplePS2Controller::dispatchMessage(PS2DeviceType deviceType, int message, void* data)
{
    if (deviceType == kDT_Keyboard && _messageInstalledKeyboard)
    {
        (*_messageActionKeyboard)(_messageTargetKeyboard, message, data);
    }
    else if (deviceType == kDT_Mouse && _messageInstalledMouse)
    {
        (*_messageActionMouse)(_messageTargetMouse, message, data);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Controller::lock()
{
    assert(_cmdbyteLock);
    IOLockLock(_cmdbyteLock);
}

void ApplePS2Controller::unlock()
{
    assert(_cmdbyteLock);
    IOLockUnlock(_cmdbyteLock);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define kDefault                "Default"

struct DSDT_HEADER
{
    uint32_t tableSignature;
    uint32_t tableLength;
    uint8_t specCompliance;
    uint8_t checkSum;
    char oemID[6];
    char oemTableID[8];
    uint32_t oemRevision;
    uint32_t creatorID;
    uint32_t creatorRevision;
};

#define DSDT_SIGNATURE ('D' | 'S'<<8 | 'D'<<16 | 'T'<<24)

static const DSDT_HEADER* getDSDT()
{
    IORegistryEntry* reg = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert");
    if (!reg)
        return NULL;
    OSDictionary* dict = OSDynamicCast(OSDictionary, reg->getProperty("ACPI Tables"));
    reg->release();
    if (!dict)
        return NULL;
    OSData* data = OSDynamicCast(OSData, dict->getObject("DSDT"));
    if (!data || data->getLength() < sizeof(DSDT_HEADER))
        return NULL;
    const DSDT_HEADER* pDSDT = (const DSDT_HEADER*)data->getBytesNoCopy();
    if (!pDSDT || data->getLength() < sizeof(DSDT_HEADER) || pDSDT->tableSignature != DSDT_SIGNATURE)
        return NULL;
    return pDSDT;
}

static void stripTrailingSpaces(char* str)
{
    char* p = str;
    for (; *p; p++)
        ;
    for (--p; p >= str && *p == ' '; --p)
        *p = 0;
}

static OSString* getPlatformOverride(IORegistryEntry* reg, const char* sz)
{
    while (reg) {
        OSString* id = OSDynamicCast(OSString, reg->getProperty(sz));
        if (id)
            return id;
        reg = reg->getParentEntry(gIOServicePlane);
    }
#if 0 //REVIEW: fallback should not be needed
    reg = IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K");
    if (reg) {
        OSString* id = OSDynamicCast(OSString, reg->getProperty(sz));
        reg->release();
        if (id)
            return id;
    }
#endif
    return NULL;
}

static OSString* getPlatformManufacturer(IORegistryEntry* reg)
{
    // allow override in PS2K ACPI device
    OSString* id = getPlatformOverride(reg, "RM,oem-id");
    if (id)
        return id;

    // otherwise use DSDT header
    const DSDT_HEADER* pDSDT = getDSDT();
    if (!pDSDT)
        return NULL;
    // copy to static data, NUL terminate, strip trailing spaces, and return
    static char oemID[sizeof(pDSDT->oemID)+1];
    bcopy(pDSDT->oemID, oemID, sizeof(pDSDT->oemID));
    oemID[sizeof(oemID)-1] = 0;
    stripTrailingSpaces(oemID);
    return OSString::withCStringNoCopy(oemID);
}

static OSString* getPlatformProduct(IORegistryEntry* reg)
{
    // allow override in PS2K ACPI device
    OSString* id = getPlatformOverride(reg, "RM,oem-table-id");
    if (id)
        return id;

    const DSDT_HEADER* pDSDT = getDSDT();
    if (!pDSDT)
        return NULL;
    // copy to static data, NUL terminate, strip trailing spaces, and return
    static char oemTableID[sizeof(pDSDT->oemTableID)+1];
    bcopy(pDSDT->oemTableID, oemTableID, sizeof(pDSDT->oemTableID));
    oemTableID[sizeof(oemTableID)-1] = 0;
    stripTrailingSpaces(oemTableID);
    return OSString::withCStringNoCopy(oemTableID);
}

static OSDictionary* _getConfigurationNode(OSDictionary *root, const char *name);

static OSDictionary* _getConfigurationNode(OSDictionary *root, OSString *name)
{
    OSDictionary *configuration = NULL;
    
    if (root && name) {
        if (!(configuration = OSDynamicCast(OSDictionary, root->getObject(name)))) {
            if (OSString *link = OSDynamicCast(OSString, root->getObject(name))) {
                const char* p1 = link->getCStringNoCopy();
                const char* p2 = p1;
                for (; *p2 && *p2 != ';'; ++p2);
                if (*p2 != ';') {
                    configuration = _getConfigurationNode(root, link);
                }
                else {
                    if (OSString* strip = OSString::withString(link)) {
                        strip->setChar(0, (unsigned)(p2 - p1));
                        configuration = _getConfigurationNode(root, strip);
                        strip->release();
                    }
                }
            }
        }
    }
    
    return configuration;
}

static OSDictionary* _getConfigurationNode(OSDictionary *root, const char *name)
{
    OSDictionary *configuration = NULL;
    
    if (root && name) {
        OSString *nameNode = OSString::withCStringNoCopy(name);
        
        configuration = _getConfigurationNode(root, nameNode);
        
        OSSafeReleaseNULL(nameNode);
    }
    
    return configuration;
}

OSDictionary* ApplePS2Controller::getConfigurationNode(IORegistryEntry* entry, OSDictionary* list)
{
    OSDictionary *configuration = NULL;

    if (OSString *manufacturer = getPlatformManufacturer(entry))
        if (OSDictionary *manufacturerNode = OSDynamicCast(OSDictionary, list->getObject(manufacturer)))
            if (!(configuration = _getConfigurationNode(manufacturerNode, getPlatformProduct(entry))))
                configuration = _getConfigurationNode(manufacturerNode, kDefault);
    
    return configuration;
}

OSObject* ApplePS2Controller::translateEntry(OSObject* obj)
{
    // Note: non-NULL result is retained...

    // if object is another array, translate it
    if (OSArray* array = OSDynamicCast(OSArray, obj))
        return translateArray(array);

    // if object is a string, may be translated to boolean
    if (OSString* string = OSDynamicCast(OSString, obj))
    {
        // object is string, translate special boolean values
        const char* sz = string->getCStringNoCopy();
        if (sz[0] == '>')
        {
            // boolean types true/false
            if (sz[1] == 'y' && !sz[2])
                return OSBoolean::withBoolean(true);
            else if (sz[1] == 'n' && !sz[2])
                return OSBoolean::withBoolean(false);
            // escape case ('>>n' '>>y'), replace with just string '>n' '>y'
            else if (sz[1] == '>' && (sz[2] == 'y' || sz[2] == 'n') && !sz[3])
                return OSString::withCString(&sz[1]);
        }
    }
    return NULL; // no translation
}

OSObject* ApplePS2Controller::translateArray(OSArray* array)
{
    // may return either OSArray* or OSDictionary*

    int count = array->getCount();
    if (!count)
        return NULL;

    OSObject* result = array;

    // if first entry is an empty array, process as array, else dictionary
    OSArray* test = OSDynamicCast(OSArray, array->getObject(0));
    if (test && test->getCount() == 0)
    {
        // using same array, but translating it...
        array->retain();

        // remove bogus first entry
        array->removeObject(0);
        --count;

        // translate entries in the array
        for (int i = 0; i < count; ++i)
        {
            if (OSObject* obj = translateEntry(array->getObject(i)))
            {
                array->replaceObject(i, obj);
                obj->release();
            }
        }
    }
    else
    {
        // array is key/value pairs, so must be even
        if (count & 1)
            return NULL;

        // dictionary constructed to accomodate all pairs
        int size = count >> 1;
        if (!size) size = 1;
        OSDictionary* dict = OSDictionary::withCapacity(size);
        if (!dict)
            return NULL;

        // go through each entry two at a time, building the dictionary
        for (int i = 0; i < count; i += 2)
        {
            OSString* key = OSDynamicCast(OSString, array->getObject(i));
            if (!key)
            {
                dict->release();
                return NULL;
            }
            // get value, use translated value if translated
            OSObject* obj = array->getObject(i+1);
            OSObject* trans = translateEntry(obj);
            if (trans)
                obj = trans;
            dict->setObject(key, obj);
            OSSafeReleaseNULL(trans);
        }
        result = dict;
    }

    // Note: result is retained when returned...
    return result;
}

OSDictionary* ApplePS2Controller::getConfigurationOverride(IOACPIPlatformDevice* acpi, const char* method)
{
    // attempt to get configuration data from provider
    OSObject* r = NULL;
    if (kIOReturnSuccess != acpi->evaluateObject(method, &r))
        return NULL;

    // for translation, method must return array
    OSObject* obj = NULL;
    OSArray* array = OSDynamicCast(OSArray, r);
    if (array)
        obj = translateArray(array);
    OSSafeReleaseNULL(r);

    // must be dictionary after translation, even though array is possible
    OSDictionary* result = OSDynamicCast(OSDictionary, obj);
    if (!result)
    {
        OSSafeReleaseNULL(obj);
        return NULL;
    }
    return result;
}

OSDictionary* ApplePS2Controller::makeConfigurationNode(OSDictionary* list, const char* section)
{
    if (!list)
        return NULL;

    lock(); // called from various probe functions, must protect against re-rentry

    // first merge Default with specific platform profile overrides
    OSDictionary* result = 0;
    OSDictionary* defaultNode = _getConfigurationNode(list, kDefault);
    OSDictionary* platformNode = getConfigurationNode(this, list);
    if (defaultNode)
    {
        // have default node, result is merge with platform node
        result = OSDictionary::withDictionary(defaultNode);
        if (result && platformNode)
            result->merge(platformNode);
    }
    else if (platformNode)
    {
        // no default node, try to use just platform node
        result = OSDictionary::withDictionary(platformNode);
    }

    // check RMCF cache, otherwise load by calling RMCF
    OSDictionary* over = _rmcfCache;
    if (!over)
    {
        // look for a parent that is ACPI... this will find PS2K (or eqivalent)
        IORegistryEntry* entry = this;
        IOACPIPlatformDevice* acpi = NULL;
        while (entry)
        {
            acpi = OSDynamicCast(IOACPIPlatformDevice, entry);
            if (acpi)
                break;
            entry = entry->getParentEntry(gIOServicePlane);
        }
        if (acpi)
        {
            // get override configuration data from ACPI RMCF
            over = getConfigurationOverride(acpi, "RMCF");
            _rmcfCache = over;
        }
    }

    if (over)
    {
        // check specific section, merge...
        if (OSDictionary* sect = OSDynamicCast(OSDictionary, over->getObject(section)))
        {
            if (!result)
            {
                // no default/platform to merge with, result is just the section
                result = OSDictionary::withDictionary(sect);
            }
            else
            {
                // otherwise merge the section
                result->merge(sect);
            }
        }
    }

    unlock();

    return result;
}
