//
//  SynapticsDispatch.cpp
//  VoodooPS2Controller
//
//  Created by гык-sse2 on 25.10.16.
//  Copyright © 2016 rehabman. All rights reserved.
//

// enable for trackpad debugging
//#ifdef DEBUG_MSG
#define DEBUG_VERBOSE
//#define PACKET_DEBUG
//#endif

#define kTPDN "TPDN" // Trackpad Disable Notification

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include "VoodooPS2Controller.h"
#include "VoodooPS2SynapticsTouchPad.h"

#define abs(x) ((x) < 0 ? -(x) : (x))

const char* ApplePS2SynapticsTouchPad::modeName(int touchmode) {
    switch (touchmode) {
        case MODE_NOTOUCH:
            return "MODE_NOTOUCH";
        case MODE_PREDRAG:
            return "MODE_PREDRAG";
        case MODE_DRAGNOTOUCH:
            return "MODE_DRAGNOTOUCH";
        case MODE_MOVE:
            return "MODE_MOVE";
        case MODE_VSCROLL:
            return "MODE_VSCROLL";
        case MODE_HSCROLL:
            return "MODE_HSCROLL";
        case MODE_CSCROLL:
            return "MODE_CSCROLL";
        case MODE_MTOUCH:
            return "MODE_MTOUCH";
        case MODE_DRAG:
            return "MODE_DRAG";
        case MODE_DRAGLOCK:
            return "MODE_DRAGLOCK";
        case MODE_WAIT1RELEASE:
            return "MODE_WAIT1RELEASE";
        case MODE_WAIT2TAP:
            return "MODE_WAIT2TAP";
        case MODE_WAIT2RELEASE:
            return "MODE_WAIT2RELEASE";
        default:
            return "INVALID MODE";
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::onDragTimer(void)
{
    touchmode=MODE_NOTOUCH;
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    UInt32 buttons = middleButton(lastbuttons, now_abs, fromPassthru);
    dispatchRelativePointerEventX(0, 0, buttons, now_abs);
    ignore_ew_packets=false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// dragnotouch -> draglock(3)
void ApplePS2SynapticsTouchPad::dispatchEventsWithPacket(UInt8* packet, UInt32 packetSize)
{
    uint64_t now_abs;
    uint64_t now_ns;
    UInt32 buttons;
    int x, y, xraw, yraw, z, w, f;
    int tm1;
    {
    // Here is the format of the 6-byte absolute format packet.
    // This is with wmode on, which is pretty much what this driver assumes.
    // This is a "trackpad specific" packet.
    //
    //      7  6  5  4  3  2  1  0
    //    -----------------------
    // [0]  1  0 W3 W2  0 W1  R  L  (W bits 3..2, W bit 1, R/L buttons)
    // [1] YB YA Y9 Y8 XB XA X9 X8  (Y bits 11..8, X bits 11..8)
    // [2] Z7 Z6 Z5 Z4 Z3 Z2 Z1 Z0  (Z-pressure, bits 7..0)
    // [3]  1  1 YC XC  0 W0 RD LD  (Y bit 12, X bit 12, W bit 0, RD/LD)
    // [4] X7 X6 X5 X4 X3 X2 X1 X0  (X bits 7..0)
    // [5] Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (Y bits 7..0)
    
    // This is the format of the 6-byte encapsulation packet.
    // Encapsulation packets are used for PS2 pass through mode, which
    // allows another PS2 device to be connected as a slave to the
    // touchpad.  The touchpad acts as a host for the second evice
    // and forwards packets with a special value for w (w=3)
    // So when w=3 (W3=0,W2=0,W1=1,W0=1), this is what the packets
    // look like.
    //
    //      7  6  5  4  3  2  1  0
    //    -----------------------
    // [0]  1  0  0  0  0  1  R  L  (R/L are for touchpad)
    // [1] YO XO YS XS  1  M  R  L  (packet byte 0, Y/X overflow, Y/X sign, buttons)
    // [2]  0  0 B5 B4 Z3 Z2 Z1 Z0  (packet byte 3, B4,B5 buttons, Z=wheel)
    // [3]  1  1  x  x  0  1  R  L  (x=reserved, R/L are for touchpad)
    // [4] X7 X6 X5 X4 X3 X3 X1 X0  (packet byte 1, X delta)
    // [5] Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (packet byte 2, Y delta)
    
    clock_get_uptime(&now_abs);
    absolutetime_to_nanoseconds(now_abs, &now_ns);

    //
    // Parse the packet
    //
    
    w = ((packet[3]&0x4)>>2)|((packet[0]&0x4)>>1)|((packet[0]&0x30)>>2);
    
    if (_extendedwmode && 2 == w)
    {
        // deal with extended W mode encapsulated packet
        dispatchEventsWithPacketEW(packet, packetSize);
        return;
    }
    
    // allow middle click to be simulated the other two physical buttons
    UInt32 buttonsraw = packet[0] & 0x03; // mask for just R L
    buttons = buttonsraw;
    
    // deal with pass through packet buttons
    if (passthru && 3 == w)
        passbuttons = packet[1] & 0x7; // mask for just M R L
    
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    buttons |= passbuttons;
    lastbuttons = buttons;
    
    // now deal with pass through packet moving/scrolling
    if (passthru && 3 == w)
    {
        // New Lenovo clickpads do not have buttons, so LR in packet byte 1 is zero and thus
        // passbuttons is 0.  Instead we need to check the trackpad buttons in byte 0 and byte 3
        // However for clickpads that would miss right clicks, so use the last clickbuttons that
        // were saved.
        UInt32 combinedButtons = buttons | ((packet[0] & 0x3) | (packet[3] & 0x3)) | _clickbuttons;
        
        SInt32 dx = ((packet[1] & 0x10) ? 0xffffff00 : 0 ) | packet[4];
        SInt32 dy = ((packet[1] & 0x20) ? 0xffffff00 : 0 ) | packet[5];
        if (mousemiddlescroll && (packet[1] & 0x4)) // only for physical middle button
        {
            // middle button treats deltas for scrolling
            SInt32 scrollx = 0, scrolly = 0;
            if (abs(dx) > abs(dy))
                scrollx = dx * mousescrollmultiplierx;
            else
                scrolly = dy * mousescrollmultipliery;
            dispatchScrollWheelEventX(scrolly, -scrollx, 0, now_abs);
            dx = dy = 0;
        }
        dx *= mousemultiplierx;
        dy *= mousemultipliery;
        dispatchRelativePointerEventX(dx, -dy, combinedButtons, now_abs);
        #ifdef DEBUG_VERBOSE
                static int count = 0;
                IOLog("ps2: passthru packet dx=%d, dy=%d, buttons=%d (%d)\n", dx, dy, combinedButtons, count++);
        #endif
        return;
    }
    
    // otherwise, deal with normal wmode touchpad packet
    xraw = packet[4]|((packet[1]&0x0f)<<8)|((packet[3]&0x10)<<8);
    yraw = packet[5]|((packet[1]&0xf0)<<4)|((packet[3]&0x20)<<7);
    // scale x & y to the axis which has the most resolution
    if (xupmm < yupmm)
        xraw = xraw * yupmm / xupmm;
    else if (xupmm > yupmm)
        yraw = yraw * xupmm / yupmm;
    z = packet[2];
    f = z>z_finger ? w>=4 ? 1 : w+2 : 0;   // number of fingers
    ////int v = w;  // v is not currently used... but maybe should be using it
    if (_extendedwmode && _reportsv && f > 1)
    {
        // in extended w mode, v field (width) is encoded in x & y & z, with multifinger
        ////v = (((xraw & 0x2)>>1) | ((yraw & 0x2)) | ((z & 0x1)<<2)) + 8;
        xraw &= ~0x2;
        yraw &= ~0x2;
        z &= ~0x1;
    }
    x = xraw;
    y = yraw;
    
    // recalc middle buttons if finger is going down
    if (0 == lastf && f > 0)
        buttons = middleButton(buttonsraw | passbuttons, now_abs, fromCancel);
    
    if (lastf > 0 && f > 0 && lastf != f)
    {
        // ignore deltas for a while after finger change
        ignoredeltas = ignoredeltasstart;
    }
    
    if (lastf != f)
    {
        // reset averages after finger change
        x_undo.reset();
        y_undo.reset();
        x_avg.reset();
        y_avg.reset();
    }
    
    // unsmooth input (probably just for testing)
    // by default the trackpad itself does a simple decaying average (1/2 each)
    // we can undo it here
    if (unsmoothinput)
    {
        x = x_undo.filter(x);
        y = y_undo.filter(y);
    }
    
    // smooth input by unweighted average
    if (smoothinput)
    {
        x = x_avg.filter(x);
        y = y_avg.filter(y);
    }
    
    if (ignoredeltas)
    {
        lastx = x;
        lasty = y;
        if (--ignoredeltas == 0)
        {
            x_undo.reset();
            y_undo.reset();
            x_avg.reset();
            y_avg.reset();
        }
    }
    
    // Note: This probably should be different for two button ClickPads,
    // but we really don't know much about it and how/what the second button
    // on such a ClickPad is used.
    
    // deal with ClickPad touchpad packet
    if (clickpadtype)
    {
        // ClickPad puts its "button" presses in a different location
        // And for single button ClickPad we have to provide a way to simulate right clicks
        int clickbuttons = packet[3] & 0x3;
        if (!_clickbuttons && clickbuttons)
        {
            // use primary packet by default
            int xx = x;
            int yy = y;
            clickedprimary = (MODE_MTOUCH != touchmode);
            // need to use secondary packet if receiving them
            if (_extendedwmode && !clickedprimary && tracksecondary)
            {
                xx = lastx2;
                yy = lasty2;
            }
            DEBUG_LOG("ps2: now_ns=%lld, touchtime=%lld, diff=%lld cpct=%lld (%s) w=%d (%d,%d)\n", now_ns, touchtime, now_ns-touchtime, clickpadclicktime, now_ns-touchtime < clickpadclicktime ? "true" : "false", w, isFingerTouch(z), isInRightClickZone(xx, yy));
            // change to right click if in right click zone, or was two finger "click"
            if (isFingerTouch(z) && (
                                     ((rightclick_corner == 2 && isInRightClickZone(xx, yy)) ||
                                      (rightclick_corner == 1 && isInLeftClickZone(xx, yy)))
                                     || (0 == w && (now_ns-touchtime < clickpadclicktime || MODE_NOTOUCH == touchmode))))
            {
                DEBUG_LOG("ps2p: setting clickbuttons to indicate right\n");
                clickbuttons = 0x2;
            }
            else
                DEBUG_LOG("ps2p: setting clickbuttons to indicate left\n");
            setClickButtons(clickbuttons);
        }
        // always clear _clickbutton state, when ClickPad is not clicked
        if (!clickbuttons)
            setClickButtons(0);
        buttons |= _clickbuttons;
        lastbuttons = buttons;
    }
    
    #ifdef DEBUG_VERBOSE
        tm1 = touchmode;
    #endif
    }
    
    /*
     Three-finger drag should work the following way:
     When three fingers touch the touchpad, start dragging
     While dragging, ignore touches with not 3 fingers
     When all fingers are released, go to DRAGNOTOUCH mode
     In DRAGNOTOUCH mode:
        one or two fingers can be placed on the touchpad, and it prolongs the drag stop timer
        if one or two fingers MOVE on the touchpad or are RELEASED, dragging is stopped (click to stop)
        when three fingers are on the touchpad, continue dragging in DRAGLOCK mode
     */
    
    if (isFingerTouch(z) && (touchmode == MODE_DRAGLOCK || touchmode == MODE_DRAG) && threefingerdrag && w != 1) { // Ignore one-finger and two-finger touches when dragging with three fingers
        lastf=f;
        return;
    }
    if (touchmode == MODE_DRAGNOTOUCH && threefingerdrag && w != 1) {
        lastf=f;
        return;
    }
    
    if (z<z_finger && isTouchMode())
    {
        xrest=yrest=scrollrest=0;
        inSwipeLeft=inSwipeRight=inSwipeUp=inSwipeDown=0;
        xmoved=ymoved=0;
        untouchtime=now_ns;
        tracksecondary=false;
        
        #ifdef DEBUG_VERBOSE
                if (dy_history.count())
                    IOLog("ps2: newest=%llu, oldest=%llu, diff=%llu, avg: %d/%d=%d\n", time_history.newest(), time_history.oldest(), time_history.newest()-time_history.oldest(), dy_history.sum(), dy_history.count(), dy_history.average());
                else
                    IOLog("ps2: no time/dy history\n");
        #endif
        
        // check for scroll momentum start
        if (MODE_MTOUCH == touchmode && momentumscroll && momentumscrolltimer)
        {
            // releasing when we were in touchmode -- check for momentum scroll
            if (dy_history.count() > momentumscrollsamplesmin &&
                (momentumscrollinterval = time_history.newest() - time_history.oldest()))
            {
                momentumscrollsum = dy_history.sum();
                momentumscrollcurrent = momentumscrolltimer * momentumscrollsum;
                momentumscrollrest1 = 0;
                momentumscrollrest2 = 0;
                setTimerTimeout(scrollTimer, momentumscrolltimer);
            }
        }
        time_history.reset();
        dy_history.reset();
                DEBUG_LOG("ps2: now_ns-touchtime=%lld (%s)\n", (uint64_t)(now_ns-touchtime)/1000, now_ns-touchtime < maxtaptime?"true":"false");
        if (now_ns-touchtime < maxtaptime && clicking)
        {
            switch (touchmode)
            {
                case MODE_DRAG:
                    // Exiting drag if clicked
                    if (!immediateclick)
                    {
                        buttons&=~0x7;
                        dispatchRelativePointerEventX(0, 0, buttons|0x1, now_abs);
                        dispatchRelativePointerEventX(0, 0, buttons, now_abs);
                    }
                    /*if (wastriple && rtap)
                        buttons |= !swapdoubletriple ? 0x4 : 0x02;
                    else if (wasdouble && rtap)
                        buttons |= !swapdoubletriple ? 0x2 : 0x04;
                    else*/
                        buttons |= 0x1;
                    ignore_ew_packets=false;
                    touchmode=MODE_NOTOUCH;
                    break;
                    
                case MODE_DRAGLOCK:
                    touchmode = MODE_NOTOUCH;
                    break;
                    
                default:
                    if (wastriple && rtap)
                    {
                        buttons |= !swapdoubletriple ? 0x4 : 0x02;
                        touchmode=MODE_NOTOUCH;
                    }
                    else if (wasdouble && rtap)
                    {
                        buttons |= !swapdoubletriple ? 0x2 : 0x04;
                        touchmode=MODE_NOTOUCH;
                    }
                    else
                    {
                        buttons |= 0x1;
                        touchmode=dragging ? MODE_PREDRAG : MODE_NOTOUCH;
                    }
                    break;
            }
        }
        else
        {
            if ((touchmode==MODE_DRAG || touchmode==MODE_DRAGLOCK)
                && (draglock || draglocktemp || (dragTimer && dragexitdelay)))
            {
                touchmode=MODE_DRAGNOTOUCH;
                if (!draglock && !draglocktemp)
                {
                    cancelTimer(dragTimer);
                    setTimerTimeout(dragTimer, dragexitdelay);
                }
            }
            else
            {
                touchmode=MODE_NOTOUCH;
                ignore_ew_packets=false;
                draglocktemp=0;
            }
        }
        wasdouble=false;
        wastriple=false;
    }
    
    // cancel pre-drag mode if second tap takes too long
    if (touchmode==MODE_PREDRAG && now_ns-untouchtime >= maxdragtime)
        touchmode=MODE_NOTOUCH;
    
    // Note: This test should probably be done somewhere else, especially if to
    // implement more gestures in the future, because this information we are
    // erasing here (time of touch) might be useful for certain gestures...
    
    #ifdef DEBUG_VERBOSE
        int tm2 = touchmode;
    #endif
    int dx = 0, dy = 0;
    
    switch (touchmode)
    {
        case MODE_DRAG:
        case MODE_DRAGLOCK:
            if (MODE_DRAGLOCK == touchmode || (!immediateclick || now_ns-touchtime > maxdbltaptime))
                buttons|=0x1;
            // fall through
        case MODE_MOVE:
            if (lastf == f && (!palm || (w<=wlimit && z<=zlimit)))
            {
                dx = x-lastx+xrest;
                dy = lasty-y+yrest;
                xrest = dx % divisorx;
                yrest = dy % divisory;
                if (abs(dx) > bogusdxthresh || abs(dy) > bogusdythresh)
                    dx = dy = xrest = yrest = 0;
            }
            break;
            
        case MODE_MTOUCH:
            switch (w)
        {
            default: // two finger (0 is really two fingers, but...)
                if (_extendedwmode && 0 == w && _clickbuttons)
                {
                    // clickbuttons are set, so no scrolling, but...
                    if (clickpadtrackboth || !clickedprimary)
                    {
                        // clickbuttons set by secondary finger, so move with primary delta...
                        if (lastf == f && (!palm || (w<=wlimit && z<=zlimit)))
                        {
                            dx = x-lastx+xrest;
                            dy = lasty-y+yrest;
                            xrest = dx % divisorx;
                            yrest = dy % divisory;
                            if (abs(dx) > bogusdxthresh || abs(dy) > bogusdythresh)
                                dx = dy = xrest = yrest = 0;
                        }
                    }
                    break;
                }
                ////if (palm && (w>wlimit || z>zlimit))
                if (lastf != f)
                    break;
                if (palm && z>zlimit)
                    break;
                if (!wsticky && w<=wlimit && w>3)
                {
                    dy_history.reset();
                    time_history.reset();
                    clickedprimary = _clickbuttons;
                    tracksecondary=false;
                    touchmode=MODE_MOVE;
                    break;
                }
                if (palm_wt && now_ns-keytime < maxaftertyping)
                    break;
                dy = (wvdivisor) ? (y-lasty+yrest) : 0;
                dx = (whdivisor&&hscroll) ? (lastx-x+xrest) : 0;
                yrest = (wvdivisor) ? dy % wvdivisor : 0;
                xrest = (whdivisor&&hscroll) ? dx % whdivisor : 0;
                // check for stopping or changing direction
                if ((dy < 0) != (dy_history.newest() < 0) || dy == 0)
                {
                    // stopped or changed direction, clear history
                    dy_history.reset();
                    time_history.reset();
                }
                // put movement and time in history for later
                dy_history.filter(dy);
                time_history.filter(now_ns);
                //REVIEW: filter out small movements (Mavericks issue)
                if (abs(dx) < scrolldxthresh)
                {
                    xrest = dx;
                    dx = 0;
                }
                if (abs(dy) < scrolldythresh)
                {
                    yrest = dy;
                    dy = 0;
                }
                if (0 != dy || 0 != dx)
                {
                    dispatchScrollWheelEventX(wvdivisor ? dy / wvdivisor : 0, (whdivisor && hscroll) ? dx / whdivisor : 0, 0, now_abs);
                    ////IOLog("ps2: dx=%d, dy=%d (%d,%d) z=%d w=%d\n", dx, dy, x, y, z, w);
                    dx = dy = 0;
                }
                break;
                
            case 1: // three finger
                if (threefingerhorizswipe || threefingervertswipe) {
                    xmoved += lastx-x;
                    ymoved += y-lasty;
                    // dispatching 3 finger movement
                    if (ymoved > swipedy && !inSwipeUp && threefingervertswipe)
                    {
                        inSwipeUp=1;
                        inSwipeDown=0;
                        ymoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeUp, &now_abs);
                        break;
                    }
                    if (ymoved < -swipedy && !inSwipeDown && threefingervertswipe)
                    {
                        inSwipeDown=1;
                        inSwipeUp=0;
                        ymoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeDown, &now_abs);
                        break;
                    }
                    if (xmoved < -swipedx && !inSwipeRight && threefingerhorizswipe)
                    {
                        inSwipeRight=1;
                        inSwipeLeft=0;
                        xmoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeRight, &now_abs);
                        break;
                    }
                    if (xmoved > swipedx && !inSwipeLeft && threefingerhorizswipe)
                    {
                        inSwipeLeft=1;
                        inSwipeRight=0;
                        xmoved = 0;
                        _device->dispatchKeyboardMessage(kPS2M_swipeLeft, &now_abs);
                        break;
                    }
                }
        }
            break;
            
        case MODE_VSCROLL:
        {
            if (!vsticky && (x<redge || w>wlimit || z>zlimit))
            {
                touchmode=MODE_NOTOUCH;
                break;
            }
            if (palm_wt && now_ns-keytime < maxaftertyping)
                break;
            dy = y-lasty+scrollrest;
            scrollrest = dy % vscrolldivisor;
            //REVIEW: filter out small movements (Mavericks issue)
            if (abs(dy) < scrolldythresh)
            {
                scrollrest = dy;
                dy = 0;
            }
            if (dy)
            {
                dispatchScrollWheelEventX(dy / vscrolldivisor, 0, 0, now_abs);
                dy = 0;
            }
        }
            break;
            
        case MODE_HSCROLL:
        {
            if (!hsticky && (y>bedge || w>wlimit || z>zlimit))
            {
                touchmode=MODE_NOTOUCH;
                break;
            }
            if (palm_wt && now_ns-keytime < maxaftertyping)
                break;
            dx = lastx-x+scrollrest;
            scrollrest = dx % hscrolldivisor;
            //REVIEW: filter out small movements (Mavericks issue)
            if (abs(dx) < scrolldxthresh)
            {
                scrollrest = dx;
                dx = 0;
            }
            if (dx)
            {
                dispatchScrollWheelEventX(0, dx / hscrolldivisor, 0, now_abs);
                dx = 0;
            }
        }
            break;
            
        case MODE_CSCROLL:
        {
            if (palm_wt && now_ns-keytime < maxaftertyping)
                break;
            if (y < centery)
                dx = x-lastx;
            else
                dx = lastx-x;
            if (x < centerx)
                dx += lasty-y;
            else
                dx += y-lasty;
            dx += scrollrest;
            scrollrest = dx % cscrolldivisor;
            //REVIEW: filter out small movements (Mavericks issue)
            if (abs(dx) < scrolldxthresh)
            {
                scrollrest = dx;
                dx = 0;
            }
            if (dx)
            {
                dispatchScrollWheelEventX(dx / cscrolldivisor, 0, 0, now_abs);
                dx = 0;
            }
        }
            break;
            
        case MODE_DRAGNOTOUCH:
            buttons |= 0x1;
            // fall through
        case MODE_PREDRAG:
            if (!immediateclick && (!palm_wt || now_ns-keytime >= maxaftertyping))
                buttons |= 0x1;
        case MODE_NOTOUCH:
            break;
            
        default:
            ; // nothing
    }
    
    // capture time of tap, and watch for double tap
    if (isFingerTouch(z))
    {
        // taps don't count if too close to typing or if currently in momentum scroll
        if ((!palm_wt || now_ns-keytime >= maxaftertyping) && !momentumscrollcurrent)
        {
            if (!isTouchMode())
            {
                touchtime=now_ns;
                touchx=x;
                touchy=y;
            }
            ////if (w>wlimit || w<3)
            if (0 == w)
                wasdouble = true;
            else if (_buttonCount >= 3 && 1 == w)
                wastriple = true;
        }
        // any touch cancels momentum scroll
        momentumscrollcurrent = 0;
    }
    
    // switch modes, depending on input
    if (touchmode==MODE_PREDRAG && isFingerTouch(z))
    {
        touchmode=MODE_DRAG;
        draglocktemp = _modifierdown & draglocktempmask;
    }
    if (touchmode==MODE_DRAGNOTOUCH && isFingerTouch(z) &&
        (!threefingerdrag || // one-finger drag
         (threefingerdrag && w == 1))) // three-finger drag
    {
        if (dragTimer)
            cancelTimer(dragTimer);
        touchmode=MODE_DRAGLOCK;
    }
    ////if ((w>wlimit || w<3) && isFingerTouch(z) && scroll && (wvdivisor || (hscroll && whdivisor)))
    if (MODE_MTOUCH != touchmode && (w>wlimit || w<2) && isFingerTouch(z))
    {
        if (w == 1 && threefingerdrag)
        {
            touchmode=MODE_DRAG;
            ignore_ew_packets=true;
        }
        else
            touchmode=MODE_MTOUCH;
        tracksecondary=false;
    }
    
    if (scroll && cscrolldivisor)
    {
        if (touchmode==MODE_NOTOUCH && z>z_finger && y>tedge && (ctrigger==1 || ctrigger==9))
            touchmode=MODE_CSCROLL;
        if (touchmode==MODE_NOTOUCH && z>z_finger && y>tedge && x>redge && (ctrigger==2))
            touchmode=MODE_CSCROLL;
        if (touchmode==MODE_NOTOUCH && z>z_finger && x>redge && (ctrigger==3 || ctrigger==9))
            touchmode=MODE_CSCROLL;
        if (touchmode==MODE_NOTOUCH && z>z_finger && x>redge && y<bedge && (ctrigger==4))
            touchmode=MODE_CSCROLL;
        if (touchmode==MODE_NOTOUCH && z>z_finger && y<bedge && (ctrigger==5 || ctrigger==9))
            touchmode=MODE_CSCROLL;
        if (touchmode==MODE_NOTOUCH && z>z_finger && y<bedge && x<ledge && (ctrigger==6))
            touchmode=MODE_CSCROLL;
        if (touchmode==MODE_NOTOUCH && z>z_finger && x<ledge && (ctrigger==7 || ctrigger==9))
            touchmode=MODE_CSCROLL;
        if (touchmode==MODE_NOTOUCH && z>z_finger && x<ledge && y>tedge && (ctrigger==8))
            touchmode=MODE_CSCROLL;
    }
    if ((MODE_NOTOUCH==touchmode || (MODE_HSCROLL==touchmode && y>=bedge)) &&
        z>z_finger && x>redge && vscrolldivisor && scroll)
    {
        touchmode=MODE_VSCROLL;
        scrollrest=0;
    }
    if ((MODE_NOTOUCH==touchmode || (MODE_VSCROLL==touchmode && x<=redge)) &&
        z>z_finger && y<bedge && hscrolldivisor && hscroll && scroll)
    {
        touchmode=MODE_HSCROLL;
        scrollrest=0;
    }
    if (touchmode==MODE_NOTOUCH && z>z_finger)
        touchmode=MODE_MOVE;
    
    // dispatch dx/dy and current button status
    dispatchRelativePointerEventX(dx / divisorx, dy / divisory, buttons, now_abs);
    
    // always save last seen position for calculating deltas later
    lastx=x;
    lasty=y;
    lastf=f;
    
    #ifdef DEBUG_VERBOSE
        IOLog("ps2: dx=%d, dy=%d (%d,%d) z=%d w=%d mode=(%s,%s,%s) buttons=%d wasdouble=%d\n", dx, dy, x, y, z, w, modeName(tm1), modeName(tm2), modeName(touchmode), buttons, wasdouble);
    #endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ApplePS2SynapticsTouchPad::dispatchEventsWithPacketEW(UInt8* packet, UInt32 packetSize)
{
    if (ignore_ew_packets)
        return;
    
    UInt8 packetCode = packet[5] >> 4;    // bits 7-4 define packet code
    
    // deal only with secondary finger packets (never saw any of the others)
    if (1 != packetCode)
        return;
    
    UInt32 buttons = packet[0] & 0x03; // mask for just R L
    
    int xraw = (packet[1]<<1) | (packet[4]&0x0F)<<9;
    int yraw = (packet[2]<<1) | (packet[4]&0xF0)<<5;
#ifdef DEBUG_VERBOSE
    DEBUG_LOG("ps2: secondary finger pkt (%d, %d) (%04x, %04x) = { %02x, %02x, %02x, %02x, %02x, %02x }\n", xraw, yraw, xraw, yraw, packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
#endif
    // scale x & y to the axis which has the most resolution
    if (xupmm < yupmm)
        xraw = xraw * yupmm / xupmm;
    else if (xupmm > yupmm)
        yraw = yraw * xupmm / yupmm;
    int z = (packet[5]&0x0F)<<1 | (packet[3]&0x30)<<1;
    if (!isFingerTouch(z))
    {
        DEBUG_LOG("ps2: secondary finger packet received without finger touch (z=%d)\n", z);
        return;
    }
    ////int v = 0;
    if (_reportsv)
    {
        // if _reportsv is 1, v field (width) is encoded in x & y & z
        ////v = (packet[5]&0x1)<<2 | (packet[2]&0x1)<<1 | (packet[1]&0x1)<<0;
        xraw &= ~0x2;
        yraw &= ~0x2;
        z &= ~0x2;
    }
    int x = xraw;
    int y = yraw;
    ////int w = z + 8;
    
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    uint64_t now_ns;
    absolutetime_to_nanoseconds(now_abs, &now_ns);
    
    // if there are buttons set in the last pass through packet, then be sure
    // they are set in any trackpad dispatches.
    // otherwise, you might see double clicks that aren't there
    buttons |= passbuttons;
    
    // if first secondary packet, clear some state...
    if (!tracksecondary)
    {
        x2_undo.reset();
        y2_undo.reset();
        x2_avg.reset();
        y2_avg.reset();
        xrest2 = 0;
        yrest2 = 0;
    }
    
    // unsmooth input (probably just for testing)
    // by default the trackpad itself does a simple decaying average (1/2 each)
    // we can undo it here
    if (unsmoothinput)
    {
        x = x2_undo.filter(x);
        y = y2_undo.filter(y);
    }
    
    // smooth input by unweighted average
    if (smoothinput)
    {
        x = x2_avg.filter(x);
        y = y2_avg.filter(y);
    }
    
    // two things could be happening with secondary packets...
    // we are either tracking movement because the primary finger is holding ClickPad
    //  -or-
    // we are tracking movement with primary finger and secondary finger is being
    //  watched in case ClickPad goes down...
    // both cases in MODE_MTOUCH...
    
    int dx = 0;
    int dy = 0;
    
    if ((clickpadtrackboth || clickedprimary) && _clickbuttons)
    {
        // cannot calculate deltas first thing through...
        if (tracksecondary)
        {
            ////if ((palm && (w>wlimit || z>zlimit)))
            ////    return;
            dx = x-lastx2+xrest2;
            dy = lasty2-y+yrest2;
            xrest2 = dx % divisorx;
            yrest2 = dy % divisory;
            if (abs(dx) > bogusdxthresh || abs(dy) > bogusdythresh)
                dx = dy = xrest = yrest = 0;
            dispatchRelativePointerEventX(dx / divisorx, dy / divisory, buttons|_clickbuttons, now_abs);
        }
    }
    else
    {
        // Note: This probably should be different for two button ClickPads,
        // but we really don't know much about it and how/what the second button
        // on such a ClickPad is used.
        
        // deal with ClickPad touchpad packet
        if (clickpadtype)
        {
            // ClickPad puts its "button" presses in a different location
            // And for single button ClickPad we have to provide a way to simulate right clicks
            int clickbuttons = packet[3] & 0x3;
            if (!_clickbuttons && clickbuttons)
            {
                // change to right click if in right click zone
                if (isInRightClickZone(x, y)
                    || (now_ns-touchtime < clickpadclicktime || MODE_NOTOUCH == touchmode))
                {
                    DEBUG_LOG("ps2s: setting clickbuttons to indicate right\n");
                    clickbuttons = 0x2;
                }
                else
                    DEBUG_LOG("ps2s: setting clickbuttons to indicate left\n");
                setClickButtons(clickbuttons);
                clickedprimary = false;
            }
            // always clear _clickbutton state, when ClickPad is not clicked
            if (!clickbuttons)
                setClickButtons(0);
            buttons |= _clickbuttons;
        }
        dispatchRelativePointerEventX(0, 0, buttons, now_abs);
    }
    
#ifdef DEBUG_VERBOSE
    DEBUG_LOG("ps2: (%d,%d,%d) secondary finger dx=%d, dy=%d (%d,%d) z=%d (%d,%d,%d,%d)\n", clickedprimary, _clickbuttons, tracksecondary, dx, dy, x, y, z, lastx2, lasty2, xrest2, yrest2);
#endif
    
    lastx2 = x;
    lasty2 = y;
    tracksecondary = true;
}
