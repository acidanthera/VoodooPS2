//#import "VoodooPS2Pref.h"
#import "VoodooPS2synapticsPane.h"
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOKitKeys.h>




static io_service_t io_service=0;
static CFMutableDictionaryRef dict=0; 



IOReturn sendNumber (const char * key, unsigned int number, io_service_t service)
{
	IOReturn retvalue = kIOReturnError;
	CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key, CFStringGetSystemEncoding());
	CFNumberRef cf_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &number);
	if (cf_number) 
	{ 
		retvalue = IORegistryEntrySetCFProperty(service, cf_key, cf_number);
		
        CFDictionarySetValue (dict, cf_key, cf_number);
//		CFRelease(cf_number); 
	}
//	if (cf_key) CFRelease(cf_key);
	return retvalue;
}

int getNumber (const char * key, io_service_t io_service)
{
	CFNumberRef num; 
	int tt=0;
	CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key, CFStringGetSystemEncoding());
	if ((num=(CFNumberRef)IORegistryEntryCreateCFProperty(io_service, cf_key, kCFAllocatorDefault, 0)))
	{
		CFDictionarySetValue (dict, cf_key, num);
		CFNumberGetValue (num,kCFNumberIntType,&tt);
//		CFRelease(num);	
	}
//	if (cf_key) CFRelease(cf_key);
	return tt;
}

IOReturn sendLongNumber (const char * key, unsigned long long number, io_service_t service)
{
	IOReturn retvalue = kIOReturnError;
	CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key, CFStringGetSystemEncoding());
	CFNumberRef cf_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &number);
	if (cf_number) 
	{ 
		CFDictionarySetValue (dict, cf_key, cf_number);
		retvalue = IORegistryEntrySetCFProperty(service, cf_key, cf_number);
//
        
        
        CFRelease(cf_number);
	}
//	if (cf_key) CFRelease(cf_key);
	return retvalue;
}

long long getLongNumber (const char * key, io_service_t io_service)
{
	CFNumberRef num; 
	long long tt=0;
	CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key, CFStringGetSystemEncoding());
	if ((num=(CFNumberRef)IORegistryEntryCreateCFProperty(io_service, cf_key, kCFAllocatorDefault, 0)))
	{
		CFDictionarySetValue (dict, cf_key, num);
		CFNumberGetValue (num,kCFNumberLongLongType,&tt);
//		CFRelease(num);	
	}
//	if (cf_key) CFRelease(cf_key);
	return tt;
}

IOReturn sendBoolean (const char * key, long bl, io_service_t service)
{
	IOReturn retvalue = kIOReturnError;
	CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key, CFStringGetSystemEncoding());
	CFDictionarySetValue (dict, cf_key, bl?kCFBooleanTrue:kCFBooleanFalse);
	retvalue = IORegistryEntrySetCFProperty(service, cf_key, bl?kCFBooleanTrue:kCFBooleanFalse);
   
//	if (cf_key) CFRelease(cf_key);
	return retvalue;
}

int getBoolean (const char * key, io_service_t io_service)
{
	CFBooleanRef num; 
	int tt=0;
	CFStringRef cf_key = CFStringCreateWithCString(kCFAllocatorDefault, key, CFStringGetSystemEncoding());
	if ((num=(CFBooleanRef)IORegistryEntryCreateCFProperty(io_service, cf_key, kCFAllocatorDefault, 0)))
	{
		CFDictionarySetValue (dict, cf_key, num);
		tt=CFBooleanGetValue (num);
//		CFRelease(num);	
	}
//	if (cf_key) CFRelease(cf_key);
	return tt;
}

@implementation VoodooPS2Pref

- (void) mainViewDidLoad
{
}

- (void) awakeFromNib
{
	io_service = IOServiceGetMatchingService(0, IOServiceMatching("AppleUSBMultitouchDriver"));
	if (!io_service)
	{
		NSRunCriticalAlertPanel( 
								NSLocalizedString( @"AppleUSBMultitouchDriver not found", "MsgBox"), 
								NSLocalizedString( @"Error", "MsgBoxTitle" ), nil, nil, nil );
 		return;
	}	
	dict=CFDictionaryCreateMutable(NULL,0, &kCFTypeDictionaryKeyCallBacks ,NULL);	
	[speedSliderX setDoubleValue:101-getNumber("DivisorX", io_service)];
    [speedSliderY setDoubleValue:101-getNumber("DivisorY", io_service)];
	[maxTapTimeSlider setDoubleValue:getLongNumber("MaxTapTime", io_service)!=0?
	 getLongNumber("MaxTapTime", io_service)/2500000.0:40];
	[fingerZSlider setDoubleValue:getNumber("FingerZ", io_service)];
	[tedgeSlider setDoubleValue:getNumber("TopEdge", io_service)/70.0];
	[bedgeSlider setDoubleValue:getNumber("BottomEdge", io_service)/70.0];
	[ledgeSlider setDoubleValue:getNumber("LeftEdge", io_service)/70.0];
	[redgeSlider setDoubleValue:getNumber("RightEdge", io_service)/70.0];
	[centerXSlider setDoubleValue:getNumber("CenterX", io_service)/70.0];
	[centerYSlider setDoubleValue:getNumber("CenterY", io_service)/70.0];
	[hscrollSlider setDoubleValue:getNumber("HorizontalScrollDivisor", io_service)?
	 101-getNumber("HorizontalScrollDivisor", io_service):71];
	[hscrollSlider setEnabled:getNumber("HorizontalScrollDivisor", io_service)!=0];
	[vscrollSlider setDoubleValue:getNumber("VerticalScrollDivisor", io_service)?
	 101-getNumber("VerticalScrollDivisor", io_service):71];
	[vscrollSlider setEnabled:getNumber("VerticalScrollDivisor", io_service)!=0];
	[cscrollSlider setDoubleValue:getNumber("CircularScrollDivisor", io_service)?
	 101-getNumber("CircularScrollDivisor", io_service):71];
	[cscrollSlider setEnabled:getNumber("CircularScrollDivisor", io_service)!=0];
	[cTrigger setEnabled:getNumber("CircularScrollDivisor", io_service)!=0];
	[cTrigger selectItemAtIndex: getNumber("CircularScrollTrigger", io_service)-1];
	[mhSlider setDoubleValue:getNumber("MultiFingerHorizontalDivisor", io_service)?
	 101-getNumber("MultiFingerHorizontalDivisor", io_service):71];
	[mhSlider setEnabled:getNumber("MultiFingerHorizontalDivisor", io_service)!=0];
	[mvSlider setDoubleValue:getNumber("MultiFingerVerticalDivisor", io_service)?
	 101-getNumber("MultiFingerVerticalDivisor", io_service):71];
	[mvSlider setEnabled:getNumber("MultiFingerVerticalDivisor", io_service)!=0];
	[mwSlider setDoubleValue:getNumber("MultiFingerWLimit", io_service)!=17?
	 (getNumber("MultiFingerWLimit", io_service)-4)*(100.0/12):(100.0/6)];
	[mwSlider setEnabled:getNumber("MultiFingerWLimit", io_service)!=17];
	
	[stabTapButton setState:getBoolean("StabilizeTapping", io_service)];
	[hRateButton setState:getBoolean("ExtendedWmode", io_service)];
	[hScrollButton setState:getNumber("HorizontalScrollDivisor", io_service)!=0];
	[vScrollButton setState:getNumber("VerticalScrollDivisor", io_service)!=0];
	[hScrollButton setState:getNumber("HorizontalScrollDivisor", io_service)!=0];
	[cScrollButton setState:getNumber("CircularScrollDivisor", io_service)!=0];
	[vsScrollButton setState:getBoolean("StickyVerticalScrolling", io_service)];
	[vsScrollButton setEnabled:getBoolean("VerticalScrollDivisor", io_service)!=0];
	[hsScrollButton setState:getBoolean("StickyHorizontalScrolling", io_service)];
	[hsScrollButton setEnabled:getBoolean("HorizontalScrollDivisor", io_service)!=0];
	[msButton setState:getBoolean("StickyMultiFingerScrolling", io_service)];

	[hmScrollButton setState:getNumber("MultiFingerHorizontalDivisor", io_service)!=0];
	[vmScrollButton setState:getNumber("MultiFingerVerticalDivisor", io_service)!=0];
	[adwButton setState:getNumber("MultiFingerWLimit", io_service)!=17];
}

- (void)didUnselect
{
	FILE *f;
	CFDataRef dat1;
	UInt8 *dat2;
	
	if (!dict)
		return;
	dat1=CFPropertyListCreateXMLData (kCFAllocatorDefault, dict);
	if (!dat1)
	{
		NSRunCriticalAlertPanel( 
								NSLocalizedString( @"Couldn't create XML", "MsgBox"),
								NSLocalizedString( @"Error creating XML data", "MsgBoxBody" ), nil, nil, nil );		
		return;
	}
	
	dat2=(UInt8 *) malloc (CFDataGetLength (dat1));
	CFDataGetBytes (dat1, CFRangeMake(0,CFDataGetLength(dat1)), dat2);
	if (!dat2)
	{
		NSRunCriticalAlertPanel( 
								NSLocalizedString( @"Couldn't alocate memory ", "MsgBox"), 
								NSLocalizedString( @"Error allocating memory", "MsgBoxBody" ), nil, nil, nil );		
		return;
	}
	
    
      //f=fopen ([[NSHomeDirectory() stringByAppendingString:[NSString stringWithCString: "/Library/Preferences/org.voodoo.SynapticsTouchpad.plist"]] UTF8String], "wb");
    f=fopen ([[NSHomeDirectory() stringByAppendingString:[NSString stringWithCString: "/Library/Preferences/org.voodoo.SynapticsTouchpad.plist" encoding:NSASCIIStringEncoding]] UTF8String], "wb");
	//NSString *fName = nil;
	//f=fopen ([[fName  stringByAppendingString: @"/Library/Preferences/org.voodoo.SynapticsTouchpad.plist"] UTF8String], "wb");
    //f=authopen ([[fName  stringByAppendingString: @"/Library/Preferences/org.voodoo.SynapticsTouchpad.plist"] UTF8String], "wb");
	
    if (!f)
	{
		NSRunCriticalAlertPanel( 
								NSLocalizedString( @"Couldn't save plist", "MsgBox"), 
								NSLocalizedString( @"Error opening file", "MsgBoxBody" ), nil, nil, nil );		
		return;
	}
	fwrite(dat2, 1, CFDataGetLength (dat1), f);
	fclose (f);
	return ;
}

- (IBAction) SlideSpeedXAction: (id) sender
{
	sendNumber("DivisorX", 101-[speedSliderX doubleValue], io_service);
}

- (IBAction) SlideSpeedYAction: (id) sender
{
	sendNumber("DivisorY", 101-[speedSliderY doubleValue], io_service);
}

- (IBAction) ButtonHighRateAction: (id) sender
{
	sendBoolean("ExtendedWmode", [hRateButton state], io_service);
}

- (IBAction) SlideFingerZAction: (id) sender
{
	sendNumber("FingerZ", [fingerZSlider doubleValue], io_service);
}

- (IBAction) SlideTEdgeAction: (id) sender
{
	sendNumber("TopEdge", [tedgeSlider doubleValue]*70, io_service);
}
- (IBAction) SlideBEdgeAction: (id) sender
{
	sendNumber("BottomEdge", [bedgeSlider doubleValue]*70, io_service);
}
- (IBAction) SlideLEdgeAction: (id) sender
{
	sendNumber("LeftEdge", [ledgeSlider doubleValue]*70, io_service);
}

- (IBAction) SlideREdgeAction: (id) sender
{
	sendNumber("RightEdge", [redgeSlider doubleValue]*70, io_service);
}

- (IBAction) SlideCenterXAction: (id) sender
{
	sendNumber("CenterX", [centerXSlider doubleValue]*70, io_service);
}

- (IBAction) SlideCenterYAction: (id) sender
{
	sendNumber("CenterY", [centerYSlider doubleValue]*70, io_service);
}

- (IBAction) TapAction: (id) sender
{
	sendLongNumber("MaxTapTime", [maxTapTimeSlider doubleValue]*2500000.0,io_service);
	sendBoolean("StabilizeTapping", [stabTapButton state], io_service);
}

- (IBAction) HscrollAction: (id) sender
{
	if ([hScrollButton state])
	{
		[hscrollSlider setEnabled:1];
		[hsScrollButton setEnabled:1];
		sendNumber("HorizontalScrollDivisor", 101-[hscrollSlider doubleValue],io_service);
		sendBoolean("StickyHorizontalScrolling", [hsScrollButton state], io_service);
	}
	else
	{
		[hscrollSlider setEnabled:0];
		[hsScrollButton setEnabled:0];
		sendNumber("HorizontalScrollDivisor", 0, io_service);
	}		
}

- (IBAction) CscrollAction: (id) sender
{
	if ([cScrollButton state])
	{
		[cscrollSlider setEnabled:1];
		[cTrigger setEnabled:1];
		sendNumber("CircularScrollDivisor", 101-[hscrollSlider doubleValue],io_service);
		sendNumber("CircularScrollTrigger", (int)[cTrigger indexOfSelectedItem]+1, io_service);
	}
	else
	{
		[cscrollSlider setEnabled:0];
		[cTrigger setEnabled:0];
		sendNumber("CircularScrollDivisor", 0, io_service);
	}		
}

- (IBAction) VscrollAction: (id) sender
{
	if ([vScrollButton state])
	{
		[vscrollSlider setEnabled:1];
		[vsScrollButton setEnabled:1];
		sendNumber("VerticalScrollDivisor", 101-[vscrollSlider doubleValue],io_service);
		sendBoolean("StickyVerticalScrolling", [vsScrollButton state], io_service);
	}
	else
	{
		[vscrollSlider setEnabled:0];
		[vsScrollButton setEnabled:0];
		sendNumber("VerticalScrollDivisor", 0, io_service);
	}		
}

- (IBAction) MHscrollAction: (id) sender
{
	if ([hmScrollButton state])
	{
		[mhSlider setEnabled:1];
		sendNumber("MultiFingerHorizontalDivisor", 101-[mhSlider doubleValue],io_service);
	}
	else
	{
		[mhSlider setEnabled:0];
		sendNumber("MultiFingerHorizontalDivisor", 0, io_service);
	}		
}

- (IBAction) MVscrollAction: (id) sender
{
	if ([vmScrollButton state])
	{
		[mvSlider setEnabled:1];
		sendNumber("MultiFingerVerticalDivisor", 101-[mvSlider doubleValue],io_service);
	}
	else
	{
		[mvSlider setEnabled:0];
		sendNumber("MultiFingerVerticalDivisor", 0, io_service);
	}		
}

- (IBAction) ADWAction: (id) sender
{
	if ([adwButton state])
	{
		[mwSlider setEnabled:1];
		sendNumber("MultiFingerWLimit", 4+12*[mwSlider doubleValue]/100,
				   io_service);
	}
	else
	{
		[mwSlider setEnabled:0];
		sendNumber("MultiFingerWLimit", 17, io_service);
	}		
}


- (IBAction) ButtonMStickyAction: (id) sender
{
	sendBoolean("StickyMultiFingerScrolling", [msButton state], io_service);
}

@end
