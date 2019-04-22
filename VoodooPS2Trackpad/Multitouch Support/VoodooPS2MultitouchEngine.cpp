//
//  VoodooI2CMultitouchEngine.cpp
//  VoodooI2C
//
//  Created by Alexandre on 22/09/2017.
//  Copyright © 2017 Alexandre Daoud. All rights reserved.
//

#include "VoodooPS2MultitouchEngine.hpp"
#include "VoodooPS2MultitouchInterface.hpp"
#include "VoodooPS2DigitiserTransducer.hpp"

#define super IOService
OSDefineMetaClassAndStructors(VoodooPS2MultitouchEngine, IOService);

UInt8 VoodooPS2MultitouchEngine::getScore() {
    return 0x0;
}

MultitouchReturn VoodooPS2MultitouchEngine::handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) {
    if (event.contact_count)
        IOLog("Contact Count: %d\n", event.contact_count);
    
    for (int index = 0, count = event.transducers->getCount(); index < count; index++) {
        VoodooPS2DigitiserTransducer* transducer = OSDynamicCast(VoodooPS2DigitiserTransducer, event.transducers->getObject(index));
        
        if (!transducer)
            continue;
        
        if (transducer->tip_switch)
            IOLog("Transducer ID: %d, X: %d, Y: %d, Z: %d, Pressure: %d\n", transducer->secondary_id, transducer->coordinates.x.value(), transducer->coordinates.y.value(), transducer->coordinates.z.value(), transducer->tip_pressure.value());
    }

    return MultitouchReturnContinue;
}

bool VoodooPS2MultitouchEngine::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    interface = OSDynamicCast(VoodooPS2MultitouchInterface, provider);

    if (!interface)
        return false;

    interface->open(this);

    setProperty("VoodooI2CServices Supported", kOSBooleanTrue);

    registerService();

    return true;
}

void VoodooPS2MultitouchEngine::stop(IOService* provider) {
    if (interface) {
        interface->close(this);
        OSSafeReleaseNULL(interface);
    }

    super::stop(provider);
}
