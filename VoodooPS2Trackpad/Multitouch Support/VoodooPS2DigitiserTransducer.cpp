//
//  VoodooI2CDigitiserTransducer.cpp
//  VoodooI2C
//
//  Created by Alexandre on 13/09/2017.
//  Copyright © 2017 Alexandre Daoud. All rights reserved.
//

#include "VoodooPS2DigitiserTransducer.hpp"

#define super OSObject
OSDefineMetaClassAndStructors(VoodooPS2DigitiserTransducer, OSObject);

bool VoodooPS2DigitiserTransducer::serialize(OSSerialize* serializer) {
    OSDictionary* temp_dictionary = OSDictionary::withCapacity(2);

    bool result = false;

    if (temp_dictionary) {
        temp_dictionary->setObject(kIOHIDElementParentCollectionKey, collection);
        temp_dictionary->serialize(serializer);
        temp_dictionary->release();

        result = true;
    }
    
    return result;
}

VoodooPS2DigitiserTransducer* VoodooPS2DigitiserTransducer::transducer(DigitiserTransducerType transducer_type, IOHIDElement* digitizer_collection) {
    VoodooPS2DigitiserTransducer* transducer = NULL;
    
    transducer = new VoodooPS2DigitiserTransducer;
    
    if (!transducer)
        goto exit;
    
    if (!transducer->init()) {
		transducer->release();
        transducer = NULL;
        goto exit;
    }
    
    transducer->type        = transducer_type;
    transducer->collection  = digitizer_collection;
    transducer->in_range    = false;

exit:
    return transducer;
}
