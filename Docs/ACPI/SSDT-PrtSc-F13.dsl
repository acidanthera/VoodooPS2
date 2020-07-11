// This sample shows how to remap the PrtSc key to F13
// F13, for example, could then be mapped to one of the image capture functions
// via SysPrefs->Keyboard->Shortcuts
DefinitionBlock ("", "SSDT", 2, "ACDT", "ps2", 0)
{
    External (_SB_.PCI0.LPCB.PS2K, DeviceObj)
    
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Keyboard", Package()
        {
            "Custom PS2 Map", Package()
            {
                Package(){},
                "e037=64", // PrtSc=F13
            },
        },
    })
}
//EOF
