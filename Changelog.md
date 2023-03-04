VoodooPS2 Changelog
============================
#### v2.3.4
- Fixed device count detection when `ps2rst=0` is set.

#### v2.3.3
- Fixed rapidly opening pages in browsers while scrolling with the trackpoint
- Fixed buttons on various trackpads (especially those without trackpoints attached)
- Fixed DynamicEWMode problem on Lenovo ThinkPad Laptops (acidanthera/bugtracker#890)

#### v2.3.2
- Added `ps2kbdonly=1` argument not to disable touchpad line on reboot, thx @Kethen

#### v2.3.1
- Fixed disabled keyboard after reboot

#### v2.3.0
- Fixed variable shadowing

#### v2.2.9
- Improved stability of ALPS touchpads
- V8 touchpads can pass all four fingers to VoodooInput natively
- Fixed unpressing during 3 fingers gesture on ALPS V7
- Use VoodooTrackpoint for trackstick and non-MT ALPS touchpads

#### v2.2.8
- Added ALPS touchpad support
- Fixed ELAN trackpoint scrolling in the wrong direction

#### v2.2.7
- Fixed kernel panic after S3

#### v2.2.6
- Fixed some touchpads not waking after S3

#### v2.2.5
- Added support for touchpads with multiplexors

#### v2.2.4
- Fixed incorrect command gate initialization causing panics

#### v2.2.3
- Added `DisableDeepSleep` to workaround ACPI S3 wakes on some Synaptics touchpads

#### v2.2.2
- Added NumLockSupport & NumLockOnAtBoot

#### v2.2.1
- Fix issue with registering of services matched by property name "RM,deliverNotifications". It solves issue with broadcasting timestamp for the last pressed key and handling of QuietTimeAfterTyping [see bug #1415](https://github.com/acidanthera/bugtracker/issues/1415) 

#### v2.2.0
- Added VoodooRmi compatibility to allow external touchpad resets

#### v2.1.9
- Disabled PrntScr remap by default, see `SSDT-PrtSc-Remap.dsl` for example to re-enable it
- Disabled Command and Option remap by default, see `SSDT-Swap-CommandOption.dsl` for example to re-enable it

#### v2.1.8
- Added support for receiving input form other kexts
- Fixed dynamic coordinate refresh for ELAN v3 touchpads

#### v2.1.7
- Initial MacKernelSDK and Xcode 12 compatibility
- Added support for select ELAN touchpads by BAndysc and hieplpvip
- Added constants for 11.0 support

#### v2.1.6
- Upgraded to VoodooInput 1.0.7
- Fixed swiping desktops when holding a dragged item by improving thumb detection
- Fixed keyboard timeout error on some laptop configurations
- Fix Command key being pressed after disabling the keyboard and trackpad with Command-PrtScr key combo
- Added a message to allow other kexts to disable the keyboard

#### v2.1.5
- Upgraded to VoodooInput 1.0.6
- Added logo + print scr hotkey to disable trackpad

#### v2.1.4
- Upgraded to VoodooInput 1.0.5
- Improved Smart Zoom and four finger gestures
- Enabled Extended W mode on all trackpads that support it
- Added ability for one special key to block trackpad longer than other keys
- Added a new Force Touch emulation mode

#### v2.1.3
- Added `ps2rst=0` boot-arg for select CFL laptop compatibility
- Added compatibility with VoodooInput 1.0.4
- VoodooInput is now bundled with VoodooPS2

#### v2.1.2
- Fixed initialisation race conditions causing kernel panics

#### v2.1.1
- Fixed kext unloading causing kernel panics
- Fixed Caps Lock LED issues (thx @Goshin)

#### v2.1.0
- Improved reliability of three-finger dragging
- Improved reliability of finger renumbering algorithm
- Support for four-finger swipes
- Experimental support for four-finger pinch and spread gestures (very unstable)
- Magic Trackpad 2 simulation moved to VoodooInput project

#### v2.0.4
- Patched CapsLock workaround for Mojave

#### v2.0.3
- Use touchpad sleep mode during mainboard sleep
- Improve physical and trackpad button support

#### v2.0.2
- Unified release archive names
- Initial workaround to rare kernel panics
- Rebranding and conflict resolution with other drivers
- Improve reliability of edge swipe gestures
- Support macOS 10.11 to 10.15 beta
- Support Xcode 11 beta
- Support clicking with multiple fingers
- Fix bugs in finger renumbering algorithm
- Ignore hovering above the touchpad
- \[Temporary\] Support overriding touchpad dimensions for old touchpads that don't report minimum coordinates

Change of earlier versions may be tracked in [commits](https://github.com/acidanthera/VoodooPS2/commits/master) and [releases](https://github.com/acidanthera/VoodooPS2/releases).
