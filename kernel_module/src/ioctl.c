//////////////////////////////////////////////////////////////////////
//                      North Carolina State University
//
//
//
//                             Copyright 2018
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng, Yu-Chia Liu
//
//   Description:
//     Core of Kernel Module for Processor Container
//
////////////////////////////////////////////////////////////////////////

#include "memory_container.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>

struct container {
	__u64 cid;
	struct container_thread* thread; //represent head of thread list
	struct container* next;
	struct mutex mylock; //each container will have its own lock, this improves efficiency over global lock mechanism
} *con_head = NULL;

struct container_thread {
	pid_t pid;
	struct task_struct* tsk;
	struct container_object* object; //represent head of memory object list
	struct container_thread* next;
};

struct container_object {
	__u64 oid;
	struct vm_area_struct* vma;
	struct container_object* next;
};

/**
This function returns container associated with current task
**/
struct container* find_container_of_current_task(void) {
	struct container* temp = con_head;
	while(temp) {
		if(temp->thread->pid == current->pid) //thread found in this container at first position or anywhere after
			break;
		temp = temp->next;
	}
	return temp;
}


int memory_container_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long len = vma->vm_end - vma->vm_start;
	int ret ;

	ret = remap_pfn_range(vma, vma->vm_start, pfn, len, vma->vm_page_prot);
	if (ret < 0) {
	    pr_err("could not map the address area\n");
	    return -EIO;
	}
}


int memory_container_lock(struct memory_container_cmd __user *user_cmd)
{
    container *myContainer = find_container_of_current_task();
    mutex_lock(&myContainer->mylock);
    return 0;
}


int memory_container_unlock(struct memory_container_cmd __user *user_cmd)
{
    container *myContainer = find_container_of_current_task();
    mutex_unlock(&myContainer->mylock);
    return 0;
}


int memory_container_delete(struct memory_container_cmd __user *user_cmd)
{
    return 0;
}


int memory_container_create(struct memory_container_cmd __user *user_cmd)
{
printk(user_cmd->cid);
    return 0;
}


int memory_container_free(struct memory_container_cmd __user *user_cmd)
{
    return 0;
}


/**
 * control function that receive the command in user space and pass arguments to
 * corresponding functions.
 */
int memory_container_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    switch (cmd)
    {
    case MCONTAINER_IOCTL_CREATE:
        return memory_container_create((void __user *)arg);
    case MCONTAINER_IOCTL_DELETE:
        return memory_container_delete((void __user *)arg);
    case MCONTAINER_IOCTL_LOCK:
        return memory_container_lock((void __user *)arg);
    case MCONTAINER_IOCTL_UNLOCK:
        return memory_container_unlock((void __user *)arg);
    case MCONTAINER_IOCTL_FREE:
        return memory_container_free((void __user *)arg);
    default:
        return -ENOTTY;
    }
}
