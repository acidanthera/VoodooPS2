# really just some handy scripts...

.PHONY: all
all:
	xcodebuild -scheme All -configuration Debug
	xcodebuild -scheme All -configuration Release

.PHONY: clean
clean:
	xcodebuild -scheme All -configuration Debug clean
	xcodebuild -scheme All -configuration Release clean

.PHONY: install_debug
install_debug:
	sudo cp -R ./Build/Products/Debug/VoodooPS2Controller.kext /System/Library/Extensions
	sudo touch /System/Library/Extensions
	sudo cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist /Library/LaunchDaemons
	sudo cp ./Build/Products/Debug/VoodooPS2Daemon /usr/bin

.PHONY: install
install: install_kext install_daemon

.PHONY: install_kext
install_kext:
	sudo cp -R ./Build/Products/Release/VoodooPS2Controller.kext /System/Library/Extensions
	sudo touch /System/Library/Extensions

.PHONY: install_kext_mouse
install_kext_mouse:
	sudo cp -R ./Build/Products/Release/VoodooPS2Controller.kext /System/Library/Extensions
	sudo rm -r /System/Library/Extensions/VoodooPS2Controller.kext/Contents/PlugIns/VoodooPS2Trackpad.kext
	sudo touch /System/Library/Extensions

.PHONY: install_daemon
install_daemon:
	sudo cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist /Library/LaunchDaemons
	sudo cp ./Build/Products/Release/VoodooPS2Daemon /usr/bin

install.sh: makefile
	make -n install >install.sh
	chmod +x install.sh

.PHONY: distribute
distribute:
	rm -r ./Distribute
	mkdir ./Distribute
	cp -R ./Build/Products/ ./Distribute
	find ./Distribute -path *.DS_Store -delete
	find ./Distribute -path *.dSYM -exec echo rm -r {} \; >/tmp/org.voodoo.rm.dsym.sh
	chmod +x /tmp/org.voodoo.rm.dsym.sh
	/tmp/org.voodoo.rm.dsym.sh
	rm /tmp/org.voodoo.rm.dsym.sh
	cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist ./Distribute
	rm -r ./Distribute/Debug/VoodooPS2synapticsPane.prefPane
	rm -r ./Distribute/Release/VoodooPS2synapticsPane.prefPane
	rm ./Distribute/Debug/synapticsconfigload
	rm ./Distribute/Release/synapticsconfigload

