/*
 * Utility functions for trap handling in Supervisor mode.
 */

#include "riscv.h"
#include "process.h"
#include "strap.h"
#include "syscall.h"
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"

//
// handling the syscalls. will call do_syscall() defined in kernel/syscall.c
//
static void handle_syscall(trapframe *tf) {
  // tf->epc points to the address that our computer will jump to after the trap handling.
  // for a syscall, we should return to the NEXT instruction after its handling.
  // in RV64G, each instruction occupies exactly 32 bits (i.e., 4 Bytes)
  tf->epc += 4;

  // TODO (lab1_1): remove the panic call below, and call do_syscall (defined in
  // kernel/syscall.c) to conduct real operations of the kernel side for a syscall.
  // IMPORTANT: return value should be returned to user app, or else, you will encounter
  // problems in later experiments!
  //panic( "call do_syscall to accomplish the syscall and lab1_1 here.\n" );
  tf->regs.a0=do_syscall((tf->regs.a0),(tf->regs.a1), (tf->regs.a2), (tf->regs.a3), (tf->regs.a4), (tf->regs.a5),(tf->regs.a6), (tf->regs.a7));

}

//
// global variable that store the recorded "ticks". added @lab1_3
static uint64 g_ticks = 0;
//
// added @lab1_3
//
void handle_mtimer_trap() {
  sprint("Ticks %d\n", g_ticks);
  // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
  // field in sip register.
  // hint: use write_csr to disable the SIP_SSIP bit in sip.
  //panic( "lab1_3: increase g_ticks by one, and clear SIP field in sip register.\n" );
  g_ticks++;
  write_csr(sip, 0);

}

//
// the page fault handler. added @lab2_3. parameters:
// sepc: the pc when fault happens;
// stval: the virtual address that causes pagefault when being accessed.
//
void handle_user_page_fault(uint64 mcause, uint64 sepc, uint64 stval) {
  sprint("handle_page_fault: %lx\n", stval);
  switch (mcause) {
    case CAUSE_STORE_PAGE_FAULT:
      // TODO (lab2_3): implement the operations that solve the page fault to
      // dynamically increase application stack.
      // hint: first allocate a new physical page, and then, maps the new page to the
      // virtual address that causes the page fault.
      {
      void* pa = alloc_page();
      //分配一个物理页，将所分配的物理页面映射到stval所对应的虚拟地址上
      user_vm_map((pagetable_t)current->pagetable, stval, 1, (uint64)pa, prot_to_type(PROT_WRITE | PROT_READ, 1));
      }
      //panic( "You need to implement the operations that actually handle the page fault in lab2_3.\n" );

      break;
    default:
      sprint("unknown page fault.\n");
      break;
  }
}

//
// implements round-robin scheduling. added @lab3_3
//
void rrsched() {
  // TODO (lab3_3): implements round-robin scheduling.
  // hint: increase the tick_count member of current process by one, if it is bigger than
  // TIME_SLICE_LEN (means it has consumed its time slice), change its status into READY,
  // place it in the rear of ready queue, and finally schedule next process to run.
  current->tick_count++;
  if(current->tick_count >= TIME_SLICE_LEN)
  {
    current->tick_count=0;
    current->status = READY;
    insert_to_ready_queue( current );
    schedule();
  }
  //panic( "You need to further implement the timer handling in lab3_3.\n" );

}

//
// kernel/smode_trap.S will pass control to smode_trap_handler, when a trap happens
// in S-mode.
//
void smode_trap_handler(void) {
  // make sure we are in User mode before entering the trap handling.
  // we will consider other previous case in lab1_3 (interrupt).
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  assert(current);
  // save user process counter.
  current->trapframe->epc = read_csr(sepc);

  // if the cause of trap is syscall from user application.
  // read_csr() and CAUSE_USER_ECALL are macros defined in kernel/riscv.h
  uint64 cause = read_csr(scause);

  // use switch-case instead of if-else, as there are many cases since lab2_3.
  switch (cause) {
    case CAUSE_USER_ECALL:
      handle_syscall(current->trapframe);
      break;
    case CAUSE_MTIMER_S_TRAP:
      handle_mtimer_trap();
      // invoke round-robin scheduler. added @lab3_3
      rrsched();
      break;
    // added @lab5_2
    case CAUSE_MEXTERNEL_S_TRAP:
      {
        //reset the PLIC so that we can get the next external interrupt.
        volatile int irq = *(uint32 *)0xc201004L;
        *(uint32 *)0xc201004L = irq;
        volatile int *ctrl_reg = (void *)(uintptr_t)0x6000000c;
        *ctrl_reg = *ctrl_reg | (1 << 4);
        // TODO (lab5_2): implment the case of CAUSE_MEXTERNEL_S_TRAP.
        // hint: the case of CAUSE_MEXTERNEL_S_TRAP is to get data from UART address and wake 
        // the process. therefore, you need to construct an update_uartvalue_ctx structure
        // then store the interrupt processing process pid and the uart value in it
        // and use this structure to update the wake callback context of the process
        // finally call do_wake to wake up the process.
        volatile uint32 *rx = (void*)(uintptr_t)0x60000000;
        uint32 data = *rx;

        uint64 handle_intr_pid = 0;  // the pid of the process that will handle this interrupt
        struct update_uartvalue_ctx *ctx = (struct update_uartvalue_ctx *)alloc_page();
        ctx->uartvalue = (char)data;
        ctx->pid = handle_intr_pid;
        set_wake_callback(handle_intr_pid, NULL, ctx);  // add wake callback context

        do_wake(handle_intr_pid);
        //panic( "You have to implement CAUSE_MEXTERNEL_S_TRAP to get data from UART and wake the process 0 in lab5_2.\n" );

        break;
      }
    case CAUSE_STORE_PAGE_FAULT:
    case CAUSE_LOAD_PAGE_FAULT:
      // the address of missing page is stored in stval
      // call handle_user_page_fault to process page faults
      handle_user_page_fault(cause, read_csr(sepc), read_csr(stval));
      break;
    default:
      sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
      sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
      panic( "unexpected exception happened.\n" );
      break;
  }

  // continue (come back to) the execution of current process.
  switch_to(current);
}
