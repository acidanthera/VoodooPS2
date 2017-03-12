### VoodooPS2Controller for XPS 13 Series

<<<<<<< HEAD
### How to use?
=======
2017-01-24

swiping threshold is decreased
improved two finger tap (for right click simulation)
added a few more gestures. TrackpadRightClick must be set to false

* Action2FingersTap
* Action3FingersTap
* Action4FingersTap

2017-01-24

swiping threshold is decreased
improved two finger tap (for right click simulation)
added a few more gestures. TrackpadRightClick must be set to false

- Action2FingersTap
- Action3FingersTap
- Action4FingersTap

2017-01-23

added these new gestures
<<<<<<< HEAD

using four fingers

* ActionSwipe4FingersUp
* ActionSwipe4FingersDown
* ActionSwipe4FingersLeft
* ActionSwipe4FingersRight
* Action4FingersSpread
* Action4FingersPinch

three fingers

* Action3FingersSpread
* Action3FingersPinch
=======

using four fingers

- ActionSwipe4FingersUp
- ActionSwipe4FingersDown
- ActionSwipe4FingersLeft
- ActionSwipe4FingersRight
- Action4FingersSpread
- Action4FingersPinch

three fingers

- Action3FingersSpread
- Action3FingersPinch
>>>>>>> icedman/master
 .. in addition to 3 fingers swipe up, down, left, right

two fingers

<<<<<<< HEAD
* ActionZoomIn
* ActionZoomOut
* ActionSwipeFromEdge
* ActionSwipeDownFromEdge
* ActionSwipeLeftFromEdge
* ActionSwipeRightFromEdge
=======
- ActionZoomIn
- ActionZoomOut
- ActionSwipeFromEdge
- ActionSwipeDownFromEdge
- ActionSwipeLeftFromEdge
- ActionSwipeRightFromEdge
>>>>>>> icedman/master


2016-12-16

Integrated 3 finger drag from tluck's fork

2016-12-15

* Fix jumpy pointer
* Smoother horizontal scrolling with momentum

## Modified VoodooPS2Controller by RehabMan


### How to Install:

Please read and follow the important instructions for installing in the wiki:

https://github.com/RehabMan/OS-X-Voodoo-PS2-Controller/wiki/How-to-Install


### Downloads:

Downloads are available on Bitbucket:

https://bitbucket.org/RehabMan/os-x-voodoo-ps2-controller/downloads

Note: Archived (old) downloads are available on Google Code:

https://code.google.com/p/os-x-voodoo-ps2-controller/downloads/list
>>>>>>> icedman/master

TO-Do

### Build Environment

My build environment is currently Xcode 8.2.1, using SDK 10.8, targeting OS X 10.12

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
