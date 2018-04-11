// This sample shows how to remap the Left Control to CapsLock,
// and CapsLock to Left Control.
DefinitionBlock ("", "SSDT", 2, "hack", "ps2", 0)
{
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Keyboard", Package()
        {
            "Custom ADB Map", Package()
            {
                Package(){},
                "3a=3b",    // 3a is PS2 for capslock, 3b is ADB for left control (normal map is 3a=39)
                "1d=39",    // 1d is PS2 for left control, 39 is ADB for caps lock (normal map is 1d=3b)
            },
        },
    })
}
//EOF