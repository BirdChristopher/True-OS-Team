#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "lib/kernel/list.h"
#include "vm/frame.h"
#include "threads/malloc.h"

/*
    swap table的字段

    int idx;                    //表项的索引
    bool in_use;                //是否被占用
    struct thread *user_proc;   //使用者是哪个线程
    void* addr;                 //对应的虚拟地址
*/

struct block *ss_block;

int space_size;
int max_idx_block;

struct lock swap_lock;

bool swap_table_in_use[1024]; //简单用一个数组来表示所有的swap_slots
struct thread *swap_table_user_proc[1024];
void *paddrs[1024];
void *uaddrs[1024];

void swap_table_init();

bool free_swap_slot(int slot_idx);

bool free_proc_slots(struct thread *);

bool swap_out_frame(struct frame_item *);

bool swap_back_frame(struct frame_item *, int);

int check_swap_table(struct thread *, void *);

#endif