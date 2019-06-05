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
    bool attach(IOService* provider) override;
    void detach(IOService* provider) override;
    bool init(OSDictionary* properties) override;
    void free() override;
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    
    MultitouchReturn handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) override;
    
    IOService* parent;
    
protected:
private:
    VoodooPS2MT2SimulatorDevice* simulator;
    VoodooPS2MT2ActuatorDevice* actuator;
};


#endif /* VoodooI2CNativeEngine_hpp */
