// VoodooPS2Mouse.kext has the ability to appear as a trackpad.
// Most "trackpad" related settings don't work with it, but it will
// enable a few extra features. 
DefinitionBlock("", "SSDT", 2, "ACDT", "ps2", 0)
{
    External (_SB_.PCI0.LPCB.PS2K, DeviceObj)
    
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Mouse", Package()
        {
            "ActLikeTrackpad", ">y",
        },
    })
}
//EOF
