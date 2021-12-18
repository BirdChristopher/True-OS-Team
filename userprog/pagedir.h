#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/synch.h"

struct lock pagedir_lock;

uint32_t *pagedir_create(void);
void pagedir_destroy(uint32_t *pd);
bool pagedir_set_page(uint32_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page(uint32_t *pd, const void *upage, int size);
void pagedir_clear_page(uint32_t *pd, void *upage);
void pagedir_restore_page(uint32_t *pd, void *upage);

bool pagedir_is_dirty(uint32_t *pd, const void *upage);
void pagedir_set_dirty(uint32_t *pd, const void *upage, bool dirty);

bool pagedir_is_accessed(uint32_t *pd, const void *upage);
void pagedir_set_accessed(uint32_t *pd, const void *upage, bool accessed);

bool pagedir_is_readonly(uint32_t *pd, const void *upage, const void *paddr);

void pagedir_activate(uint32_t *pd);
bool pointer_check_valid(uint32_t *pd, const void *uaddr, int size);
bool string_check_valid(uint32_t *pd, const char *str_pt);
bool is_valid_read_buffer_addr(void *stack_pt, void *buf_addr);
#endif /* userprog/pagedir.h */
