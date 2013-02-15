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

#include "ApplePS2KeyboardDevice.h"
#include "VoodooPS2Controller.h"

// =============================================================================
// ApplePS2KeyboardDevice Class Implementation
//

#define super IOService
OSDefineMetaClassAndStructors(ApplePS2KeyboardDevice, IOService);

bool ApplePS2KeyboardDevice::attach( IOService * provider )
{
  if( !super::attach(provider) )  return false;

  assert(_controller == 0);
  _controller = (ApplePS2Controller *)provider;
  _controller->retain();

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::detach( IOService * provider )
{
  assert(_controller == provider);
  _controller->release();
  _controller = 0;

  super::detach(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::installInterruptAction(OSObject *         target,
                                                    PS2InterruptAction interruptAction,
                                                    PS2PacketAction packetAction)
{
  _controller->installInterruptAction(kDT_Keyboard, target, interruptAction, packetAction);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::uninstallInterruptAction()
{
  _controller->uninstallInterruptAction(kDT_Keyboard);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::installPowerControlAction(
                                                OSObject *            target,
                                                PS2PowerControlAction action)
{
  _controller->installPowerControlAction(kDT_Keyboard, target, action);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::uninstallPowerControlAction()
{
  _controller->uninstallPowerControlAction(kDT_Keyboard);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2Request * ApplePS2KeyboardDevice::allocateRequest(int max)
{
  return _controller->allocateRequest(max);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::freeRequest(PS2Request * request)
{
  _controller->freeRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2KeyboardDevice::submitRequest(PS2Request * request)
{
  return _controller->submitRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::submitRequestAndBlock(PS2Request * request)
{
  _controller->submitRequestAndBlock(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::installMessageAction(OSObject* target, PS2MessageAction action)
{
  _controller->installMessageAction(kDT_Keyboard, target, action);
}

void ApplePS2KeyboardDevice::uninstallMessageAction()
{
  _controller->uninstallMessageAction(kDT_Keyboard);
}

void ApplePS2KeyboardDevice::dispatchMouseMessage(int message, void *data)
{
  _controller->dispatchMessage(kDT_Mouse, message, data);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt8 ApplePS2KeyboardDevice::setCommandByte(UInt8 setBits, UInt8 clearBits)
{
    return _controller->setCommandByte(setBits, clearBits);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2KeyboardDevice::lock()
{
    _controller->lock();
}

void ApplePS2KeyboardDevice::unlock()
{
    _controller->unlock();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 0);
OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 1);
OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 2);
OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 3);
OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 4);
OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 5);
OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 6);
OSMetaClassDefineReservedUnused(ApplePS2KeyboardDevice, 7);
