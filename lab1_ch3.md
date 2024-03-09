多核任务主要修改：
1. 在 `kernel.c` 和 `config.h` 中为不同CPU 划分不同空间
```c
config.h:
// user stack top
#define USER_STACK(i) (0x81100000 + i * STACK_SIZE)

// the stack used by PKE kernel when a syscall happens
#define USER_KSTACK(i) (0x81200000 + i * STACK_SIZE)

// the trap frame used to assemble the user "process"
#define USER_TRAP_FRAME(i) (0x81300000 + i * STACK_SIZE)
```

```c
kernel.c:
void load_user_program(process *proc) {
    int hart_id = read_tp();
//    sprint("-----hartid = %d------\n", hart_id);
  // USER_TRAP_FRAME is a physical address defined in kernel/config.h
  proc->trapframe = (trapframe *)(uint64)USER_TRAP_FRAME(hart_id);
  memset(proc->trapframe, 0, sizeof(trapframe));
  // USER_KSTACK is also a physical address defined in kernel/config.h
  proc->kstack = USER_KSTACK(hart_id);
  proc->trapframe->regs.sp = USER_STACK(hart_id);
  proc->trapframe->regs.tp = hart_id;

  // load_bincode_from_host_elf() is defined in kernel/elf.c
  load_bincode_from_host_elf(proc);
}
```

2. 在 `minit.c` 和 `syscall.c` 中分别设置启动和退出同步
```c
minit.c:
    if (hartid == 0) {
      // init the spike file interface (stdin,stdout,stderr)
      // functions with "spike_" prefix are all defined in codes under spike_interface/,
      // sprint is also defined in spike_interface/spike_utils.c
      spike_file_init();
//      sprint("In m_start, hartid:%d\n", hartid);

      // init HTIF (Host-Target InterFace) and memory by using the Device Table Blob (DTB)
      // init_dtb() is defined above.
      init_dtb(dtb);
    }
    sync_barrier(&counter, NCPU);
    sprint("In m_start, hartid:%d\n", hartid);
    write_tp(hartid);
```

```c
syscall.c:
static volatile int exit_counter = 0;
ssize_t sys_user_exit(uint64 code) {
    int hart_id = read_tp();
//  sprint("hartid = ?: User exit with code:%d.\n", code);
    sprint("hartid = %d: User exit with code:%d.\n", hart_id, code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
//  sprint("hartid = ?: shutdown with code:%d.\n", code);
    sync_barrier(&exit_counter, NCPU);
    if (hart_id == 0) {
        sprint("hartid = %d: shutdown with code:%d.\n", hart_id, code);
        shutdown(code);
    }
    return 0;
}
```

3. 在 `strap.c` 和 `process.c` 中进行时钟同步，进程等资源的隔离
```c
stap.c:
static uint64 g_ticks[NCPU] = {0};
//
// added @lab1_3
//
void handle_mtimer_trap() {
    int hart_id = read_tp();
  sprint("Ticks %d\n", g_ticks[hart_id]);
  // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
  // field in sip register.
  // hint: use write_csr to disable the SIP_SSIP bit in sip.
//  panic( "lab1_3: increase g_ticks by one, and clear SIP field in sip register.\n" );
  g_ticks[hart_id]++;
    write_csr(sip,0);

}
```

```c
process.c:
process* current[NCPU] = {NULL};
```

4. 在 `elf.c` 中读入不同CPU对应的文件
```c
void load_bincode_from_host_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");
    int hart_id = read_tp();

//  sprint("hartid = ?: Application: %s\n", arg_bug_msg.argv[0]);

//    sprint("-----hartid = %d------\n", hart_id);
    sprint("hartid = %d: Application: %s\n",hart_id , arg_bug_msg.argv[hart_id]);
  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

//  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
    info.f = spike_file_open(arg_bug_msg.argv[hart_id], O_RDONLY, 0);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the host spike file
  spike_file_close( info.f );

//  sprint("hartid = ?: Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
    sprint("hartid = %d: Application program entry point (virtual address): 0x%lx\n", hart_id, p->trapframe->epc);

}
```