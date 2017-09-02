//
//  main.cpp
//  VoodooPS2Daemon
//
//  Created by RehabMan on 1/5/13.
//  Copyright (c) 2013 RehabMan. All rights reserved.
//
//  The purpose of this daemon is to watch for USB mice being connected or disconnected from
//  the system.  This done by monitoring changes to the ioreg.
//
//  When changes in the status are detected, this information is sent to the trackpad
//  driver through a ioreg property. When the trackpad driver sees the chagnes to the property it
//  can decide to enable or disable the trackpad as appropriate.
//
//  This code was loosely based on "Another USB Notification Example" at:
//  http://www.opensource.apple.com/source/IOUSBFamily/IOUSBFamily-540.4.1/Examples/Another%20USB%20Notification%20Example/
//

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFString.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach.h>
#include <unistd.h>
#include <sys/utsname.h>

// notification data for IOServiceAddInterestNotification
typedef struct NotificationData
{
    io_object_t	notification;
} NotificationData;

static IONotificationPortRef g_NotifyPort;
static io_iterator_t g_AddedIter;

static int g_MouseCount;
static io_service_t g_ioservice;

static int g_startupDelay = 1000000;
static int g_notificationDelay = 20000;

#ifdef DEBUG
#define DEBUG_LOG(args...)   do { printf(args); fflush(stdout); } while (0)
#else
#define DEBUG_LOG(args...)   do { } while (0)
#endif
#define ALWAYS_LOG(args...)   do { printf(args); fflush(stdout); } while (0)

// SendMouseCount
//
// This function sends the current mouse count to the trackpad driver
// It is called whenever the mouse count changes

static void SendMouseCount(int nCount)
{
    if (g_ioservice)
    {
        CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, "MouseCount", CFStringGetSystemEncoding());
        CFNumberRef cf_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &nCount);
        kern_return_t kr = IORegistryEntrySetCFProperty(g_ioservice, cf_key, cf_number);
        if (KERN_SUCCESS != kr)
            DEBUG_LOG("IORegistryEntrySetCFProperty() returned error 0x%08x\n", kr);
        CFRelease(cf_key);
        CFRelease(cf_number);
    }
}


// DeviceNotification
//
// This function deals with IOUSBInterface nodes we previously expressed an interest
// in because they were USB mice.
// This is used to keep track of USB mice getting terminated

static void DeviceNotification(void* refCon, io_service_t service, natural_t messageType, void* messageArgument)
{
    NotificationData* pData = (NotificationData*)refCon;
    if (kIOMessageServiceIsTerminated == messageType)
    {
        if (g_MouseCount)
            --g_MouseCount;
        DEBUG_LOG("mouse count is now: %d\n", g_MouseCount);
        IOObjectRelease(pData->notification);
        free(pData);
        SendMouseCount(g_MouseCount);
    }
}

static uint64_t GetNumericProperty(io_service_t service, const char* name)
{
    // next check on alternate device in legacy tree
    uint64_t value = 0;
    CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, name, CFStringGetSystemEncoding());
    CFTypeRef prop = IORegistryEntryCreateCFProperty(service, str, kCFAllocatorDefault, 0);
    if (prop)
    {
        if (CFNumberGetTypeID() == CFGetTypeID(prop) &&
            CFNumberGetValue((CFNumberRef)prop, kCFNumberLongLongType, &value))
        {
            //DEBUG_LOG("property %s is = %llx\n", name, value);
        }
        CFRelease(prop);
    }
    CFRelease(str);
    return value;
}

static void CheckForMouseAndRegisterInterest(io_service_t service)
{
    // check for HID device that is a mouse
    if (GetNumericProperty(service, "bInterfaceClass") == 3 &&
        GetNumericProperty(service, "bInterfaceSubClass") == 1 &&
        GetNumericProperty(service, "bInterfaceProtocol") == 2)
    {
#ifdef DEBUG
        unsigned idVendor = (unsigned)GetNumericProperty(service, "idVendor");
        unsigned idProduct = (unsigned)GetNumericProperty(service, "idProduct");
        DEBUG_LOG("found mouse %04x:%04x\n", idVendor, idProduct);
#endif
        NotificationData* pData = (NotificationData*)malloc(sizeof(*pData));
        if (pData != NULL)
        {
            kern_return_t kr = IOServiceAddInterestNotification(g_NotifyPort, service, kIOGeneralInterest, DeviceNotification, pData, &pData->notification);
            if (KERN_SUCCESS != kr)
            {
                DEBUG_LOG("IOServiceAddInterestNotification returned 0x%08x\n", kr);
                return;
            }
            ++g_MouseCount;
            DEBUG_LOG("mouse count is now: %d\n", g_MouseCount);
        }
    }
}

// InterfaceAdded
//
// This function deals with USB devices as they are connected.  We look for HID
// USB devices that are mice.

static void InterfaceAdded(void *refCon, io_iterator_t iter1)
{
    usleep(g_notificationDelay); // wait 20ms for entry to populate

    int oldMouseCount = g_MouseCount;
    io_service_t service;
    while ((service = IOIteratorNext(iter1)))
    {
#ifdef DEBUG
        io_name_t name;
        kern_return_t kr1 = IORegistryEntryGetName(service, name);
        if (KERN_SUCCESS == kr1)
            DEBUG_LOG("name = '%s'\n", name);
#endif

        // check in the normal part of the registry
        CheckForMouseAndRegisterInterest(service);
        IOObjectRelease(service);
    }
    if (oldMouseCount != g_MouseCount)
        SendMouseCount(g_MouseCount);
}


// SignalHandler
//
// Deal with being terminated.  We might be able to eventually use this to turn off the
// trackpad LED as the system is shutting down.

static void SignalHandler1(int sigraised)
{
    DEBUG_LOG("\nInterrupted\n");

    // special shutdown sequence
    //  - no longer tracking MouseCount, so set to zero
    //  - and send special -1 MouseCount so LED can be forced off 
    SendMouseCount(0);
    SendMouseCount(-1);
    
    // clean up here
    if (g_AddedIter)
    {
        IOObjectRelease(g_AddedIter);
        g_AddedIter = 0;
    }
    IONotificationPortDestroy(g_NotifyPort);
    
    if (g_ioservice)
    {
        IOObjectRelease(g_ioservice);
        g_ioservice = 0;
    }
   
    // exit(0) should not be called from a signal handler.  Use _exit(0) instead
    _exit(0);
}

// main
//
// Entry point from command line or (eventually) launchd LaunchDaemon
//

int main(int argc, const char *argv[])
{
#ifdef DEBUG
    time_t current_time = time(NULL);
    char* c_time_string = ctime(&current_time);
    size_t l = strlen(c_time_string);
    if (l > 0)
        c_time_string[l-1] = 0;
    DEBUG_LOG("%s: VoodooPS2Daemon 1.8.28 starting...\n", c_time_string);
#endif

    // parse arguments...
    for (int i = 1; i < argc; i++)
    {
        if (0 == strcmp(argv[i], "--startupDelay"))
        {
            if (++i < argc && argv[i])
                g_startupDelay = atoi(argv[i]);
        }
        if (0 == strcmp(argv[i], "--notificationDelay"))
        {
            if (++i < argc && argv[i])
                g_notificationDelay = atoi(argv[i]);
        }
    }
    DEBUG_LOG("g_startupDelay: %d\n", g_startupDelay);
    DEBUG_LOG("g_notificationDelay: %d\n", g_notificationDelay);

    // Note: on Snow Leopard, the system is not ready to enumerate USB devices, so we wait a
    // bit before continuing...
    usleep(g_startupDelay);

    // first check for trackpad driver
	g_ioservice = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ApplePS2SynapticsTouchPad"));
	if (!g_ioservice)
	{
        // otherwise, talk to mouse driver
        g_ioservice = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ApplePS2Mouse"));
        if (!g_ioservice)
        {
            DEBUG_LOG("No ApplePS2SynapticsTouchPad or ApplePS2Mouse found\n");
            return -1;
        }
	}
    
    // Set up a signal handler so we can clean up when we're interrupted from the command line
    // or otherwise asked to terminate.
    if (SIG_ERR == signal(SIGINT, SignalHandler1))
        DEBUG_LOG("Could not establish new SIGINT handler\n");
    if (SIG_ERR == signal(SIGTERM, SignalHandler1))
        DEBUG_LOG("Could not establish new SIGTERM handler\n");
    
    kern_return_t kr;
    utsname system_info;
    uname(&system_info);
    DEBUG_LOG("System version: %s\n", system_info.release);
    int major_version = atoi(system_info.release);
    DEBUG_LOG("major_version = %d\n", major_version);

    // Create dictionary to match all USB devices
    //CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
    CFMutableDictionaryRef matchingDict = IOServiceMatching("IOUSBInterface");
    if (!matchingDict)
    {
        DEBUG_LOG("Can't create a USB matching dictionary\n");
        return -1;
    }

    // Create a notification port and add its run loop event source to our run loop
    // This is how async notifications get set up.
    g_NotifyPort = IONotificationPortCreate(kIOMasterPortDefault);
    CFRunLoopSourceRef runLoopSource = IONotificationPortGetRunLoopSource(g_NotifyPort);
    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(runLoop, runLoopSource, kCFRunLoopDefaultMode);
    
    // Now set up a notification to be called when a device is first matched by I/O Kit.
    // Note that this will not catch any devices that were already plugged in so we take
    // care of those later.
    kr = IOServiceAddMatchingNotification(g_NotifyPort, kIOFirstMatchNotification, matchingDict, InterfaceAdded, NULL, &g_AddedIter);
    if (KERN_SUCCESS != kr)
    {
        DEBUG_LOG("IOServiceAddMatchingNotification failed(%08x)\n", kr);
        return -1;
    }

    // Iterate once to get already-present devices and arm the notification
    DEBUG_LOG("Initial iterate\n");
    InterfaceAdded(NULL, g_AddedIter);
    DEBUG_LOG("Initial iterate done\n");

    // Start the run loop. Now we'll receive notifications.
    CFRunLoopRun();
    
    // We should never get here
    DEBUG_LOG("Unexpectedly back from CFRunLoopRun()!\n");
    
    return 0;
}


