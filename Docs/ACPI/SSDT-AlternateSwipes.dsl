// This sample shows how to remap the the default swipes to Option+Arrows,
// instead of the Ctrl+Option+Arrows
DefinitionBlock ("", "SSDT", 2, "hack", "ps2", 0)
{
    Name(_SB.PCI0.LPCB.PS2K.RMCF, Package()
    {
        "Keyboard", Package()
        {
            "ActionSwipeUp", "37 d, 7e d, 7e u, 37 u",
            "ActionSwipeDown", "37 d, 7d d, 7d u, 37 u",
            "ActionSwipeLeft", "37 d, 7b d, 7b u, 37 u",
            "ActionSwipeRight", "37 d, 7c d, 7c u, 37 u",
        },
    })
}
//EOF
