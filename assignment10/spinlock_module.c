#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/list.h>
#include "ocalclock.h"
#define NUM_THREAD 4

spinlock_t slock; // spinlock
spin_lock_init(&slock);

static struct task_struct *threads[NUM_THREAD];

unsigned long long add_time, search_time, del_time;
unsigned long long add_cnt, search_cnt, del_cnt;

struct my_node {
	int data;
	struct list_head list;
};

//struct mutex mlock; // mutex lock
//mutex_init(&mlock);

//struct rw_semaphore rwsem; // rw sema
//init_rwsem(&rwsem);

LIST_HEAD(my_list);

/* sets each thread's range boundary */
void set_iter_range(int thread_id, int range_bound[])
{
	range_bound[0] = (thread_id - 1) * 250000;
	range_bound[1] = range_bound[0] + 250000 - 1;
	
	printk(KERN_INFO "thread #%d range: %d ~ %d\n", 
			thread_id, range_bound[0], range_bound[1]);
}

/* insertion */
void *add_to_list(int thread_id, int range_bound[])
{
	int i;
	struct my_node *first = NULL;
	struct timespec localclock[2];
	
	// acquire lock for sequential insertion
	spin_lock(&slock);
	
	for (i = range_bound[0]; i < range_bound[0] + 250000; i++) {
		struct my_node *new = kmalloc(sizeof(struct my_node), GFP_KERNEL);
		new->data = i;
		if (!first) 
			first = new;
		
		getrawmonotonic(&localclock[0]);
		list_add_tail(&new->list, &my_list);
		getrawmonotonic(&localclock[1]);
		ocalclock(localclock, &add_time, &add_cnt);
	}
	
	spin_unlock(&slock);
	
	return first;
}

int search_list(int thread_id, void *data, int range_bound[])
{	
	struct timespec localclock[2];
	struct my_node *cur = (struct my_node *) data, *tmp;
	
	// start from "data" and search 250K nodes
	spin_lock(&slock);
	
	list_for_each_entry_safe(cur, tmp, &my_list, list) {
		getrawmonotonic(&localclock[0]);
		if (cur->data == range_bound[1]) {
			printk("thread #%d searched range: %d ~ %d\n", 
						thread_id, range_bound[0], range_bound[1]);
			getrawmonotonic(&localclock[1]);
			ocalclock(localclock, &search_time, &search_cnt);
			spin_unlock(&slock);
			return cur->data;
		}
		getrawmonotonic(&localclock[1]);
		ocalclock(localclock, &search_time, &search_cnt);
	}
	
	spin_unlock(&slock);
	
	return 0;
}

int delete_from_list(int thread_id, int range_bound[])
{
	struct my_node *cur, *tmp;
	struct timespec localclock[2];
	
	spin_lock(&slock);
	
	list_for_each_entry_safe(cur, tmp, &my_list, list) {
		getrawmonotonic(&localclock[0]);
		if (cur->data >= range_bound[0] && cur->data <= range_bound[1]) {
			list_del(&cur->list);
			kfree(cur);
			getrawmonotonic(&localclock[1]);
			ocalclock(localclock, &del_time, &del_cnt);
		}
	} 
	
	printk("thread #%d deleted range: %d ~ %d", 
				thread_id, range_bound[0], range_bound[1]);
	spin_unlock(&slock);
	return 0;
}

/* per-thread function */
static int work_fn(void *data)
{
	int range_bound[2];
	int thread_id = *(int *) data;
	
	set_iter_range(thread_id, range_bound);
	void *ret = add_to_list(thread_id, range_bound);
	search_list(thread_id, ret, range_bound);
	delete_from_list(thread_id, range_bound);
	
	while(!kthread_should_stop()) {
		msleep(500);
	}
	printk(KERN_INFO "thread #%d stopped!\n", thread_id);
	return 0;
}

static int __init mod_init(void)
{	
	int i;
	printk("Entering spinlock module...\n");
	for (i = 0; i < NUM_THREAD; i++) {
		threads[i] = kthread_create(work_fn,(void*)(long)(i+1),"T%d",i+1);
		wake_up_process(threads[i]);
	}
	return 0;
}

static void __exit mod_exit(void) 
{
	printk("spinlock_module_cleanup: \
	     Spinlock linked list insert time: %llu ns, count: %llu",
	     add_time, add_cnt);	
	     
	printk("spinlock_module_cleanup: \
	     Spinlock linked list search time: %llu ns, count: %llu",
	     search_time, search_cnt);	
	     
	printk("spinlock_module_cleanup: \
	     Spinlock linked list delete time: %llu ns, count: %llu",
	     del_time, del_cnt);	
		
	int i;
	for (i = 0; i < NUM_THREAD; i++) {
		kthread_stop(threads[i]);
		threads[i] = NULL;
		printk("thread #%d stopped!\n", i+1);
	}
	
	printk("exiting spinlock module...\n");
}

module_init(mod_init)
module_exit(mod_exit)
