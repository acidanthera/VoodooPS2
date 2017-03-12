#!/bin/sh

ioio=/usr/local/bin/ioio

$ioio -s ApplePS2SynapticsTouchPad Dragging 1
$ioio -s ApplePS2SynapticsTouchPad DragExitDelayTime 500000000
# $ioio -s ApplePS2SynapticsTouchPad DragExitDelayTime 250000000
$ioio -s ApplePS2Keyboard "Swap command and option" true

 #3 finger gestures are disabled when three-finger drag is on
# avoid using with three finger drag
$ioio -s ApplePS2Keyboard Action3FingersSpread :""
$ioio -s ApplePS2Keyboard Action3FingersPinch :""

# cmd-left/right
$ioio -s ApplePS2Keyboard ActionSwipeLeft :"" #37 d, 1e d, 1e u, 37 u"
$ioio -s ApplePS2Keyboard ActionSwipeRight :"" #37 d, 21 d, 21 u, 37 u"
$ioio -s ApplePS2Keyboard ActionSwipeUp :""
$ioio -s ApplePS2Keyboard ActionSwipeDown :""

# ctrl-right/left/up/down
$ioio -s ApplePS2Keyboard ActionSwipe4FingersLeft :"3b d, 7c d, 7c u, 3b u"
$ioio -s ApplePS2Keyboard ActionSwipe4FingersRight :"3b d, 7b d, 7b u, 3b u"
$ioio -s ApplePS2Keyboard ActionSwipe4FingersUp :"3b d, 7e d, 7e u, 3b u"
$ioio -s ApplePS2Keyboard ActionSwipe4FingersDown :"3b d, 7d d, 7d u, 3b u"

# not ready
$ioio -s ApplePS2Keyboard Action4FingersSpread :""
$ioio -s ApplePS2Keyboard Action4FingersPinch :""

# cmd-space
$ioio -s ApplePS2Keyboard Action2FingersTap :"" # implementation not ready
$ioio -s ApplePS2Keyboard Action3FingersTap :"" #37 d, 31 d, 31 u, 37 u"
$ioio -s ApplePS2Keyboard Action4FingersTap :"37 d, 31 d, 31 u, 37 u"

$ioio -s ApplePS2SynapticsTouchPad TrackpadRightClick 1
$ioio -s ApplePS2SynapticsTouchPad TrackpadMiddleClick 1

$ioio -s ApplePS2SynapticsTouchPad TrackpadThreeFingerDrag 1

$ioio -s ApplePS2Keyboard LogScanCodes 0

