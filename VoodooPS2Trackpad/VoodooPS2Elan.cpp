//
//  VoodooPS2Elan.cpp
//  VoodooPS2Trackpad
//
//  Created by Kishor Prins, Bartosz Korczyński on 24/06/2020.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#include "VoodooPS2Elan.h"

#include "VoodooInputMultitouch/VoodooInputTransducer.h"
#include "VoodooInputMultitouch/VoodooInputMessages.h"

bool ApplePS2Elan::init(OSDictionary * properties) {
    if (!super::init(properties)) {
        return false;
    }

    return true;
}

ApplePS2Elan* ApplePS2Elan::probe(IOService* provider, SInt32* score) {

    return this;
}

bool ApplePS2Elan::start(IOService* provider) {
    if(!super::start(provider)) {
        return false;
    }
    return true;
}

void ApplePS2Elan::stop(IOService* provider) {
    super::stop(provider);
};


bool ApplePS2Elan::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();

        return true;
    }

    return super::handleOpen(forClient, options, arg);
}

void ApplePS2Elan::handleClose(IOService *forClient, IOOptionBits options) {
    OSSafeReleaseNULL(voodooInputInstance);
    super::handleClose(forClient, options);
}
