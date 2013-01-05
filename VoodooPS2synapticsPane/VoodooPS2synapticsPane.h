#import <PreferencePanes/PreferencePanes.h>


@interface VoodooPS2Pref : NSPreferencePane 
{
    IBOutlet NSSlider *speedSliderX;
    IBOutlet NSSlider *speedSliderY;
	IBOutlet NSSlider * maxTapTimeSlider;
	IBOutlet NSSlider * fingerZSlider;
	IBOutlet NSSlider * tedgeSlider;
	IBOutlet NSSlider * bedgeSlider;
	IBOutlet NSSlider * ledgeSlider;
	IBOutlet NSSlider * redgeSlider;
	IBOutlet NSSlider * centerXSlider;
	IBOutlet NSSlider * centerYSlider;
	IBOutlet NSSlider * hscrollSlider;
	IBOutlet NSSlider * vscrollSlider;
	IBOutlet NSSlider * cscrollSlider;
	IBOutlet NSSlider * mhSlider;
	IBOutlet NSSlider * mvSlider;
	IBOutlet NSSlider * mwSlider;
	IBOutlet NSButton * stabTapButton;
	IBOutlet NSButton * hRateButton;
	IBOutlet NSButton * hScrollButton;
	IBOutlet NSButton * vScrollButton;
	IBOutlet NSButton * cScrollButton;
	IBOutlet NSButton * hsScrollButton;
	IBOutlet NSButton * vsScrollButton;
	IBOutlet NSButton * hmScrollButton;
	IBOutlet NSButton * vmScrollButton;
	IBOutlet NSButton * adwButton;
	IBOutlet NSButton * msButton;
	IBOutlet NSPopUpButton * cTrigger;
}

- (void) mainViewDidLoad;
- (void) awakeFromNib;
- (void) didUnselect;
- (IBAction) SlideSpeedXAction: (id) sender;
- (IBAction) SlideSpeedYAction: (id) sender;
- (IBAction) TapAction: (id) sender;

- (IBAction) ButtonHighRateAction: (id) sender;
- (IBAction) SlideFingerZAction: (id) sender;
- (IBAction) SlideTEdgeAction: (id) sender;
- (IBAction) SlideBEdgeAction: (id) sender;
- (IBAction) SlideLEdgeAction: (id) sender;
- (IBAction) SlideREdgeAction: (id) sender;
- (IBAction) SlideCenterXAction: (id) sender;
- (IBAction) SlideCenterYAction: (id) sender;

- (IBAction) HscrollAction: (id) sender;
- (IBAction) VscrollAction: (id) sender;
- (IBAction) CscrollAction: (id) sender;
- (IBAction) MHscrollAction: (id) sender;
- (IBAction) MVscrollAction: (id) sender;
- (IBAction) ADWAction: (id) sender;
- (IBAction) ButtonMStickyAction: (id) sender;
@end
