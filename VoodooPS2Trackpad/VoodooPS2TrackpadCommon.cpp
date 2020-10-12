//
//  VoodooPS2TrackpadCommon.cpp
//  VoodooPS2Trackpad
//
//  Created by usrsse2 on 12.10.2020.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#include "VoodooPS2TrackpadCommon.h"
#include "VoodooInputMultitouch/VoodooInputEvent.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"


void rescale(IOService *self, IOService *voodooInput,
             int &x, int x_min, int x_max, int x_res,
             int &y, int y_min, int y_max, int y_res) {
    bool needs_update = false;
    
    clip(x, x_min, x_max, 5 * x_res, needs_update);
    clip(y, y_min, y_max, 5 * y_res, needs_update);
    
    if (needs_update) {
        self->setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, x_max - x_min, 32);
        self->setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, y_max - y_min, 32);
        
        self->setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, (x_max - x_min + 1) * 100 / x_res, 32);
        self->setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, (y_max - y_min + 1) * 100 / y_res, 32);
        
        if (voodooInput) {
            VoodooInputDimensions dims = {
                static_cast<SInt32>(x_min), static_cast<SInt32>(x_max),
                static_cast<SInt32>(y_min), static_cast<SInt32>(y_max)
            };
            
            self->messageClient(kIOMessageVoodooInputUpdateDimensionsMessage, voodooInput, &dims, sizeof(dims));
        }
    }
}
