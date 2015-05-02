## Modified VoodooPS2Controller by RehabMan


### How to Install:

Please read and follow the important instructions for installing in the wiki:

https://github.com/RehabMan/OS-X-Voodoo-PS2-Controller/wiki/How-to-Install


### Downloads:

Downloads are available on Bitbucket:

https://bitbucket.org/RehabMan/os-x-voodoo-ps2-controller/downloads

Note: Archived (old) downloads are available on Google Code:

https://code.google.com/p/os-x-voodoo-ps2-controller/downloads/list


### Build Environment

My build environment is currently Xcode 6.1, using SDK 10.8, targeting OS X 10.6.

No other build environment is supported.


### 32-bit Builds

Currently, builds are provided only for 64-bit systems.  32-bit/64-bit FAT binaries are not provided.  But you can build your own should you need them.  I do not test 32-bit, and there may be times when the repo is broken with respect to 32-bit builds, but I do check on major releases to see if the build still works for 32-bit.

Here's how to build 32-bit (universal):

- xcode 4.6.3
- open VoodooPS2Controller.xcodeproj
- click on VoodooPS2Controller at the top of the project tree
- select VoodooPS2Controller under Project
- change Architectures to 'Standard (32/64-bit Intel)'

probably not necessary, but a good idea to check that the targets don't have overrides:
- multi-select all the Targets
- check/change Architectures to 'Standard (32/64-bit Intel)'
- build (either w/ menu or with make)

Or, if you have the command line tools installed, just run:

- For FAT binary (32-bit and 64-bit in one binary)
make BITS=3264

- For 32-bit only
make BITS=32


### Source Code:

The source code is maintained at the following sites:

https://code.google.com/p/os-x-voodoo-ps2-controller/

https://github.com/RehabMan/OS-X-Voodoo-PS2-Controller

https://bitbucket.org/RehabMan/os-x-voodoo-ps2-controller


### Feedback:

Please use the following threads on tonymacx86.com for feedback, questions, and help:

http://www.tonymacx86.com/hp-probook/75649-new-voodoops2controller-keyboard-trackpad.html#post468941
http://www.tonymacx86.com/mountain-lion-laptop-support/87182-new-voodoops2controller-keyboard-trackpad-clickpad-support.html#post538365


### Fun Facts:

While implementing the "just for fun" feature in the keyboard driver where Ctrl+Alt+Del maps to the power key (for selection of Restart, Sleep, Shutdown), I discovered that if you invoke this function with the Ctrl and Alt (Command) keys down, the system will do an abrupt and unsafe restart.  You can verify this yourself by holding down the Ctrl and Alt keys while pressing the actual power button.


### Known issues:

- Very rarely, both the keyboard and trackpad are not working after a fresh boot or after sleep, even on systems where this is normally not a problem.  As of the v1.7.15a release this problem appears to be solved.  Time will tell.

- Very rarely, the keyboard/trackpad may become unresponsive or a key may repeat indefinitely.  As of the v1.7.15a release this problem appears to be solved.  Time will tell.

Note: often times you will see either of the two problems mentioned above right after installing.  Generally, another reboot and/or repair permissions & rebuild caches will fix the problem.


### Change Log:

2015-05-02 v1.8.15

- Fix a problem with phantom key event e027 on certain ProBook laptops causing spurious/random fnkeys toggle

- Allow for discrete fnkeys toggle and discrete trackpad toggle setup


2015-02-23 v1.8.14

- Fix a problem with u430 F9 key when "Use all F1, F2..." is selected.  This resulted in a new way to send both make/break keys from ACPI Notify (0x03xx and 0x04xx).


2014-10-16 v1.8.13

- Default for USBMouseStopsTrackpad is now zero instead of one.  This means the trackpad will not be disabled at the login screen when a USB mouse is plugged in.

- turn off FakeMiddleButton in VoodooPS2Mouse.kext

- some tweaks for ideapad

- tuned movement/acceleration to better match the MacBookAir6,2.  Changed resolution to 400 from higher values, which seems to help...

- other fixes/changes: see commit log for more information


2014-05-23 v1.8.12

- Fix bugs.  See commit log/issues database for details.

- Finished Macro Inversion option for converting Fn+fkeys in challenging situations.

- Lenovo u430 profile is working well now  (due to Macro Inversion and other features).


2014-02-24 v1.8.11

- Implement ability to send key strokes from ACPI.  This is useful when certain keys are not handled via the PS2 bus, but instead are handled elsewhere in DSDT, usually via EC query methods.  With this I was able to make the native brightness keys work as normal keys on a Lenovo U430.

- Implement ability to call into ACPI when keys are pressed.  Scan codes e0f0 through e0ff are reserved to call back into RKAx (where X is a hex digit corresponding to the last digit of the scan code, eg. 0-9 A-F).  Use Custom PS2 Map to map a real scan code to e0f0-e0ff, then define RKA0 up to RKAF in your PS2K device.  Note: Your keyboard device must be called PS2K. 

- A few pull requests from others.  See commit log.

- [wip] Profile for Lenovo u430 keyboard.


2014-01-21 v1.8.10

- Implement BrightnessHack option for laptops with non-working Brightness keys.  If set in the keyboard Info.plist, this setting will allow screen brightness to be changed via Ctrl+Alt+NumPad Plus/Minus keys.


2013-12-05 v1.8.9

- Fixed F1 sleep issue in Mavericks

- Added Platform Profiles for various laptops (HP 6560b, Dell N5110), including special key switching for certain Dell/Samsung machines.

- Added workaround for unexpected trackpad data


2013-09-13 v1.8.8

- Fixed jitter/twitching that happens when using two finger scroll on 10.9 Mavericks.  Threshold is currently 10.  Can be customized in the trackpad Info.plist with the ScrollDeltaThreshX and ScrollDeltaThreshY items.

- Implemented "Dragging" (double-tap-hold for drag) to be more like a real Mac.  Now the drag will remain in effect for one second after releasing your finger from the touchpad (before it was immediate).  This makes it easier to drag/resize/extend selections larger distance.  The timeout is controlled by DragExitDelayTime in the trackpad Info.plist.  It can be changed to zero (0) to revert to the original behavior.

- Implemented a slight tweak to the averaging algorithm.  Now using 4 samples instead of 3.


2013-08-15 v1.8.7

- Fix a bug which prevents VoodooPS2Controller.kext from loading on 10.9 Mavericks.  The problem was the class ApplePS2Controller was not exported properly.

2013-08-05 v1.8.6

- Add support for HPQOEM:17F3 (HP ProBook 4440s)

2013-07-01 v1.8.5

- Added support for HPQOEM:17F0 (HP ProBook 4340s)

- Corrected error in setProperties for ApplePS2Controller.  Now works with utility 'ioio'


2013-05-26 v1.8.4

- Added option to override DSDT oemId/oemTableId via ioreg properties on PS2K device.  This is for clover as it is necessary to override oemId because Clover patches the DSDT to reflect oemId as "Apple ".  In order to work around this bug, we can now provide an "RM,oem-id" property in the PS2K device.  Although it isn't necessary (for Clover), you can also provide an override oemTableId via a property "RM,oem-table-id".  For an example of use, see the ProBook 4530s patches (02_DSDTPatch.txt, search for PS2K).  This allows you to use the driver unmodified on Clover and still get the proper configuration selected via Platform Profile setup, provided you have the proper injection in your DSDT.  This may end up being useful for other computers as well, when the OEM has chosen poor names for oemId/oemTableId.


2013-05-07 v1.8.3

- WakeDelay in ioreg for ApplePS2Controller is now correct integer type instead of bool.

- Added support for HP ProBook 5330m.

- Eliminated FakeSMC dependency.  The drivers now look directly at DSDT in ioreg to determine what to use for configuration instead of the "mb-product"/"mb-manufacturer" properties of FakeSMC.

- Remove extraneous mapping for R_OPTION in ProBook-87 keyboard configuration.


2013-04-07 v1.8.2

- Fixed problem under Snow Leopard where VoodooPS2Keyboard.kext was not loading.

- Fix issue with Eject (Insert) causes DVD to eject immediately instead of after a delay.  Now the driver honors the HIDF12EjectDelay configuration setting, which by default is 250 ms.  You can also change it in the Info.plist if you wish.  250 ms provides a slight delay that avoids accidental eject.

- Added support for HP:4111 keyboard map/configuration for HP ProBook 4520s.

- Changed the default assignment of the right "menu/application" key (on 4530s in between right Alt and right Ctrl).  Prior to this release it was assigned to Apple 'Fn' key.  Now it is assigned such that it is 'right Windows' which maps to Apple 'option.'  This is configurable in the keyboard driver's Info.plist.

- Fixed a bug where special functions/media keys were not mapped to F-keys by default until you checked then unchecked the option for this in SysPrefs -> Keyboard.


2013-03-15 v1.8.1

- New feature: It is now possible to toggle the Keyboard Prefs "Use all F1, F2…" option with a keyboard hotkey.  On the ProBook 4xx0s series, it is Ctrl+'prt sc'.

- New feature: Added keyboard mapping/ADB (92) code for the Eject key.  On the ProBook 4xx0s series, it is assigned to the 'insert' key.  See this link for more information on the various modifiers that can be used with the Eject key: http://support.apple.com/kb/ht1343

- Supported added to Info.plist for keyboards on ProBook 8460p/6470b laptops. Credits to nguyenmac and kpkp.

- Platform Profile computer specific sections in Info.plist are now merged with the 'Default' section making it easier to create/manage alternate configurations, because you only have to enter the differences/overrides.

- Fixed a bug in the ProBook specific Info.plist (for those not using kozlek's FakeSMC) where SleepPressTime was not set to 3000.

- Developers: Updated to Xcode 4.61, and also optimized a bit for size and exporting less symbols into kernel namespace.


2013-03-04 v1.8.0

- Feature: Info.plist content is now driven off the mb-manufacturer/mb-product ioreg properties provided by kozlek's FakeSMC.  This allows different keyboard layouts/trackpad settings to be based on which machine/motherboard the drivers find themselves running on.  As of this time, support has been added for the Probook 4x30s series (HP/167C).  Users who create custom settings for other hardware are encouraged to submit their Info.plist changes and mb-manufacturer/mb-product IDs from the FakeSMC device in the ioreg.  I will integrate these new profiles into future builds.  This also means that I'm only distributing one version of the package from now on, with special instructions for ProBook users not using the latest FakeSMC.

- bug fix: Fixed UnsmoothInput option.  It was not working at all.

- Changed the default Resolution/ScrollResolution from 2300 to 2950.  This results in more control for finer movements, but slower overall acceleration.  You may have to adjust your pointer speeds in System Prefs -> Trackpad to suit your preference.

- bug fix: Fixed a problem on Snow Leopard if Kernel Cache was being used.

- For developers: Fixed 32-bit build.


2013-02-26 v1.7.17

- bug fix: Evidently there is an OS X bug in IOMallocAligned.  For some reason only affects the debug version of this driver (Release version was fine for some unknown reason).  Temporarily move back to IOMalloc until it can be resolved.


2013-02-25 v1.7.16

- bug fix: Fix some problems with pass through packets (pass through capability applies if you have both a trackpad and a stick).

- Add support for 'genADB' for keyboards without a number pad.  Hold down Alt (command), then type digits to simulate an ADB code, then release Alt.  This features is only available with the DEBUG version.  Previously this worked only with the numpad digits.

- Add LogScanCodes property to keyboard driver.  Now you can log scan codes to the Console system.log, even in the Release version.  Use 'ioio' to set this property to true, and you will see scan codes logged in the Console.  Set it back to false when you're done.


2013-02-21 v1.7.15a (beta)

- bug fix: Fix problem (again) with startup sequence (a multithreaded issue), where it was causing the keyboard to be non-responsive or indefinitely repeat a key.


2013-02-20 v1.7.15 (beta)

- Slight tweak to middle button handling: The code will now commit to a single left/right button if you touch the touchpad after pressing one of the buttons (instead of waiting the 100ms to see if a middle button event should happen instead).

- bug fix: Fixed a problem with startup (multithreaded issue), where the keyboard would not work or indefinitely repeat at the login screen.

- bug fix: Fix problem with holding down Alt and using numpad digits to type an ADB code (a feature only for debug mode). Had to undo most of "non-chaotic" startup and approach from another angle.

- New feature: Some keyboards do not generate 'break' codes when a key is released.  This causes OS X to believe the user is holding the key down.  Fn+F1 through Fn+F12 can have this.  Seems to be common on Dell laptops.  Since I failed to find a way to force the keyboard to generate break codes for these keys (some controllers seem to just ignore this), I have implemented the ability to specify these keys by their scan code in the keyboard driver's Info.plist. You must specify the scan code of each key in the "Breakless PS2" section.  See VoodooPS2Keyboard/VoodooPS2Keyboard-Breakless-Info.plist for an example (tested with my Probook keyboard for all Fn+fkeys).


2013-02-17 v1.7.14 (beta)

- Bug fix: Fixed problem where Synaptics trackpad would load even if a Synaptics device was not detected.

- Internal change: Re-wrote the interrupt handling code.  Prior to this version, data was not pulled from the PS2 port until the "work loop" got around to it.  All the interrupt routines did was signal the scheduler to wake the work loop.  With this version, data is collected directly in the interrupt routine, placed in a ring buffer, and the work loop is only scheduled when a complete packet has arrived.  So, for the keyboard driver, the work loop is activated whenever a full scan code sequence has been sent (from 1 to 3 bytes), for the mouse driver, when a full 3-byte packet has arrived, and for the trackpad driver, when a full 6-byte packet has arrived.  Not only is this more efficient (only scheduling the work loop when there is actual work to do), it is also safer as data is gathered from the PS2 port as soon as it is available.  This is a pretty major change.

- setProperties/setParamProperties is now fully implemented in the keyboard driver.  This means you can use 'ioio' to change configuration values on demand.  It also means that eventually the prefpane will be able to manipulate the keyboard Info.plist options just like it can with the trackpad.  When I get around to working on the prefpane…

- Added an option to most Info.plist to allow a device to be "disabled."  By setting DisableDevice to true, you can keep that driver code from loading and being considered for probing.  By default, none of the drivers are disabled, but disabling them may improve startup performance as well as reduce the chances of things going wrong.  I think a future version of the ProBook Installer should automatically disable the devices not used on the ProBook.

- Implemented a "non-chaotic" startup.  Turns out that OS X will start all devices in parallel.  While this can make the system start up faster, it might not be such a good idea for devices interacting with one resource, in this case, the PS2 port.  The keyboard driver now waits for the PS2Controller to finish starting before starting it's "probe" process.  And the mouse/trackpad drivers wait for the keyboard driver to finish starting before starting their "probe" process.  A future version may make this optional. 

- The keyboard driver's "probe" function now will always return success.  I have not found a reliable way to detect a keyboard actually being present or not, so it will always load and in the Release version does not test for the keyboard existing at all.  Also important to note that the mouse/trackpad drivers now wait for the keyboard to load (see above), so it is really necessary to have the keyboard driver loading always anyway.

- Added a special case for Synaptics type 0x46 (really a hack for Probooks with a Synaptics stick) to report led present, because we know it is there and it works.

- Cleaned up some of the logic for middle button clicks, such that releasing either button will immediately release the middle button.


Note: v1.7.13 skipped.


2013-02-07 v1.7.12 (beta)

- Implemented middle click by clicking both the left and right physical buttons at one time.  Time for button clicks to be considered middle instead of right/left is configurable as MiddleClickTime.  Default is 100ms.

- Implemented a new option in the Info.plist for the trackpad: ImmediateClick.  Set by default to false. If true, it changes the behavior of clicking and dragging with tap and double-tap hold.  Prior to this option, a tap does not register its button up until the double click time has passed.  For some actions and clicking on some buttons (when the application does the button action after the mouse button up has been received), this made the touchpad clicks seem sluggish.  The reason this was necessary was to make the double-tap-hold for drag to work… to make it work, the button is held for the duration of the double click time, thus the delay for the button up.  If this was not done, certain drag operations wouldn't work because of the way the system is written.  Some drag operations do not start on a double-click.  For example, dragging a window will not start if it is on a double click.  You can try this with a mouse (double-click-hold, then try to drag -- it doesn't take).  That's why the original code holds that button even though you've already completed the click with the first tap: it had to otherwise the next tap as part of the double-tap-hold sequence would be seen as a double-click and dragging the title bar wouldn't work.  OS X is very inconsistent here with this.  For example, you can double-click-drag a scroll bar.  When ImmediateClick is set to true, after you complete the tap the button will immediately be reported as up (it is only down for a short time).  In order to make double-tap-hold still work for dragging windows on the desktop, the button down for the second tap (tap+hold really) is not sent until at least double click time has passed.  This means that dragging does not engage for a little bit after the double-tap-hold sequence is initiated.

- Internal: General cleanup, especially around manipulation of the command byte.


2013-02-04 v1.7.11 (beta)

- Fixed a bug, previously documented as a known issue, where some trackpads were unresponsive after waking up from sleep (Probook 4540s, for example).  The fix is to re-initialize the 8042 keyboard controller on wake from sleep and to initialize the keyboard first, mouse second after wake from sleep instead of the original opposite order.

- Fixed a bug, previously documented as a known issue, where if your trackpad was in absolute mode (using VoodooPS2Trackpad.kext) and you restarted without turning off the laptop after switching to using only the mouse driver (VoodooPS2Mouse.kext), the trackpad was not correctly reset into relative mode and as such it didn't work properly.  The same thing would happen on transitions from other operating systems (Windows or Ubuntu) and then booting into OS X using VoodooPS2Mouse.kext.

- Rarely, the keyboard and trackpad would stop working, especially just after logging in.  Since this is an intermittent problem, it is difficult to tell if this is fixed.  But it seemed to be getting worse lately.  And there is a lot more properties being set from the system in setParamProperties (because the drivers are responding to more and more settings available in System Preferences).  These property settings happen at login… to apply the user's preferences.  After looking at some sources for IOHIDSystem, I discovered Apple routes all work for setParamProperties through a command gate in order to synchronize on the work-loop thread.  This fix is now implemented.

- VoodooPS2Mouse.kext now will now send Synaptics specific data only when it is detected a Synaptics device.  This was an issue specifically with ActLikeTrackpad option in VoodooPS2Mouse.kext.


2013-01-29 v1.7.10 (beta)

- Fixed bugs in ClickPad support. Especially right click logic.

- Time from first touch to clicking "pad button" is now configurable for ClickPads.  Info.plist variable is ClickPadClickTime (Default is 300ms)

- It is possible again to build a 32-bit version, should it be needed.  I am still not providing 32-bit capability with the official builds.


2013-01-27 v1.7.9 (beta)
- Added capability to scale interleaved PS/2 "passthrough" packets to scale the resolution up to the trackpad resolution.  See MouseMultiplierX and MouseMultiplierY in the trackpad's Info.plist

- Modifier key(s) used for "temporary drag lock" feature is now configurable (previous release it was hardcoded to control).  This is controlled by DragLockTempMask in the trackpad Info.plist.  Set to 262148 for control key, 524296 for command (alt) key, and 1048592 for option (windows) key.  Please note the default configuration of the keyboard Info.plist has the command and option swapped, so in that case, it is 1048592 for option (windows) key, and 524296 for the command (alt) key.

- Swipe Up, Down, Left, Right are now assigned by default to the following keyboard combinations: Control+Command+UpArrow, Control+Command+DownArrow, Control+Command+LeftArrow, Control+Command+RightArrow.  This should work better with international keyboards.  You will need to use System Preferences -> Keyboard -> Keyboard Shortcuts to adjust to assign these keys to your desired actions.  If you were using three finger swipe left and right for back/forward in your web browser, you will need to reconfigure these actions via the Info.plist or use a program like KeyRemap4MacBook to remap the keys generated to the keys used by your browser for forward/back (that's what I plan to do).

- Implemented support for System Preferences -> Keyboard -> "Use All F1, F2, etc. keys as standard function keys."  Now it is possible to have the Fn+fkeys/fkeys swap honor this setting in System Preferences.  But to enable this feature, the Info.plist must contain two new items "Function Keys Standard" and "Function Keys Special"  If these items are present, then the option will be available in System Preferences -> Keyboard.  If not, the option is not available.  The format of these two keys is the same as "Custom PS2 Map" the difference being that "Function Keys Standard" is in effect when the option is checked, and "Function Keys Special" is invoked when the option is not checked.  The proper mapping is implemented for the Probook 4x30s in VoodooPS2Keyboard-RemapFN-Info.plist.  In "Function Keys Standard" the mapping is removed.  And in "Function Keys Special" fn+fkeys and fkeys are swapped.  Any laptop should be able to have support created for it by modifying these keys as long as the scan codes can be determined (Fn+fkeys scan codes vary between specific laptop models).

- Cleaned up keyboard debug messages to make it easier to create custom key mappings.  Eventually, the wiki on keyboard remapping will reflect this.

- Implemented support for changing the keyboard backlight on certain notebooks.  See this thread for further information: http://www.tonymacx86.com/mountain-lion-laptop-support/86141-asus-g73jh-keyboard-backlighting-working.html

- Implemented support for changing screen brightness via ACPI methods in the DSDT.  You need some understanding of ACPI to try this feature.


2013-01-24 v1.7.8 (beta)
- Added acceleration table as suggested by valko.  This makes small movements more precise and large movements quicker.

- Implemented "Momentum Scroll."  This allows scrolling to continue after you have released from the trackpad.  There is probably some work that could still be done here to make it match the feel of a real Mac, but I think it may be close.  Please provide feedback.  This feature is enabled by default, but you can turn it off in System Preferences -> Accessibility -> Mouse & Trackpad -> Trackpad Options.

- Implemented support for System Preferences -> Accessibility -> "Ignore built-in trackpad when mouse or wireless trackpad is present"  If set, the trackpad will be disabled when there is one or more USB mice plugged in.  You must install the VoodooPS2Daemon as described in the installation instructions for this to work.  This is also implemented for VoodooPS2Mouse.kext if ActLikeTrackpad is set.

- Added a "temporary Drag Lock" feature.  If you enter Drag (double tap+hold) with the Command key down, it will be as if you had "Drag Lock" set in trackpad preferences, but just for that drag operation.  The drag is ended by tapping, just like normal drag lock.

- Added support for "middle button."  You can get a middle button click if you use three-finger tap.  This is enabled by setting ButtonCount to 3 in Info.plist.  If this causes an issue or you wish to disable it, set ButtonCount to 2 in the Info.plist.  In addition, if you wish to reverse the function of two-finger tap and three-finger tap, set SwapDoubleTriple to true in the Info.plist.

- Added support for Synaptics ClickPad(™).  These trackpads have a single button under the entire pad.  In order to make these trackpads usable, the trackpad must be placed into "Extended W Mode" which allows the driver to obtain data from both a primary and secondary finger.  Support for these trackpads should be considered experimental since it has only been tested via simulation with a Probook trackpad (which is not a ClickPad).  Let me know how/if it works.

- Key sequences for trackpad 3-finger swipes are now configurable in the keyboard driver Info.plist.  Any combination of keys can be sent.  Controlled by the following configuration items: ActionSwipeUp, ActionSwipeDown, ActionSwipeLeft, ActionSwipeRight.

- By default, the horizontal scroll area at the bottom and the vertical scroll area at the right are not enabled.  You can re-enable them by setting the HorizonalScrollDivisor and VerticalScrollDivisor to one (1).

- Fixed a bug where if the trackpad was "disabled" before a system restart, the LED remained lit.  The LED is now turned off at boot and during shutdown/restart.

- Separated Divisor in Info.plist to DivisorX and DivisorY.  This may help those of you with different trackpads than the Probook one.  For the Probook both of these variables are set to one (no adjustment).

- Started tweaking synapticsconfigload and the Preference Pane.  These features are not ready for general use yet, and therefore are not included in the binary distribution.

- For developers: Added makefile for command line builds, and added shared schemes to project to make it easier to build.


2012-11-29 v1.7.7
- Integrated the chiby/valko trackpad init code for non-HP Probook trackpads. This is for wake from sleep and cold boot initialize using undocumented trackpad commands determined by chiby by monitoring the communication line between the PC and the trackpad.  This should allow the trackpad driver to work with more models of trackpads.

- Integrated valko three-finger gesture code (with tweaks).  These 3-finger gestures are as follows:
  swipe left: sends keyboard Command+[
  swipe right: sends keyboard Command+]
  swipe up: sends keyboard F9
  swipe down: sends keyboard F10

A future version will allow these command mappings to be changed in the Info.plist.  In this version, they are hard-coded.

The amount of movement is controlled by SwipeDeltaX and SwipeDeltaY in the Info.plist.  The default for this version is 800.

So, Command+[, Command+] should correspond loosely to back/forward, respectively.  And F9 and F10 can be mapped to various functions (Launchpad, Show Desktop, Mission Control, etc.) by changing the assignments in the System Preferences -> Keyboard.  

- Changed the 2-finger scroll logic to allow two-fingers held very tightly, even with "ignore accidental trackpad input" turned on.  The trackpad driver sends bad data when the two fingers are held together like this (it sends it as one 'wide' finger).


2012-10-30 v1.7.6
- Changed the default value of MaxDragTime in Info.plist for trackpad.  Anything larger than 240ms will cause incorrect behavior with the Finder's forward and back buttons.  Changed it to 230ms to avoid this issue.


2012-10-20 v1.7.5
- Added default behaviors for Fn+F4, Fn+F5, Fn+F6.  Fn+F4 is "Video Mirror" -- it toggles display mirror mode.  Fn+F5 is Mission Control.  Fn+F6 is Launchpad.  These keys were previously unmapped by default (when no Custom ADB Map was present in Info.plist).

- In the debug version only, added the ability to generate any ADB code you want.  To do so, hold down Alt, then type the ADB code with the numpad number keys (0-9). The resulting code is sent after you release the Alt key.  This was how I discovered the ADB code for the Launchpad is 0x83 (Alt-down, 1, 3, 1, Alt-up).

- "Just for fun"... implemented three finger salute.

- Fixed a bug where key repeat for keys with extended scan codes (those that start with e0) may not have been repeating properly.  This bug was introduced when the keyboard mapping feature was added.

- Made scrolling (both multi-finger and single-finger) much, much smoother.

- Allow transitions from horizontal scrolling to vertical scrolling without falling into "move" mode.  This allows you to horizontally scroll right across the bottom of the pad, and onto the bezel, then returning back onto the pad (without lifting your finger) to resume horizontal scrolling.  Although not very useful, you can also horizontally scroll into the lower left corner, then move up to vertically scroll in the right margin.  The previous version would "lose" the scroll mode when moving off the right side or the horizontal scroll zone (because upon reentry, it would enter vertical scroll mode and not be able to resume horizontal scroll mode upon entering the horizontal scroll margin area).

- Fixed a bug where trackpad input/pointer position would demonstrate a slight glitch when changing the trackpad configuration in System Preferences.

- Added ability to disable/enable trackpad by double tapping on the upper left corner.  The area of the trackpad that is used for this function is configurable in the Info.plist.  By default (DisableZoneControl=0) this feature is enabled if your trackpad has an LED.  You can disable this feature by setting DisableZoneControl=-1 in Info.plist.  You can enable this feature for trackpads that don't have an LED by setting DisableZoneControl=1.

- Added a smoothing algorithm to process the input from the trackpad.  Still experimenting with this to tweak the parameters, but it is coming along.  This is controlled by two Info.plist settings: SmoothInput and UnsmoothInput.  By default, the trackpad itself does a little smoothing on its own (1:2 decaying average).  If you set the UnsmoothInput option, it will undo the action the trackpad is implementing (a decaying average can be mathematically reversed).  If you set SmoothInput, a simple average with a history of three samples is used.  By default, both UnsmoothInput and SmoothInput are set.

- Added a movement threshold for tap to left click and two-finger tap to right click.  For left clicks the threshold is 50. So if while tapping, you move more than 50, the tap is not converted to a click.  The threshold for right clicks is 100 as there tends to be more movement detected from the trackpad hardware with a two finger tap. These values can be adjusted in Info.plist.  This was mainly put in place to avoid accidental entry into drag mode when rapidly moving (with multiple quick swipes across the trackpad).

- Palm rejection/accidental input now honors system trackpad preferences setting "Ignore Accidental Trackpad Input", so you can turn it off.  I would not recommend turning it off.  The system actually sets three different options when you enable this option in System Preferences ("PalmNoAction While Typing", "PalmNoAction Permanent", and "OutsidezoneNoAction When Typing").  The Trackpad code pays attention to each one separately, although they are all set or cleared as a group.  Perhaps there is some command line way of setting them individually.

- Implements a defined zone in the left and right margins (and potentially top and bottom) where input is ignored while typing (see Zone* in Info.plist).  This is enabled if you "Ignore Accidental Trackpad Input"

- Modifier keys going down are ignored as far as determining timing related to accidental input.  This allows you to position the pointer with the trackpad over something you want to click on (say a website URL) and then hold Ctrl (or other modifier) then tap to click.  This is only for keydown events and only for shift, control, alt(command), and windows(option) keys.

- Trackpad code now determines automatically if your Trackpad has an LED and disables turning on/off the LED if it isn't present.

- Trackpad code now determines automatically if your Trackpad has pass through support and enables pass through only if the guest PS/2 device is present.  This avoids bad things happening (mouse buttons getting stuck down) if a non-pass through trackpad sends pass through packets.

- The Mouse driver in this version has minimal support for "Ignore Accidental Trackpad Input". In particular, it is able to ignore buttons and scrolling while typing.  Note that this means real buttons too, not just simulated buttons via tapping (since it is a mouse driver, it can't tell the difference).  This feature is only in effect if "ActLikeTrackpad" is set to Yes in Info.plist.

- You can make the Mouse driver act like a trackpad. If you set "ActLikeTrackpad" to Yes in the Info.plist, the mouse driver will enable trackpad like features.  This includes the Trackpad settings in System Preferences (although many options don't have an effect).  This allows you to turn on/off scrolling, as well as "Ignore Accidental Trackpad Input"

- There is also support for enabling and disabling the mouse (trackpad) with the keyboard, including support for the Synaptics LED.  You probably only want to set this if you actually have a Synaptics, as the code doesn't quite check properly.


2012-10-15 v1.7.4
- Implemented experimental support for pass through packets from a guest PS2 device.  These are 3-byte packets encapsulated in a 6-byte touchpad packet that are used to interleave input from a guest device such as a track point stick.  I don't have such a device, but I've implemented code to pass through the x & y deltas, as well as some logic to deal with merging the buttons from that device and the trackpad.

- Improved handling of two-finger tap for right-click.  There is still more to come in this area

- Improved ignoring of accidental tap to click, two-finger tap to click.

- Improved clicking on system menu when double click time is relatively long.

- Changed keyboard map to include functions for F4 and F5.  F4 is video mirror toggle.  F5 is dashboard.

- Integrated AppleACPIPS2Nub.kext into VoodooPS2Controller.kext. 
(IMPORTANT: This means AppleACPIPS2Nub.kext is not required and MUST be removed for a properly working system).


2012-10-13 v1.7.3
- OOPS bug: Now the sleep button timeout really works.

- Bug fix: VoodooPS2Mouse.kext now works… At least it does with the HP Touchpad.  There was a problem with the way the mouse was being initialized.  Hopefully that doesn't break regular PS/2 mice.  This feature might be useful for people who can't use the touchpad driver for one reason or another.  In the near future, I plan to implement some of the "ignore accidental input" features into this driver.


2012-10-13 v1.7.2
- Add capability to make custom keyboard maps.  Both ps2 to ps2 scan code mapping and ps2 to adb mapping is available by creating a simple table in Info.plist.  Eventually, I'll write a wiki explaining how custom keyboard maps work.

- Implement option in Info.plist to control how the sleep button works.  By setting SleepPressTime (expressed in milliseconds) to something other than zero, you can control how long the user must press the sleep button before the laptop enters sleep.  Proper function here depends on the scan code being an auto-repeat scan code.  So, if you assign a sleep time with the normal scan code table, you will have to press Fn+F1 for the time, then release (because it doesn't repeat).  This is primarily for use in conjunction with remapping the keyboard.  If you wanted to swap the Fn+Fkeys with Fkeys, for example, your sleep button would become F1.  Since it is very easy to hit F1 by accident (while reaching for Esc), you can use this option to keep from invoking the sleep function accidentally.

- Default layout of keys uses keyboard mapping to swap Fkeys for Fn+fkeys.

- Fixed the bug where if you had dragging enabled, and you tapped on a menu, the menu would go away most of the time.

- Improved detection of accidental input especially while typing.  Note: This is extremely new code and probably is not perfect.  Please report any problems you see. 

- Fixed a bug where turning on/off trackpad caused a bonk sound.

- Added support for Synaptics trackpad type 0x46 (normal is 0x47).  In this case, the LED setting will be disabled for wake from sleep.  But there is still more work to support this trackpad to come as time/information permits.


2012-10-11 v1.7.1:
- Fix problem with WiFi key (again).  I had it fixed once, but evidently regressed at some point.


2012-10-10 v1.7.0:
- Basics of palm rejection are implemented.  Still need to make the code pay attention to system settings

- Fn+Del(sysreq) is implemented as a way to toggle touchpad on/off
   (on 4x30s keyboards without numpad, it seems to be Fn+Insert(prtscrn))
   (this would include the 4230, 4330, and 4430)


2012-10-04 
- Initial copy/commit.


### History

This repository contains a modified VoodooPS2Controller.  Original sources came from this post on Insanely Mac:

http://www.insanelymac.com/forum/topic/236835-updated-2012-genericbrightnesskext/

The sources were pretty old at the time I pulled them (last modified 2009), so there were a few bug fixes they were missing (and could still be missing), but at this point it is the best source base I've found.  Current Voodoo sources, in my opinion, were not workable.  I couldn't make the Touchpad work at all, and the keyboard driver was doing very strange things (brightness causing computer to reboot).  They just didn't seem very close to the versions that we have been using for the HP ProBook.  Certainly it would be possible to reconcile these two versions, but after comparing one to the other, I couldn't see any reason to.

I have tried to make the initial commit of this code reasonably close to the sources which I downloaded from the link mentioned above.  That way it should be easy to see the progression forward.  The commits after that are a different story.  There is the occasional gratuitous change that I made while reviewing and attempting to understand the code.  That's what you get from a guy that is working for free.

Please note that I'm only testing this on my HP ProBook 4530s. Although it should work fine on other laptops with a Synaptics Trackpad, I haven't tested it on anything but my computer.  By the way, the HP ProBook's trackpad reports version 7.5.  Related to this is the fact that I haven't tested the "mouse only" part of this driver (ie. VoodooPS2Mouse.kext).  I don't have a desktop hack at this point, and even if I did, I don't have any desktop computers with PS/2 ports.

Also, there is a Preference Pane in here, and perhaps at one time it worked.  I haven't tested it.  Not at all.  It builds and that is all I know.  At some point, I will take a look at that, and perhaps add some features or at least make it work.

Documentation on the Synaptics hardware can be obtained (at least at the time I write this) from the Synaptics website:

http://www.synaptics.com/resources/developers

I based my code here on the "Synaptics PS/2 TouchPad Interfacing Guide, PN 511-000275-01 Rev.B"  I would include the document in the github repository here, but the document is copyrighted and I didn't want to ruffle any feathers.


### Future

My intention is to eventually enhance both the Synaptics Trackpad support as well as they keyboard to support the following features:


Touchpad:

- disable touchpad if USB mouse is plugged in and "Ignore built-in trackpad when mouse or wireless trackpad is present" in Accessibility settings in System Preferences.
  (DONE)

- calibrate movement/sensitivity to mouse
  (since they share the same config, it would be great not to have to adjust)
  (note: they are pretty close, but could be tweaked a bit)
  (DONE)

- investigate using extended-W mode
  (haven't done much here except read the spec)

- more gestures, as time permits (currently two-finger scrolling and three-finger swipe)

- implement touch pad on/off in upper left corner 
  (DONE)

- clean up IOLog and allow for more information in Debug version
  (DONE)

- if possible, implement LED indication for touchpad on/off
  (HP ProBook specific)
  (DONE)

- implement high resolution mode for Synaptics
  (may already be implemented but not enabled in Info.plist)
  (DONE -- this version seems smoother than the one we were using)

- implement palm rejection (accidental input)
  (DONE)

- investigate doing something to make movement smoother
  (implement some kind of decaying average to smooth spikes in the input stream)
  (DONE)

- implement a threshold of movement that will cancel a click drag
  (this would avoid unwanted drag detection)
  (one way to avoid this is for the user to set the fastest double click speed)
  (DONE)

- Fix bug where trying to open a Menu with a tap does not work: Menu opens, but
  most of the time immediately closes.
  (DONE)


Keyboard:

- Correct volume indications 
  (for some reason these are not working right now)
  (DONE)

- Make wireless key work for turning wireless on/off
  (HP ProBook specific)
  (DONE)

- Allow for some limited form of custom key mappings
  (instead of hardcoding scan codes for specific laptops)
  (DONE)

- Allow Fn+Fkeys to be swapped for FKeys (without Fn)
  (DONE -- use generic keyboard remapping above)


Mouse:

- Implement LED on/off for Synaptics touch pads operating as a PS2 mouse
  (DONE)

- Make the VoodooPS2Mouse.kext work for trackpads in mouse simulation mode. For some reason it arrived broken when I got the code.
  (DONE).

- Add "ignore input after typing" features to mouse driver.  A little weird to make for a real PS2 mouse, but super nice for laptops with trackpads operating in mouse simulation mode.
  (DONE)



PrefPane:

- Maybe test it and see if it works (it works, but there is a lot of options that don't make sense for Probook users)

- Also, it would be nice if preferences would stick across reboots... (this works via synapticsconfigload, but needs work)


### Original Credits

VoodooPS2Controller (core): turbo

Resolution fix for PS2Mouse: mackerintel

Synaptics driver: mackerintel

Sentelic Driver: nhand42

Alps driver: phb

Keyboard fixes: Chunnan & jape

Synaptics Prefpane design: bumby

Synpatics Prefpane: mackerintel

Great thanks to Dense for helping with activating vanilla trackpad prefpane
