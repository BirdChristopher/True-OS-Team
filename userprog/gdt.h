#ifndef USERPROG_GDT_H
#define USERPROG_GDT_H

#include "threads/loader.h"

/* Segment selectors.
   More selectors are defined by the loader in loader.h. */
//就是所谓的段序号，用于在segment table中查找
#define SEL_UCSEG 0x1B /* User code selector. */
#define SEL_UDSEG 0x23 /* User data selector. */
#define SEL_TSS 0x28   /* Task-state segment. */
#define SEL_CNT 6      /* Number of segments. */

void gdt_init(void);

#endif /* userprog/gdt.h */
