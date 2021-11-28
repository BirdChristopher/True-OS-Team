#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "pagedir.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "kernel/list.h"
#include "lib/string.h"

static void
halt(struct intr_frame *frame)
{
  return;
}

static void
exit(struct intr_frame *frame)
{
  struct thread *cur = thread_current();
  void *kernel_esp = frame->esp;
  //此处应该无需检查指针合法性
  if (!pointer_check_valid(cur->pagedir, frame->esp + 4, 4))
  {
    cur->return_code = -1;
    thread_exit();
  }
  int status = *(int *)(kernel_esp + 4);
  cur->return_code = status;
  thread_exit();
}

static void
exec(struct intr_frame *frame)
{
  return;
}

static void
wait(struct intr_frame *frame)
{
  return;
}

//todo : 创建文件
static void
create(struct intr_frame *frame)
{
  struct thread *cur = thread_current();
  //参数地址frame->esp未定
  if (!pointer_check_valid(cur->pagedir, frame->esp + 24, 4)) //检查参数地址的合法性
  {
    cur->return_code = -1;
    thread_exit();
    // frame->eax = -1;
    // return;
  }
  char *file_name = *(char **)(frame->esp + 16);
  //创建文件名为空的文件，返回创建失败或直接以 -1 退出。
  if (file_name == NULL || !string_check_valid(cur->pagedir, file_name) || *file_name == '\0') //检查指针所指向空间的合法性
  {
    cur->return_code = -1;
    thread_exit();
    // frame->eax = -1;
    // return;
  }
  // printf("file_name is %s\n", file_name);
  // printf("file_name is %s\n", *(char **)(frame->esp + 8));
  // printf("file_name is %s\n", *(char **)(frame->esp + 16));
  // printf("file_name is %s\n", *(char **)(frame->esp + 20));
  // printf("file_name is %s\n", *(char **)(frame->esp + 24));
  // printf("file_name is %s\n", *(char **)(frame->esp + 28));
  lock_file();
  off_t size = *(off_t *)(frame->esp + 20);
  frame->eax = filesys_create(file_name, size);
  release_file();
  return;
}

//todo : 移除文件
static void
remove(struct intr_frame *frame)
{
  struct thread *cur = thread_current();
  if (!pointer_check_valid(cur->pagedir, frame->esp + 4, 4)) //检查参数地址的合法性
  {
    cur->return_code = -1;
    thread_exit();
    // frame->eax = -1;
    // return;
  }
  char *file_name = *(char **)(frame->esp + 4);
  //移除文件名为空的文件，返回创建失败或直接以 -1 退出。
  if (file_name == NULL || *file_name == '\0' || !string_check_valid(cur->pagedir, file_name)) //检查指针所指向空间的合法性
  {
    cur->return_code = -1;
    thread_exit();
    // frame->eax = -1;
    // return;
  }
  lock_file();
  frame->eax = filesys_remove(file_name);
  release_file();
  return;
}

static void
open(struct intr_frame *frame)
{
  struct thread *cur = thread_current();
  if (!pointer_check_valid(cur->pagedir, frame->esp + 4, 4)) //检查参数地址的合法性
  {
    cur->return_code = -1;
    thread_exit();
  }

  char *file_name = *(char **)(frame->esp + 4);
  if (file_name == NULL || !string_check_valid(cur->pagedir, file_name)) //检查指针所指向空间的合法性
  {
    cur->return_code = -1;
    thread_exit();
  }

  // printf("file_name is %s\n", file_name);

  if (*file_name == '\0') //检查文件名合法性
  {
    //fd=-1 lxt
    frame->eax = -1;
    return;
  }
  lock_file();
  struct file *file = filesys_open(file_name); //检查是否正常打开
  release_file();
  if (file == NULL)
  {
    frame->eax = -1;
    return;
  }

  struct fd_item *new_file_item = malloc(sizeof(struct fd_item));

  new_file_item->file = file;
  new_file_item->fd_num = ++cur->fd_num;

  list_push_back(&cur->fd_list, &new_file_item->elem);
  frame->eax = cur->fd_num;
  return;
}

static void
filesize(struct intr_frame *frame)
{
  return;
}

//todo : 读入文件
static void
read(struct intr_frame *frame)
{
  lock_file();
  struct thread *cur = thread_current();
  if (!pointer_check_valid(cur->pagedir, frame->esp + 28, 4)) //检查参数地址的合法性
  {
    cur->return_code = -1;
    thread_exit();
  }
  int fd = *(int *)(frame->esp + 20);
  uint8_t *buffer = *(uint8_t **)(frame->esp + 24);

  off_t size = *(off_t *)(frame->esp + 28);
  int actual_size = 0;

  //检查buffer是否合法
  if (!is_valid_user_pointer(buffer, 1) || !is_valid_user_pointer(buffer + size, 1))
  {
    cur->return_code = -1;
    thread_exit();
  }

  if (fd == 0)
  {
    for (int i = 0; i < size; i++)
    {
      buffer[i] = input_getc();
    }
    release_file();
    frame->eax = size;
  }

  else
  {
    // printf("1 fd is %d\n", fd);
    struct file *file = get_file_by_fd(fd);
    // printf("2\n");
    if (file != NULL && fd > 2)
    {
      // printf("here\n");
      // lock_file();
      frame->eax = file_read(file, (void *)(frame->esp + 24), size);
      release_file();
      // printf("file_size is %d\n", size);
      // printf("file_act is %d\n", frame->eax);
      // printf("fd is %d\n", *(int **)(frame->esp + 20));
      // printf("buffer is %s\n", *(char **)(frame->esp + 24));
      // while (true)
      // {
      // }
    }
    else
    {
      release_file();
      frame->eax = -1;
    }
    return;
  }
}

//todo : 写入文件
static void
write(struct intr_frame *frame) //todo:没写完
{
  struct thread *cur = thread_current();
  if (!pointer_check_valid(cur->pagedir, frame->esp + 28, 4))
  {
    cur->return_code = -1;
    thread_exit();
  }
  int fd = *(int *)(frame->esp + 20);
  uint8_t *buffer = *(uint8_t **)(frame->esp + 24);
  int size = *(int *)(frame->esp + 28);

  //检查buffer是否合法
  if (!is_valid_user_pointer(buffer, 1) || !is_valid_user_pointer(buffer + size, 1))
  {
    cur->return_code = -1;
    thread_exit();
  }

  //有的时候字符串太长需要截断，这是完全有可能的.
  int actual_size = 0;
  while (actual_size < size)
  {
    actual_size++;
  }

  if (fd == 1)
  {
    putbuf(buffer, actual_size);
  }
  else
  {
    struct file *file = get_file_by_fd(fd);
    if (file != NULL && fd > 2)
    {
      lock_file();
      actual_size = file_write(file, buffer, size);
      release_file();
    }
    else
    {
      actual_size = -1;
    }
  }
  frame->eax = actual_size;
  //返回值
  return;
}

static void
seek(struct intr_frame *frame)
{
  return;
}

static void
tell(struct intr_frame *frame)
{
  return;
}

static void
close(struct intr_frame *frame)
{
  struct thread *cur = thread_current();
  if (!pointer_check_valid(cur->pagedir, frame->esp + 4, 4))
  {
    cur->return_code = -1;
    thread_exit();
  }

  int fd = *(int *)(frame->esp + 4);

  struct list_elem *e;
  for (e = list_begin(&cur->fd_list); e != list_end(&cur->fd_list); e = list_next(e))
  {
    struct fd_item *item = list_entry(e, struct fd_item, elem);
    if (item->fd_num == fd)
    {
      list_remove(e);
      file_close(item->file);
      free(item);

      return;
    }
  }

  cur->return_code = -1; //如果关闭了不存在的fd或者fd 0,1,2, 直接报错-1退出
  thread_exit();
}

static void syscall_handler(struct intr_frame *);

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f UNUSED)
{
  struct thread *cur = thread_current();
  //检查ESP合法性
  if (!pointer_check_valid(cur->pagedir, f->esp, 4))
  {
    thread_current()->return_code = -1;
    // printf("invalid user pointer spotted! %8x\n", fake_esp);
    thread_exit();
  }

  int syscall_number = *(int *)(f->esp);
  switch (syscall_number)
  {
  case SYS_HALT:
    halt(f);
    break;

  case SYS_EXIT:
    exit(f);
    break;

  case SYS_EXEC:
    exec(f);
    break;

  case SYS_WAIT:
    wait(f);
    break;

  case SYS_CREATE:
    create(f);
    break;

  case SYS_REMOVE:
    remove(f);
    break;

  case SYS_OPEN:
    open(f);
    break;

  case SYS_FILESIZE:
    filesize(f);
    break;

  case SYS_READ:
    read(f);
    break;

  case SYS_WRITE:
    write(f);
    break;

  case SYS_SEEK:
    seek(f);
    break;

  case SYS_TELL:
    tell(f);
    break;

  case SYS_CLOSE:
    close(f);
    break;

  default:
    break;
  }
  // thread_exit(); 不应直接退出
}
