// VoodooPS2Mouse.kext has the ability to appear as a trackpad.
// Most "trackpad" related settings don't work with it, but it will
// enable a few extra features. 
DefinitionBlock("", "SSDT", 2, "hack", "ps2", 0)
{
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Mouse", Package()
        {
            "ActLikeTrackpad", ">y",
        },
    })
}
//EOF
