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
	struct container* next;
	struct container_object* object; //container's object list head
	struct mutex mylock; //each container will have its own lock, this improves efficiency over global lock mechanism
} *con_head = NULL;

struct container_object {
	__u64 oid;
	pid_t pid;
	struct vm_area_struct* vma;
	struct container_object* next;
};

/**
This function returns container associated with current task
**/
struct container* find_container_of_current_task(void) {
	struct container* temp = con_head;
	while(temp) {
		struct container_object* temp_object = temp->object;
		while(temp_object) {
			if(temp->object->pid == current->pid) //thread found in this container at first position or anywhere after
			 return temp;
			temp_object = temp_object->next;
		}
		temp = temp->next;
	}
	return temp;
}

/**
This function returns container associated with cid provided
**/
struct container* find_my_container(__u64 cid) {
	struct container* temp = con_head;
	while(temp) {
		if(temp->cid == cid)
		  break;
		temp = temp->next;
	}
	return temp;
}

/**
This function returns container_object associated with oid provided
**/
struct container_object* find_my_memory_object(__u64 oid) {
	struct container* temp = find_container_of_current_task();
	struct container_object* temp_object = NULL;
        if(temp) {
		 temp_object = temp->object;
		 while(temp_object) {
			if(temp_object->oid == oid)
			  break;
			temp_object = temp_object->next;
		 }
	}
	return temp_object;
}

int memory_container_mmap(struct file *filp, struct vm_area_struct *vma)
{
        printk("vma->offset");
        printk("mmap: %llu", vma->vm_pgoff);
	unsigned long len = vma->vm_end - vma->vm_start;
	char* mem = (char*)kmalloc(sizeof(char), GFP_KERNEL);
        unsigned long pfn = virt_to_phys((void *)mem)>>PAGE_SHIFT;
	int ret = remap_pfn_range(vma, vma->vm_start, pfn, len, vma->vm_page_prot);
	if (ret < 0) {
	    pr_err("could not map the address area\n");
	    return -EIO;
	}
	SetPageReserved(virt_to_page(((unsigned long)mem)));
	struct container_object* myObject = find_my_memory_object(vma->vm_pgoff);
        myObject->vma = vma;
        return ret;
}


int memory_container_lock(struct memory_container_cmd __user *user_cmd)
{
    printk("lock cid:%llu", user_cmd->cid);
    struct container* myContainer = find_container_of_current_task();
    mutex_lock(&myContainer->mylock);
    return 0;
}


int memory_container_unlock(struct memory_container_cmd __user *user_cmd)
{
    printk("unlock cid:%llu", user_cmd->cid);
    struct container* myContainer = find_container_of_current_task();
    mutex_unlock(&myContainer->mylock);
    return 0;
}


int memory_container_delete(struct memory_container_cmd __user *user_cmd)
{
	printk("delete cid:%llu", user_cmd->cid);
    return 0;
}


int memory_container_create(struct memory_container_cmd __user *user_cmd)
{
	printk("inside in create");
	printk("create cid:%llu", user_cmd->cid);
	printk("create oid:%llu", user_cmd->oid);
	struct container* myContainer;
	struct container_object* myObject;
	struct  memory_container_cmd temp;
	
	copy_from_user(&temp, user_cmd, sizeof(struct memory_container_cmd));

	//if container head is not null, then finding current container
	if(con_head) {
		myContainer = find_my_container((&temp)->cid);
		if(!myContainer) { //container not found, create new
			struct container* temp_head = con_head;
			while(temp_head && temp_head->next)
				temp_head = temp_head->next;
			myContainer = (struct container*)kmalloc(sizeof(struct container), GFP_KERNEL);
			myContainer->cid = (&temp)->cid; 
			myContainer->next = NULL;
			myContainer->object = NULL;
			mutex_init(&myContainer->mylock);
			temp_head->next = myContainer;
		}
	} else { //creating new container
		myContainer = (struct container*)kmalloc(sizeof(struct container), GFP_KERNEL);
		myContainer->cid = (&temp)->cid; 
		myContainer->next = NULL;
		myContainer->object = NULL;
		mutex_init(&myContainer->mylock);
		con_head = myContainer;//initializing head
	}
	
	//creating new memory object based on current task
	myObject = (struct container_object*)kmalloc(sizeof(struct container_object), GFP_KERNEL);
	myObject->pid = current->pid;
	myObject->oid = (&temp)->oid;
        myObject->vma = NULL;
	myObject->next = NULL;
	
	mutex_lock(&myContainer->mylock);

	//if containers thread is not null
	if(myContainer->object) {
		struct container_object* temp_object = myContainer->object;
		while(temp_object && temp_object->next)
			temp_object = temp_object->next;
		temp_object->next = myObject;
		mutex_unlock(&myContainer->mylock); //unlocking before sleep
	} else {
		myContainer->object = myObject;
		mutex_unlock(&myContainer->mylock);
	}
    return 0;
}


int memory_container_free(struct memory_container_cmd __user *user_cmd)
{
    printk("free cid:%llu", user_cmd->cid);
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
