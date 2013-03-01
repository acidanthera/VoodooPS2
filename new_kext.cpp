//
//  new_kext.cpp
//
//  Created by RehabMan on 2/3/13.
//  Copyright (c) 2013 rehabman. All rights reserved.
//
//  Full complement of operator new/delete for use within kext environment.
//

#include "new_kext.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// Note: Originally these helper functions were exported as C++ functions
//  instead of extern "C", and they had much longer names.
//
//  But that causes problems for Snow Leopard (crash using Kernel Cache at startup).
//  Not sure if it is related to the C++ names, or just the long names, but
//  this does work with the shorter extern "C" names, even on Snow Leopard.
//
//  Also realize that the calls to IOMallocAligned/IOFreeAligned are mapped to
//  IOMalloc/IOFree because there appears to be a bug in the aligned variants.
//  See new_kext.h for the macros...
//
// Note: For now we are not using this code, as it is easier to just use the
//  built-in operator new/delete.  I'm keeping it here, just in case it becomes
//  useful in the future.
//

extern "C"
{

void* _opnew(size_t size)
{
    size_t* p = (size_t*)IOMallocAligned(sizeof(size_t) + size, sizeof(void*));
    if (p)
        *p++ = size;
    return p;
}

void _opdel(void* p)
{
    assert(p);
    if (p)
    {
        size_t* t = (size_t*)p-1;
        IOFreeAligned(t, *t);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void* _opnewa(size_t size)
{
    size_t* p = (size_t*)IOMallocAligned(sizeof(size_t) + size, sizeof(void*));
    if (p)
        *p++ = size;
    return p;
}

void _opdela(void* p)
{
    assert(p);
    if (p)
    {
        size_t* t = (size_t*)p-1;
        IOFreeAligned(t, *t);
    }
}
    
} // extern "C"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

