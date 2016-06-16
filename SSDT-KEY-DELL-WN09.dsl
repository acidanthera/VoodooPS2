// Fix for Dell breakless PS2 keys
// This SSDT selects the Dell profile in the Info.plist for VoodooPS2Keyboard.kext

DefinitionBlock ("", "SSDT", 2, "hack", "ps2dell", 0)
{
    External(_SB.PCI0.LPCB.PS2K, DeviceObj)
    Scope(_SB.PCI0.LPCB.PS2K)
    {
        // Select Dell specific keyboard map in VoodooPS2Keyboard.kext
        Method(_DSM, 4)
        {
            If (!Arg2) { Return (Buffer() { 0x03 } ) }
            Return (Package()
            {
                "RM,oem-id", "DELL",
                "RM,oem-table-id", "WN09",
            })
        }
    }
}
// EOF
