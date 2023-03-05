/*
 * PS/2 Mouse stub for SMBus Devices
 */

#include "ApplePS2SMBusStub.h"

OSDefineMetaClassAndStructors(ApplePS2SMBusStub, IOService);

bool ApplePS2SMBusStub::start(IOService *provider) {
    _device = (ApplePS2MouseDevice*)provider;
    _device->installInterruptAction(this,
                                    OSMemberFunctionCast(PS2InterruptAction, this, &ApplePS2SMBusStub::interruptOccurred),
                                    OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2SMBusStub::packetReady));
    registerService();
    return true;
}

void ApplePS2SMBusStub::stop(IOService *provider) {
    _device->uninstallInterruptAction();
}

PS2InterruptResult ApplePS2SMBusStub::interruptOccurred(UInt8 data) {
    // TODO: Maybe detect [AA, 00] announcements and reset?
    
    IOLog("VoodooPS2Stub::interruptOccured - Unexpected Data: %x\n", data);
    return kPS2IR_packetReady;
}

void ApplePS2SMBusStub::packetReady(void) {
    
}
