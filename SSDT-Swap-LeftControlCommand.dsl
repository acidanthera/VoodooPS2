// This sample shows how to remap the Left Control to Command,
// and Command (left Alt) to Left Control.
DefinitionBlock ("", "SSDT", 2, "hack", "ps2", 0)
{
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Keyboard", Package()
        {
            "Custom ADB Map", Package()
            {
                Package(){},
                "1d=3a",    // 1d is PS2 for left control, 3a is ADB for command
                "38=3b",    // 38 is PS2 for left alt, 3b is ADB for left control
            },
        },
    })
}
//EOF