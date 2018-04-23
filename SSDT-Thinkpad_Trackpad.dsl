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
            "UnsmoothInput", ">y",
            "Thinkpad", ">y",
            "DivisorX", 1,
            "DivisorY", 1,
            "FingerZ", 47,
            "MaxTapTime", 100000000,
            "MomentumScrollThreshY", 16,
            "MouseMultiplierX", 8,
            "MouseMultiplierY", 8,
            "MouseScrollMultiplierX", 2,
            "MouseScrollMultiplierY", 2,
            "MultiFingerHorizontalDivisor", 4,
            "MultiFingerVerticalDivisor", 4,
            "Resolution", 3200,
            "ScrollDeltaThreshX", 10,
            "ScrollDeltaThreshY", 10,
        },
    })
}
//EOF
