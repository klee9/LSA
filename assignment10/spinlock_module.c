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

DEFINE_SPINLOCK(slock);

static struct task_struct *threads[NUM_THREAD];

unsigned long long add_time, search_time, del_time;
unsigned long long add_cnt, search_cnt, del_cnt;

struct my_node {
	int data;
	struct list_head list;
};

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
    struct my_node *new_node = NULL;
    struct timespec localclock[2];
    
    for (i = range_bound[0]; i <= range_bound[1]; i++) {
        new_node = kmalloc(sizeof(struct my_node), GFP_KERNEL);
        new_node->data = i;
        
        getrawmonotonic(&localclock[0]);
        spin_lock(&slock);
        list_add_tail(&new_node->list, &my_list);
        spin_unlock(&slock);
        getrawmonotonic(&localclock[1]);
        ocalclock(localclock, &add_time, &add_cnt);

		if (!first) first = new_node; 
    }
    
    return first;
}

int search_list(int thread_id, void *data, int range_bound[])
{
	struct timespec localclock[2];
	struct my_node *cur = (struct my_node *) data, *tmp;
	
	// start from "data" and search 250K nodes
	spin_lock(&slock);

	getrawmonotonic(&localclock[0]);
	list_for_each_entry_safe(cur, tmp, &my_list, list) {
		if (cur->data >= range_bound[0] && cur->data <= range_bound[1]) {
			search_cnt++;
		}
		if (searched == range_bound[1] - range_bound[0]) {
			printk("thread #%d searched range: %d ~ %d\n", 
						thread_id, range_bound[0], range_bound[1]);
			getrawmonotonic(&localclock[1]);
			ocalclock(localclock, &search_time, &search_cnt);
			spin_unlock(&slock);
			return 0;
		}
	}
	return 0;
}

int delete_from_list(int thread_id, int range_bound[])
{
	struct my_node *cur, *tmp;
	struct timespec localclock[2];
	
	spin_lock(&slock);
	getrawmonotonic(&localclock[0]);
	list_for_each_entry_safe(cur, tmp, &my_list, list) {
		if (cur->data >= range_bound[0] && cur->data <= range_bound[1]) {
			del_cnt++;
			list_del(&cur->list);
			kfree(cur);
		}
	} 
	
	printk("thread #%d deleted range: %d ~ %d", 
				thread_id, range_bound[0], range_bound[1]);
	getrawmonotonic(&localclock[1]);
	ocalclock(localclock, &del_time, &del_cnt);
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
	static int tids[NUM_THREAD];
	
	printk(KERN_INFO "Entering spinlock module...\n");
	for (i = 0; i < NUM_THREAD; i++) {
		tids[i] = i + 1;
		threads[i] = kthread_create(work_fn, &tids[i], "T%d", i+1);
		
		if (IS_ERR(threads[i])) {
			printk(KERN_ERR "Failed to create thread #%d\n", i+1);
			threads[i] = NULL;
		}
		
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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("20212329 Keon Lee");

module_init(mod_init);
module_exit(mod_exit);
