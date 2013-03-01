//
//  new_kext.h
//
//  Created by RehabMan on 2/3/13.
//  Copyright (c) 2013 rehabman. All rights reserved.
//

#ifndef _NEW_KEXT_H
#define _NEW_KEXT_H

#include <IOKit/IOLib.h>

extern "C"
{
    // helper functions for export
    void* _opnew(size_t size);
    void _opdel(void* p);
    void* _opnewa(size_t size);
    void _opdela(void *p);
} // extern "C"

// placement new
inline void* operator new(size_t, void* where) { return where; }

// global scope new/delete
inline void* operator new(size_t size) { return ::_opnew(size); }
inline void operator delete(void* p) { return ::_opdel(p); }

// global scope array new/delete
inline void* operator new[](size_t size) { return ::_opnewa(size); }
inline void operator delete[](void *p) { return ::_opdela(p); }

//REVIEW: seems that IOMallocAligned is broken in OS X... don't use it for now!
#define IOMallocAligned(x,y) IOMalloc(x)
#define IOFreeAligned(x,y) IOFree(x,y)

#endif // _NEW_KEXT_H
