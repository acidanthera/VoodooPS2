// Fix for Dell breakless PS2 keys
// This SSDT selects the Dell profile in the Info.plist for VoodooPS2Keyboard.kext

// Note: the path to your ACPI keyboard object must match as specified in the SSDT
// This example assumes _SB.PCI0.LPCB.PS2K
// Other common paths are _SB.PCI0.LPCB.KBC, _SB.PCI0.LPC.KBD, etc.

DefinitionBlock ("", "SSDT", 2, "ACDT", "ps2dell", 0)
{
    External (_SB_.PCI0.LPCB.PS2K, DeviceObj)
    
    // Select Dell specific keyboard map in VoodooPS2Keyboard.kext
    Method(_SB.PCI0.LPCB.PS2K._DSM, 4)
    {
        If (!Arg2) { Return (Buffer() { 0x03 } ) }
        Return (Package()
        {
            "RM,oem-id", "DELL",
            "RM,oem-table-id", "WN09",
        })
    }
}
// EOF
