# really just some handy scripts...

.PHONY: all
all:
	xcodebuild $(OPTIONS) -scheme All -configuration Debug
	xcodebuild $(OPTIONS) -scheme All -configuration Release

.PHONY: clean
clean:
	xcodebuild -scheme All -configuration Debug clean $(OPTIONS)
	xcodebuild -scheme All -configuration Release clean $(OPTIONS)

.PHONY: update_kernelcache
update_kernelcache:
	sudo touch /System/Library/Extensions
	sudo kextcache --system-prelinked-kernel -arch x86_64

.PHONY: rehabman_special_settings
rehabman_special_settings:
	sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:Synaptics TouchPad:Platform Profile:Default:DragLockTempMask' 262148" /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext/Contents/Info.plist
	sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:Synaptics TouchPad:Platform Profile:HP:ProBook:FingerZ' 47" /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext/Contents/Info.plist

.PHONY: install_debug
install_debug:
	sudo cp -R ./Build/Products/Debug/VoodooPS2Controller.kext /System/Library/Extensions
	make rehabman_special_settings
	sudo cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist /Library/LaunchDaemons
	sudo cp ./Build/Products/Debug/VoodooPS2Daemon /usr/bin
	make update_kernelcache

.PHONY: install
install: install_kext install_daemon

.PHONY: install_kext
install_kext:
	sudo cp -R ./Build/Products/Release/VoodooPS2Controller.kext /System/Library/Extensions
	make rehabman_special_settings
	make update_kernelcache

.PHONY: install_mouse
install_mouse:
	sudo cp -R ./Build/Products/Release/VoodooPS2Controller.kext /System/Library/Extensions
	sudo rm -r /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext
	sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:ApplePS2Mouse:Platform Profile:HP:ProBook:DisableDevice' No" /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Mouse.kext/Contents/Info.plist
	make update_kernelcache

.PHONY: install_mouse_debug
install_mouse_debug:
	sudo cp -R ./Build/Products/Debug/VoodooPS2Controller.kext /System/Library/Extensions
	sudo rm -r /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext
	sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:ApplePS2Mouse:Platform Profile:HP:ProBook:DisableDevice' No" /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Mouse.kext/Contents/Info.plist
	make update_kernelcache

.PHONY: install_daemon
install_daemon:
	sudo cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist /Library/LaunchDaemons
	sudo cp ./Build/Products/Release/VoodooPS2Daemon /usr/bin

install.sh: makefile
	make -n install >install.sh
	chmod +x install.sh

.PHONY: distribute
distribute:
	if [ -e ./Distribute ]; then rm -r ./Distribute; fi
	mkdir ./Distribute
	cp -R ./Build/Products/ ./Distribute
	find ./Distribute -path *.DS_Store -delete
	find ./Distribute -path *.dSYM -exec echo rm -r {} \; >/tmp/org.voodoo.rm.dsym.sh
	chmod +x /tmp/org.voodoo.rm.dsym.sh
	/tmp/org.voodoo.rm.dsym.sh
	rm /tmp/org.voodoo.rm.dsym.sh
	cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist ./Distribute/
	rm -r ./Distribute/Debug/VoodooPS2synapticsPane.prefPane
	rm -r ./Distribute/Release/VoodooPS2synapticsPane.prefPane
	rm ./Distribute/Debug/synapticsconfigload
	rm ./Distribute/Release/synapticsconfigload
	mkdir ./Distribute/ProBook
	cp ./VoodooPS2Keyboard/VoodooPS2Keyboard-RemapFN-Info.plist ./Distribute/ProBook/Keyboard-Info.plist
	cp ./VoodooPS2Trackpad/VoodooPS2Trackpad-Info.plist ./Distribute/ProBook/Trackpad-Info.plist
	/usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:Synaptics TouchPad:Platform Profile:Default:FingerZ' 40" ./Distribute/ProBook/Trackpad-Info.plist
	ditto -c -k --sequesterRsrc --zlibCompressionLevel 9 ./Distribute ./Archive.zip
	mv ./Archive.zip ./Distribute/`date +RehabMan-Voodoo-%Y-%m%d.zip`
