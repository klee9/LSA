#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/list.h>
#define NUM_THREAD 4
#define BILLION 1000000000UL

static int counter = 0;
static struct task_struct *threads[NUM_THREAD];

unsigned long long add_time = 0, search_time = 0, del_time = 0;
unsigned long long add_cnt = 0, search_cnt = 0, del_cnt = 0;

struct my_node {
	int data;
	struct list_head list;
};

DEFINE_MUTEX(mlock);
LIST_HEAD(my_list);

/* calclock integrated for debugging */
unsigned long long calclock(struct timespec *myclock, unsigned long long *total_time, unsigned long long *total_count)
{
	unsigned long long timedelay=0, temp=0, temp_n=0;

	if (myclock[1].tv_nsec >= myclock[0].tv_nsec) {
		temp = myclock[1].tv_sec - myclock[0].tv_sec;
		temp_n = myclock[1].tv_nsec - myclock[0].tv_nsec;
		timedelay = BILLION * temp + temp_n;
	} else {
		temp = myclock[1].tv_sec - myclock[0].tv_sec - 1;
		temp_n = BILLION + myclock[1].tv_nsec - myclock[0].tv_nsec;
		timedelay = BILLION * temp + temp_n;
	}

	__sync_fetch_and_add(total_time, timedelay);
	__sync_fetch_and_add(total_count, 1);

	return timedelay;
}

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

    // a local list head
    struct list_head local_list;
    INIT_LIST_HEAD(&local_list);

    // insert to temp list
    for (i = range_bound[0]; i <= range_bound[1]; i++) {
		getrawmonotonic(&localclock[0]);
        new_node = kmalloc(sizeof(struct my_node), GFP_KERNEL);
        if (!new_node) break;
        new_node->data = i;
								
        // save reference to the first node
        if (!first) first = new_node; 
        list_add_tail(&new_node->list, &local_list);
		getrawmonotonic(&localclock[1]);
        calclock(localclock, &add_time, &add_cnt);
    }
    
    // splice
    mutex_lock(&mlock);
    list_splice_tail(&local_list, &my_list);
    mutex_unlock(&mlock);
    
    return first;
}

int search_list(int thread_id, void *data, int range_bound[])
{
    int i;
	struct timespec localclock[2];
    struct my_node *cur = (struct my_node *) data, *tmp;

	// start from "data" iterate 250k nodes
	for (i = range_bound[0]; i <= range_bound[1]; i++) {
		mutex_lock(&mlock);
		getrawmonotonic(&localclock[0]);
		cur = list_next_entry(cur, list);
		if (&cur->list == &my_list)
			cur = list_first_entry(&my_list, struct my_node, list);
		getrawmonotonic(&localclock[1]);
		calclock(localclock, &search_time, &search_cnt);
		mutex_unlock(&mlock);
	}
    
    printk(KERN_INFO "thread #%d searched range: %d ~ %d", thread_id, range_bound[0], range_bound[1]);
    return 0;
}

int delete_from_list(int thread_id, int range_bound[])
{
    struct my_node *cur, *tmp;
    struct timespec localclock[2];

    mutex_lock(&mlock);
    list_for_each_entry_safe(cur, tmp, &my_list, list) {
		// check if the node's data is within the target range
		if (cur->data >= range_bound[0] && cur->data <= range_bound[1]) {
		    getrawmonotonic(&localclock[0]);
		    list_del(&cur->list);
		    getrawmonotonic(&localclock[1]);
		    calclock(localclock, &del_time, &del_cnt);
		        
		    kfree(cur);
		}
    }
    mutex_unlock(&mlock);
   
    printk(KERN_INFO "thread #%d deleted range: %d ~ %d\n", 
           thread_id, range_bound[0], range_bound[1]);
    
    return 0;
}

/* per-thread function */
static int work_fn(void *data)
{
	int range_bound[2];
	int thread_id = (int)data;
	
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
	printk(KERN_INFO "Entering mutex lock module...\n");
	int i;
	for (i = 0; i < NUM_THREAD; i++) {
		threads[i] = kthread_create(work_fn, (void*)(int)(i+1), "T%d", i);
		wake_up_process(threads[i]);
	}
	return 0;
}

static void __exit mod_exit(void) 
{
	printk("Mutex lock linked list insert time: %llu ns, count: %llu", add_time, add_cnt);	
	printk("Mutex lock linked list search time: %llu ns, count: %llu", search_time, search_cnt);	 
	printk("Mutex lock linked list delete time: %llu ns, count: %llu", del_time, del_cnt);	
		
	int i;
	for (i = 0; i < NUM_THREAD; i++) {
		kthread_stop(threads[i]);
		threads[i] = NULL;
	}
	
	printk("exiting mutex lock module...\n");
}


MODULE_AUTHOR("20212329 Keon Lee");

module_init(mod_init)
module_exit(mod_exit)
