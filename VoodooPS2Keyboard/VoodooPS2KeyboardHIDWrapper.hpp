//
//  VoodooPS2KeyboardHIDWrapper.hpp
//  VoodooPS2Keyboard
//
//  Created by Gwydien on 8/28/23.
//  Copyright © 2023 Acidanthera. All rights reserved.
//

#ifndef VoodooPS2KeyboardHIDWrapper_hpp
#define VoodooPS2KeyboardHIDWrapper_hpp

#include <IOKit/hid/IOHIDDevice.h>
#include "VoodooPS2EventDriver.hpp"

class VoodooPS2KeyboardHIDWrapper : public IOHIDDevice {
    OSDeclareDefaultStructors(VoodooPS2KeyboardHIDWrapper);
public:
    bool start(IOService *start) override;
    void stop(IOService *start) override;
    bool handleOpen(IOService *forClient, IOOptionBits options, void *arg) override;
    void handleClose(IOService *forClient, IOOptionBits options) override;
    
    IOReturn newReportDescriptor(IOMemoryDescriptor **descriptor) const override;
    
    virtual OSString* newManufacturerString() const override;
    virtual OSNumber* newPrimaryUsageNumber() const override;
    virtual OSNumber* newPrimaryUsagePageNumber() const override;
    virtual OSString* newProductString() const override;
    virtual OSString* newTransportString() const override;
    virtual OSNumber* newVendorIDNumber() const override;
    virtual OSNumber* newProductIDNumber() const override;

    void dispatchKeyboardEvent(AbsoluteTime time, uint16_t usagePage, uint16_t usage, uint8_t value);
    
private:
    VoodooPS2KeyboardHIDEventDriver *eventDriver {nullptr};
};

#endif /* VoodooPS2KeyboardHIDWrapper_hpp */
