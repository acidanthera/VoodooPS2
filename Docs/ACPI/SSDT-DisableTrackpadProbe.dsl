// For computers that either have no trackpad (eg. desktop with PS2 mouse)
// or laptops without any support by VoodooPS2Trackpad.kext, you can disable
// each of the trackpad drivers, such that they don't probe.
//
// This can improve the reliability of VoodooPS2Mouse.kext and is more efficient as well.
DefinitionBlock("", "SSDT", 2, "hack", "ps2", 0)
{
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Synaptics TouchPad", Package()
        {
            "DisableDevice", ">y",
        },
        "ALPS GlidePoint", Package()
        {
            "DisableDevice", ">y",
        },
        "Sentelic FSP", Package()
        {
            "DisableDevice", ">y",
        },
    })
}
//EOF
