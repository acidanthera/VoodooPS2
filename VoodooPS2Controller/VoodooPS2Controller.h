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

#ifndef _APPLEPS2CONTROLLER_H
#define _APPLEPS2CONTROLLER_H

#include <libkern/version.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include "ApplePS2Device.h"

class ApplePS2KeyboardDevice;
class ApplePS2MouseDevice;

//
// This section describes the problem with the PS/2 controller design and what
// we are doing about it (OUT_OF_ORDER_DATA_CORRECTION_FEATURE).
//
// While the controller processes requests sent by the client drivers, at some
// point in most requests, a read needs to be made from the data port to check
// an acknowledge or receive some sort of data.  We illustrate this issue with
// an example -- a write LEDs request to the keyboard:
//
// 1. Write        Write LED command.
// 2. Read   0xFA  Verify the acknowledge (0xFA).
// 3. Write        Write LED state.
// 4. Read   0xFA  Verify the acknowledge (0xFA).
//
// The problem is that the keyboard (when it is enabled) can send key events
// to the controller at any time, including when the controller is expecting
// to read an acknowledge next.  What ends up happening is this sequence:
//
// a. Write        Write LED command.
// b. Read   0x21  Keyboard reports [F] key was depressed, not realizing that
//                 we're still expecting a response to the command we  JUST
//                 sent the keyboard.  We receive 0x21 as a response to our
//                 command, and figure the command failed.
// c. Get    0xFA  Keyboard NOW decides to respond to the command with an
//                 acknowledge.    We're not waiting to read anything, so
//                 this byte gets dispatched to the driver's interrupt
//                 handler, which spews out an error message saying it
//                 wasn't expecting an acknowledge.
//
// What can we do about this?  In the above case, we can take note of the fact
// that we are specifically looking for the 0xFA acknowledgement byte (through
// the information passed in the kPS2C_ReadAndCompare primitive).  If we don't
// receive this byte next on the input data stream, we put the byte we did get
// aside for a moment, and give the keyboard (or mouse) a second chance to
// respond correctly.
//
// If we receive the 0xFA acknowledgement byte on the second read, that we
// assume that situation described above just happened.   We transparently
// dispatch the first byte to the driver's interrupt handler, where it was
// meant to go, and return the second correct byte to the read-and-compare
// logic, where it was meant to go.  Everyone wins.
//
// The only situation this feature cannot help is where a kPS2C_ReadDataPort
// primitive is issued in place of a kPS2C_ReadDataPortAndCompare primitive.
// This is necessary in some requests because the driver does not know what
// it is going to receive.   This can be illustrated in the mouse get info
// command.
//
// 1. Write        Prepare to write to mouse.
// 2. Write        Write information command.
// 3. Read   0xFA  Verify the acknowledge (0xFA). __-> mouse can report mouse
// 4. Read         Get first information byte.    __-> packet bytes in between
// 5. Read         Get second information byte.   __-> these reads
// 6. Rrad         Get third information byte.
//
// Controller cannot build any defenses against this.  It is suggested that the
// driver writer disable the mouse first, then send any dangerous commands, and
// re-enable the mouse when the command completes. 
//
// Note that the OUT_OF_ORDER_DATA_CORRECTION_FEATURE can be turned off at
// compile time.    Please see the readDataPort:expecting: method for more
// information about the assumptions necessary for this feature.
//

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Definitions
//

// Enable debugger support (eg. mini-monitor).

#define DEBUGGER_SUPPORT 0

// Enable dynamic "second chance" re-ordering of input stream data if a
// command response fails to match the expected byte.

#define OUT_OF_ORDER_DATA_CORRECTION_FEATURE 1

// Enable handling of interrupt data in workloop instead of at interrupt
// time.  This way is easier to debug.  For production use, this should
// be zero, such that PS2 data is buffered at real interrupt time, and handled
// as packets later in the workloop.

#define HANDLE_INTERRUPT_DATA_LATER 0
#define WATCHDOG_TIMER 0

// Interrupt definitions.

#define kIRQ_Keyboard           1
#define kIRQ_Mouse              12
#define kIPL_Keyboard           6
#define kIPL_Mouse              3

// Port timings.

#define kDataDelay              7       // usec to delay before data is valid

// Ports used to control the PS/2 keyboard/mouse and read data from it.

#define kDataPort               0x60    // keyboard data & cmds (read/write)
#define kCommandPort            0x64    // keybd status (read), command (write)

// Bit definitions for kCommandPort read values (status).

#define kOutputReady            0x01    // output (from keybd) buffer full
#define kInputBusy              0x02    // input (to keybd) buffer full
#define kSystemFlag             0x04    // "System Flag"
#define kCommandLastSent        0x08    // 1 = cmd, 0 = data last sent
#define kKeyboardInhibited      0x10    // 0 if keyboard inhibited
#define kMouseData              0x20    // mouse data available

// Watchdog timer definitions

#define kWatchdogTimerInterval  100

// Enable Mux commands
// Constants are from Linux
// https://github.com/torvalds/linux/blob/c2d7ed9d680fd14aa5486518bd0d0fa5963c6403/drivers/input/serio/i8042.c#L685-L693

#define kDP_MuxCmd              0xF0
#define kDP_EnableMuxCmd1       0x56
#define kDP_GetMuxVersion       0xA4
#define kDP_DisableMuxCmd1      0xF6
#define kDP_DisableMuxCmd2      0xA5

#if DEBUGGER_SUPPORT
// Definitions for our internal keyboard queue (holds keys processed by the
// interrupt-time mini-monitor-key-sequence detection code).

#define kKeyboardQueueSize 32            // number of KeyboardQueueElements

typedef struct KeyboardQueueElement KeyboardQueueElement;
struct KeyboardQueueElement
{
  queue_chain_t chain;
  UInt8         data;
};
#endif //DEBUGGER_SUPPORT

// Info.plist definitions

#define kDisableDevice          "DisableDevice"
#define kPlatformProfile        "Platform Profile"

#ifdef DEBUG
#define kMergedConfiguration    "Merged Configuration"
#endif

// ps2rst flags
#define RESET_CONTROLLER_ON_BOOT    1
#define RESET_CONTROLLER_ON_WAKEUP  2

class IOACPIPlatformDevice;

enum {
    kPS2PowerStateSleep  = 0,
    kPS2PowerStateDoze   = 1,
    kPS2PowerStateNormal = 2,
    kPS2PowerStateCount
};

// i8042 Mux indexes
#define PS2_MUX_IDX     2
#define PS2_MUX_PORTS   4

// Normally, the i8042 controller has 2 ports. With the mux active,
// there are 5 ports. 1 Keyboard port and 4 mux ports. All the muxed ports
// share the same IRQ. When the controller is in the multiplexer mode, the
// index for the aux port is skipped.

enum {
    kPS2KbdIdx = 0,
    kPS2AuxIdx = 1,
    kPS2MuxIdx = PS2_MUX_IDX,
    kPS2MaxIdx = PS2_MUX_IDX + PS2_MUX_PORTS
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2Controller Class Declaration
//

class EXPORT ApplePS2Controller : public IOService
{
  typedef IOService super;
  OSDeclareDefaultStructors(ApplePS2Controller);
    
public:
  // interrupt-time variables and functions
#if HANDLE_INTERRUPT_DATA_LATER
  IOInterruptEventSource * _interruptSourceMouse {nullptr};
  IOInterruptEventSource * _interruptSourceKeyboard {nullptr};
#endif
  IOInterruptEventSource * _interruptSourceQueue {nullptr};

#if DEBUGGER_SUPPORT
  bool _debuggingEnabled {false};

  void lockController(int * state);
  void unlockController(int state);

  bool doEscape(UInt8 key);
  bool dequeueKeyboardData(UInt8 * key);
  void enqueueKeyboardData(UInt8 key);
#endif //DEBUGGER_SUPPORT

private:
  IOWorkLoop *             _workLoop {nullptr};
  queue_head_t             _requestQueue {nullptr};
  IOLock*                  _requestQueueLock {nullptr};
  IOLock*                  _cmdbyteLock {nullptr};

  bool                     _interruptInstalledKeyboard {false};
  int                      _interruptInstalledMouse {0};

  int                      _ignoreInterrupts {0};
  int                      _ignoreOutOfOrder {0};
    
  ApplePS2Device *         _devices [kPS2MaxIdx] {nullptr};

  IONotifier*              _publishNotify {nullptr};
  IONotifier*              _terminateNotify {nullptr};
    
  OSSet*                   _notificationServices {nullptr};
    
#if DEBUGGER_SUPPORT
  IOSimpleLock *           _controllerLock {nullptr};       // mach simple spin lock

  KeyboardQueueElement *   _keyboardQueueAlloc {nullptr};   // queues' allocation space
  queue_head_t             _keyboardQueue {nullptr};        // queue of available keys
  queue_head_t             _keyboardQueueUnused {nullptr};  // queue of unused entries

  bool                     _extendedState {false};
  UInt16                   _modifierState {0};
#endif //DEBUGGER_SUPPORT

  thread_call_t            _powerChangeThreadCall {0};
  UInt32                   _currentPowerState {kPS2PowerStateNormal};
  bool                     _hardwareOffline {false};
  bool   				   _suppressTimeout {false};
  int                      _wakedelay {10};
  bool                     _mouseWakeFirst {false};
  bool                     _muxPresent {false};
  IOCommandGate*           _cmdGate {nullptr};
#if WATCHDOG_TIMER
  IOTimerEventSource*      _watchdogTimer {nullptr};
#endif
  OSDictionary*            _rmcfCache {nullptr};
  const OSSymbol*          _deliverNotification {nullptr};

  int                      _resetControllerFlag {RESET_CONTROLLER_ON_BOOT | RESET_CONTROLLER_ON_WAKEUP};

  virtual PS2InterruptResult _dispatchDriverInterrupt(size_t port, UInt8 data);
  virtual void dispatchDriverInterrupt(size_t port, UInt8 data);
#if HANDLE_INTERRUPT_DATA_LATER
  virtual void  interruptOccurred(IOInterruptEventSource *, int);
#endif
  void handleInterrupt(bool watchdog = false);
#if WATCHDOG_TIMER
  void onWatchdogTimer();
#endif
  virtual void  processRequest(PS2Request * request);
  virtual void  processRequestQueue(IOInterruptEventSource *, int);

#if OUT_OF_ORDER_DATA_CORRECTION_FEATURE
  virtual UInt8 readDataPort(size_t port, UInt8 expectedByte);
#endif

  virtual UInt8 readDataPort(size_t port);
  virtual void  writeCommandPort(UInt8 byte);
  virtual void  writeDataPort(UInt8 byte);
  void resetController(void);
  bool setMuxMode(bool);
    
  static void interruptHandlerMouse(OSObject*, void* refCon, IOService*, int);
  static void interruptHandlerKeyboard(OSObject*, void* refCon, IOService*, int);
   
  void notificationHandlerPublishGated(IOService * newService, IONotifier * notifier);
  bool notificationHandlerPublish(void * refCon, IOService * newService, IONotifier * notifier);
    
  void notificationHandlerTerminateGated(IOService * newService, IONotifier * notifier);
  bool notificationHandlerTerminate(void * refCon, IOService * newService, IONotifier * notifier);

  void dispatchMessageGated(int* message, void* data);
    
  static void setPowerStateCallout(thread_call_param_t param0,
                                   thread_call_param_t param1);

  static IOReturn setPowerStateAction(OSObject * target,
                                      void * arg0, void * arg1,
                                      void * arg2, void * arg3);

  virtual void setPowerStateGated(UInt32 newPowerState);

  virtual void dispatchDriverPowerControl(UInt32 whatToDo, size_t port);
  void free(void) override;
  IOReturn setPropertiesGated(OSObject* props);
  void submitRequestAndBlockGated(PS2Request* request);
  
  size_t getPortFromStatus(UInt8 status);

public:
  bool init(OSDictionary * properties) override;
  ApplePS2Controller* probe(IOService* provider, SInt32* score) override;
  bool start(IOService * provider) override;
  void stop(IOService * provider) override;

  IOWorkLoop * getWorkLoop() const override;

  void enableMuxPorts();
  virtual void installInterruptAction(size_t port);
  virtual void uninstallInterruptAction(size_t port);

  virtual PS2Request*  allocateRequest(int max = kMaxCommands);
  virtual void         freeRequest(PS2Request * request);
  virtual bool         submitRequest(PS2Request * request);
  virtual void         submitRequestAndBlock(PS2Request * request);
  virtual UInt8        setCommandByte(UInt8 setBits, UInt8 clearBits);
  void setCommandByteGated(PS2Request* request);

  IOReturn setPowerState(unsigned long powerStateOrdinal,
                                 IOService *   policyMaker) override;
    
  virtual void dispatchMessage(int message, void* data);
    
  IOReturn setProperties(OSObject* props) override;
  virtual void lock();
  virtual void unlock();
    
  static OSDictionary* getConfigurationNode(IORegistryEntry* entry, OSDictionary* list);
  virtual OSDictionary* makeConfigurationNode(OSDictionary* list, const char* section);

  OSDictionary* getConfigurationOverride(IOACPIPlatformDevice* acpi, const char* method);
  OSObject* translateArray(OSArray* array);
  OSObject* translateEntry(OSObject* obj);
};

#endif /* _APPLEPS2CONTROLLER_H */
