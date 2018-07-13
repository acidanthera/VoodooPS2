/*
 * Copyright (c) 2002 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.2 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include "VoodooPS2Controller.h"
#include "VoodooPS2SentelicFSP.h"

enum {
    kModeByteValueGesturesEnabled  = 0x00,
    kModeByteValueGesturesDisabled = 0x04
};

// =============================================================================
// ApplePS2SentelicFSP Class Implementation
//

OSDefineMetaClassAndStructors(ApplePS2SentelicFSP, IOHIPointing);

UInt32 ApplePS2SentelicFSP::deviceType()
{ return NX_EVS_DEVICE_TYPE_MOUSE; };

UInt32 ApplePS2SentelicFSP::interfaceID()
{ return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

IOItemCount ApplePS2SentelicFSP::buttonCount() { return 2; };
IOFixed     ApplePS2SentelicFSP::resolution()  { return _resolution; };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SentelicFSP::init(OSDictionary* dict)
{
    //
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.
    //
    
    if (!super::init(dict))
        return false;

    // initialize state
    _device                    = 0;
    _interruptHandlerInstalled = false;
    _packetByteCount           = 0;
    _resolution                = (100) << 16; // (100 dpi, 4 counts/mm)
    _touchPadModeByte          = kModeByteValueGesturesDisabled;
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define FSP_REG_DEVICE_ID       0x00
#define FSP_REG_VERSION         0x01
#define FSP_REG_REVISION        0x04
#define FSP_REG_OPC_QDOWN       0x31
#define FSP_REG_SYSCTL1         0x10
#define FSP_REG_ONPAD_CTL       0x43

#define FSP_DEVICE_MAGIC		0x01
#define FSP_BIT_EN_REG_CLK      0x20
#define FSP_BIT_EN_OPC_TAG      0x80

#define FSP_PKT_TYPE_NORMAL_OPC (0x03)
#define FSP_PKT_TYPE_SHIFT      (6)
#define FSPDRV_FLAG_EN_OPC      (0x800)

#define FSP_BIT_ONPAD_ENABLE    0x01
#define FSP_BIT_FIX_VSCR        0x08

int fsp_ps2_command(ApplePS2MouseDevice * device, PS2Request * request, int cmd)
{
    request->commands[0].command  = kPS2C_WriteCommandPort;
    request->commands[0].inOrOut  = kCP_TransmitToMouse;
    request->commands[1].command  = kPS2C_WriteDataPort;
    request->commands[1].inOrOut  = cmd;
    request->commands[2].command  = kPS2C_ReadDataPort;
    request->commands[2].inOrOut  = 0;

    request->commandsCount = 3;
    device->submitRequestAndBlock(request);

    //IOLog("ApplePS2Trackpad: Sentelic FSP: fsp_ps2_command(cmd = %0x) => %0x\n", cmd, request->commands[2].inOrOut);

    return (request->commandsCount == 3) ? request->commands[2].inOrOut : -1;
}

int fsp_reg_read(ApplePS2MouseDevice * device, PS2Request * request, int reg)
{
    int register_select = 0x66;
    int register_value = reg;

    // mangle reg to avoid collision with reserved values
    if (reg == 10 || reg == 20 || reg == 40 || reg == 60 || reg == 80 || reg == 100 || reg == 200) {
        register_value = (reg >> 4) | (reg << 4);
        register_select = 0xCC;
    } else if (reg == 0xe9 || reg == 0xee || reg == 0xf2 || reg == 0xff) {
        register_value = ~reg;
        register_select = 0x68;
    }

    fsp_ps2_command(device, request, 0xf3);
    fsp_ps2_command(device, request, 0x66);
    fsp_ps2_command(device, request, 0x88);
    fsp_ps2_command(device, request, 0xf3);
    fsp_ps2_command(device, request, register_select);
    fsp_ps2_command(device, request, register_value);

    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_GetMouseInformation;
    request->commands[1].command  = kPS2C_ReadDataPort;
    request->commands[1].inOrOut  = 0;
    request->commands[2].command  = kPS2C_ReadDataPort;
    request->commands[2].inOrOut  = 0;
    request->commands[3].command  = kPS2C_ReadDataPort;
    request->commands[3].inOrOut  = 0;

    request->commandsCount = 4;
    device->submitRequestAndBlock(request);

    //IOLog("ApplePS2Trackpad: Sentelic FSP: fsp_reg_read(reg = %0x) => %0x\n", reg, request->commands[3].inOrOut);

    return (request->commandsCount == 4) ? request->commands[3].inOrOut : -1;
}

void fsp_reg_write(ApplePS2MouseDevice * device, PS2Request * request, int reg, int val)
{
    int register_select = 0x55;
    int register_value = reg;

    // mangle reg to avoid collision with reserved values
    if (reg == 10 || reg == 20 || reg == 40 || reg == 60 || reg == 80 || reg == 100 || reg == 200) {
        register_value = (reg >> 4) | (reg << 4);
        register_select = 0x77;
    } else if (reg == 0xe9 || reg == 0xee || reg == 0xf2 || reg == 0xff) {
        register_value = ~reg;
        register_select = 0x74;
    }

    fsp_ps2_command(device, request, 0xf3);
    fsp_ps2_command(device, request, register_select);
    fsp_ps2_command(device, request, register_value);

    register_select = 0x33;
    register_value = val;

    // mangle val to avoid collision with reserved values
    if (val == 10 || val == 20 || val == 40 || val == 60 || val == 80 || val == 100 || val == 200) {
        register_value = (val >> 4) | (val << 4);
        register_select = 0x44;
    } else if (val == 0xe9 || val == 0xee || val == 0xf2 || val == 0xff) {
        register_value = ~val;
        register_select = 0x47;
    }

    fsp_ps2_command(device, request, 0xf3);
    fsp_ps2_command(device, request, register_select);
    fsp_ps2_command(device, request, register_value);

    //IOLog("ApplePS2Trackpad: Sentelic FSP: fsp_reg_write(reg = %0x, val = %0x)\n", reg, val);
}

void fsp_write_enable(ApplePS2MouseDevice * device, PS2Request * request, int enable)
{
    int wen = fsp_reg_read(device, request, FSP_REG_SYSCTL1);

    if (enable)
        wen |= FSP_BIT_EN_REG_CLK; 
    else
        wen &= ~FSP_BIT_EN_REG_CLK;

    fsp_reg_write(device, request, FSP_REG_SYSCTL1, wen);
}

void fsp_opctag_enable(ApplePS2MouseDevice * device, PS2Request * request, int enable)
{
    fsp_write_enable(device, request, true);

    int opc = fsp_reg_read(device, request, FSP_REG_OPC_QDOWN);

    if (enable)
        opc |= FSP_BIT_EN_OPC_TAG; 
    else
        opc &= ~FSP_BIT_EN_OPC_TAG;

    fsp_reg_write(device, request, FSP_REG_OPC_QDOWN, opc);

    fsp_write_enable(device, request, false);
}

int fsp_intellimouse_mode(ApplePS2MouseDevice * device, PS2Request * request)
{
    request->commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut = kDP_SetMouseSampleRate;
    request->commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut = 200;

    request->commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut = kDP_SetMouseSampleRate;
    request->commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut = 200;

    request->commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut = kDP_SetMouseSampleRate;
    request->commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[5].inOrOut = 80;

    request->commands[6].command = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[6].inOrOut = kDP_GetId;
    request->commands[7].command = kPS2C_ReadDataPort;
    request->commands[7].inOrOut = 0;

    request->commandsCount = 8;
    device->submitRequestAndBlock(request);

    //IOLog("ApplePS2Trackpad: Sentelic FSP: fsp_intellimouse_mode() => %0x\n", request->commands[7].inOrOut);

    return request->commands[7].inOrOut;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

ApplePS2SentelicFSP* ApplePS2SentelicFSP::probe( IOService * provider, SInt32 * score )
{
    DEBUG_LOG("ApplePS2SentelicFSP::probe entered...\n");
    
    //
    // The driver has been instructed to verify the presence of the actual
    // hardware we represent. We are guaranteed by the controller that the
    // mouse clock is enabled and the mouse itself is disabled (thus it
    // won't send any asynchronous mouse data that may mess up the
    // responses expected by the commands we send it).
    //
    
    ApplePS2MouseDevice* device  = (ApplePS2MouseDevice*)provider;
    
    if (!super::probe(provider, score))
        return 0;

    // find config specific to Platform Profile
    OSDictionary* list = OSDynamicCast(OSDictionary, getProperty(kPlatformProfile));
    OSDictionary* config = device->getController()->makeConfigurationNode(list, "Sentelic FSP");
    if (config)
    {
        // if DisableDevice is Yes, then do not load at all...
        OSBoolean* disable = OSDynamicCast(OSBoolean, config->getObject(kDisableDevice));
        if (disable && disable->isTrue())
        {
            config->release();
            return 0;
        }
#ifdef DEBUG
        // save configuration for later/diagnostics...
        setProperty(kMergedConfiguration, config);
#endif
    }
    OSSafeReleaseNULL(config);

    bool success = false;
    TPS2Request<> request;
    if (fsp_reg_read(device, &request, FSP_REG_DEVICE_ID) == FSP_DEVICE_MAGIC)
    {
        _touchPadVersion =
		(fsp_reg_read(device, &request, FSP_REG_VERSION) << 8) |
		(fsp_reg_read(device, &request, FSP_REG_REVISION));
		
        success = true;
    }
	
    DEBUG_LOG("ApplePS2SentelicFSP::probe leaving.\n");
    return (success) ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SentelicFSP::start( IOService * provider )
{ 
    UInt64 gesturesEnabled;
	
    //
    // The driver has been instructed to start. This is called after a
    // successful probe and match.
    //
	
    if (!super::start(provider))
        return false;
	
    //
    // Maintain a pointer to and retain the provider object.
    //
	
    _device = (ApplePS2MouseDevice *) provider;
    _device->retain();
    
    //
    // Enable the mouse clock and disable the mouse IRQ line.
    //

    //
    // Announce hardware properties.
    //
	
    IOLog("ApplePS2Trackpad: Sentelic FSP %d.%d.%d\n", 
		(_touchPadVersion >> 12) & 0x0F,
		(_touchPadVersion >> 8) & 0x0F,
		(_touchPadVersion) & 0x0F);
	
    //
    // Default to 3-byte packets, will try and enable 4-byte packets later
    //

    _packetSize = 3;

    //
    // Advertise the current state of the tapping feature.
    //
	
    gesturesEnabled = (_touchPadModeByte == kModeByteValueGesturesEnabled) ? 1 : 0;
    setProperty("Clicking", gesturesEnabled, sizeof(gesturesEnabled)*8);
	
    //
    // Must add this property to let our superclass know that it should handle
    // trackpad acceleration settings from user space.  Without this, tracking
    // speed adjustments from the mouse prefs panel have no effect.
    //
	
    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
	
    //
    // Lock the controller during initialization
    //
    
    _device->lock();
    
    //
    // Finally, we enable the trackpad itself, so that it may start reporting
    // asynchronous events.
    //
	
    setTouchPadEnable(true);
	
    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //
	
    _device->installInterruptAction(this,
                                    OSMemberFunctionCast(PS2InterruptAction, this, &ApplePS2SentelicFSP::interruptOccurred),
                                    OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2SentelicFSP::packetReady));
    _interruptHandlerInstalled = true;
	
    // now safe to allow other threads
    _device->unlock();
    
    //
    // Install our power control handler.
    //
	
    _device->installPowerControlAction( this, OSMemberFunctionCast(PS2PowerControlAction, this, &ApplePS2SentelicFSP::setDevicePowerState));
    _powerControlHandlerInstalled = true;
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SentelicFSP::stop( IOService * provider )
{
    //
    // The driver has been instructed to stop.  Note that we must break all
    // connections to other service objects now (ie. no registered actions,
    // no pointers and retains to objects, etc), if any.
    //
	
    assert(_device == provider);
	
    //
    // Disable the mouse itself, so that it may stop reporting mouse events.
    //
	
    setTouchPadEnable(false);
	
    //
    // Uninstall the interrupt handler.
    //
	
    if ( _interruptHandlerInstalled )  _device->uninstallInterruptAction();
    _interruptHandlerInstalled = false;
	
    //
    // Uninstall the power control handler.
    //
	
    if ( _powerControlHandlerInstalled ) _device->uninstallPowerControlAction();
    _powerControlHandlerInstalled = false;
	
    //
    // Release the pointer to the provider object.
    //
	
    OSSafeReleaseNULL(_device);
	
	super::stop(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PS2InterruptResult ApplePS2SentelicFSP::interruptOccurred( UInt8 data )
{
    //
    // This will be invoked automatically from our device when asynchronous
    // events need to be delivered. Process the trackpad data. Do NOT issue
    // any BLOCKING commands to our device in this context.
    //
    // Ignore all bytes until we see the start of a packet, otherwise the
    // packets may get out of sequence and things will get very confusing.
    //
    if (_packetByteCount == 0 && ((data == kSC_Acknowledge) || !(data & 0x08)))
    {
        DEBUG_LOG("%s: Unexpected byte0 data (%02x) from PS/2 controller\n", getName(), data);
        return kPS2IR_packetBuffering;
    }
	
    //
    // Add this byte to the packet buffer. If the packet is complete, that is,
    // we have the three bytes, dispatch this packet for processing.
    //
	
    UInt8* packet = _ringBuffer.head();
    packet[_packetByteCount++] = data;
    if (_packetByteCount == _packetSize)
    {
        _ringBuffer.advanceHead(kPacketLengthMax);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }
    return kPS2IR_packetBuffering;
}

void ApplePS2SentelicFSP::packetReady()
{
    // empty the ring buffer, dispatching each packet...
    while (_ringBuffer.count() >= kPacketLengthMax)
    {
        dispatchRelativePointerEventWithPacket(_ringBuffer.tail(), _packetSize);
        _ringBuffer.advanceTail(kPacketLengthMax);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SentelicFSP::dispatchRelativePointerEventWithPacket(UInt8* packet, UInt32 packetSize)
{
    //
    // Process the three byte relative format packet that was retreived from the
    // trackpad. The format of the bytes is as follows:
    //
    //  7  6  5  4  3  2  1  0
    // -----------------------
    // YO XO YS XS  1  M  R  L
    // X7 X6 X5 X4 X3 X3 X1 X0  (X delta)
    // Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (Y delta)
    // Z7 Z6 Z5 Z4 Z3 Z2 Z1 Z0  (Z delta) (iff 4 byte packets)
    //
	
    UInt32      buttons = 0;
    SInt32      dx, dy, dz;
    uint64_t    now_abs;
	
    if ((_touchPadModeByte == kModeByteValueGesturesEnabled) ||         // pad clicking enabled
        (packet[0] >> FSP_PKT_TYPE_SHIFT) != FSP_PKT_TYPE_NORMAL_OPC)   // real button
    {
		if ( (packet[0] & 0x1) ) buttons |= 0x1;  // left button   (bit 0 in packet)
		if ( (packet[0] & 0x2) ) buttons |= 0x2;  // right button  (bit 1 in packet)
		if ( (packet[0] & 0x4) ) buttons |= 0x4;  // middle button (bit 2 in packet)
    }
    
    dx = ((packet[0] & 0x10) ? 0xffffff00 : 0 ) | packet[1];
    dy = -(((packet[0] & 0x20) ? 0xffffff00 : 0 ) | packet[2]);
    
    clock_get_uptime(&now_abs);
    dispatchRelativePointerEventX(dx, dy, buttons, now_abs);

    if (packetSize == 4)
    {
        dz = (int)(packet[3] & 8) - (int)(packet[3] & 7);
        dispatchScrollWheelEventX(dz, 0, 0, now_abs);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SentelicFSP::setTouchPadEnable( bool enable )
{
    //
    // Instructs the trackpad to start or stop the reporting of data packets.
    // It is safe to issue this request from the interrupt/completion context.
    //
	
    // (mouse enable/disable command)
    TPS2Request<> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut =  enable ? kDP_Enable : kDP_SetDefaultsAndDisable;
    request.commandsCount = 1;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
	
    // enable one-pad-click tagging, so we can filter them out!
    fsp_opctag_enable(_device, &request, true);

    // turn on intellimouse mode (4 bytes per packet)
    if (fsp_intellimouse_mode(_device, &request) == 4)
        _packetSize = 4;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

UInt32 ApplePS2SentelicFSP::getTouchPadData( UInt8 dataSelector )
{
    TPS2Request<13> request;
    
    // Disable stream mode before the command sequence.
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
	
    // 4 set resolution commands, each encode 2 data bits.
    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_SetMouseResolution;
    request.commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut  = (dataSelector >> 6) & 0x3;
	
    request.commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut  = kDP_SetMouseResolution;
    request.commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut  = (dataSelector >> 4) & 0x3;
	
    request.commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut  = kDP_SetMouseResolution;
    request.commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut  = (dataSelector >> 2) & 0x3;
	
    request.commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[7].inOrOut  = kDP_SetMouseResolution;
    request.commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[8].inOrOut  = (dataSelector >> 0) & 0x3;
	
    // Read response bytes.
    request.commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[9].inOrOut  = kDP_GetMouseInformation;
    request.commands[10].command = kPS2C_ReadDataPort;
    request.commands[10].inOrOut = 0;
    request.commands[11].command = kPS2C_ReadDataPort;
    request.commands[11].inOrOut = 0;
    request.commands[12].command = kPS2C_ReadDataPort;
    request.commands[12].inOrOut = 0;
    request.commandsCount = 13;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
	
    UInt32 returnValue = (UInt32)(-1);
    if (request.commandsCount == 13) // success?
    {
        returnValue = ((UInt32)request.commands[10].inOrOut << 16) |
		((UInt32)request.commands[11].inOrOut <<  8) |
		((UInt32)request.commands[12].inOrOut);
    }
    return returnValue;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

IOReturn ApplePS2SentelicFSP::setParamProperties( OSDictionary * dict )
{
    OSNumber * clicking = OSDynamicCast( OSNumber, dict->getObject("Clicking") );
	
    if ( clicking )
    {    
        UInt8  newModeByteValue = clicking->unsigned32BitValue() & 0x1 ?
		kModeByteValueGesturesEnabled :
		kModeByteValueGesturesDisabled;
		
        if (_touchPadModeByte != newModeByteValue)
        {
            _touchPadModeByte = newModeByteValue;
			
            //
            // Advertise the current state of the tapping feature.
            //
			
            setProperty("Clicking", clicking);
        }
    }
	
    return super::setParamProperties(dict);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SentelicFSP::setDevicePowerState( UInt32 whatToDo )
{
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            
            //
            // Disable touchpad (synchronous).
            //
			
            setTouchPadEnable( false );
            break;
			
        case kPS2C_EnableDevice:
			
            //
            // Must not issue any commands before the device has
            // completed its power-on self-test and calibration.
            //
			
            IOSleep(1000);
			
            //
            // Clear packet buffer pointer to avoid issues caused by
            // stale packet fragments.
            //
			
            _packetByteCount = 0;
            _ringBuffer.reset();
			
            //
            // Finally, we enable the trackpad itself, so that it may
            // start reporting asynchronous events.
            //
			
            setTouchPadEnable( true );
            break;
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool ApplePS2SentelicFSP::setTouchPadModeByte(UInt8 modeByteValue, bool enableStreamMode)
{
    TPS2Request<12> request;
    
    // Disable stream mode before the command sequence.
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;
	
    // 4 set resolution commands, each encode 2 data bits.
    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_SetMouseResolution;
    request.commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut  = (modeByteValue >> 6) & 0x3;
	
    request.commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut  = kDP_SetMouseResolution;
    request.commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut  = (modeByteValue >> 4) & 0x3;
	
    request.commands[5].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut  = kDP_SetMouseResolution;
    request.commands[6].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut  = (modeByteValue >> 2) & 0x3;
	
    request.commands[7].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[7].inOrOut  = kDP_SetMouseResolution;
    request.commands[8].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[8].inOrOut  = (modeByteValue >> 0) & 0x3;
	
    // Set sample rate 20 to set mode byte 2. Older pads have 4 mode
    // bytes (0,1,2,3), but only mode byte 2 remain in modern pads.
    request.commands[9].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[9].inOrOut  = kDP_SetMouseSampleRate;
    request.commands[10].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[10].inOrOut = 20;
	
    request.commands[11].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[11].inOrOut  = enableStreamMode ? kDP_Enable : kDP_SetMouseScaling1To1;
    request.commandsCount = 12;
    assert(request.commandsCount <= countof(request.commands));
    
    _device->submitRequestAndBlock(&request);
    return 12 == request.commandsCount;
}
