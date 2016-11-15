#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include "mips/trapframe.h"
#include <vfs.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>

#include <kern/fcntl.h>
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
  
   // change status
  pinfo->exit_status = true;
  pinfo->exit_code = exitcode;
  
  if (pinfo->parent == NULL){
      // since there's no parent, the exit status is not insteresting
      pid_table[pinfo->pid] = false;
      destroy_pinfo(pinfo);
  }
  
  cv_broadcast(pid_table_cv, pid_table_lock);
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
    
    //find the children first
    struct process_info *pinfo = (curproc->info)->child_link;
    
    while (pinfo != NULL){
        if (pinfo->pid == pid){
            break;
        }
        pinfo = pinfo->next_sibling;
    }
    
    lock_acquire(pid_table_lock);
    
    //check if its a children
    if (pinfo == NULL || pinfo->pid != pid){
        if (pid_table[pid]){
            lock_release(pid_table_lock);
            return ECHILD; // not a child
        }else{
            kprintf("O\n");
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
    as_copy(curproc->p_addrspace, &child_addsp);
    
    if (child_addsp == NULL){
        kfree(child_name);
        kfree(child_proc);
        kfree(child_tf);
        *retval = -1;
        return ENOMEM;
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
    KASSERT(child_proc->info != NULL);
    //Step5: Fork the thread
    
    void **void_package = kmalloc(sizeof(void *)*2);
    void_package[0] = (void *)child_tf;
    void_package[1] = (void *)child_addsp;
    
    int result = thread_fork(child_name, child_proc, &enter_forked_process, void_package, 0);
    
    if (result) {
        kfree(child_name);
        kfree(child_tf);
        as_destroy(child_addsp);
        lock_acquire(pid_table_lock);
        pid_table[pid] = false;
        lock_release(pid_table_lock);
        proc_destroy(child_proc);
        return ENOMEM; // out of memory
    }
    
    KASSERT(retval != NULL);
    
    *retval = (child_proc->info)->pid;
    return (0);
}

int
sys_execv(int *retval, userptr_t program, userptr_t args){
    
    int result;
    
    *retval = -1;
    //int argc = 0;
    
    // copy arguments and program name into the kernal
    int argc;
    char **argcpy = copying_arg(program, args, &argc); // argc are expected to be at least 1
    
    if (argcpy == NULL){
        return E2BIG; // out of memory
    }
    
    result = runprog(argc, argcpy, true);
    
    return result;
}

char **
copying_arg(userptr_t program, userptr_t args, int *count){
    
    // count the number of args
    int arg_count = 1;
    char **current_arg = (char ** )args;
    
    while (current_arg != NULL){
        arg_count++;
        current_arg++;
    }
    
    //allocate args, giving one extra NULL pointer at the end to avoid errors
    char **arg_return = kmalloc(sizeof(char *) * (arg_count+1));
    
    if (arg_return == NULL){
        return NULL; // out of memory
    }
    
    //copy the program name as the first arguement
    arg_return[0] = kstrdup((const char *) program);
    if (arg_return[0] == NULL){
        kfree(arg_return);
        return NULL; // out of memory
    }
    
    //Assign all NULL pointers
    for (int i = 1; i < arg_count + 1; i++){
        arg_return[i] = NULL;
    }
    
    //copy args
    current_arg = (char **) args; // starting from argument 0
    for (int i = 1; i < arg_count; i++){
        arg_return[i] = kstrdup((const char *) current_arg[i-1]);
        
        // perform cleanup if error
        if (arg_return[i] == NULL){
            runprog_cleanup(arg_count, arg_return);
            break;
        }
    }
    
    *count = arg_count;
    
    return arg_return;
}

void runprog_cleanup(int argc, char **args){
    
    for (int i = 0 ; i < argc; i++){
        if (args[i] != NULL){
            kfree(args[i]);
        }
    }
    
    kfree(args);
}


int
runprog(int arg_count, char **args, bool km_used)
{
	struct addrspace *as, *oldas;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

    char *progname = kstrdup(args[0]);
    if (progname == NULL){
        return E2BIG;
    }
    
	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
    kfree(progname); // free the name
	if (result) {
		return result;
	}

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	oldas = curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
        // cleanup and reactivate old address space (execv should maintain the original process if it failed)
        as_deactivate();
        as_destroy(as);
        
        if (oldas){
            curproc_setas(oldas);
            as_activate();
        }
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
        as_deactivate();
        as_destroy(as);
        
        if (oldas){
            curproc_setas(oldas);
            as_activate();
        }
		return result;
	}
    
    //userptr_t user_arg = args_to_userspace(&stackptr, arg_count, args);// align arguments to the user stack
    
    if (km_used){
        runprog_cleanup(arg_count, args);
    }
    
    (void) user_arg;
    
	/* Warp to user mode. */
	enter_new_process(0, NULL, stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

userptr_t
args_to_userspace(vaddr_t *stackptr_, int argc, char **args)
{
    vaddr_t stackptr = *stackptr_;
    char **user_arg;
    
    stackptr -= sizeof(char *) * (argc + 1); // give enough space to the stack
    user_arg = (char **)stackptr;
    
    //copy the strings to the stack
    for (int i = 0; i < argc; i++){
        int len = strlen(args[i]) + 1;
        // move stack ptr
        stackptr -= len;
        user_arg[i] = (char *) stackptr;
        copyout((const void *) args[i], (userptr_t) stackptr, len);
    }
    
    user_arg[argc] = NULL; // append a null to the end the user stack
    
    *stackptr_ = stackptr;
    
    return (userptr_t) user_arg;
}

#endif /* OPT_A2 */
