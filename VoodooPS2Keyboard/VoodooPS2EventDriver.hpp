//
//  HIDPS2EventDriver.hpp
//  HIDPS2Keyboard
//
//  Created by Gwydien on 8/27/23.
//  Copyright © 2023 coolstar. All rights reserved.
//

#ifndef HIDPS2EventDriver_hpp
#define HIDPS2EventDriver_hpp

#include <IOKit/hid/IOHIDEventService.h>

class VoodooPS2KeyboardHIDEventDriver : public IOHIDEventService
{
    OSDeclareDefaultStructors( VoodooPS2KeyboardHIDEventDriver );
    friend class VoodooPS2KeyboardHIDWrapper;

public:
    bool start(IOService *provider) override {
        IOHIDInterface *interface = OSDynamicCast(IOHIDInterface, provider);
        if (!IOHIDEventService::start(provider)) {
            return false;
        }
        
        registerService();
        return interface->open(this, 0, nullptr, nullptr);
    }
};

#endif /* HIDPS2EventDriver_hpp */
