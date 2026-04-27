#ifndef __MEM_COMMON_H
#define __MEM_COMMON_H

#define TASK_COMM_LEN 16
#define MAX_ENTRIES 10240

enum event_type {
    EVENT_ALLOC = 1,
    EVENT_FREE  = 2,
};

struct mem_event {
    __u32 pid;
    __u32 tgid;
    char  comm[TASK_COMM_LEN];
    __u64 size;
    __u64 ptr;
    __u64 timestamp_ns;
    __s32 stack_id;
    __u32 type;
};

#endif /* __MEM_COMMON_H */
