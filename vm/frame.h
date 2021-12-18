#ifndef VM_FRAME_H
#define VM_FRAME_H

// 用于有关frame table的接口
#include "lib/kernel/list.h"
#include "threads/palloc.h"

// 页框表。貌似用啥数据结构性能都不会有太大提升
struct list frame_table;

//注意，如果一个frame被释放不用了，那么需要将这个frame_item也释放掉
struct frame_item
{
    void *upage;              //该页框对应的user virtual addr
    uint32_t *vaddr_pointer;  //该页框对应的kernel virtual addr,一旦生成便不再改变
    bool used;                //是否已占用
    struct thread *user_proc; //标识使用这个frame的进程
    //todo: 可能需要其他信息
    struct list_elem elem;
};

struct list_elem *accessed_pt; //用于二次机会算法的指针
struct lock frame_lock;

bool if_frames_run_out();
struct frame_item *obtain_new_frame_core(enum palloc_flags flag);
void *obtain_new_frame(enum palloc_flags flag);
void free_frame(void *page_pt);
void free_process_frames(struct thread *thr);
struct frame_item *choose_frame_to_evict();
struct frame_item *find_frame_by_vaddr(void *vaddr);

#endif