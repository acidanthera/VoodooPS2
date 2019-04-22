//
//  VoodooI2CNativeEngine.cpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#include "VoodooPS2NativeEngine.hpp"

#define super VoodooPS2MultitouchEngine
OSDefineMetaClassAndStructors(VoodooPS2NativeEngine, VoodooPS2MultitouchEngine);

bool VoodooPS2NativeEngine::attach(IOService* provider) {
    if (!super::attach(provider))
        return false;
    
    return true;
}

void VoodooPS2NativeEngine::detach(IOService* provider) {
    super::detach(provider);
}

bool VoodooPS2NativeEngine::init(OSDictionary* properties) {
    if (!super::init(properties))
        return false;
    
    return true;
}

MultitouchReturn VoodooPS2NativeEngine::handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) {
    if (simulator)
        simulator->constructReport(event, timestamp);
    
    return MultitouchReturnContinue;
}

void VoodooPS2NativeEngine::free() {
    super::free();
}

bool VoodooPS2NativeEngine::start(IOService* provider) {
    if (!super::start(provider))
        return false;
    
    parent = provider;
    simulator = OSTypeAlloc(VoodooPS2MT2SimulatorDevice);
    actuator = OSTypeAlloc(VoodooPS2MT2ActuatorDevice);
    
    if (!simulator->init(NULL) ||
        !simulator->attach(this) ||
        !simulator->start(this)) {
        IOLog("%s Could not initialise simulator\n", getName());
        OSSafeReleaseNULL(simulator);
    }
    
    if (!actuator->init(NULL) ||
        !actuator->attach(this) ||
        !actuator->start(this)) {
        IOLog("%s Could not initialise actuator\n", getName());
        OSSafeReleaseNULL(actuator);
    }
    
    if (!simulator || !actuator)
        return false;
    
    return true;
}

void VoodooPS2NativeEngine::stop(IOService* provider) {
    super::stop(provider);
}
