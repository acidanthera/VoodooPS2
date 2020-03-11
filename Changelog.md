VoodooPS2 Changelog
============================
#### v2.1.3
- Added `ps2rst=0` boot-arg for select CFL laptop compatibility

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
