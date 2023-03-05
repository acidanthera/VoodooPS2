/*
 * PS/2 Mouse stub for SMBus Devices
 */

#ifndef ApplePS2SMBusStub_hpp
#define ApplePS2SMBusStub_hpp

#include "ApplePS2MouseDevice.h"


class ApplePS2SMBusStub : public IOService {
    OSDeclareDefaultStructors(ApplePS2SMBusStub);

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    
    PS2InterruptResult interruptOccurred(UInt8 data);
    void packetReady(void);
private:
    ApplePS2MouseDevice *_device;
};

#endif /* ApplePS2SMBusStub_hpp */
