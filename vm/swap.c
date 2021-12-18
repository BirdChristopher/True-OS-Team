#include "devices/block.h"
#include "threads/malloc.h"
#include "swap.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

void swap_table_init()
{
    ss_block = block_get_role(BLOCK_SWAP);
    if (ss_block == NULL)
    {
        printf("WARNING: no available block device for swap\n");
    }
    block_sector_t block_num = block_size(ss_block);
    block_print_stats();
    space_size = block_num / 8;
    max_idx_block = 0;
}

bool free_swap_slot(int slot_idx)
{
    return false;
}

bool free_proc_slots(struct thread *proc)
{
    return true;
}

//返回true表示成功，false表示失败
bool swap_out_frame(struct frame_item *frame)
{
    lock_acquire(&swap_lock);
    int i, begin_sec, end_sec, j;
    void *frame_pt = frame->vaddr_pointer;
    char *tmp_pt;
    // printf("swap out paddr %p, uaddr is %p,current tid is %d,belonger is %d\n", frame->vaddr_pointer, frame->upage, thread_current()->tid, frame->user_proc->tid);
    for (i = 0; i < space_size; i++)
    {
        if (swap_table_in_use[i] == false)
        {
            swap_table_in_use[i] = true;
            swap_table_user_proc[i] = frame->user_proc;
            paddrs[i] = frame->vaddr_pointer;
            uaddrs[i] = frame->upage;
            // printf("CHECKPOINT3 IN swap_out_frame\n");
            begin_sec = 8 * i;
            end_sec = begin_sec + 7;
            for (j = 0; j < 8; j++)
            {
                tmp_pt = (char *)frame_pt + j * (BLOCK_SECTOR_SIZE);
                // printf("CHECKPOINT4 IN swap_out_frame\n");
                block_write(ss_block, begin_sec + j, tmp_pt);
            }
            // printf("CHECKPOINT5 IN swap_out_frame\n");
            if (i > max_idx_block)
                max_idx_block = i;
            // printf("swap out, addr is %p, upage is %p\n", paddrs[i], frame->upage);
            lock_release(&swap_lock);
            return true;
        }
    }
    lock_release(&swap_lock);
    return false;
}

bool swap_back_frame(struct frame_item *frame, int slot_idx)
{
    int begin_sec, end_sec, j;
    void *frame_pt = frame->vaddr_pointer;
    char *tmp_pt;
    lock_acquire(&swap_lock);
    // printf("before swap %d::%d,frame is %p\n", *(int *)frame_pt, *(int *)(frame_pt + 0x8ec), frame_pt);
    begin_sec = 8 * slot_idx;
    end_sec = begin_sec + 7;
    for (j = 0; j < 8; j++)
    {
        tmp_pt = (char *)frame_pt + j * (BLOCK_SECTOR_SIZE);
        block_read(ss_block, begin_sec + j, tmp_pt);
    }
    // printf("after swap %d::%d,frame is %p\n", *(int *)frame_pt, *(int *)(frame_pt + 0x8ec), frame_pt);
    swap_table_in_use[slot_idx] = false;
    lock_release(&swap_lock);
    return true;
}

int check_swap_table(struct thread *target_thread, void *addr)
{
    int i;
    lock_acquire(&swap_lock);
    void *page = pg_round_down(addr);
    for (i = 0; i <= max_idx_block; i++)
    {
        if (uaddrs[i] == page && swap_table_user_proc[i] == target_thread && swap_table_in_use[i])
        {
            lock_release(&swap_lock);
            return i;
        }
    }
    lock_release(&swap_lock);
    return -1;
}