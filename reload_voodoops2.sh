#!/bin/sh

set -x
cp /Users/iceman/Developer/OS-X-Voodoo-PS2-Controller/build/Products/Release/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext/Contents/MacOS/VoodooPS2Trackpad /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext/Contents/MacOS/VoodooPS2Trackpad 
cp /Users/iceman/Developer/OS-X-Voodoo-PS2-Controller/build/Products/Release/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Keyboard.kext/Contents/MacOS/VoodooPS2Keyboard /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Keyboard.kext/Contents/MacOS/VoodooPS2Keyboard 
kextunload /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext
kextload /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext

kextunload /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Keyboard.kext
kextload /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Keyboard.kext