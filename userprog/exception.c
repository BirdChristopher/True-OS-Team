#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);
static bool is_stack_access(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f);
static int is_access_swap_slot(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f);
static bool is_access_mmap(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f);
static bool is_valid_access(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f);
/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void exception_init(void)
{
   /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
   intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
   intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
   intr_register_int(5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

   /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
   intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
   intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
   intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
   intr_register_int(7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
   intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
   intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
   intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
   intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
   intr_register_int(19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

   /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
   intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void exception_print_stats(void)
{
   printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill(struct intr_frame *f)
{
   // printf("-1 in kill()\n");
   thread_current()->return_code = -1;
   if (thread_current()->parent != NULL)
   {
      sema_up(&thread_current()->parent->load_sem);
   }
   thread_exit();

   /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */

   /* The interrupt frame's code segment value tells us where the
     exception originated. */
   switch (f->cs)
   {
   case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf("%s: dying due to interrupt %#04x (%s).\n",
             thread_name(), f->vec_no, intr_name(f->vec_no));
      intr_dump_frame(f);
      thread_exit();

   case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame(f);
      PANIC("Kernel bug - unexpected interrupt in kernel");

   default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name(f->vec_no), f->cs);
      thread_exit();
   }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". 
   

   需要解决的问题：
   1. 如何判断是否是合法access？
      合法的：栈增长；文件映射；swap slot；
      非法：地址既不在page_table,也不在swap or file，也就是以上三种情况之外的情况？？？
   2. 如何判断是栈增长带来的page fault，还是页not present带来的page fault等？
   3. 如何fetch data？

   退出page_fault后，pintos将会重试执行eip指向的命令。
   */
static void
page_fault(struct intr_frame *f)
{
   bool not_present; /* True: not-present page, false: writing r/o page. */
   bool write;       /* True: access was write, false: access was read. */
   bool user;        /* True: access by user, false: access by kernel. */
   void *fault_addr; /* Fault address. */
   int idx;
   /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
   asm("movl %%cr2, %0"
       : "=r"(fault_addr));

   /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
   intr_enable();

   /* Count page faults. */
   page_fault_cnt++;

   /* Determine cause. */
   not_present = (f->error_code & PF_P) == 0;
   write = (f->error_code & PF_W) != 0;
   user = (f->error_code & PF_U) != 0;

   /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
   // printf("Page fault at %p: %s error %s page in %s context.\n",
   //        fault_addr,
   //        not_present ? "not present" : "rights violation",
   //        write ? "writing" : "reading",
   //        user ? "user" : "kernel");
   // printf("esp is %p right now, eip is %p right now\n", f->esp, f->eip);
   //会不会出现分一个页装不下的情况？？
   // printf("%d %d %d\n", not_present, write, user);
   if (is_valid_access(not_present, write, user, fault_addr, f)) //判断是否是合法引用
   {
      if ((idx = is_access_swap_slot(not_present, write, user, fault_addr, f)) != -1) //缺失页应从swap_slot中取回
      {
         // printf("page fault: trying to access swap slot\n");
         if (!reinstall_page(idx, pg_round_down(fault_addr)))
         {
            printf("swap back from slot fail\n");
            kill(f);
         }
      }
      else if (is_access_mmap(not_present, write, user, fault_addr, f)) //缺失页应从mmap中取出
         printf("a page should be fetched from mmap\n");
      else if (is_stack_access(not_present, write, user, fault_addr, f)) //普通的栈增长或者其他缺页情形，应分配一个全新页
      {
         // printf("try2stack grow right now\n");
         if (!add_stack_page(pg_round_down(fault_addr)))
         {
            printf("stack growth fail, all resources used up!\n");
            kill(f);
         }
         // printf("after try 2 grow stack\n");
      }
      else //不符以上任何条件，将直接KILL
      {
         // printf("page fault do not match any valid circumstance,kill,fault addr is %p,tid is %d\n", fault_addr, thread_current()->tid);
         kill(f);
      }
   }
   else
   {
      // printf("Page fault at %p: %s error %s page in %s context.\n",
      //        fault_addr,
      //        not_present ? "not present" : "rights violation",
      //        write ? "writing" : "reading",
      //        user ? "user" : "kernel");
      // printf("esp is %p right now, eip is %p right now\n", f->esp, f->eip);
      kill(f); //包括写入只读页面等其他情况
   }
}

static bool
is_valid_access(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f)
{
   if (!not_present || fault_addr == NULL || !is_user_vaddr(fault_addr))
      return false;
   return true;
}

static bool
is_stack_access(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f)
{
   // printf("delta is %d,not present is %d,write is %d,esp==0 is %d\n", delta, not_present ? 1 : 0, write ? 1 : 0, (int)esp == 0 ? 1 : 0);
   if (!write) //不可能向一个从未分配页空间的地址读数据，所以可以直接排除
      return false;

   int delta = (int)(f->esp - fault_addr);
   if (user)
   {
      if (delta == 4 || delta == 32) //PUSH or PUSHA
         return true;
      else if (delta <= 0)
         return is_user_vaddr(fault_addr);
   }
   else
      return is_user_vaddr(fault_addr);
}

static int
is_access_swap_slot(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f)
{
   //直接去swap_table查，查到则返回在block device中的序号，否则返回-1
   return check_swap_table(thread_current(), fault_addr);
   //TODO: kernel中查不到？？？
}

//TODO: 补充mmap的判定逻辑
static bool
is_access_mmap(bool not_present, bool write, bool user, void *fault_addr, struct intr_frame *f)
{
   return false;
}
