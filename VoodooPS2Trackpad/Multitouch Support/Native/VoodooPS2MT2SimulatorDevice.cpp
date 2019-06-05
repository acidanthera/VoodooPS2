//
//  VoodooI2CMT2SimulatorDevice.cpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#include "VoodooPS2MT2SimulatorDevice.hpp"
#include "VoodooPS2NativeEngine.hpp"

#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>

#ifdef DEBUG_MSG
#define DEBUG_LOG(args...)  do { IOLog(args); } while (0)
#else
#define DEBUG_LOG(args...)  do { } while (0)
#endif

#define AbsoluteTime_to_scalar(x)    (*(uint64_t *)(x))
/* t1 -= t2 */
#define SUB_ABSOLUTETIME(t1, t2)                (AbsoluteTime_to_scalar(t1) -=                AbsoluteTime_to_scalar(t2))

#define super IOHIDDevice
OSDefineMetaClassAndStructors(VoodooPS2MT2SimulatorDevice, IOHIDDevice);

unsigned char report_descriptor[] = {0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x85, 0x02, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0x95, 0x04, 0x75, 0x08, 0x81, 0x01, 0xc0, 0xc0, 0x05, 0x0d, 0x09, 0x05, 0xa1, 0x01, 0x06, 0x00, 0xff, 0x09, 0x0c, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x10, 0x85, 0x3f, 0x81, 0x22, 0xc0, 0x06, 0x00, 0xff, 0x09, 0x0c, 0xa1, 0x01, 0x06, 0x00, 0xff, 0x09, 0x0c, 0x15, 0x00, 0x26, 0xff, 0x00, 0x85, 0x44, 0x75, 0x08, 0x96, 0x6b, 0x05, 0x81, 0x00, 0xc0};

void VoodooPS2MT2SimulatorDevice::constructReport(VoodooI2CMultitouchEvent multitouch_event, AbsoluteTime timestamp) {
    if (!ready_for_reports)
        return;
    
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooPS2MT2SimulatorDevice::constructReportGated), &multitouch_event, &timestamp);
}

void VoodooPS2MT2SimulatorDevice::constructReportGated(VoodooI2CMultitouchEvent& multitouch_event, AbsoluteTime& timestamp) {
    if (!ready_for_reports)
        return;
    
    input_report.ReportID = 0x02;
    input_report.Unused[0] = 0;
    input_report.Unused[1] = 0;
    input_report.Unused[2] = 0;
    input_report.Unused[3] = 0;
    input_report.Unused[4] = 0;
    
    VoodooPS2DigitiserTransducer* transducer = OSDynamicCast(VoodooPS2DigitiserTransducer, multitouch_event.transducers->getObject(0));
    
    if (!transducer)
        return;

    // physical button
    input_report.Button = transducer->physical_button.value();
    
    // touch active
    
    // multitouch report id
    input_report.multitouch_report_id = 0x31; //Magic
    
    // timestamp
    AbsoluteTime relative_timestamp = timestamp;
    SUB_ABSOLUTETIME(&relative_timestamp, &start_timestamp);
    
    UInt64 milli_timestamp;
    
    absolutetime_to_nanoseconds(relative_timestamp, &milli_timestamp);
    
    milli_timestamp /= 1000000;
    
    input_report.timestamp_buffer[0] = (milli_timestamp << 0x3) | 0x4;
    input_report.timestamp_buffer[1] = (milli_timestamp >> 0x5) & 0xFF;
    input_report.timestamp_buffer[2] = (milli_timestamp >> 0xd) & 0xFF;
    
    // finger data
    int first_unknownbit = -1;
    bool input_active = false;
    
    for (int i = 0; i < MAX_FINGER_COUNT; i++) {
        VoodooPS2DigitiserTransducer* transducer = OSDynamicCast(VoodooPS2DigitiserTransducer, multitouch_event.transducers->getObject(i));
        
        if (!transducer || !transducer->is_valid)
            continue;
        
        if (transducer->type == kDigitiserTransducerStylus) {
            continue;
        }
        
        if (!transducer->tip_switch.value()) {
        } else {
            input_active = true;
        }
        
        MAGIC_TRACKPAD_INPUT_REPORT_FINGER& finger_data = input_report.FINGERS[i];
        
        SInt16 x_min = 3678;
        SInt16 y_min = 2479;

        IOFixed scaled_x = (((transducer->coordinates.x.value()) * 1.0f) / (engine->interface->logical_max_x + 1 - engine->interface->logical_min_x)) * 7612;
        IOFixed scaled_y = (((transducer->coordinates.y.value()) * 1.0f) / (engine->interface->logical_max_y + 1 - engine->interface->logical_min_y)) * 5065;
        
        IOFixed scaled_old_x = (((transducer->coordinates.x.last.value) * 1.0f) / (engine->interface->logical_max_x + 1 - engine->interface->logical_min_x)) * 7612;
        uint8_t scaled_old_x_truncated = scaled_old_x;
        
        int newunknown = stashed_unknown[i];
        
        if (abs(scaled_x - scaled_old_x_truncated) > 50){
            if (scaled_x <= 23){
                newunknown = 0x44;
            } else if (scaled_x <= 27){
                newunknown = 0x64;
            } else if (scaled_x <= 37){
                newunknown = 0x84;
            } else if (scaled_x <= 2307){
                newunknown = 0x94;
            } else if (scaled_x <= 3059){
                newunknown = 0x90;
            } else if (scaled_x <= 4139){
                newunknown = 0x8c;
            } else if (scaled_x <= 5015){
                newunknown = 0x88;
            } else if (scaled_x <= 7553){
                newunknown = 0x94;
            } else if (scaled_x <= 7600){
                newunknown = 0x84;
            } else if (scaled_x <= 7605){
                newunknown = 0x64;
            } else {
                newunknown = 0x44;
            }
        }
        
        if(first_unknownbit == -1) {
            first_unknownbit = newunknown;
        }
        newunknown = first_unknownbit - (4 * i);
        
        DEBUG_LOG("constructReportGated: finger[%d] tip_pressure=%d tip_width=%d button=%d x=%d y=%d", i, transducer->tip_pressure.value(), transducer->tip_width.value(), input_report.Button, scaled_x, scaled_y);

        finger_data.Pressure = transducer->tip_pressure.value();
        finger_data.Size = transducer->tip_width.value();
        finger_data.Touch_Major = transducer->tip_width.value();
        finger_data.Touch_Minor = transducer->tip_width.value();

        if (/*transducer->tip_pressure.value() ||*/ (input_report.Button)) {
            finger_data.Pressure = 120;
        }
        

        stashed_unknown[i] = newunknown;
        
        SInt16 adjusted_x = scaled_x - x_min;
        SInt16 adjusted_y = scaled_y - y_min;
        adjusted_y = adjusted_y * -1;
        
        uint16_t rawx = *(uint16_t *)&adjusted_x;
        uint16_t rawy = *(uint16_t *)&adjusted_y;
        
        finger_data.AbsX = rawx & 0xff;
        
        finger_data.AbsXY = 0;
        finger_data.AbsXY |= (rawx >> 8) & 0x0f;
        if ((rawx >> 15) & 0x01)
            finger_data.AbsXY |= 0x10;
        
        finger_data.AbsXY |= (rawy << 5) & 0xe0;
        finger_data.AbsY[0] = (rawy >> 3) & 0xff;
        finger_data.AbsY[1] = (rawy >> 11) & 0x01;
        if ((rawy >> 15) & 0x01)
            finger_data.AbsY[1] |= 0x02;
        
        finger_data.AbsY[1] |= newunknown;
        
        finger_data.Orientation_Origin = (128 & 0xF0) | ((transducer->secondary_id + 1) & 0xF);
    }
    
    if (input_active)
        input_report.TouchActive = 0x3;
    else
        input_report.TouchActive = 0x2;
    
    int total_report_len = (9 * multitouch_event.contact_count) + 12;
    IOBufferMemoryDescriptor* buffer_report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, total_report_len);
    buffer_report->writeBytes(0, &input_report, total_report_len);
    
    handleReport(buffer_report, kIOHIDReportTypeInput);
    buffer_report->release();
    buffer_report = NULL;
    
    if (!input_active) {
        total_report_len = (9 * MAX_FINGER_COUNT) + 12;
        
        for (int i = 0; i < MAX_FINGER_COUNT; i++) {
            input_report.FINGERS[i].Size = 0x0;
            input_report.FINGERS[i].Pressure = 0x0;
            input_report.FINGERS[i].Touch_Major = 0x0;
            input_report.FINGERS[i].Touch_Minor = 0x0;
        }
        
        milli_timestamp += 10;
        
        input_report.timestamp_buffer[0] = (milli_timestamp << 0x3) | 0x4;
        input_report.timestamp_buffer[1] = (milli_timestamp >> 0x5) & 0xFF;
        input_report.timestamp_buffer[2] = (milli_timestamp >> 0xd) & 0xFF;
        
        buffer_report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, total_report_len);
        buffer_report->writeBytes(0, &input_report, total_report_len);
        handleReport(buffer_report, kIOHIDReportTypeInput);
        buffer_report->release();
        
        for (int i = 0; i < MAX_FINGER_COUNT; i++) {
            input_report.FINGERS[i].AbsY[1] &= ~0xF4;
            input_report.FINGERS[i].AbsY[1] |= 0x14;
        }

        milli_timestamp += 10;
        
        input_report.timestamp_buffer[0] = (milli_timestamp << 0x3) | 0x4;
        input_report.timestamp_buffer[1] = (milli_timestamp >> 0x5) & 0xFF;
        input_report.timestamp_buffer[2] = (milli_timestamp >> 0xd) & 0xFF;

        buffer_report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, total_report_len);
        buffer_report->writeBytes(0, &input_report, total_report_len);
        handleReport(buffer_report, kIOHIDReportTypeInput);
        buffer_report->release();
        
        milli_timestamp += 10;
        
        input_report.timestamp_buffer[0] = (milli_timestamp << 0x3) | 0x4;
        input_report.timestamp_buffer[1] = (milli_timestamp >> 0x5) & 0xFF;
        input_report.timestamp_buffer[2] = (milli_timestamp >> 0xd) & 0xFF;

        buffer_report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, 12);
        buffer_report->writeBytes(0, &input_report, 12);
        handleReport(buffer_report, kIOHIDReportTypeInput);
        buffer_report->release();
    }
    
    //input_report = {};
}

bool VoodooPS2MT2SimulatorDevice::getMultitouchPreferences(void* target, void* ref_con, IOService* multitouch_device, IONotifier* notifier) {
    VoodooPS2MT2SimulatorDevice* simulator = (VoodooPS2MT2SimulatorDevice*)target;
    
    IOLog("VoodooI2C: Got multitouch matched\n");
    
    simulator->multitouch_device_preferences = OSDynamicCast(OSDictionary, simulator->getProperty("MultitouchPreferences", gIOServicePlane, kIORegistryIterateRecursively));
    
    if (simulator->multitouch_device_preferences) {
        IOLog("VoodooI2C: Got multitouch preferences\n");
        
        simulator->multitouch_device_notifier->disable();
        simulator->multitouch_device_notifier->remove();
    }
    
    return true;
}

bool VoodooPS2MT2SimulatorDevice::start(IOService* provider) {
    if (!super::start(provider))
        return false;
    
    clock_get_uptime(&start_timestamp);
    
    engine = OSDynamicCast(VoodooPS2NativeEngine, provider);
    
    if (!engine)
        return false;
    
    work_loop = this->getWorkLoop();
    if (!work_loop) {
        IOLog("%s Could not get a IOWorkLoop instance\n", getName());
        releaseResources();
        return false;
    }
    
    work_loop->retain();
    
    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLog("%s Could not open command gate\n", getName());
        releaseResources();
        return false;
    }
    
    PMinit();
    engine->parent->joinPMtree(this);
    registerPowerDriver(this, VoodooI2CIOPMPowerStates, kVoodooI2CIOPMNumberPowerStates);
    
    /*
     // We can't use any factor, because we need to stretch the trackpad to MT2 coordinate system for edge gestures to work.
    if(engine->interface->physical_max_x)
        factor_x = engine->interface->logical_max_x / engine->interface->physical_max_x;
    
    if(engine->interface->physical_max_y)
        factor_y = engine->interface->logical_max_y / engine->interface->physical_max_y;
    
    IOLog("Factor: %d, %d", factor_x, factor_y);
    
    if (!factor_x)
        factor_x = 1;
    
    if (!factor_y)
        factor_y = 1;
     */
    
    multitouch_device_notifier = addMatchingNotification(gIOFirstPublishNotification, IOService::serviceMatching("AppleMultitouchDevice"), VoodooPS2MT2SimulatorDevice::getMultitouchPreferences, this, NULL, 0);
    
    ready_for_reports = true;
    
    return true;
}

void VoodooPS2MT2SimulatorDevice::stop(IOService* provider) {
    releaseResources();
    
    PMstop();
    
    super::stop(provider);
}

IOReturn VoodooPS2MT2SimulatorDevice::setPowerState(unsigned long whichState, IOService* whatDevice) {
    if (whatDevice != this)
        return kIOReturnInvalid;
    if (whichState == 0){
        // ready_for_reports = false;
    } else {
        // ready_for_reports = true;
    }
    return kIOPMAckImplied;
}

void VoodooPS2MT2SimulatorDevice::releaseResources() {
    if (command_gate) {
        work_loop->removeEventSource(command_gate);
        command_gate->release();
        command_gate = NULL;
    }
    
    if (work_loop)
        OSSafeReleaseNULL(work_loop);
}

IOReturn VoodooPS2MT2SimulatorDevice::setReport(IOMemoryDescriptor* report, IOHIDReportType reportType, IOOptionBits options) {
    
    UInt32 report_id = options & 0xFF;
    
    if (report_id == 0x1) {
        char* raw_buffer = (char*)IOMalloc(report->getLength());
        
        report->prepare();
        
        report->readBytes(0, raw_buffer, report->getLength());
        
        report->complete();

		if (new_get_report_buffer != nullptr) {
			new_get_report_buffer->release();
		}
        
        new_get_report_buffer = OSData::withCapacity(1);
        
        UInt8 value = raw_buffer[1];
        
        if (value == 0xDB) {
            unsigned char buffer[] = {0x1, 0xDB, 0x00, 0x49, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        if (value == 0xD1) {
            unsigned char buffer[] = {0x1, 0xD1, 0x00, 0x01, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        if (value == 0xD3) {
            unsigned char buffer[] = {0x1, 0xD3, 0x00, 0x0C, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        if (value == 0xD0) {
            unsigned char buffer[] = {0x1, 0xD0, 0x00, 0x0F, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        if (value == 0xA1) {
            unsigned char buffer[] = {0x1, 0xA1, 0x00, 0x06, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        if (value == 0xD9) {
            unsigned char buffer[] = {0x1, 0xD9, 0x00, 0x10, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        if (value == 0x7F) {
            unsigned char buffer[] = {0x1, 0x7F, 0x00, 0x04, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        if (value == 0xC8) {
            unsigned char buffer[] = {0x1, 0xC8, 0x00, 0x01, 0x00};
            new_get_report_buffer->appendBytes(buffer, sizeof(buffer));
        }
        
        IOFree(raw_buffer, report->getLength());
    }
    
    return kIOReturnSuccess;
}

IOReturn VoodooPS2MT2SimulatorDevice::getReport(IOMemoryDescriptor* report, IOHIDReportType reportType, IOOptionBits options) {
    UInt32 report_id = options & 0xFF;
	Boolean owns_buffer = true;

    OSData* get_buffer = OSData::withCapacity(1);

	if (get_buffer == nullptr) {
		return kIOReturnNoResources;
	}

    if (report_id == 0x0) {
        unsigned char buffer[] = {0x0, 0x01};
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0x1) {
		owns_buffer = false;
        get_buffer->release();
        get_buffer = new_get_report_buffer;
    }

	if (get_buffer == nullptr) {
		return kIOReturnNoResources;
	}
    
    if (report_id == 0xD1) {
        unsigned char buffer[] = {0xD1, 0x81};
        //Family ID = 0x81
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0xD3) {
        unsigned char buffer[] = {0xD3, 0x01, 0x16, 0x1E, 0x03, 0x95, 0x00, 0x14, 0x1E, 0x62, 0x05, 0x00, 0x00};
        //Sensor Rows = 0x16
        //Sensor Columns = 0x1e
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0xD0) {
        unsigned char buffer[] = {0xD0, 0x02, 0x01, 0x00, 0x14, 0x01, 0x00, 0x1E, 0x00, 0x02, 0x14, 0x02, 0x01, 0x0E, 0x02, 0x00}; //Sensor Region Description
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0xA1) {
        unsigned char buffer[] = {0xA1, 0x00, 0x00, 0x05, 0x00, 0xFC, 0x01}; //Sensor Region Param
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0xD9) {
        //Sensor Surface Width = 0x3cf0 (0xf0, 0x3c) = 15.600 cm
        //Sensor Surface Height = 0x2b20 (0x20, 0x2b) = 11.040 cm
        
        // It's already in 0.01 mm units
        uint32_t rawWidth = engine->interface->physical_max_x;
        uint32_t rawHeight = engine->interface->physical_max_y;
        
        uint8_t rawWidthLower = rawWidth & 0xff;
        uint8_t rawWidthHigher = (rawWidth >> 8) & 0xff;
        
        uint8_t rawHeightLower = rawHeight & 0xff;
        uint8_t rawHeightHigher = (rawHeight >> 8) & 0xff;
        
        unsigned char buffer[] = {0xD9, rawWidthLower, rawWidthHigher, 0x00, 0x00, rawHeightLower, rawHeightHigher, 0x00, 0x00, 0x44, 0xE3, 0x52, 0xFF, 0xBD, 0x1E, 0xE4, 0x26}; //Sensor Surface Description
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0x7F) {
        unsigned char buffer[] = {0x7F, 0x00, 0x00, 0x00, 0x00};
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0xC8) {
        unsigned char buffer[] = {0xC8, 0x08};
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0x2) {
        unsigned char buffer[] = {0x02, 0x01};
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    if (report_id == 0xDB) {
        uint32_t rawWidth = engine->interface->physical_max_x;
        uint32_t rawHeight = engine->interface->physical_max_y;
        
        uint8_t rawWidthLower = rawWidth & 0xff;
        uint8_t rawWidthHigher = (rawWidth >> 8) & 0xff;
        
        uint8_t rawHeightLower = rawHeight & 0xff;
        uint8_t rawHeightHigher = (rawHeight >> 8) & 0xff;
        
        unsigned char buffer[] = {0xDB, 0x01, 0x02, 0x00,
            /* Start 0xD1 */ 0xD1, 0x81, /* End 0xD1 */
            0x0D, 0x00,
            /* Start 0xD3 */ 0xD3, 0x01, 0x16, 0x1E, 0x03, 0x95, 0x00, 0x14, 0x1E, 0x62, 0x05, 0x00, 0x00, /* End 0xD3 */
            0x10, 0x00,
            /* Start 0xD0 */ 0xD0, 0x02, 0x01, 0x00, 0x14, 0x01, 0x00, 0x1E, 0x00, 0x02, 0x14, 0x02, 0x01, 0x0E, 0x02, 0x00, /* End 0xD0 */
            0x07, 0x00,
            /* Start 0xA1 */ 0xA1, 0x00, 0x00, 0x05, 0x00, 0xFC, 0x01, /* End 0xA1 */
            0x11, 0x00,
            /* Start 0xD9 */ 0xD9, rawWidthLower, rawWidthHigher, 0x00, 0x00, rawHeightLower, rawHeightHigher, 0x00, 0x00, 0x44, 0xE3, 0x52, 0xFF, 0xBD, 0x1E, 0xE4, 0x26,
            /* Start 0x7F */ 0x7F, 0x00, 0x00, 0x00, 0x00 /*End 0x7F */};
        get_buffer->appendBytes(buffer, sizeof(buffer));
    }
    
    report->writeBytes(0, get_buffer->getBytesNoCopy(), get_buffer->getLength());

	if (owns_buffer) {
		get_buffer->release();
	}

    return kIOReturnSuccess;
}

IOReturn VoodooPS2MT2SimulatorDevice::newReportDescriptor(IOMemoryDescriptor** descriptor) const {
    IOBufferMemoryDescriptor* report_descriptor_buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(report_descriptor));
    
    if (!report_descriptor_buffer) {
        IOLog("%s Could not allocate buffer for report descriptor\n", getName());
        return kIOReturnNoResources;
    }
    
    report_descriptor_buffer->writeBytes(0, report_descriptor, sizeof(report_descriptor));
    *descriptor = report_descriptor_buffer;
    
    return kIOReturnSuccess;
}

OSString* VoodooPS2MT2SimulatorDevice::newManufacturerString() const {
    return OSString::withCString("Apple Inc.");
}

OSNumber* VoodooPS2MT2SimulatorDevice::newPrimaryUsageNumber() const {
    return OSNumber::withNumber(kHIDUsage_GD_Mouse, 32);
}

OSNumber* VoodooPS2MT2SimulatorDevice::newPrimaryUsagePageNumber() const {
    return OSNumber::withNumber(kHIDPage_GenericDesktop, 32);
}

OSNumber* VoodooPS2MT2SimulatorDevice::newProductIDNumber() const {
    return OSNumber::withNumber(0x272, 32);
}

OSString* VoodooPS2MT2SimulatorDevice::newProductString() const {
    return OSString::withCString("Magic Trackpad 2");
}

OSString* VoodooPS2MT2SimulatorDevice::newSerialNumberString() const {
    return OSString::withCString("VoodooI2C Magic Trackpad 2 Simulator");
}

OSString* VoodooPS2MT2SimulatorDevice::newTransportString() const {
    return OSString::withCString("I2C");
}

OSNumber* VoodooPS2MT2SimulatorDevice::newVendorIDNumber() const {
    return OSNumber::withNumber(0x5ac, 32);
}

OSNumber* VoodooPS2MT2SimulatorDevice::newLocationIDNumber() const {
    return OSNumber::withNumber(0x14400000, 32);
}

OSNumber* VoodooPS2MT2SimulatorDevice::newVersionNumber() const {
    return OSNumber::withNumber(0x804, 32);
}
