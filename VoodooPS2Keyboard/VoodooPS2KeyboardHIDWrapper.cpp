//
//  VoodooPS2KeyboardHIDWrapper.cpp
//  VoodooPS2Keyboard
//
//  Created by Gwydien on 8/28/23.
//  Copyright © 2023 Acidanthera. All rights reserved.
//

#include "VoodooPS2KeyboardHIDWrapper.hpp"
#include <IOKit/hid/IOHIDUsageTables.h>
#include <IOKit/hid/AppleHIDUsageTables.h>

OSDefineMetaClassAndStructors(VoodooPS2KeyboardHIDWrapper, IOHIDDevice);
OSDefineMetaClassAndStructors(VoodooPS2KeyboardHIDEventDriver, IOHIDEventService);

static constexpr uint8_t ReportDescriptor[] = {
    0x05, 0x01,                         // Usage Page (Generic Desktop)
    0x09, 0x06,                         // USAGE (Keyboard)
    0xa1, 0x01,                         // COLLECTION (Application)
    0x85, 0x01,                         //   REPORT_ID (Keyboard)
    0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
    0x19, 0xe0,                         //   USAGE_MINIMUM (Keyboard LeftControl)
    0x29, 0xe7,                         //   USAGE_MAXIMUM (Keyboard Right GUI)
    0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                         //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         //   REPORT_SIZE (1)
    0x95, 0x08,                         //   REPORT_COUNT (8)
    0x81, 0x02,                         //   INPUT (Data,Var,Abs)
    0x95, 0x01,                         //   REPORT_COUNT (1)
    0x75, 0x08,                         //   REPORT_SIZE (8)
    0x81, 0x03,                         //   INPUT (Cnst,Var,Abs)
    0x95, 0x05,                         //   REPORT_COUNT (5)
    0x75, 0x01,                         //   REPORT_SIZE (1)
    0x05, 0x08,                         //   USAGE_PAGE (LEDs)
    0x19, 0x01,                         //   USAGE_MINIMUM (Num Lock)
    0x29, 0x05,                         //   USAGE_MAXIMUM (Kana)
    0x91, 0x02,                         //   OUTPUT (Data,Var,Abs)
    0x95, 0x01,                         //   REPORT_COUNT (1)
    0x75, 0x03,                         //   REPORT_SIZE (3)
    0x91, 0x03,                         //   OUTPUT (Cnst,Var,Abs)
    0x95, 0x06,                         //   REPORT_COUNT (6)
    0x75, 0x08,                         //   REPORT_SIZE (8)
    0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
    0x25, 0x65,                         //   LOGICAL_MAXIMUM (101)
    0x05, 0x07,                         //   USAGE_PAGE (Keyboard)
    0x19, 0x00,                         //   USAGE_MINIMUM (Reserved (no event indicated))
    0x29, 0x65,                         //   USAGE_MAXIMUM (Keyboard Application)
    0x81, 0x00,                         //   INPUT (Data,Ary,Abs)
    0xc0,                               // END_COLLECTION
};

bool VoodooPS2KeyboardHIDWrapper::start(IOService *provider) {
    setProperty(kIOHIDBuiltInKey, kOSBooleanTrue);
    setProperty("HIDDefaultBehavior", kOSBooleanTrue);
    setProperty("AppleVendorSupported", kOSBooleanTrue);
    
    registerService();
    if (!IOHIDDevice::start(provider)) return false;
    
    return true;
}

void VoodooPS2KeyboardHIDWrapper::stop(IOService *provider) {
    return IOHIDDevice::stop(provider);
}

void VoodooPS2KeyboardHIDWrapper::dispatchKeyboardEvent(AbsoluteTime time, uint16_t usagePage, uint16_t usage, uint8_t value) {
    if (eventDriver != nullptr) {
        eventDriver->dispatchKeyboardEvent(time, usagePage, usage, value);
    }
}

bool VoodooPS2KeyboardHIDWrapper::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    VoodooPS2KeyboardHIDEventDriver *temp = OSDynamicCast(VoodooPS2KeyboardHIDEventDriver, forClient);
    if (eventDriver == nullptr && temp != nullptr && IOHIDDevice::handleOpen(forClient, options, arg)) {
        eventDriver = temp;
        return true;
    }
    
    return IOHIDDevice::handleOpen(forClient, options, arg);
}

void VoodooPS2KeyboardHIDWrapper::handleClose(IOService *forClient, IOOptionBits options) {
    if (forClient == eventDriver) {
        eventDriver = nullptr;
    }
    
    IOHIDDevice::handleClose(forClient, options);
}

IOReturn VoodooPS2KeyboardHIDWrapper::newReportDescriptor(IOMemoryDescriptor **descriptor) const {
    IOBufferMemoryDescriptor *buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(ReportDescriptor));

    if (buffer == NULL) return kIOReturnNoResources;
    buffer->writeBytes(0, ReportDescriptor, sizeof(ReportDescriptor));
    *descriptor = buffer;
    
    return kIOReturnSuccess;
}

OSString* VoodooPS2KeyboardHIDWrapper::newManufacturerString() const {
    return OSString::withCString("Acidanthera");
}

OSNumber* VoodooPS2KeyboardHIDWrapper::newPrimaryUsageNumber() const {
    return OSNumber::withNumber(kHIDUsage_GD_Keyboard, 32);
}

OSNumber* VoodooPS2KeyboardHIDWrapper::newPrimaryUsagePageNumber() const {
    return OSNumber::withNumber(kHIDPage_GenericDesktop, 32);
}

/*
 * Apple vendor is needed for keyboard backlight filters to attach.
 * To prevent Apple event drivers from attaching, use a bogus
 *  product ID.
 */

OSNumber* VoodooPS2KeyboardHIDWrapper::newVendorIDNumber() const {
    // Apple Vendor needed for
    return OSNumber::withNumber(1452, 16);
}

OSNumber* VoodooPS2KeyboardHIDWrapper::newProductIDNumber() const {
    return OSNumber::withNumber(0x9999, 32);
}

OSString* VoodooPS2KeyboardHIDWrapper::newProductString() const {
    return OSString::withCString("PS2 Keyboard");
}

OSString* VoodooPS2KeyboardHIDWrapper::newTransportString() const {
    return OSString::withCString("PS2");
}
