#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "lib/kernel/hash.h"

enum page_usage
{
    STACK_PAGE,
    HEAP_PAGE
};

struct page_list_item
{
    struct list_elem elem;
    void *paddr;
    void *uaddr; //KEY,用户空间虚拟地址
    bool writable;
    bool present;
    enum page_usage usage;
};

struct page_list_item *find_page_in_spl(struct list *target_table, void *vaddr, bool present);

bool register_spl_pte(struct list *, void *, void *, bool, enum page_usage);

bool add_stack_page(void *);

bool reinstall_page(int slot_idx, void *upage);

void free_list_item(struct list_elem *e, void *aux);

#endif