## Modified VoodooPS2Controller by RehabMan


### How to Install:

It is important that you follow these instructions as it is not a good idea to have two different ApplePS2Controller.kexts under different names:

- remove /S/L/E/AppleACPIPS2Nub.kext (note: this is only for version 1.7.4 or greater)

		sudo rm -rf /System/Library/Extensions/AppleACPIPS2Nub.kext
- remove /S/L/E/ApplePS2Controller.kext

		sudo rm -rf /System/Library/Extensions/ApplePS2Controller.kext
- install VoodooPS2Controller.kext using your favorite Kext installer (Kext Wizard) (note: for versions prior to 1.7.4, you must install AppleACPIPS2Nub.kext as well)
- starting with version 1.7.8, install the VoodooPS2Daemon 

		sudo cp org.rehabman.voodoo.driver.Daemon.plist /Library/LaunchDaemons
		sudo cp VoodooPS2Daemon /usr/bin

- optional: rebuild permissions and kernel cache
- reboot

Download link: https://github.com/RehabMan/OS-X-Voodoo-PS2-Controller/downloads


### Feedback:

Please use the following thread on tonymacx86.com for feedback, questions, and help:

http://www.tonymacx86.com/hp-probook/75649-new-voodoops2controller-keyboard-trackpad.html#post468941


### Fun Facts:

While implementing the "just for fun" feature in the keyboard driver where Ctrl+Alt+Del maps to the power key (for selection of Restart, Sleep, Shutdown), I discovered that if you invoke this function with the Ctrl and Alt (Command) keys down, the system will do an abrupt and unsafe restart.  You can verify this yourself by holding down the Ctrl and Alt keys while pressing the actual power button.


### Known issues:

- If your trackpad is in absolute mode (you were using VoodooPS2Trackpad.kext) and you restart without turning off the laptop after switching to VoodooPS2Mouse.kext (that is, after removing VoodooPS2Trackpad.kext), the trackpad is not correctly reset into relative mode.  This means it doesn't work right at all.  As a work around, turn the computer off completely.  I suspect the same thing happens if you have OS X using VoodooPS2Mouse.kext and you boot into Windows or Ubuntu, then restart back into OS X.

- If you set ActLikeTrackpad=Yes for VoodooPS2Mouse, things might not go so well on a non-Synaptics trackpad.  To work around this issue, set DisableLEDUpdating=Yes.

- Some Trackpads have an issue waking up from sleep (some Probook 4540s, for example).  If you have this issue, and have some dev experience, please experiment with the code and see if you can come up with a solution.

- Very rarely, both the keyboard and trackpad are not working after a fresh boot or after sleep, even on systems where this is normally not a problem.


### Change Log:

future release v1.7.9
- Added capability to scale interleaved PS/2 "passthrough" packets to scale the resolution up to the trackpad resolution.  See MouseMultiplierX and MouseMultiplierY in the trackpad's Info.plist

- Modifier key(s) used for "temporary drag lock" feature is now configurable (previous release it was hardcoded to control).  This is controlled by DragLockTempMask in the trackpad Info.plist.  Set to 262148 for control key, 524296 for command (alt) key, and 1048592 for option (windows) key.  Please note the default configuration of the keyboard Info.plist has the command and option swapped, so in that case, it is 1048592 for option (windows) key, and 524296 for the command (alt) key.


2012-01-24 v1.7.8
- Added acceleration table as suggested by valko.  This makes small movements more precise and large movements quicker.

- Implemented "Momentum Scroll."  This allows scrolling to continue after you have released from the trackpad.  There is probably some work that could still be done here to make it match the feel of a real Mac, but I think it may be close.  Please provide feedback.  This feature is enabled by default, but you can turn it off in System Preferences -> Accessibility -> Mouse & Trackpad -> Trackpad Options.

- Implemented support for System Preferences -> Accessibility -> "Ignore built-in trackpad when mouse or wireless trackpad is present"  If set, the trackpad will be disabled when there is one or more USB mice plugged in.  You must install the VoodooPS2Daemon as described in the installation instructions for this to work.  This is also implemented for VoodooPS2Mouse.kext if ActLikeTrackpad is set.

- Added a "temporary Drag Lock" feature.  If you enter Drag (double tap+hold) with the Control key down, it will be as if you had "Drag Lock" set in trackpad preferences, but just for that drag operation.  The drag is ended by tapping, just like normal drag lock.

- Added support for "middle button."  You can get a middle button click if you use three-finger tap.  This is enabled by setting ButtonCount to 3 in Info.plist.  If this causes an issue or you wish to disable it, set ButtonCount to 2 in the Info.plist.  In addition, if you wish to reverse the function of two-finger tap and three-finger tap, set SwapDoubleTriple to true in the Info.plist.

- Added support for Synaptics ClickPad(™).  These trackpads have a single button under the entire pad.  In order to make these trackpads usable, the trackpad must be placed into "Extended W Mode" which allows the driver to obtain data from both a primary and secondary finger.  Support for these trackpads should be considered experimental since it has only been tested via simulation with a Probook trackpad (which is not a ClickPad).  Let me know how/if it works.

- Key sequences for trackpad 3-finger swipes are now configurable in the keyboard driver Info.plist.  Any combination of keys can be sent.  Controlled by the following configuration items: ActionSwipeUp, ActionSwipeDown, ActionSwipeLef, ActionSwipeRight.

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
