#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/interrupt.h"

bool register_spl_pte(struct list *target_list, void *upage, void *kpage, bool writable, enum page_usage usage)
{
    struct page_list_item *new_item = malloc(sizeof(struct page_list_item));
    // printf("now in register_spl_pte,checkpoint 0,new_item pt is %d\n", new_item == NULL ? 0 : 1111);
    new_item->paddr = kpage;
    new_item->uaddr = upage;
    new_item->writable = writable;
    new_item->present = true;
    new_item->usage = usage;
    // printf("now in register_spl_pte,checkpoint 1\n");
    struct frame_item *frame_item = find_frame_by_vaddr(kpage);
    frame_item->upage = upage;

    list_push_back(target_list, &new_item->elem);

    return true;
}

//在这里只考虑拿，从哪里拿不在此处实现
bool add_stack_page(void *upage)
{
    struct thread *cur = thread_current();
    // printf("before obtain new frame\n");
    struct frame_item *new_frame = obtain_new_frame_core(PAL_USER | PAL_ZERO);
    if (new_frame == NULL)
        return false;

    new_frame->upage = upage;

    install_page(upage, new_frame->vaddr_pointer, true);
    if (!register_spl_pte(&cur->page_table, upage, new_frame->vaddr_pointer, true, STACK_PAGE))
    {
        //页表注册失败，需要释放已申请的页框
        printf("register fail,upage is %p\n", upage);
        free_frame(new_frame);
        return false;
    }
    return true;
}

void free_list_item(struct list_elem *e, void *aux)
{
    struct page_list_item *item = list_entry(e, struct page_list_item, elem);
    free(item);
}

/* 在链表中查找对应虚拟地址的记录 */
struct page_list_item *find_page_in_spl(struct list *target_table, void *uaddr, bool present)
{
    struct list_elem *l_e = NULL;
    for (l_e = list_begin(target_table); l_e != list_end(target_table); l_e = list_next(l_e))
    {
        struct page_list_item *item = list_entry(l_e, struct page_list_item, elem);
        if (item->uaddr == uaddr && item->present == present)
        {
            return item;
        }
    }
    return NULL;
}

/*
    传入目标数据所在的块的下标，尝试在内存中寻找一个页，并将block device中的目标数据抄到这个页中。
    
    新页的获取方式无需在本函数中考虑。
*/
bool reinstall_page(int slot_idx, void *upage)
{
    struct frame_item *f = obtain_new_frame_core(PAL_USER);
    if (f == NULL)
    {
        printf("resources all used up!\n");
        return false;
    }

    // enum intr_level old_level = intr_get_level();
    // intr_disable();

    f->upage = upage;
    swap_back_frame(f, slot_idx);
    pagedir_set_page(thread_current()->pagedir, upage, f->vaddr_pointer, true);
    struct page_list_item *entry = find_page_in_spl(&thread_current()->page_table, upage, false);
    entry->present = true;

    return true;
}