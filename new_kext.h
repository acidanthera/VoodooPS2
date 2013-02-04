//
//  new_kext.h
//
//  Created by RehabMan on 2/3/13.
//  Copyright (c) 2013 rehabman. All rights reserved.
//

#ifndef _NEW_KEXT_H
#define _NEW_KEXT_H

#include <IOKit/IOLib.h>

// helper functions for export
void* operator_new(size_t size);
void operator_delete(void* p);
void* operator_new_array(size_t size);
void operator_delete_array(void *p);

// placement new
inline void* operator new(size_t, void* where) { return where; }

// global scope new/delete
inline void* operator new(size_t size) { return ::operator_new(size); }
inline void operator delete(void* p) { return ::operator_delete(p); }

// global scope array new/delete
inline void* operator new[](size_t size) { return ::operator_new_array(size); }
inline void operator delete[](void *p) { return ::operator_delete_array(p); }

#endif // _NEW_KEXT_H
