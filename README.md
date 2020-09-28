VoodooPS2
=========

[![Build Status](https://travis-ci.com/acidanthera/VoodooPS2.svg?branch=master)](https://travis-ci.com/acidanthera/VoodooPS2)

New **VoodooPS2Trackpad** uses VoodooInput's Magic Trackpad II emulation in order to use macOS native driver instead of handling all gestures itself. This enables the use of any from one to four finger gestures defined by Apple including:
* Look up & data detectors
* Secondary click (*with two fingers, in bottom left corner\*, in bottom right corner\**)
* Tap to click
* Scrolling
* Zoom in or out
* Smart zoom
* Rotate
* Swipe between pages
* Swipe between full-screen apps (*with three or four fingers*)
* Notification Centre
* Mission Control (*with three or four fingers*)
* App Exposé (*with three or four fingers*)
* Dragging with or without drag lock (*configured in 'Accessibility'/'Universal Access' prefpane*)
* Three finger drag (*configured in 'Accessibility'/'Universal Access' prefpane, may work unreliably\*\**)
* Launchpad (*may work unreliably*)
* Show Desktop (*may work unreliably*)
* Screen zoom (*configured in 'Accessibility'/'Universal Access' -> Zoom -> Advanced -> Controls -> Use trackpad gesture to zoom*)

It also supports **BetterTouchTool**.

In addition this kext supports **Force Touch** emulation (*configured in `Info.plist`*):
* **Mode 0** – Force Touch emulation is disabled (*you can also disable it in **System Preferences** without setting the mode*).
* **Mode 1** – Force Touch emulation using a physical button: on ClickPads (touchpads which have the whole surface clickable (the physical button is inside the laptop under the bottom of touchpad)), the physical button can be remapped to Force Touch. In such mode a tap is a regular click, if **Tap to click** gesture is enabled in **System Preferences**, and a click is a Force Touch. This mode is convenient for people who usually tap on the touchpad, not click.
* **Mode 2** – *'wide tap'*: for Force Touch one needs to increase the area of a finger touching the touchpad\*\*\*. The necessary width can be set in `Info.plist`.
* **Mode 3** – pressure value is passed to the system as is; this mode shouldn't be used.
* **Mode 4** (*by @Tarik02*) – pressure is passed to the system using the following formula: ![formula](Docs/force_touch.png)  
The parameters in the formula are configured using `ForceTouchCustomUpThreshold`, `ForceTouchCustomDownThreshold` and `ForceTouchCustomPower` keys in `Info.plist` or configuration SSDT. Note that `ForceTouchCustomDownThreshold` is the *upper* limit on the pressure value and vice versa, because it corresponds to the touchpad being fully pressed *down*.

For Elan touchpad, only mode 0 and mode 1 are supported.

## Installation and compilation

For VoodooPS2Trackpad.kext to work multitouch interface engine, named VoodooInput.kext, is required.

- For released binaries a compatible version of VoodooInput.kext is already included in the PlugIns directory.
- For custom compiled versions VoodooInput.kext bootstrapping is required prior to compilation.
    By default Xcode project will do this automatically. If you prefer to have your own control over the
    process execute the following command in the project directory to have VoodooInput bootstrapped:

    ```
    src=$(/usr/bin/curl -Lfs https://raw.githubusercontent.com/acidanthera/VoodooInput/master/VoodooInput/Scripts/bootstrap.sh) && eval "$src" || exit 1
    ```

## Touchpad and Keyboard Input Toggle

This kext supports disabling touch input by pressing the Printscreen key on your keyboard, or the touchpad disable key on many laptops.  Simply press the key to toggle touchpad input off and again to toggle it back on.

In addition, for 2-in-1 systems that do not support disabling the keyboard in hardware while in tablet mode you may toggle keyboard input off and on by holding option(Windows) and pressing the Printscreen key.  Repeat the keypress to re-enable keyboard input.  These settings are runtime only and do not persist across a reboot.

## Credits:
* VoodooPS2Controller etc. – turbo, mackerintel, @RehabMan, nhand42, phb, Chunnan, jape, bumby (see RehabMan's repository).
* Magic Trackpad 2 reverse engineering and implementation – https://github.com/alexandred/VoodooI2C project team.
* VoodooPS2Trackpad integration – @kprinssu.
* Force Touch emulation and finger renumbering algorithm** – @usr-sse2.
* Elan touchpad driver – linux kernel contributors, @kprinssu, @BAndysc and @hieplpvip

\* On my touchpad this gesture was practically impossible to perform with the old VoodooPS2Trackpad. Now it works well.

\*\* Due to the limitations of PS/2 bus, Synaptics touchpad reports only the number of fingers and coordinates of two of them to the computer. When there are two fingers on the touchpad and third finger is added, a 'jump' may happen, because the coordinates of one of the fingers are replaced with the coordinates of the added finger. Finger renumbering algorithm estimates the distance from old coordinates to new ones in order to hide this 'jump' from the OS ~~and to calculate approximate position of the 'hidden' finger, in assumption that fingers move together in parallel to each other~~. Now third and fourth fingers are reported at the same position as one of the first two fingers. It allows Launchpad/Show desktop gesture to work more reliably.

\*\*\* The touchpad reports both finger width (ranged from 4 to 15) and pressure (ranged from 0 to 255), but in practice the measured width is almost always 4, and the reported pressure depends more on actual touch width than on actual pressure.
