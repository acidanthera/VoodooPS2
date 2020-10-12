//
//  VoodooPS2TrackpadCommon.h
//  VoodooPS2Trackpad
//
//  Created by Le Bao Hiep on 27/09/2020.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#ifndef VoodooPS2TrackpadCommon_h
#define VoodooPS2TrackpadCommon_h

#include <IOKit/IOService.h>

typedef enum {
    FORCE_TOUCH_DISABLED = 0,
    FORCE_TOUCH_BUTTON = 1,
    FORCE_TOUCH_THRESHOLD = 2,
    FORCE_TOUCH_VALUE = 3,
    FORCE_TOUCH_CUSTOM = 4
} ForceTouchMode;

template <typename TValue, typename TLimit, typename TMargin>
static void clip_no_update_limits(TValue& value, TLimit minimum, TLimit maximum, TMargin margin)
{
    if (value < minimum)
        value = minimum;
    if (value > maximum)
        value = maximum;
}

template <typename TValue, typename TLimit, typename TMargin>
static void clip(TValue& value, TLimit& minimum, TLimit& maximum, TMargin margin, bool &dimensions_changed)
{
    if (value < minimum - margin) {
        dimensions_changed = true;
        minimum = value + margin;
    }
    if (value > maximum + margin) {
        dimensions_changed = true;
        maximum = value - margin;
    }
    clip_no_update_limits(value, minimum, maximum, margin);
}

void rescale(IOService *self, IOService *voodooInput,
             int &x, int x_min, int x_max, int x_res,
             int &y, int y_min, int y_max, int y_res);

#endif /* VoodooPS2TrackpadCommon_h */
