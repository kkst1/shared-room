#ifndef __MEM_USER_H
#define __MEM_USER_H

#include "../include/mem_common.h"

static const char *event_type_str(__u32 type)
{
    switch (type) {
    case EVENT_ALLOC: return "ALLOC";
    case EVENT_FREE:  return "FREE";
    default:          return "UNKNOWN";
    }
}

#endif /* __MEM_USER_H */
