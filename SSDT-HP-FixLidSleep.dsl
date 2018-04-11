// SSDT-HP-FixLidSleep.dsl
// Fix for LID sleep on certain HP computers.
// Make sure you verify _SB.PCI0.LPCB.PS2K ACPI path.
DefinitionBlock ("", "SSDT", 2, "hack", "ps2", 0)
{
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Keyboard", Package()
        {
            "Custom PS2 Map", Package()
            {
                Package(){},
                "e005=0", // nullify ps2 code 'e0 05' to prevent keygen at lid close
            },
        },
    })
}
//EOF
