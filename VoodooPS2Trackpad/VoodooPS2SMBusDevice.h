//
//  VoodooPS2SMBusDevice.hpp
//  VoodooPS2Trackpad
//
//  Created by Avery Black on 9/13/24.
//  Copyright © 2024 Acidanthera. All rights reserved.
//

#ifndef VoodooPS2SMBusDevice_hpp
#define VoodooPS2SMBusDevice_hpp


#include "../VoodooPS2Controller/ApplePS2MouseDevice.h"
#include <IOKit/IOCommandGate.h>

#include "VoodooPS2TrackpadCommon.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ApplePS2SmbusDevice Class Declaration
//  Synaptics and Elans devices still need resets over PS2. This acts as a
//  PS/2 stub driver that attaches in lieu of the full touchpad driver to reset
//  on wakeups and sleep. This also prevents other devices attaching and using
//  the otherwise unused PS/2 interface
//

class EXPORT ApplePS2SmbusDevice : public IOService {
    typedef IOService super;
    OSDeclareDefaultStructors(ApplePS2SmbusDevice);
public:
    static ApplePS2SmbusDevice *withReset(bool resetNeeded, OSDictionary *data, uint8_t addr);
    
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    
private:
    ApplePS2MouseDevice *_nub {nullptr};
    bool _resetNeeded {false};
    OSDictionary *_data {nullptr};
    uint8_t _addr{0};
    
    IOReturn resetDevice();
    void powerAction(uint32_t ordinal);
};

#endif /* VoodooPS2SMBusDevice_hpp */
