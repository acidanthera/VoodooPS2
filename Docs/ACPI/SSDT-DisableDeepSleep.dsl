// For computers with Synaptics touchpad that wake immediately after
// going to sleep deeper touchpad sleep can be disabled.
//
// This will cause the touchpad to consume more power in ACPI S3.
DefinitionBlock("", "SSDT", 2, "ACDT", "ps2", 0)
{
    External (_SB_.PCI0.LPCB.PS2K, DeviceObj)

    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Synaptics TouchPad", Package()
        {
            "DisableDeepSleep", ">y",
        }
    })
}
//EOF
