#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "devices/block.h"
#include "vm/swap.h"
#include "threads/interrupt.h"
#include "vm/page.h"

static debug_func(int i);

bool if_frames_run_out()
{
    //todo
}

void *obtain_new_frame(enum palloc_flags flag)
{
    return obtain_new_frame_core(flag)->vaddr_pointer;
}

/*
    获得一个新的页，并将之加入到frame_table.
    在其中实现evict等其他算法
    内核中需要空间时不应该用该函数包装，因为那些空间取自kernel pool
    暂时只考虑从用户空间取页，内核空间不归我管(
*/
struct frame_item *obtain_new_frame_core(enum palloc_flags flag)
{
    // lock_acquire(&frame_lock);
    void *new_frame_pointer = palloc_get_page(flag);
    if (new_frame_pointer == NULL)
    {
        struct frame_item *victim = choose_frame_to_evict();

        if (!swap_out_frame(victim))
            return NULL;
        struct thread *victim_proc = victim->user_proc;
        pagedir_clear_page(victim->user_proc->pagedir, victim->upage);
        //TODO 按要求将新页面清零
        struct list spl_pt = victim_proc->page_table;
        struct page_list_item *page_item = find_page_in_spl(&victim_proc->page_table, victim->upage, true);
        page_item->present = false;
        //frame_table中修改
        victim->user_proc = thread_current(); //新页的upage的设置工作在register_spl_pte中完成
        // lock_release(&frame_lock);
        return victim;
    }
    else
    {
        struct frame_item *new_item = malloc(sizeof(struct frame_item));
        if (new_item == NULL) //若kernel pool中的page不足
        {
            palloc_free_page(new_frame_pointer);
            return NULL;
        }
        new_item->used = true;
        new_item->vaddr_pointer = new_frame_pointer;
        new_item->user_proc = thread_current();
        list_push_back(&frame_table, &new_item->elem);
        // lock_release(&frame_lock);
        return new_item;
    }
}

void free_frame(void *frame_pt)
{
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_item *item = list_entry(e, struct frame_item, elem);
        if (item->vaddr_pointer == frame_pt)
        {
            list_remove(&item->elem);
            palloc_free_page(item->vaddr_pointer);
            free(item);
        }
    }
}

void free_process_frames(struct thread *thread)
{
    struct list_elem *e, *tmp_elem;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_item *item = list_entry(e, struct frame_item, elem);
        if (item->user_proc == thread)
        {
            tmp_elem = e->prev;
            list_remove(&item->elem);
            palloc_free_page(item->vaddr_pointer);
            free(item);
            e = tmp_elem;
        }
    }
    //TODO: 还有被swap掉的所有frame！！
}

//将一个链表循环两遍装作循环链表::二次机会算法
struct frame_item *choose_frame_to_evict()
{
    // enum intr_level old_level = intr_get_level();
    // intr_disable();
    if (accessed_pt == NULL)
        accessed_pt = list_begin(&frame_table);

    for (; accessed_pt != list_end(&frame_table); accessed_pt = list_next(accessed_pt))
    {
        struct frame_item *frame = list_entry(accessed_pt, struct frame_item, elem);
        if (pagedir_is_accessed(frame->user_proc->pagedir, frame->upage))
            pagedir_set_accessed(frame->user_proc->pagedir, frame->upage, false); //给予第二次机会
        else
        {
            accessed_pt = list_next(accessed_pt); //退出前为下一次运行算法作准备
            return frame;
        }
    }

    for (accessed_pt = list_begin(&frame_table); accessed_pt != list_end(&frame_table); accessed_pt = list_next(accessed_pt))
    {
        struct frame_item *frame = list_entry(accessed_pt, struct frame_item, elem);
        if (pagedir_is_accessed(frame->user_proc->pagedir, frame->upage))
            pagedir_set_accessed(frame->user_proc->pagedir, frame->upage, false);
        else
        {
            accessed_pt = list_next(accessed_pt);
            return frame;
        }
    }
}

struct frame_item *find_frame_by_vaddr(void *vaddr)
{
    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_item *item = list_entry(e, struct frame_item, elem);
        if (item->vaddr_pointer == vaddr)
            return item;
    }
    return NULL;
}