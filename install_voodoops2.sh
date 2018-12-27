#!/bin/sh

set -x
cp -R /Users/iceman/Developer/OS-X-Voodoo-PS2-Controller/build/Products/Release/VoodooPS2Controller.kext /System/Library/Extensions/
sudo chown -R root:wheel /System/Library/Extensions/VoodooPS2Controller.kext
sudo chmod -R 755 /System/Library/Extensions/VoodooPS2Controller.kext