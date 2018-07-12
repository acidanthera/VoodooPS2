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

#include "ApplePS2Device.h"
#include "VoodooPS2Controller.h"

OSDefineMetaClassAndStructors(ApplePS2Device, IOService);

// =============================================================================
// ApplePS2Device Class Implementation
//

bool ApplePS2Device::attach(IOService * provider)
{
  if (!super::attach(provider))
      return false;

  assert(_controller == 0);
  _controller = (ApplePS2Controller*)provider;
  _controller->retain();

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::detach( IOService * provider )
{
  assert(_controller == provider);
  _controller->release();
  _controller = 0;

  super::detach(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2Request * ApplePS2Device::allocateRequest(int max)
{
  return _controller->allocateRequest(max);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::freeRequest(PS2Request * request)
{
  _controller->freeRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2Device::submitRequest(PS2Request * request)
{
  return _controller->submitRequest(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::submitRequestAndBlock(PS2Request * request)
{
  _controller->submitRequestAndBlock(request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt8 ApplePS2Device::setCommandByte(UInt8 setBits, UInt8 clearBits)
{
    return _controller->setCommandByte(setBits, clearBits);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::lock()
{
    _controller->lock();
}

void ApplePS2Device::unlock()
{
    _controller->unlock();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::installInterruptAction(OSObject *         target,
                                                    PS2InterruptAction interruptAction,
                                                    PS2PacketAction packetAction)
{
    _controller->installInterruptAction(_deviceType, target, interruptAction, packetAction);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::uninstallInterruptAction()
{
    _controller->uninstallInterruptAction(_deviceType);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::installPowerControlAction(
                                                       OSObject *            target,
                                                       PS2PowerControlAction action)
{
    _controller->installPowerControlAction(_deviceType, target, action);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::uninstallPowerControlAction()
{
    _controller->uninstallPowerControlAction(_deviceType);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Device::dispatchMessage(int message, void *data)
{
    _controller->dispatchMessage(message, data);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2Controller* ApplePS2Device::getController()
{
    return _controller;
}
