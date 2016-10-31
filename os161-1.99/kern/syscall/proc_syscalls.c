#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include "opt-A2.h"

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);
  
  // free pid and resolve parent/children relationship
  KASSERT(curproc->info != NULL);
  
#if OPT_A2
  lock_acquire(pid_table_lock);
  
  struct process_info *pinfo = curproc->info;
  
  // free pid process:
  // check if parent is freed
  // clean up children first
  struct process_info *children = pinfo->child_link;
  struct process_info *prev = NULL;
  
  while (children != NULL){
      
      // delete and free pid for all children who parent has not waited and died.
      if (children->exit_status == true){
        struct process_info *del = children;
        pid_table[children->pid] = false;
        children = children->next_sibling;
        destroy_pinfo(del);
        
        // link the previous and the next one
        if (prev != NULL){
            prev->next_sibling = children->next_sibling;
        }
        
      }else {
        children->parent = NULL;
        prev = children;
        children = children->next_sibling;
      }
  }
  
  if (pinfo->parent == NULL){
      // since there's no parent, the exit status is not insteresting
      pid_table[pinfo->pid] = false;
      destroy_pinfo(pinfo);
  }else{
      // change status
      pinfo->exit_status = true;
      pinfo->exit_code = exitcode;
  
      cv_broadcast(pid_table_cv, pid_table_lock);
  }
  
  lock_release(pid_table_lock);
  
#endif
  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
    
#if OPT_A2
    KASSERT(curproc != NULL);
    *retval = (curproc->info)->pid;
#else
    /* for now, this is just a stub that always returns a PID of 1 */
    /* you need to fix this to make it work properly */
    *retval = 1;
#endif
    return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */
#if OPT_A2
    if (options != WAIT_MYPGRP){
        return EINVAL;
    }
    
    if (pid < 0 || pid > PID_MAX){
        return ESRCH;
    }
    
    lock_acquire(pid_table_lock);
    
    //find the children first
    struct process_info *pinfo = (curproc->info)->child_link;
    
    while (pinfo != NULL){
        if (pinfo->pid == pid)
            break;
            
        pinfo = pinfo->next_sibling;
    }
    
    //check if its a children
    if (pinfo == NULL || pinfo->pid != pid){
        if (pid_table[pid]){
            lock_release(pid_table_lock);
            return ECHILD; // not a child
        }else{
            lock_release(pid_table_lock);
            return ESRCH; // no such child
        }
    }
    
    //check if child has exited
    while (pinfo->exit_status == false){
        cv_wait(pid_table_cv, pid_table_lock);
    }
    
    exitstatus = pinfo->exit_code;
    lock_release(pid_table_lock);
    
#else

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  
#endif

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2
int 
sys_fork(struct trapframe *tf, pid_t *retval){
    
    KASSERT(curproc != NULL);
    
    //Step1: Create new name for the children proc
    char *child_name = kmalloc(sizeof(char) * NAME_MAX);
    strcpy(child_name, curproc->p_name);
    strcat(child_name, "_children");
    
    //Step 1:Create process structure for child process
    struct proc *child_proc = proc_create_runprogram_sub(child_name);
    
    if (child_proc == NULL){
        kfree(child_name);
        return ENOMEM; // out of memory
    }
    
    //Step2: Create and Copy address space from parent
    struct addrspace *child_addsp = NULL;
    
    // Also trapframe
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    
    if (child_tf == NULL){
        kfree(child_name);
        kfree(child_proc);
        return ENOMEM; // out of memory
    }
    
    // copy address space
    int err = as_copy(curproc->p_addrspace, &child_addsp);
    
    if (err){
        kfree(child_name);
        kfree(child_proc);
        kfree(child_tf);
        return err;
    }

    //Step3: Attach newly copied     address space to child
    child_proc->p_addrspace = child_addsp;// attach to children proc
    memcpy(child_tf, tf, sizeof(struct trapframe));// deep copy trapframe
    
    
    //Step4: Assign PID and create parent/child relationship
    
    // find a free pid slot
    lock_acquire(pid_table_lock);
    
    pid_t pid = find_free_pid();
    
    if (pid == -1){
        lock_release(pid_table_lock);
        *retval = -1;
        return ENPROC; 
    }
    
    pid_table[pid] = true;
    
    lock_release(pid_table_lock);
    
    //create process_info and parent-child relationship
    child_proc->info = create_pinfo();
    add_child_proc(curproc, child_proc);
    add_pid(child_proc->info, pid);
    
    //Step5: Fork the thread
    
    void **void_package = kmalloc(sizeof(void *)*2);
    void_package[0] = (void *)child_tf;
    void_package[1] = (void *)child_addsp;
    
    int result = thread_fork(child_name, child_proc, &enter_forked_process, void_package, 0);
    
    if (result) {
        kfree(child_name);
        kfree(child_tf);
        as_destroy(child_addsp);
        proc_destroy(child_proc);
        return ENOMEM; // out of memory
    }
    kprintf("create process reached2/n");
    
    *retval = (child_proc->info)->pid;
    
    return (0);
}

#endif /* OPT_A2 */
