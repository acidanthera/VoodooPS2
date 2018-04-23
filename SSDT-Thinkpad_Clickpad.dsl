DefinitionBlock ("", "SSDT", 2, "hack", "ps2", 0)
{
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
        },
    })
}
//EOF
