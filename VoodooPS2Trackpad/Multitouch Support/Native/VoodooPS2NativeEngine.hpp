//
//  VoodooI2CNativeEngine.hpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#ifndef VoodooI2CNativeEngine_hpp
#define VoodooI2CNativeEngine_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOService.h>

#include "../VoodooPS2MultitouchEngine.hpp"
#include "VoodooPS2MT2SimulatorDevice.hpp"
#include "VoodooPS2MT2ActuatorDevice.hpp"

class VoodooPS2NativeEngine : public VoodooPS2MultitouchEngine {
    OSDeclareDefaultStructors(VoodooPS2NativeEngine);
    
public:
    bool attach(IOService* provider);
    void detach(IOService* provider);
    bool init(OSDictionary* properties);
    void free();
    bool start(IOService* provider);
    void stop(IOService* provider);
    
    MultitouchReturn handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp);
    
    IOService* parent;
    
protected:
private:
    VoodooPS2MT2SimulatorDevice* simulator;
    VoodooPS2MT2ActuatorDevice* actuator;
};


#endif /* VoodooI2CNativeEngine_hpp */
