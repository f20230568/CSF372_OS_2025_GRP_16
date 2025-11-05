#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

#define STACK_MAX (1024 * 1024)

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);


void
exception_init (void) 
{
 
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");


  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

static void
kill (struct intr_frame *f) 
{
  
  switch (f->cs)
    {
    case SEL_UCSEG:
   
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:

      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:

      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  
  bool write;        
  bool user;         
  void *fault_addr;  

  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  intr_enable ();
  page_fault_cnt++;

  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  bool success = false;

  if (user && not_present)
    {
      
      success = page_in(fault_addr);
      
      if (!success)
        {
          
          bool is_stack_access = 
                is_user_vaddr(fault_addr) &&
                (fault_addr >= (PHYS_BASE - STACK_MAX)) &&
                (fault_addr >= f->esp - 32); // Near the stack pointer (for PUSH)

          if (is_stack_access)
            {
             
              void *stack_page_vaddr = pg_round_down(fault_addr);
              
              
              struct page *new_page = page_allocate(stack_page_vaddr, false); // false = writable
              
              if (new_page != NULL)
                {
                  success = page_in(fault_addr);
                }
            }
        }
    }

  if (success)
    {
      return;
    }


  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
}