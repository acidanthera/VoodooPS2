//
//  compat.cpp
//  VoodooPS2Controller
//
//  Created by user on 24/07/2019.
//  Copyright © 2019 vit9696. All rights reserved.
//

#include <libkern/c++/OSObject.h>

#ifdef __MAC_10_15

/**
 *  Ensure the symbol is not exported
 */
#define PRIVATE __attribute__((visibility("hidden")))

/**
 *  For private fallback symbol definition
 */
#define WEAKFUNC __attribute__((weak))

// macOS 10.15 adds Dispatch function to all OSObject instances and basically
// every header is now incompatible with 10.14 and earlier.
// Here we add a stub to permit older macOS versions to link.
// Note, this is done in both kern_util and plugin_start as plugins will not link
// to Lilu weak exports from vtable.

kern_return_t WEAKFUNC PRIVATE OSObject::Dispatch(const IORPC rpc) {
	(panic)("OSObject::Dispatch plugin stub called");
}

kern_return_t WEAKFUNC PRIVATE OSMetaClassBase::Dispatch(const IORPC rpc) {
	(panic)("OSMetaClassBase::Dispatch plugin stub called");
}

#endif
