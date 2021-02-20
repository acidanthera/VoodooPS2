// Add Support for Num Lock key
// By Default Voodoo maps Num Lock to Clear key because of the following
// 1) On USB keyboards Num Lock is translated to the Clear key
// 2) On Apple keyboards there is no Num Lock key

DefinitionBlock ("", "SSDT", 2, "ACDT", "ps2", 0)
{
    External (_SB_.PCI0.LPCB.PS2K, DeviceObj)
    
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Keyboard", Package()
        {
            "NumLockSupport", ">y",
        },
    })
}
//EOF
