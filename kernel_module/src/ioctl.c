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
	struct container_thread* thread; //container's thread list head
	struct container_object* object; //container's object list head
	struct mutex mylock; //each container will have its own lock, this improves efficiency over global lock mechanism
} *con_head = NULL;

struct container_thread {
	pid_t pid;
	struct container_thread* next;
};

struct container_object {
	__u64 oid;
	pid_t pid; //pid 0 means a zombie object
	char* mem;
	unsigned long pfn;
	struct container_object* next;
};

/**
This function invalidate all memory objects associated with the given task/thread. Very powerful functionality be careful!!
**/
void invalidate_memory_objects(struct container* container, pid_t pid) {
	struct container_object* object = container->object;
	while(object) {
		if(object->pid == pid)
			object->pid = 0; //making zombie object by masking pid with 0
		object = object->next;
	}
}

/**
This function delete single memory object associated with this container.
**/
void delete_memory_object(struct container* container, __u64 oid, pid_t pid) {
	if(container->object && container->object->oid == oid && container->object->pid == pid) { //at first location
		struct container_object* temp = container->object;
		container->object = temp->next;
		char* mem = temp->mem;
		kfree(mem);
		kfree(temp);
	} else {
		struct container_object* head = container->object;
		while(head && head->next) {
			if(head->next->oid == oid && head->next->pid == pid) {
				struct container_object* temp = head->next;
				head->next = head->next->next;
				char* mem = temp->mem;
				kfree(mem);
				kfree(temp);
				break;
			}
			head = head->next;
		}
	}
	
}

/**
This function deletes container based on cid provided and free its memory
**/
void delete_container(__u64 cid) {
	struct container* temp = con_head;
	struct container* prev = con_head;
	while(temp) {
		if(temp->cid == cid) {
		  break;
		}
		prev = temp;
		temp = temp->next;
	}
	if(prev == temp) {
		con_head = temp->next;
		mutex_unlock(&temp->mylock);
		kfree(temp);
	} else {
		prev->next = temp->next;
		mutex_unlock(&temp->mylock);
		kfree(temp);
	}
}

/**
This function returns thread associated with given container and pid
**/
struct container_thread* find_thread_in_container(struct container* container, pid_t pid) {
	struct container_thread* thread = container->thread;
	while(thread) {
		if(thread->pid == pid) break; //thread found
		thread = thread->next; //eventually will become null if thread not found
	}
	return thread;
}


/**
This function returns container associated with current task
**/
struct container* find_container_of_current_task(void) {
	struct container* temp = con_head;
	while(temp) {
		struct container_thread* thread = find_thread_in_container(temp, current->pid); //finding thread in this container
		if(thread) return temp; //found, returning container
		temp = temp->next;
	}
	return NULL; //not found
}

/**
This function returns container associated with cid provided
**/
struct container* find_my_container(__u64 cid) {
	struct container* temp = con_head;
	while(temp) {
		if(temp->cid == cid) break; //container found
		temp = temp->next;
	}
	return temp;
}

/**
This function returns container_object associated with oid provided
**/
struct container_object* find_memory_object_of_current_task(struct container* container, __u64 oid) {
	struct container_object* object = container->object;
	while(object) {
		if(object->oid == oid && (object->pid == current->pid || object->pid == 0)) //thread connected to this memory object, so returning this memory object
			 break;//found
		object = object->next; //eventually goes to null
	}
	return object;//object not found
}


int memory_container_mmap(struct file *filp, struct vm_area_struct *vma)
{
	printk("----------Inside mmap --------");
	int ret = -EIO;
	struct container* container = find_container_of_current_task();
	if(!container) return ret; //container null

	struct container_object* myObject = find_memory_object_of_current_task(container, vma->vm_pgoff);
	if(!myObject) {
		printk("mmap pgoff: %llu", vma->vm_pgoff);
		printk("current pid: %llu", current->pid);
		unsigned long size = vma->vm_end - vma->vm_start;

		char* mem = (char*)kmalloc(size, GFP_KERNEL);
		printk("mmap mem: %llu", mem);

		unsigned long pfn = virt_to_phys((void *)mem)>>PAGE_SHIFT;
		printk("mmap pfn: %llu", pfn);
		ret = remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
		if (ret < 0) {
		    pr_err("could not map the address area\n");
		    return -EIO;
		}

		myObject = (struct container_object*)kmalloc(sizeof(struct container_object), GFP_KERNEL);
		myObject->oid = vma->vm_pgoff;
		myObject->pid = current->pid;
		myObject->mem = mem;
		myObject->pfn = pfn;
		myObject->next = NULL;

		if(container->object) {//object head not null, some objects already present in this list
			struct container_object* object = container->object;
			while(object && object->next)
				object = object->next;
			object->next = myObject;
		} else {
			container->object = myObject; //first object in this container
		}
	} else {
		printk("object->pid: %llu", myObject->pid);
		printk("object->oid: %llu", myObject->oid);
		unsigned long size = vma->vm_end - vma->vm_start;
		ret = remap_pfn_range(vma, vma->vm_start, myObject->pfn, size, vma->vm_page_prot);
		if (ret < 0) {
		    pr_err("could not map the address area\n");
		    return -EIO;
		}
		myObject->pid = current->pid;
		printk("object->pid: %llu", myObject->pid);
	}
        return ret;
}


int memory_container_lock(struct memory_container_cmd __user *user_cmd)
{
    	struct container* myContainer = find_container_of_current_task();
	if(myContainer) {
    		mutex_lock(&myContainer->mylock);
    		printk("--------------- lock mycontainer cid:%llu -----------------", myContainer->cid);
	}
    	return 0;
}


int memory_container_unlock(struct memory_container_cmd __user *user_cmd)
{
    	struct container* myContainer = find_container_of_current_task();
	if(myContainer) {
	    	printk("-------------- unlock mycontainer cid:%llu -----------------", myContainer->cid);
	    	mutex_unlock(&myContainer->mylock);
	}
    	return 0;
}


int memory_container_delete(struct memory_container_cmd __user *user_cmd)
{
	printk("----------Inside delete --------");
	struct container* myContainer = find_container_of_current_task(); //finding correct container associated with this thread
	printk("cid %llu", myContainer->cid);
	printk("pid: %llu", current->pid);
	if(myContainer) { //container is not empty
		mutex_lock(&myContainer->mylock); //attaining lock
		struct container_thread* thread = myContainer->thread;
		if(thread && thread->pid == current->pid) { //trying to delete first thread
				struct container_thread* temp = myContainer->thread->next;
				struct container_thread* curr = myContainer->thread;
				printk("deleting thread %llu", curr->pid);
				if(temp) { //if not null 
					myContainer->thread = temp;
				} else {
					myContainer->thread = NULL;
				}
				invalidate_memory_objects(myContainer, curr->pid); //making all remaining objects zombies
				kfree(curr);
		} else if(thread) {
		 	while(thread && thread->next) {
				if(thread->next->pid == current->pid) {
					struct container_thread* toDelete = thread->next;
					thread->next = thread->next->next;
					printk("deleting thread %llu", toDelete->pid);
					invalidate_memory_objects(myContainer, toDelete->pid); //making all remaining objects zombies
					kfree(toDelete);
					break;	
				}
				thread = thread->next;
			}
		}  


		mutex_unlock(&myContainer->mylock);
		/*
		if(!myContainer->thread) { //if container becomes empty then delete it too
				delete_container(myContainer->cid); //lock will be released by this function
		} else {
				mutex_unlock(&myContainer->mylock);
		}*/
		
	}
	printk("----------exit delete --------");
    	return 0;
}


int memory_container_create(struct memory_container_cmd __user *user_cmd)
{	
	struct container* myContainer;
	struct  memory_container_cmd temp;
	
	copy_from_user(&temp, user_cmd, sizeof(struct memory_container_cmd));
	printk("------------- create cid:%llu -----------------", (&temp)->cid);
	
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
			myContainer->thread = NULL;
			myContainer->object = NULL;
			mutex_init(&myContainer->mylock);
			temp_head->next = myContainer;
		}
	} else { //creating new container
		myContainer = (struct container*)kmalloc(sizeof(struct container), GFP_KERNEL);
		myContainer->cid = (&temp)->cid; 
		myContainer->next = NULL;
		myContainer->thread = NULL;
		myContainer->object = NULL;
		mutex_init(&myContainer->mylock);
		con_head = myContainer;//initializing head
	}
	
	//creating new thread inside this container
	struct container_thread* myThread = (struct container_thread*)kmalloc(sizeof(struct container_thread), GFP_KERNEL);
	myThread->pid = current->pid;
	myThread->next = NULL;
	mutex_lock(&myContainer->mylock);
	printk("----------loc in create --------");
	//if containers thread is not null
	if(myContainer->thread) {
		struct container_thread* temp_thread = myContainer->thread;
		while(temp_thread && temp_thread->next)
			temp_thread = temp_thread->next;
		temp_thread->next = myThread;
		printk("----------unloc in create --------");
		mutex_unlock(&myContainer->mylock); //unlocking before sleep
	} else {
		myContainer->thread = myThread;
		printk("----------unloc in create --------");
		mutex_unlock(&myContainer->mylock);
	}
	printk("----------exit create --------");
    return 0;
}


int memory_container_free(struct memory_container_cmd __user *user_cmd)
{
	struct  memory_container_cmd temp;
	copy_from_user(&temp, user_cmd, sizeof(struct memory_container_cmd));
	printk("------------- free oid:%llu ----------------", (&temp)->oid);
	struct container* myContainer = find_container_of_current_task();
	delete_memory_object(myContainer, (&temp)->oid, current->pid); //deleting this memory object
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
