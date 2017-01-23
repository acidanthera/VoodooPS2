### VoodooPS2Controller for XPS 13 Series


### How to Install:

Please read and follow the important instructions for installing in the wiki:

https://github.com/RehabMan/OS-X-Voodoo-PS2-Controller/wiki/How-to-Install


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


### Fun Facts:

While implementing the just for fun feature in the keyboard driver where Ctrl+Alt+Del maps to the power key (for selection of Restart, Sleep, Shutdown), I discovered that if you invoke this function with the Ctrl and Alt (Command) keys down, the system will do an abrupt and unsafe restart.  You can verify this yourself by holding down the Ctrl and Alt keys while pressing the actual power button.


### Change Log:

2017-01-23

- 3f swipe left/right: show previous/next page
- 3f swipe up: mission control
- 4f swipe left/right: move right/left a window
- 4f swipe up: show desktop
- 4f swipe down: mininize front window to dock
- improve threshold to make swipe better
- Initial copy/commit base on @icedman


### Credits

Multi gesture(4f swipe, edge swipe, zoom in/out, pinch): @icedman

New Apple TouchPad PrefPane & CapsLock fix: @usr-sse2

VoodooPS2Controller (core): turbo

Resolution fix for PS2Mouse: mackerintel

Synaptics driver: mackerintel

Sentelic Driver: nhand42

Alps driver: phb

Keyboard fixes: Chunnan & jape

Synaptics Prefpane design: bumby

Synpatics Prefpane: mackerintel

Great thanks to Dense for helping with activating vanilla trackpad prefpane
