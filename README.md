## Modified VoodooPS2Controller by RehabMan

### How to Install:

It is important that you follow these instructions as it is not a good idea to have two different ApplePS2Controller.kexts under different names:

- remove /S/L/E/AppleACPIPS2Nub.kext (sudo rm -rf /System/Library/Extensions/AppleACPIPS2Nub.kext)
   (note: this is only for version 1.7.4 or greater)
- remove /S/L/E/ApplePS2Controller.kext (sudo rm -rf /System/Library/Extensions/ApplePS2Controller.kext)
- install VoodooPS2Controller.kext using your favorite Kext installer (Kext Wizard)
   (note: for versions prior to 1.7.4, you must install AppleACPIPS2Nub.kext as well)
- optional: rebuild permissions and kernel cache
- reboot


### Feedback:

Please use the following thread on tonymacx86.com for feedback, questions, and help:

http://www.tonymacx86.com/hp-probook/75649-new-voodoops2controller-keyboard-trackpad.html#post468941


### Change Log:

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
- Add capability to make custom keyboard maps.  Both ps2 to ps2 scan code mapping and ps2 to apple mapping is available by creating a simple table in Info.plist.  Eventually, I'll write a wiki explaining how custom keyboard maps work.
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

Please note that I'm only testing this on my HP ProBook 4530s. Although it should work fine on other laptops with a Synaptics Trackpad, I haven't tested it on anything but my computer.  By the way, the HP ProBook's trackpad reports version 7.5.  Related to this is the fact that I haven't tested the "mouse only" part of this driver (ie. VoodooPS2Mouse.kext).  I don't have a desktop hack at this point, and even if I did, I don't have any desktop computers with PS2 ports.

Also, there is a Pref Pane in here, and perhaps at one time it worked.  I haven't tested it.  Not at all.  It builds and that is all I know.  At some point, I will take a look at that, and perhaps add some features or at least make it work.

Documentation on the Synaptics hardware can be obtained (at least at the time I write this) from the Synaptics website:

http://www.synaptics.com/resources/developers

I based my code here on the "Synaptics PS/2 TouchPad Interfacing Guide, PN 511-000275-01 Rev.B"  I would include the document in the github repository here, but the document is copyrighted and I didn't want to ruffle any feathers.

### Future

My intention is to eventually enhance both the Synaptics Trackpad support as well as they keyboard to support the following features:

Touchpad:

- clean up IOLog and allow for more information in Debug version
  (DONE)

- if possible, implement LED indication for touchpad on/off
  (HP ProBook specific)
  (DONE)

- implement high resolution mode for Synaptics
  (may already be implemented but not enabled in Info.plist)
  (DONE -- this version seems smoother than the one we were using)

- implement palm rejection (accidental input)
  (DONE with caveat: partially implemented… still a bit of work to do here)

- implement touch pad on/off in upper left corner 
  (somewhat HP ProBook specific)

- investigate using extended-W mode
  (haven't done much here except read the spec)

- investigate doing something to make movement smoother
  (implement some kind of decaying average to smooth spikes in the input stream)

- implement a threshold of movement that will cancel a click drag
  (this would avoid unwanted drag detection)
  (one way to avoid this is for the user to set the fastest double click speed)

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

- Make the VoodooPS2Mouse.kext work for trackpads in mouse simulation mode. For some reason it arrived broken when I got the code.
  (DONE).

- Add "ignore input after typing" features to mouse driver.  A little weird to make for a real PS2 mouse, but super nice for laptops with trackpads operating in mouse simulation mode.



PrefPane:

- Maybe test it and see if it works

- Also, it would be nice if preferences would stick across reboots...


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
