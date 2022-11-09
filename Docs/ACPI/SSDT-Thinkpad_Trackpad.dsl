// Example overrides for Thinkpad models with TrackPad
DefinitionBlock ("", "SSDT", 2, "ACDT", "ps2", 0)
{
    External(_SB_.PCI0.LPCB.PS2K, DeviceObj)
    // Change _SB.PCI0.LPC.KBD if your PS2 keyboard is at a different ACPI path
    External(_SB_.PCI0.LPC.KBD, DeviceObj)
    Scope(_SB.PCI0.LPC.KBD)
    {
        // Select specific configuration in VoodooPS2Trackpad.kext
        Method(_SB.PCI0.LPCB.PS2K._DSM, 4)
        {
            If (!Arg2) { Return (Buffer() { 0x03 } ) }
            Return (Package()
            {
                "RM,oem-id", "LENOVO",
                "RM,oem-table-id", "Thinkpad_TrackPad",
            })
        }
        // Overrides (the example data here is default in the Info.plist)
        Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
        {
            "Synaptics TouchPad", Package()
            {
                "HWResetOnStart", ">y",
                "PalmNoAction When Typing", ">y",
                "FingerZ", 47,
                "TrackpointMultiplierX", 64 * 8,
                "TrackpointMultiplierY", 64 * 8,
                // Change these to 0xFFFFFFFF - 64 in order to inverse the vertical scroll direction
                // of the Trackpoint when holding the middle mouse button.
                "TrackpointScrollMultiplierX", 128,
                "TrackpointScrollMultiplierY", 128,
            },
        })
    }
}
//EOF
