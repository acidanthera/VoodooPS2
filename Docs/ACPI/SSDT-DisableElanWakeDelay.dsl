// For some computers wake delay needs to be shorter
// for ELAN touchpad to work after wakeup.

DefinitionBlock("", "SSDT", 2, "ACDT", "ps2", 0)
{
    External (_SB_.PCI0.LPCB.PS2K, DeviceObj)

    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Elantech TouchPad", Package()
        {
            "WakeDelay", 0,
        },
    })
}
//EOF
