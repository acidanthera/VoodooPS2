//
//  VoodooPS2SMBusDevice.cpp
//  VoodooPS2Trackpad
//
//  Created by Gwydien on 9/13/24.
//  Copyright © 2024 Acidanthera. All rights reserved.
//

#include "VoodooPS2SMBusDevice.h"

// Steps
// 1. If PS/2 Mode, create SMBus node
// 2. Attempt start. VRMI should have a power dependency on PS/2 controller
// 3. If SMBus start works, return PS2SmbusDevice

ApplePS2SmbusDevice *ApplePS2SmbusDevice::withReset(bool resetNeeded) {
    ApplePS2SmbusDevice *dev = OSTypeAlloc(ApplePS2SmbusDevice);
    
    if (dev == nullptr) {
        IOLog("ApplePS2SmbusDevice - Could not create PS/2 stub device\n");
        return nullptr;
    }
    
    if (!dev->init()) {
        IOLog("ApplePS2SmbusDevice - Could not init PS/2 stub device\n");
    }
    
    dev->_resetNeeded = resetNeeded;
    return dev;
}

bool ApplePS2SmbusDevice::start(IOService *provider) {
    IOReturn ret = kIOReturnSuccess;
    
    _nub = OSDynamicCast(ApplePS2MouseDevice, provider);
    if (_nub == nullptr) {
        return false;
    }
    
    if (_resetNeeded) {
        ret = resetDevice();
    }
    
    _nub->installPowerControlAction(this, OSMemberFunctionCast(PS2PowerControlAction, this, &ApplePS2SmbusDevice::powerAction));
    return ret == kIOReturnSuccess;
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

