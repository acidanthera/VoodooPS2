{\rtf1\ansi\ansicpg1252\cocoartf1187\cocoasubrtf340
\cocoascreenfonts1{\fonttbl\f0\fswiss\fcharset0 Helvetica;}
{\colortbl;\red255\green255\blue255;}
\paperw11900\paperh16840\margl1440\margr1440\vieww23320\viewh16780\viewkind0
\pard\tx720\tx1440\tx2160\tx2880\tx3600\tx4320\tx5040\tx5760\tx6480\tx7200\tx7920\tx8640\pardirnatural

\f0\fs24 \cf0 ## Voodoo Team proudly presents VoodooPS2Controller\
\pard\tx566\tx1133\tx1700\tx2267\tx2834\tx3401\tx3968\tx4535\tx5102\tx5669\tx6236\tx6803\pardirnatural
\cf0 \
### Features:\
* No need for ACPIPS2Nub. Delete it before installing this or you'll get a kernel panic\
(RehabMan Note: For me, it doesn't work without AppleACPIPS2Nub.kext, so I'm continuing to build/include it)\
* Loadable from /Extra/Extensions.mkext\
(RehabMan Note: Does anyone do this anymore?)\
* 102-key keyboard support\
* support for mixed usb/ps2 configurations\
* Resolution fix for PS2mouse (set ForceDefaultResolution to true in Contents/Plugins/VoodooPS2Mouse.kext/Info.plist to activate)\
* Support for scrolling on ALPS (untested)\
* Support for Sentelic touchpad (untested)\
* Advanced Synaptics touchpad. All kinds of scrolling known to humanity including multi-finger and side scrolling\
\
### Installation\
Just install as any kext with kexthelper or add it to /Extra/Extensions.mkext.  Personally, I like to use Kext Wizard (although I wish it was open-source)\
\
### Configuration:\
Unfortunately we found some last-minute problems with preference pane for synaptics touchpad so this release is without it. It will be made available later. However many features may be controlled using Apple's Preference Pane.  For more configuration you may want to have a look at Contents/Plugins/VoodooPS2Trackpad.kext/Info.plist at \
IOKitPersonalities/Synaptics Touchpad/Configuration node\
(RehabMan Note: The PrefPane is in here, but I haven't tried it, other than to make it build)\
\
### Credits for Original\
VoodooPS2Controller (core): turbo\
Resolution fix for PS2Mouse: mackerintel\
Synaptics driver: mackerintel\
Sentelic Driver: nhand42\
Alps driver: phb\
Keyboard fixes: Chunnan & jape\
Synaptics Prefpane design: bumby\
Synpatics Prefpane: mackerintel\
Great thanks to Dense for helping with activating vanilla trackpad prefpane\
\
### Current Version\
Cleanup and current version: RehabMan\
}