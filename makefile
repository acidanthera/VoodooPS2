# really just some handy scripts...

DIST=RehabMan-Voodoo
INSTDIR=/System/Library/Extensions
KEXT=VoodooPS2Controller.kext

ifeq ($(findstring 32,$(BITS)),32)
OPTIONS:=$(OPTIONS) -arch i386
endif

ifeq ($(findstring 64,$(BITS)),64)
OPTIONS:=$(OPTIONS) -arch x86_64
endif

.PHONY: all
all:
	xcodebuild build $(OPTIONS) -scheme All -configuration Debug
	xcodebuild build $(OPTIONS) -scheme All -configuration Release

.PHONY: clean
clean:
	xcodebuild clean $(OPTIONS) -scheme All -configuration Debug
	xcodebuild clean $(OPTIONS) -scheme All -configuration Release

.PHONY: update_kernelcache
update_kernelcache:
	sudo touch /System/Library/Extensions
	sudo kextcache -update-volume /

.PHONY: rehabman_special_settings
rehabman_special_settings:
	sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:Synaptics TouchPad:Platform Profile:Default:DragLockTempMask' 262148" $(INSTDIR)/$(KEXT)/Contents/PlugIns/VoodooPS2Trackpad.kext/Contents/Info.plist
	#sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:Synaptics TouchPad:Platform Profile:HPQOEM:ProBook:FingerZ' 47" $(INSTDIR)/$(KEXT)/Contents/PlugIns/VoodooPS2Trackpad.kext/Contents/Info.plist

.PHONY: install_debug
install_debug:
	sudo rm -Rf $(INSTDIR)/$(KEXT)
	sudo cp -R ./Build/Products/Debug/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Purple $(INSTDIR)/$(KEXT); fi
	make rehabman_special_settings
	sudo cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist /Library/LaunchDaemons
	sudo cp ./Build/Products/Debug/VoodooPS2Daemon /usr/bin
	if [ "`which tag`" != "" ]; then sudo tag -a Purple /usr/bin/VoodooPS2Daemon; fi
	make update_kernelcache

.PHONY: install
install: install_kext install_daemon

.PHONY: install_kext
install_kext:
	sudo rm -Rf $(INSTDIR)/$(KEXT)
	sudo cp -R ./Build/Products/Release/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Blue $(INSTDIR)/$(KEXT); fi
	make rehabman_special_settings
	make update_kernelcache

.PHONY: install_mouse
install_mouse:
	sudo rm -Rf $(INSTDIR)/$(KEXT)
	sudo cp -R ./Build/Products/Release/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Blue $(INSTDIR)/$(KEXT); fi
	sudo rm -R $(INSTDIR)/$(KEXT)/Contents/PlugIns/VoodooPS2Trackpad.kext
	sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:ApplePS2Mouse:Platform Profile:HPQOEM:ProBook:DisableDevice' No" $(INSTDIR)/$(KEXT)/Contents/PlugIns/VoodooPS2Mouse.kext/Contents/Info.plist
	make update_kernelcache

.PHONY: install_mouse_debug
install_mouse_debug:
	sudo rm -Rf $(INSTDIR)/$(KEXT)
	sudo cp -R ./Build/Products/Debug/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Purple $(INSTDIR)/$(KEXT); fi
	sudo rm -R $(INSTDIR)/$(KEXT)/Contents/PlugIns/VoodooPS2Trackpad.kext
	sudo /usr/libexec/PlistBuddy -c "Set ':IOKitPersonalities:ApplePS2Mouse:Platform Profile:HPQOEM:ProBook:DisableDevice' No" $(INSTDIR)/$(KEXT)/Contents/PlugIns/VoodooPS2Mouse.kext/Contents/Info.plist
	make update_kernelcache

.PHONY: install_daemon
install_daemon:
	sudo cp ./VoodooPS2Daemon/org.rehabman.voodoo.driver.Daemon.plist /Library/LaunchDaemons
	sudo cp ./Build/Products/Release/VoodooPS2Daemon /usr/bin
	if [ "`which tag`" != "" ]; then sudo tag -a Blue /usr/bin/VoodooPS2Daemon; fi

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
	ditto -c -k --sequesterRsrc --zlibCompressionLevel 9 ./Distribute ./Archive.zip
	mv ./Archive.zip ./Distribute/`date +$(DIST)-%Y-%m%d.zip`
