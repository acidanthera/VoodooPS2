//
//  VoodooPS2Elan.cpp
//  VoodooPS2Trackpad
//
//  Created by Kishor Prins, Bartosz Korczyński on 24/06/2020.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#include "VoodooPS2Elan.h"

#include <IOKit/assert.h>
#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>

#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"
#include "VoodooPS2Controller.h"

OSDefineMetaClassAndStructors(ApplePS2Elan, IOHIPointing);

bool ApplePS2Elan::init(OSDictionary * properties) {
    printf("VoodooPS2Elan: init.\n");
    if (!super::init(properties)) {
        printf("VoodooPS2Elan: init failed.\n");
        return false;
    }
    
     _device = NULL;
    voodooInputInstance = NULL;

    return true;
}

ApplePS2Elan* ApplePS2Elan::probe(IOService* provider, SInt32* score) {
    printf("VoodooPS2Elan: before probe.\n");
    if (!super::probe(provider, score)) {
        return NULL;
    }


    printf("VoodooPS2Elan: probing.\n");
    
    _device = OSDynamicCast(ApplePS2MouseDevice, provider);
    if (!_device) {
        printf("VoodooPS2Elan: no ps2 device.\n");

        return NULL;
    }
    
    resetMouse();

    printf("VoodooPS2Elan: send magic knock to the device.\n");
    // send magic knock to the device
    if (elantechDetect()) {
        printf("VoodooPS2Elan: elantouchpad not detected\n");
        return NULL;
    }
    
    resetMouse();
    
    if (elantech_query_info())
    {
        printf("VoodooPS2Elan: query info failed\n");
    }
    
    IOLog("VoodooPS2Elan: capabilities: %x %x %x\n", info.capabilities[0], info.capabilities[1], info.capabilities[2]);
    IOLog("VoodooPS2Elan: samples: %x %x %x\n", info.capabilities[0], info.capabilities[1], info.capabilities[2]);
    IOLog("VoodooPS2Elan: hw_version: %x\n", info.hw_version);
    IOLog("VoodooPS2Elan: fw_version: %x\n", info.fw_version);
    IOLog("VoodooPS2Elan: x_min: %d\n", info.x_min);
    IOLog("VoodooPS2Elan: y_min: %d\n", info.y_min);
    IOLog("VoodooPS2Elan: x_max: %d\n", info.x_max);
    IOLog("VoodooPS2Elan: y_max: %d\n", info.y_max);
    IOLog("VoodooPS2Elan: x_res: %d\n", info.x_res);
    IOLog("VoodooPS2Elan: y_res: %d\n", info.y_res);
    IOLog("VoodooPS2Elan: x_traces: %d\n", info.x_traces);
    IOLog("VoodooPS2Elan: y_traces: %d\n", info.y_traces);
    IOLog("VoodooPS2Elan: width: %d\n", info.width);
    IOLog("VoodooPS2Elan: bus: %d\n", info.bus);
    
    IOLog("VoodooPS2Elan: paritycheck: %d\n", info.paritycheck);
    IOLog("VoodooPS2Elan: jumpy_cursor: %d\n", info.jumpy_cursor);
    IOLog("VoodooPS2Elan: reports_pressure: %d\n", info.reports_pressure);
    IOLog("VoodooPS2Elan: crc_enabled: %d\n", info.crc_enabled);
    IOLog("VoodooPS2Elan: set_hw_resolution: %d\n", info.set_hw_resolution);
    IOLog("VoodooPS2Elan: has_trackpoint: %d\n", info.has_trackpoint);
    IOLog("VoodooPS2Elan: has_middle_button: %d\n", info.has_middle_button);
    
    printf("VoodooPS2Elan: elan touchpad detected. Probing finished.\n");
    
    OSDictionary* list = OSDynamicCast(OSDictionary, getProperty(kPlatformProfile));
    OSDictionary* config = _device->getController()->makeConfigurationNode(list, "Synaptics TouchPad");
    if (config)
        injectVersionDependentProperties(config);
    
    
    char buf[128];
    snprintf(buf, sizeof(buf), "elan fw. %x, hw: %d", info.fw_version, info.hw_version);
      setProperty("RM,TrackpadInfo", buf);

      //
      // Advertise the current state of the tapping feature.
      //
      // Must add this property to let our superclass know that it should handle
      // trackpad acceleration settings from user space.  Without this, tracking
      // speed adjustments from the mouse prefs panel have no effect.
      //

    int _scrollresolution = 150;
      setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
      setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDTrackpadScrollAccelerationKey);
      setProperty(kIOHIDScrollResolutionKey, _scrollresolution << 16, 32);
      // added for Sierra precise scrolling (credit usr-sse2)
      setProperty("HIDScrollResolutionX", _scrollresolution << 16, 32);
      setProperty("HIDScrollResolutionY", _scrollresolution << 16, 32);
    return this;
}

bool ApplePS2Elan::start(IOService* provider) {
    printf("VoodooPS2Elan: starting.\n");
    if(!super::start(provider)) {
        return false;
    }
    
    printf("VoodooPS2Elan: elantech_setup_ps2.\n");
    elantech_setup_ps2();
    
    TPS2Request<> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetDefaultsAndDisable;           // 0xF5, Disable data reporting
    request.commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut = kDP_SetMouseSampleRate;              // 0xF3
    request.commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut = 0x64;                                // 100 dpi
    request.commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut = kDP_SetMouseResolution;              // 0xE8
    request.commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut = 0x03;                                // 0x03 = 8 counts/mm
    request.commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut = kDP_SetMouseScaling1To1;             // 0xE6
    request.commands[6].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut = kDP_Enable;                          // 0xF4, Enable Data Reporting
    request.commandsCount = 7;
    _device->submitRequestAndBlock(&request);
    
    _device->installInterruptAction(this, OSMemberFunctionCast(PS2InterruptAction ,this, &ApplePS2Elan::interruptOccurred), OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2Elan::packetReady));
    
    //setCommandByte( kCB_EnableMouseIRQ, kCB_DisableMouseClock );
    
    Elantech_Touchpad_enable(true);
    
    VoodooInputDimensions d;
    d.min_x = info.x_min;
    d.max_x = info.x_max;
    d.min_y = info.y_min;
    d.max_y = info.y_max;
    super::messageClient(kIOMessageVoodooInputUpdateDimensionsMessage, voodooInputInstance, &d, sizeof(VoodooInputDimensions));
    
      
    
    printf("VoodooPS2Elan: started.\n");
    return true;
}

void ApplePS2Elan::stop(IOService* provider) {
    printf("VoodooPS2Elan: stopping.\n");

    _device->uninstallInterruptAction();
    
    super::stop(provider);
    printf("VoodooPS2Elan: stopped.\n");
};


bool ApplePS2Elan::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    printf("VoodooPS2Elan: handleOpen.\n");
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();

        printf("VoodooPS2Elan: handleOpened.\n");
        return true;
    }

    return super::handleOpen(forClient, options, arg);
}

void ApplePS2Elan::handleClose(IOService *forClient, IOOptionBits options) {
    printf("VoodooPS2Elan: handleClose.\n");
    OSSafeReleaseNULL(voodooInputInstance);
    super::handleClose(forClient, options);
    printf("VoodooPS2Elan: handleCloseed.\n");
}

/// elantech.c port

#define PSMOUSE_CMD_SETSCALE11    0x00e6
#define PSMOUSE_CMD_SETSCALE21    0x00e7
#define PSMOUSE_CMD_SETRES    0x10e8
#define PSMOUSE_CMD_GETINFO    0x03e9
#define PSMOUSE_CMD_SETSTREAM    0x00ea
#define PSMOUSE_CMD_SETPOLL    0x00f0
#define PSMOUSE_CMD_POLL    0x00eb    /* caller sets number of bytes to receive */
#define PSMOUSE_CMD_RESET_WRAP    0x00ec
#define PSMOUSE_CMD_GETID    0x02f2
#define PSMOUSE_CMD_SETRATE    0x10f3
#define PSMOUSE_CMD_ENABLE    0x00f4
#define PSMOUSE_CMD_DISABLE    0x00f5
#define PSMOUSE_CMD_RESET_DIS    0x00f6
#define PSMOUSE_CMD_RESET_BAT    0x02ff

template<int I>
int ApplePS2Elan::ps2_command(UInt8* params, unsigned int command)
{
    int rc = 0;

    TPS2Request<1 + I> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = command;
    for (int i = 0; i < I; ++i)
        request.commands[1 + i].command = kPS2C_ReadDataPort;
    request.commandsCount = 1 + I;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    for (int i = 0; i < I; ++i)
        params[i] = request.commands[i + 1].inOrOut;

    return request.commandsCount != 1 + I;
}

/*
 * A retrying version of ps2_command
 */
template<int I>
int ApplePS2Elan::elantech_ps2_command(unsigned char *param, int command)
{
    int rc;
    int tries = ETP_PS2_COMMAND_TRIES;

    do {
        rc = ps2_command<I>(param, command);
        if (rc == 0)
            break;
        tries--;
        printf("VoodooPS2Elan: retrying ps2 command 0x%02x (%d).\n",
                command, tries);
        IOSleep(ETP_PS2_COMMAND_DELAY);
    } while (tries > 0);

    if (rc)
        printf("VoodooPS2Elan: ps2 command 0x%02x failed.\n", command);

    return rc;
}

/*
 * ps2_sliced_command() sends an extended PS/2 command to the mouse
 * using sliced syntax, understood by advanced devices, such as Logitech
 * or Synaptics touchpads. The command is encoded as:
 * 0xE6 0xE8 rr 0xE8 ss 0xE8 tt 0xE8 uu where (rr*64)+(ss*16)+(tt*4)+uu
 * is the command.
 */

int ApplePS2Elan::ps2_sliced_command(UInt8 command)
{
    int j = 0;
    int retval;
    
    TPS2Request<> request;
    request.commands[j].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[j++].inOrOut = PSMOUSE_CMD_SETSCALE11;


    for (int i = 6; i >= 0; i -= 2) {
        UInt8 d = (command >> i) & 3;
        request.commands[j].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[j++].inOrOut = PSMOUSE_CMD_SETRES;
        
        request.commands[j].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[j++].inOrOut = d;
    }
    
    request.commandsCount = j;
    _device->submitRequestAndBlock(&request);
    
    return request.commandsCount != j;
}

/*
 * Send a Synaptics style sliced query command
 */
template<int I>
int ApplePS2Elan::synaptics_send_cmd(unsigned char c,
                unsigned char *param)
{
    if (ps2_sliced_command(c) ||
        ps2_command<I>(param, PSMOUSE_CMD_GETINFO)) {
            printf("VoodooPS2Elan: query 0x%02x failed.\n", c);
        return -1;
    }

    return 0;
}


/*
 * V3 and later support this fast command
 */
 template<int I>
int ApplePS2Elan::elantech_send_cmd(unsigned char c, unsigned char *param)
{
    if (ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
        ps2_command<0>(NULL, c) ||
        ps2_command<I>(param, PSMOUSE_CMD_GETINFO)) {
        printf("VoodooPS2Elan: query 0x%02x failed.\n", c);
        return -1;
    }

    return 0;
}

bool ApplePS2Elan::elantech_is_signature_valid(const unsigned char *param)
{
    static const unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10 };
    int i;

    if (param[0] == 0)
        return false;

    if (param[1] == 0)
        return true;

    /*
     * Some hw_version >= 4 models have a revision higher then 20. Meaning
     * that param[2] may be 10 or 20, skip the rates check for these.
     */
    if ((param[0] & 0x0f) >= 0x06 && (param[1] & 0xaf) == 0x0f &&
        param[2] < 40)
        return true;

    for (i = 0; i < sizeof(rates)/sizeof(*rates); i++)
        if (param[2] == rates[i])
            return false;

    return true;
}

/*
 * (value from firmware) * 10 + 790 = dpi
 * we also have to convert dpi to dots/mm (*10/254 to avoid floating point)
 */
static unsigned int elantech_convert_res(unsigned int val)
{
    return (val * 10 + 790) * 10 / 254;
}


int ApplePS2Elan::elantech_get_resolution_v4(unsigned int *x_res,
                      unsigned int *y_res,
                      unsigned int *bus)
{
    unsigned char param[3];

    if (elantech_send_cmd<3>(ETP_RESOLUTION_QUERY, param))
        return -1;

    *x_res = elantech_convert_res(param[1] & 0x0f);
    *y_res = elantech_convert_res((param[1] & 0xf0) >> 4);
    *bus = param[2];

    return 0;
}

template<int I>
int ApplePS2Elan::send_cmd(unsigned char c, unsigned char *param)
{
    if (info.hw_version >= 3)
        return elantech_send_cmd<I>(c, param);
    else
        return synaptics_send_cmd<I>(c, param);
}

/*
 * Use magic knock to detect Elantech touchpad
 */
int ApplePS2Elan::elantechDetect()
{
    unsigned char param[3];

    ps2_command<0>(NULL, PSMOUSE_CMD_RESET_DIS);
    
    if (ps2_command<0>( NULL, PSMOUSE_CMD_DISABLE) ||
        ps2_command<0>( NULL, PSMOUSE_CMD_SETSCALE11) ||
        ps2_command<0>( NULL, PSMOUSE_CMD_SETSCALE11) ||
        ps2_command<0>( NULL, PSMOUSE_CMD_SETSCALE11) ||
        ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
        printf("VoodooPS2Elan: sending Elantech magic knock failed.\n");
        return -1;
    }

    /*
     * Report this in case there are Elantech models that use a different
     * set of magic numbers
     */
    if (param[0] != 0x3c || param[1] != 0x03 ||
        (param[2] != 0xc8 && param[2] != 0x00)) {
        printf("VoodooPS2Elan: unexpected magic knock result 0x%02x, 0x%02x, 0x%02x.\n",
                param[0], param[1], param[2]);
        return -1;
    }

    /*
     * Query touchpad's firmware version and see if it reports known
     * value to avoid mis-detection. Logitech mice are known to respond
     * to Elantech magic knock and there might be more.
     */
    if (synaptics_send_cmd<3>(ETP_FW_VERSION_QUERY, param)) {
        printf("VoodooPS2Elan: failed to query firmware version.\n");
        return -1;
    }

    printf("VoodooPS2Elan: Elantech version query result 0x%02x, 0x%02x, 0x%02x.\n", param[0], param[1], param[2]);

    if (!elantech_is_signature_valid(param)) {
        printf("VoodooPS2Elan: Probably not a real Elantech touchpad. Aborting.\n");
        return -1;
    }

    return 0;
}

/*
 * determine hardware version and set some properties according to it.
 */
int ApplePS2Elan::elantech_set_properties()
{
    /* This represents the version of IC body. */
    int ver = (info.fw_version & 0x0f0000) >> 16;

    /* Early version of Elan touchpads doesn't obey the rule. */
    if (info.fw_version < 0x020030 || info.fw_version == 0x020600)
        info.hw_version = 1;
    else {
        switch (ver) {
        case 2:
        case 4:
                info.hw_version = 2;
            break;
        case 5:
                info.hw_version = 3;
            break;
        case 6 ... 15:
                info.hw_version = 4;
            break;
        default:
            return -1;
        }
    }

    /* decide which send_cmd we're gonna use early */
    // info.send_cmd = info.hw_version >= 3 ? elantech_send_cmd :
    //                     synaptics_send_cmd;
    // just use send_cmd
    
    /* Turn on packet checking by default */
    info.paritycheck = 1;

    /*
     * This firmware suffers from misreporting coordinates when
     * a touch action starts causing the mouse cursor or scrolled page
     * to jump. Enable a workaround.
     */
    info.jumpy_cursor =
    (info.fw_version == 0x020022 || info.fw_version == 0x020600);

    if (info.hw_version > 1) {
        /* For now show extra debug information */
        info.debug = 1;

        if (info.fw_version >= 0x020800)
            info.reports_pressure = true;
    }

    /*
     * The signatures of v3 and v4 packets change depending on the
     * value of this hardware flag.
     */
    info.crc_enabled = (info.fw_version & 0x4000) == 0x4000;

    /* Enable real hardware resolution on hw_version 3 ? */
    //info.set_hw_resolution = !dmi_check_system(no_hw_res_dmi_table);

    return 0;
}

int ApplePS2Elan::elantech_query_info()
{
    unsigned char param[3];
    unsigned char traces;

    /*
     * Do the version query again so we can store the result
     */
    if (synaptics_send_cmd<3>(ETP_FW_VERSION_QUERY, param)) {
        printf("VoodooPS2Elan: failed to query firmware version.\n");
        return -1;
    }
    
    info.fw_version = (param[0] << 16) | (param[1] << 8) | param[2];

    if (elantech_set_properties()) {
        printf("VoodooPS2Elan: unknown hardware version, aborting...\n");
        return -1;
    }
    printf("VoodooPS2Elan assuming hardware version %d (with firmware version 0x%02x%02x%02x)\n",
           info.hw_version, param[0], param[1], param[2]);

    if (send_cmd<3>(ETP_CAPABILITIES_QUERY,
                 info.capabilities)) {
        printf("VoodooPS2Elan: failed to query capabilities.\n");
        return -1;
    }
    printf("VoodooPS2Elan: Synaptics capabilities query result 0x%02x, 0x%02x, 0x%02x.\n",
           info.capabilities[0], info.capabilities[1],
           info.capabilities[2]);

    if (info.hw_version != 1) {
        if (send_cmd<3>(ETP_SAMPLE_QUERY, info.samples)) {
            printf("VoodooPS2Elan: failed to query sample data\n");
            return -1;
        }
        printf("VoodooPS2Elan: Elan sample query result %02x, %02x, %02x\n",
                      info.samples[0],
                      info.samples[1],
                      info.samples[2]);
    }

    if (info.samples[1] == 0x74 && info.hw_version == 0x03) {
        /*
         * This module has a bug which makes absolute mode
         * unusable, so let's abort so we'll be using standard
         * PS/2 protocol.
         */
        printf("VoodooPS2Elan: absolute mode broken, forcing standard PS/2 protocol\n");
        return -1;
    }

    /* The MSB indicates the presence of the trackpoint */
    info.has_trackpoint = (info.capabilities[0] & 0x80) == 0x80;

    info.x_res = 31;
    info.y_res = 31;
    if (info.hw_version == 4) {
        if (elantech_get_resolution_v4(&info.x_res,
                                       &info.y_res,
                                       &info.bus)) {
            printf("VoodooPS2Elan: failed to query resolution data.\n");
        }
    }

    /* query range information */
    switch (info.hw_version) {
    case 1:
            info.x_min = ETP_XMIN_V1;
            info.y_min = ETP_YMIN_V1;
            info.x_max = ETP_XMAX_V1;
            info.y_max = ETP_YMAX_V1;
        break;

    case 2:
            if (info.fw_version == 0x020800 ||
                info.fw_version == 0x020b00 ||
                info.fw_version == 0x020030) {
                info.x_min = ETP_XMIN_V2;
                info.y_min = ETP_YMIN_V2;
                info.x_max = ETP_XMAX_V2;
                info.y_max = ETP_YMAX_V2;
        } else {
            int i;
            int fixed_dpi;

            i = (info.fw_version > 0x020800 &&
                 info.fw_version < 0x020900) ? 1 : 2;

            if (send_cmd<3>(ETP_FW_ID_QUERY, param))
                return -1;

            fixed_dpi = param[1] & 0x10;

            if (((info.fw_version >> 16) == 0x14) && fixed_dpi) {
                if (send_cmd<3>(ETP_SAMPLE_QUERY, param))
                    return -1;

                info.x_max = (info.capabilities[1] - i) * param[1] / 2;
                info.y_max = (info.capabilities[2] - i) * param[2] / 2;
            } else if (info.fw_version == 0x040216) {
                info.x_max = 819;
                info.y_max = 405;
            } else if (info.fw_version == 0x040219 || info.fw_version == 0x040215) {
                info.x_max = 900;
                info.y_max = 500;
            } else {
                info.x_max = (info.capabilities[1] - i) * 64;
                info.y_max = (info.capabilities[2] - i) * 64;
            }
        }
        break;

    case 3:
            if (send_cmd<3>(ETP_FW_ID_QUERY, param))
            return -1;

            info.x_max = (0x0f & param[0]) << 8 | param[1];
            info.y_max = (0xf0 & param[0]) << 4 | param[2];
        break;

    case 4:
        if (send_cmd<3>(ETP_FW_ID_QUERY, param))
            return -1;

            info.x_max = (0x0f & param[0]) << 8 | param[1];
            info.y_max = (0xf0 & param[0]) << 4 | param[2];
            traces = info.capabilities[1];
            if ((traces < 2) || (traces > info.x_max))
            return -1;

            info.width = info.x_max / (traces - 1);

        /* column number of traces */
            info.x_traces = traces;

        /* row number of traces */
            traces = info.capabilities[2];
            if ((traces >= 2) && (traces <= info.y_max))
            info.y_traces = traces;

        break;
    }
    
    etd.info = info;

    /* check for the middle button: DMI matching or new v4 firmwares */
    //info.has_middle_button = dmi_check_system(elantech_dmi_has_middle_button) ||
    //              (ETP_NEW_IC_SMBUS_HOST_NOTIFY(info.fw_version) &&
    //               !elantech_is_buttonpad(info));

    return 0;
}

void ApplePS2Elan::resetMouse()
{
    UInt8 params[2];
    ps2_command<2>(params, PSMOUSE_CMD_RESET_BAT);
    
    if (params[0] != 0xaa && params[1] != 0x00)
    {
        printf("VoodooPS2Elan: failed resetting.\n");
    }
}

/*
 * Send an Elantech style special command to read a value from a register
 */
int ApplePS2Elan::elantech_read_reg(unsigned char reg,
                unsigned char *val)
{
    unsigned char param[3];
    int rc = 0;

    if (reg < 0x07 || reg > 0x26)
        return -1;

    if (reg > 0x11 && reg < 0x20)
        return -1;

    switch (etd.info.hw_version) {
    case 1:
        if (ps2_sliced_command(ETP_REGISTER_READ) ||
            ps2_sliced_command(reg) ||
            ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
            rc = -1;
        }
        break;

    case 2:
        if (elantech_ps2_command<0>( NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>( NULL, ETP_REGISTER_READ) ||
            elantech_ps2_command<0>( NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>( NULL, reg) ||
            elantech_ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
            rc = -1;
        }
        break;

    case 3 ... 4:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<3>(param, PSMOUSE_CMD_GETINFO)) {
            rc = -1;
        }
        break;
    }

    if (rc)
        printf("VoodooPS2Elan: failed to read register 0x%02x.\n", reg);
    else if (etd.info.hw_version != 4)
        *val = param[0];
    else
        *val = param[1];

    return rc;
}

/*
 * Send an Elantech style special command to write a register with a value
 */
int ApplePS2Elan::elantech_write_reg(unsigned char reg, unsigned char val)
{
    int rc = 0;

    if (reg < 0x07 || reg > 0x26)
        return -1;

    if (reg > 0x11 && reg < 0x20)
        return -1;

    switch (etd.info.hw_version) {
    case 1:
        if (ps2_sliced_command(ETP_REGISTER_WRITE) ||
            ps2_sliced_command(reg) ||
            ps2_sliced_command(val) ||
            ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;

    case 2:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_WRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, val) ||
            elantech_ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;

    case 3:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, val) ||
            elantech_ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;

    case 4:
        if (elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, reg) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, ETP_REGISTER_READWRITE) ||
            elantech_ps2_command<0>(NULL, ETP_PS2_CUSTOM_COMMAND) ||
            elantech_ps2_command<0>(NULL, val) ||
            elantech_ps2_command<0>(NULL, PSMOUSE_CMD_SETSCALE11)) {
            rc = -1;
        }
        break;
    }

    if (rc)
        printf("VoodooPS2Elan: failed to write register 0x%02x with value 0x%02x.\n",
                reg, val);

    return rc;
}

/*
 * Put the touchpad into absolute mode
 */
int ApplePS2Elan::elantech_set_absolute_mode()
{
    unsigned char val;
    int tries = ETP_READ_BACK_TRIES;
    int rc = 0;

    switch (etd.info.hw_version) {
    case 1:
            etd.reg_10 = 0x16;
            etd.reg_11 = 0x8f;
            if (elantech_write_reg(0x10, etd.reg_10) ||
                elantech_write_reg(0x11, etd.reg_11)) {
            rc = -1;
        }
        break;

    case 2:
                    /* Windows driver values */
        etd.reg_10 = 0x54;
        etd.reg_11 = 0x88;    /* 0x8a */
        etd.reg_21 = 0x60;    /* 0x00 */
        if (elantech_write_reg(0x10, etd.reg_10) ||
            elantech_write_reg(0x11, etd.reg_11) ||
            elantech_write_reg(0x21, etd.reg_21)) {
            rc = -1;
        }
        break;

    case 3:
        if (etd.info.set_hw_resolution)
            etd.reg_10 = 0x0b;
        else
            etd.reg_10 = 0x01;

        if (elantech_write_reg(0x10, etd.reg_10))
            rc = -1;

        break;

    case 4:
        etd.reg_07 = 0x01;
        if (elantech_write_reg(0x07, etd.reg_07))
            rc = -1;

        goto skip_readback_reg_10; /* v4 has no reg 0x10 to read */
    }

    if (rc == 0) {
        /*
         * Read back reg 0x10. For hardware version 1 we must make
         * sure the absolute mode bit is set. For hardware version 2
         * the touchpad is probably initializing and not ready until
         * we read back the value we just wrote.
         */
        do {
            rc = elantech_read_reg(0x10, &val);
            if (rc == 0)
                break;
            tries--;
            printf("VoodooPS2Elan: retrying read (%d).\n", tries);
            IOSleep(ETP_READ_BACK_DELAY);
        } while (tries > 0);

        if (rc) {
            printf("VoodooPS2Elan: failed to read back register 0x10.\n");
        } else if (etd.info.hw_version == 1 &&
               !(val & ETP_R10_ABSOLUTE_MODE)) {
            printf("VoodooPS2Elan: touchpad refuses to switch to absolute mode.\n");
            rc = -1;
        }
    }

 skip_readback_reg_10:
    if (rc)
        printf("VoodooPS2Elan: failed to initialise registers.\n");

    return rc;
}

/*
 * Set the appropriate event bits for the input subsystem
 */
int ApplePS2Elan::elantech_set_input_params()
{
    unsigned int x_min = info.x_min, y_min = info.y_min,
             x_max = info.x_max, y_max = info.y_max,
             width = info.width;
    
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, info.x_max - info.x_min, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, info.y_max - info.y_min, 32);

    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, (info.x_max + 1) / info.x_res, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, (info.y_max + 1) / info.y_res, 32);

    setProperty("IOFBTransform", 0ull, 32);
    setProperty("VoodooInputSupported", kOSBooleanTrue);
    registerService();
    
/*
    __set_bit(INPUT_PROP_POINTER, dev->propbit);
    __set_bit(EV_KEY, dev->evbit);
    __set_bit(EV_ABS, dev->evbit);
    __clear_bit(EV_REL, dev->evbit);

    __set_bit(BTN_LEFT, dev->keybit);
    if (info.has_middle_button)
        __set_bit(BTN_MIDDLE, dev->keybit);
    __set_bit(BTN_RIGHT, dev->keybit);

    __set_bit(BTN_TOUCH, dev->keybit);
    __set_bit(BTN_TOOL_FINGER, dev->keybit);
    __set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
    __set_bit(BTN_TOOL_TRIPLETAP, dev->keybit);

    switch (info.hw_version) {
    case 1:
        // Rocker button
        if (info.fw_version < 0x020000 &&
            (info.capabilities[0] & ETP_CAP_HAS_ROCKER)) {
            __set_bit(BTN_FORWARD, dev->keybit);
            __set_bit(BTN_BACK, dev->keybit);
        }
        input_set_abs_params(dev, ABS_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_Y, y_min, y_max, 0, 0);
        break;

    case 2:
        __set_bit(BTN_TOOL_QUADTAP, dev->keybit);
        __set_bit(INPUT_PROP_SEMI_MT, dev->propbit);
        // fall through
    case 3:
        if (info.hw_version == 3)
            elantech_set_buttonpad_prop(psmouse);
        input_set_abs_params(dev, ABS_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_Y, y_min, y_max, 0, 0);
        if (info.reports_pressure) {
            input_set_abs_params(dev, ABS_PRESSURE, ETP_PMIN_V2,
                         ETP_PMAX_V2, 0, 0);
            input_set_abs_params(dev, ABS_TOOL_WIDTH, ETP_WMIN_V2,
                         ETP_WMAX_V2, 0, 0);
        }
        input_mt_init_slots(dev, 2, INPUT_MT_SEMI_MT);
        input_set_abs_params(dev, ABS_MT_POSITION_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_MT_POSITION_Y, y_min, y_max, 0, 0);
        break;

    case 4:
        elantech_set_buttonpad_prop(psmouse);
        __set_bit(BTN_TOOL_QUADTAP, dev->keybit);
        // For X to recognize me as touchpad.
        input_set_abs_params(dev, ABS_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_Y, y_min, y_max, 0, 0);
        //
        // range of pressure and width is the same as v2,
        // report ABS_PRESSURE, ABS_TOOL_WIDTH for compatibility.
        //
        input_set_abs_params(dev, ABS_PRESSURE, ETP_PMIN_V2,
                     ETP_PMAX_V2, 0, 0);
        input_set_abs_params(dev, ABS_TOOL_WIDTH, ETP_WMIN_V2,
                     ETP_WMAX_V2, 0, 0);
        // Multitouch capable pad, up to 5 fingers.
        input_mt_init_slots(dev, ETP_MAX_FINGERS, 0);
        input_set_abs_params(dev, ABS_MT_POSITION_X, x_min, x_max, 0, 0);
        input_set_abs_params(dev, ABS_MT_POSITION_Y, y_min, y_max, 0, 0);
        input_set_abs_params(dev, ABS_MT_PRESSURE, ETP_PMIN_V2,
                     ETP_PMAX_V2, 0, 0);
        //
         //The firmware reports how many trace lines the finger spans,
         //convert to surface unit as Protocol-B requires.
         //
        input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR, 0,
                     ETP_WMAX_V2 * width, 0, 0);
        break;
    }

    input_abs_set_res(dev, ABS_X, info.x_res);
    input_abs_set_res(dev, ABS_Y, info.y_res);
    if (info.hw_version > 1) {
        input_abs_set_res(dev, ABS_MT_POSITION_X, info.x_res);
        input_abs_set_res(dev, ABS_MT_POSITION_Y, info.y_res);
    }
*/
    etd.y_max = y_max;
    etd.width = width;

    return 0;
}

/*
 * Initialize the touchpad and create sysfs entries
 */
int ApplePS2Elan::elantech_setup_ps2()
{
    int i;
    int error = -1;

    etd.parity[0] = 1;
    for (i = 1; i < 256; i++)
        etd.parity[i] = etd.parity[i & (i - 1)] ^ 1;

    if (elantech_set_absolute_mode()) {
        printf("VoodooPS2: failed to put touchpad into absolute mode.\n");
        goto init_fail;
    }

    if (info.fw_version == 0x381f17) {
        //etd.original_set_rate = psmouse->set_rate;
        //psmouse->set_rate = elantech_set_rate_restore_reg_07;
    }

    if (elantech_set_input_params()) {
        printf("VoodooPS2: failed to query touchpad range.\n");
        goto init_fail;
    }

    return 0;
    
 init_fail_tp_reg:
 init_fail_tp_alloc:
 init_fail:
    return error;
}

PS2InterruptResult ApplePS2Elan::interruptOccurred(UInt8 data) {
    UInt8* packet = ringBuffer.head();
    packet[packetByteCount++] = data;


    IOLog("VoodooPS2Elan: Got packet %x\n", data);
    
    if (packetByteCount == kPacketLengthMax)
    {
        IOLog("VoodooPS2Elan: Got full packet\n");
        ringBuffer.advanceHead(kPacketLengthMax);
        packetByteCount = 0;
        return kPS2IR_packetReady;
    }

    return kPS2IR_packetBuffering;
}

int ApplePS2Elan::elantech_packet_check_v4()
{
    unsigned char *packet = ringBuffer.tail();
    unsigned char packet_type = packet[3] & 0x03;
    unsigned int ic_version;
    bool sanity_check;
    
    IOLog("VoodooPS2Elan: Packet dump (%04x, %04x, %04x, %04x, %04x, %04x)\n", packet[0], packet[1], packet[2], packet[3], packet[4], packet[5] );

    if (etd.tp_dev && (packet[3] & 0x0f) == 0x06)
        return PACKET_TRACKPOINT;

    /* This represents the version of IC body. */
    ic_version = (etd.info.fw_version & 0x0f0000) >> 16;
    
    IOLog("VoodooPS2Elan: icVersion(%d), crc(%d), samples[1](%d) \n", ic_version, info.crc_enabled, info.samples[1]);

    /*
     * Sanity check based on the constant bits of a packet.
     * The constant bits change depending on the value of
     * the hardware flag 'crc_enabled' and the version of
     * the IC body, but are the same for every packet,
     * regardless of the type.
     */
    if (info.crc_enabled)
        sanity_check = ((packet[3] & 0x08) == 0x00);
    else if (ic_version == 7 && etd.info.samples[1] == 0x2A)
        sanity_check = ((packet[3] & 0x1c) == 0x10);
    else
        sanity_check = ((packet[0] & 0x08) == 0x00 &&
                (packet[3] & 0x1c) == 0x10);

    if (!sanity_check)
        return PACKET_UNKNOWN;

    switch (packet_type) {
    case 0:
        return PACKET_V4_STATUS;

    case 1:
        return PACKET_V4_HEAD;

    case 2:
        return PACKET_V4_MOTION;
    }

    return PACKET_UNKNOWN;
}

void ApplePS2Elan::elantechInputSyncV4() {
    //unsigned char *packet = ringBuffer.tail();
    // handle physical buttons here
    
    //inputEvent.transducers[0].isPhysicalButtonDown = packet[0] & 0x01;
}

void ApplePS2Elan::processPacketStatusV4() {
    unsigned char *packet = ringBuffer.tail();
    unsigned fingers;

    /* notify finger state change */
    fingers = packet[1] & 0x1f;
    int count = 0;
    for (int i = 0; i < ETP_MAX_FINGERS; i++) {
        if ((fingers & (1 << i)) == 0) {
            // finger has been lifted off the touchpad
            IOLog("VoodooPS2Elan: %d finger has been lifted off the touchpad\n", i);
            inputEvent.transducers[i].isTransducerActive = false;
        }
        else
        {
            IOLog("VoodooPS2Elan: %d finger has been touched the touchpad\n", i);
            inputEvent.transducers[i].isTransducerActive = true;
            inputEvent.transducers[i].type = VoodooInputTransducerType::FINGER;
            count++;
        }
    }
    
    inputEvent.contact_count = count;

    elantechInputSyncV4();
}

void ApplePS2Elan::processPacketHeadV4() {
    unsigned char *packet = ringBuffer.tail();
    int id = ((packet[3] & 0xe0) >> 5) - 1;
    int pres, traces;

    if (id < 0) {
        IOLog("VoodooPS2Elan: invalid id, aborting\n");
        return;
    }

    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    uint64_t timestamp_ns;
    
    id = 0;
    
    inputEvent.timestamp = timestamp;
    
    inputEvent.transducers[id].timestamp = timestamp;
    
    inputEvent.transducers[id].isPhysicalButtonDown = packet[0] & 1;
    
    inputEvent.transducers[id].isValid = true;
    inputEvent.transducers[id].type = VoodooInputTransducerType::FINGER;
  
    inputEvent.transducers[id].fingerType = kMT2FingerTypeIndexFinger;
    inputEvent.transducers[id].supportsPressure = false;
    
    inputEvent.transducers[id].previousCoordinates = inputEvent.transducers[id].currentCoordinates;

    inputEvent.transducers[id].currentCoordinates.x = ((packet[1] & 0x0f) << 8) | packet[2];
    inputEvent.transducers[id].currentCoordinates.y = info.y_max - (((packet[4] & 0x0f) << 8) | packet[5]);
    pres = (packet[1] & 0xf0) | ((packet[4] & 0xf0) >> 4);
    traces = (packet[0] & 0xf0) >> 4;

    inputEvent.transducers[id].currentCoordinates.pressure = pres;
    inputEvent.transducers[id].currentCoordinates.width = traces * etd.width;

    inputEvent.contact_count = 1;
    
    elantechInputSyncV4();
}

void ApplePS2Elan::processPacketMotionV4() {
    unsigned char *packet = ringBuffer.tail();
    int weight, delta_x1 = 0, delta_y1 = 0, delta_x2 = 0, delta_y2 = 0;
    int id, sid;

    id = ((packet[0] & 0xe0) >> 5) - 1;
    if (id < 0) {
        IOLog("VoodooPS2Elan: invalid id, aborting\n");
        return;
    }

    id = 0;
    
    sid = ((packet[3] & 0xe0) >> 5) - 1;
    weight = (packet[0] & 0x10) ? ETP_WEIGHT_VALUE : 1;
    /*
     * Motion packets give us the delta of x, y values of specific fingers,
     * but in two's complement. Let the compiler do the conversion for us.
     * Also _enlarge_ the numbers to int, in case of overflow.
     */
    delta_x1 = (signed char)packet[1];
    delta_y1 = (signed char)packet[2];
    delta_x2 = (signed char)packet[4];
    delta_y2 = (signed char)packet[5];
    
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);
    uint64_t timestamp_ns;
    
    inputEvent.transducers[id].timestamp = timestamp;
    inputEvent.transducers[id].previousCoordinates = inputEvent.transducers[id].currentCoordinates;

    inputEvent.transducers[id].currentCoordinates.x += delta_x1 * weight;
    inputEvent.transducers[id].currentCoordinates.y -= delta_y1 * weight;
    
    inputEvent.transducers[id].isValid = true;
    inputEvent.transducers[id].type = VoodooInputTransducerType::FINGER;
    inputEvent.transducers[id].fingerType = kMT2FingerTypeIndexFinger;
    inputEvent.transducers[id].supportsPressure = false;

    if (sid >= 0 && false) {
        inputEvent.transducers[sid].isValid = true;
        inputEvent.transducers[sid].type = VoodooInputTransducerType::FINGER;
        
        inputEvent.transducers[sid].timestamp = timestamp;
        inputEvent.transducers[sid].previousCoordinates = inputEvent.transducers[sid].currentCoordinates;
        inputEvent.transducers[sid].currentCoordinates.x += delta_x2 * weight;
        inputEvent.transducers[sid].currentCoordinates.y -= delta_y2 * weight;
    }
    
    inputEvent.contact_count = 1;

    elantechInputSyncV4();
}

void ApplePS2Elan::elantechReportAbsoluteV4(int packetType)
{
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);

    inputEvent.timestamp = timestamp;

    switch (packetType) {
        case PACKET_V4_STATUS:
            IOLog("VoodooPS2Elan: Got status packet\n");
            processPacketStatusV4();
            break;

        case PACKET_V4_HEAD:
            IOLog("VoodooPS2Elan: Got head packet\n");
            processPacketHeadV4();
            break;

        case PACKET_V4_MOTION:
            IOLog("VoodooPS2Elan: Got motion packet\n");
            processPacketMotionV4();
            break;
        case PACKET_UNKNOWN:
        default:
            /* impossible to get here */
            break;
    }
    
    if (voodooInputInstance) {
        super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &inputEvent, sizeof(VoodooInputEvent));
    }
    else
        IOLog("VoodooPS2Elan: no voodooInputInstance\n");
}

void ApplePS2Elan::packetReady()
{
    IOLog("VoodooPS2Elan: packet ready occurred\n");
    // empty the ring buffer, dispatching each packet...
    while (ringBuffer.count() >= kPacketLength)
    {
        int packetType;
        switch (info.hw_version) {
            case 1:
            case 2:
                IOLog("VoodooPS2Elan: packet ready occurred, but unsupported version\n");
                // V1 and V2 are ancient hardware, not going to implement right away
                break;
            case 3:
                IOLog("VoodooPS2Elan: packet ready occurred, but unsupported version\n");
                break;
            case 4:
                packetType = elantech_packet_check_v4();

                IOLog("VoodooPS2Elan: Packet Type %d\n", packetType);

                switch (packetType) {
                    case PACKET_UNKNOWN:
                         IOLog("VoodooPS2Elan: Handling unknown mode\n");
                        break;

                    case PACKET_TRACKPOINT:
                         IOLog("VoodooPS2Elan: Handling trackpoint mode\n");
                        break;
                    default:
                        IOLog("VoodooPS2Elan: Handling absolute mode\n");
                        elantechReportAbsoluteV4(packetType);
                }
                break;
            default:
                IOLog("VoodooPS2Elan: invalid packet received\n");
        }

        ringBuffer.advanceTail(kPacketLength);
    }
}

void ApplePS2Elan::setMouseEnable(bool enable)
{
  //
  // Instructs the mouse to start or stop the reporting of mouse events.
  // Be aware that while the mouse is enabled, asynchronous mouse events
  // may arrive in the middle of command sequences sent to the controller,
  // and may get confused for expected command responses.
  //
  // It is safe to issue this request from the interrupt/completion context.
  //

  // (mouse enable/disable command)
  TPS2Request<3> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = enable ? kDP_Enable : kDP_SetDefaultsAndDisable;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commandsCount = 3;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::setMouseSampleRate(UInt8 sampleRate)
{
  DEBUG_LOG("%s::setMouseSampleRate(0x%x)\n", getName(), sampleRate);
    
  //
  // Instructs the mouse to change its sampling rate to the given value, in
  // reports per second.
  //
  // It is safe to issue this request from the interrupt/completion context.
  //

  // (set mouse sample rate command)
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_SetMouseSampleRate;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_WriteCommandPort;
  request.commands[3].inOrOut = kCP_TransmitToMouse;
  request.commands[4].command = kPS2C_WriteDataPort;
  request.commands[4].inOrOut = sampleRate;
  request.commands[5].command = kPS2C_ReadDataPortAndCompare;
  request.commands[5].inOrOut = kSC_Acknowledge;
  request.commandsCount = 6;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2Elan::setMouseResolution(UInt8 resolution)
{
  //
  // Instructs the mouse to change its resolution given the following
  // resolution codes:
  //
  // 0 =  25 dpi
  // 1 =  50 dpi
  // 2 = 100 dpi
  // 3 = 200 dpi
  //
  // It is safe to issue this request from the interrupt/completion context.
  //
    
  DEBUG_LOG("%s::setMouseResolution(0x%x)\n", getName(), resolution);

  // (set mouse resolution command)
  TPS2Request<6> request;
  request.commands[0].command = kPS2C_WriteCommandPort;
  request.commands[0].inOrOut = kCP_TransmitToMouse;
  request.commands[1].command = kPS2C_WriteDataPort;
  request.commands[1].inOrOut = kDP_SetMouseResolution;
  request.commands[2].command = kPS2C_ReadDataPortAndCompare;
  request.commands[2].inOrOut = kSC_Acknowledge;
  request.commands[3].command = kPS2C_WriteCommandPort;
  request.commands[3].inOrOut = kCP_TransmitToMouse;
  request.commands[4].command = kPS2C_WriteDataPort;
  request.commands[4].inOrOut = resolution;
  request.commands[5].command = kPS2C_ReadDataPortAndCompare;
  request.commands[5].inOrOut = kSC_Acknowledge;
  request.commandsCount = 6;
  assert(request.commandsCount <= countof(request.commands));
  _device->submitRequestAndBlock(&request);
}

void ApplePS2Elan::Elantech_Touchpad_enable(bool enable )
{
    ps2_command<0>(NULL, (enable)?kDP_Enable:kDP_SetDefaultsAndDisable);
}


void ApplePS2Elan::injectVersionDependentProperties(OSDictionary *config)
{
    // inject properties specific to the version of Darwin that is runnning...
    char buf[32];
    OSDictionary* dict = NULL;
    do
    {
        // check for "Darwin major.minor"
        snprintf(buf, sizeof(buf), "Darwin %d.%d", version_major, version_minor);
        if ((dict = OSDynamicCast(OSDictionary, config->getObject(buf))))
            break;
        // check for "Darwin major.x"
        snprintf(buf, sizeof(buf), "Darwin %d.x", version_major);
        if ((dict = OSDynamicCast(OSDictionary, config->getObject(buf))))
            break;
        // check for "Darwin 16+" (this is what is used currently, other formats are for future)
        if (version_major >= 16 && (dict = OSDynamicCast(OSDictionary, config->getObject("Darwin 16+"))))
            break;
    } while (0);

    if (dict)
    {
        // found version specific properties above, inject...
        if (OSCollectionIterator* iter = OSCollectionIterator::withCollection(dict))
        {
            // Note: OSDictionary always contains OSSymbol*
            while (const OSSymbol* key = static_cast<const OSSymbol*>(iter->getNextObject()))
            {
                if (OSObject* value = dict->getObject(key))
                setProperty(key, value);
            }
            iter->release();
        }
    }
}
