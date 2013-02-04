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

void* operator_new(size_t size)
{
    size_t* p = (size_t*)IOMalloc(sizeof(size_t) + size);
    if (p)
        *p++ = size;
    return p;
}

void operator_delete(void* p)
{
    assert(p);
    if (p)
    {
        size_t* t = (size_t*)p-1;
        IOFree(t, *t);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void* operator_new_array(size_t size)
{
    size_t* p = (size_t*)IOMalloc(sizeof(size_t) + size);
    if (p)
        *p++ = size;
    return p;
}

void operator_delete_array(void* p)
{
    assert(p);
    if (p)
    {
        size_t* t = (size_t*)p-1;
        IOFree(t, *t);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

