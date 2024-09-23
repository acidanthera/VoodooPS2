//
//  VoodooPS2SMBusDevice.cpp
//  VoodooPS2Trackpad
//
//  Created by Gwydien on 9/13/24.
//  Copyright © 2024 Acidanthera. All rights reserved.
//

#include "VoodooPS2SMBusDevice.h"

OSDefineMetaClassAndStructors(ApplePS2SmbusDevice, IOService);

ApplePS2SmbusDevice *ApplePS2SmbusDevice::withReset(bool resetNeeded, OSDictionary *data, uint8_t addr) {
    ApplePS2SmbusDevice *dev = OSTypeAlloc(ApplePS2SmbusDevice);
    
    if (dev == nullptr) {
        IOLog("ApplePS2SmbusDevice: Could not create PS/2 stub device\n");
        return nullptr;
    }
    
    if (!dev->init()) {
        IOLog("ApplePS2SmbusDevice: Could not init PS/2 stub device\n");
        OSSafeReleaseNULL(dev);
        return nullptr;
    }
    
    dev->_resetNeeded = resetNeeded;
    dev->_data = data;
    dev->_data->retain();
    dev->_addr = addr;
    return dev;
}

bool ApplePS2SmbusDevice::start(IOService *provider) {
    IOReturn ret = kIOReturnSuccess;
    
    _nub = OSDynamicCast(ApplePS2MouseDevice, provider);
    if (_nub == nullptr) {
        return false;
    }
    
    if (_resetNeeded) {
        resetDevice();
    }
    
    ret = _nub->startSMBusCompanion(_data, _addr);
    
    _nub->installPowerControlAction(this, OSMemberFunctionCast(PS2PowerControlAction, this, &ApplePS2SmbusDevice::powerAction));
    
    OSSafeReleaseNULL(_data);
    return ret == kIOReturnSuccess;
}

void ApplePS2SmbusDevice::free() {
    OSSafeReleaseNULL(_data);
}

void ApplePS2SmbusDevice::powerAction(uint32_t ordinal) {
    if (ordinal == kPS2C_EnableDevice && _resetNeeded) {
        (void) resetDevice();
    }
}

IOReturn ApplePS2SmbusDevice::resetDevice() {
    TPS2Request<> request;
    request.commands[0].command = kPS2C_SendCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetDefaultsAndDisable;     // F5
    request.commandsCount = 1;
    _nub->submitRequestAndBlock(&request);
    
    if (request.commandsCount == 1) {
        DEBUG_LOG("VoodooPS2Trackpad: sending $FF failed: %d\n", request.commandsCount);
        return kIOReturnError;
    }
    
    return kIOReturnSuccess;
}

