// Example overrides for Thinkpad models with ClickPad
DefinitionBlock ("", "SSDT", 2, "hack", "ps2", 0)
{
    // Select specific configuration in VoodooPS2Trackpad.kext
    Method(_SB.PCI0.LPCB.PS2K._DSM, 4)
    {
        If (!Arg2) { Return (Buffer() { 0x03 } ) }
        Return (Package()
        {
            "RM,oem-id", "LENOVO",
            "RM,oem-table-id", "Thinkpad_Clickpad",
        })
    }
    // Overrides (the example data here is default in the Info.plist)
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Synaptics TouchPad", Package()
        {
            "BogusDeltaThreshX", 100,
            "BogusDeltaThreshY", 100,
            "Clicking", ">y",
            "DragLockTempMask", 0x40004,
            "DynamicEWMode", ">n",
            "FakeMiddleButton", ">n",
            "HWResetOnStart", ">y",
            //"ForcePassThrough", ">y",
            //"SkipPassThrough", ">y",
            "PalmNoAction When Typing", ">y",
            "ScrollResolution", 800,
            "SmoothInput", ">y",
            "UnsmootInput", ">y",
            "Thinkpad", ">y",
            "EdgeBottom", 0,
            "FingerZ", 30,
            "MaxTapTime", 100000000,
            "MouseMultiplierX", 2,
            "MouseMultiplierY", 2,
            "MouseScrollMultiplierX", 2,
            "MouseScrollMultiplierY", 2,
            "TrackpointScrollYMultiplier", 1, //Change this value to 0xFFFF in order to inverse the vertical scroll direction of the Trackpoint when holding the middle mouse button.
            "TrackpointScrollXMultiplier", 1, //Change this value to 0xFFFF in order to inverse the horizontal scroll direction of the Trackpoint when holding the middle mouse button.
        },
    })
}
//EOF
