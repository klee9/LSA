#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include "ocalclock.h"

#define NUM_THREAD 4
#define NODES_PER_THREAD 250000

DEFINE_SPINLOCK(slock);

static struct task_struct *threads[NUM_THREAD];

/* Global Stats */
unsigned long long add_time = 0, search_time = 0, del_time = 0;
unsigned long long add_cnt = 0, search_cnt = 0, del_cnt = 0;

struct my_node {
    int data;
    struct list_head list;
};

LIST_HEAD(my_list);

void set_iter_range(int thread_id, int range_bound[])
{
    range_bound[0] = (thread_id - 1) * NODES_PER_THREAD;
    range_bound[1] = range_bound[0] + NODES_PER_THREAD - 1;
    printk(KERN_INFO "thread #%d range: %d ~ %d\n", 
            thread_id, range_bound[0], range_bound[1]);
}

void *add_to_list(int thread_id, int range_bound[])
{
    int i;
    struct my_node *new_node = NULL;
    struct timespec localclock[2];
    
    for (i = range_bound[0]; i <= range_bound[1]; i++) {
        // 1. Alloc Outside Lock
        new_node = kmalloc(sizeof(struct my_node), GFP_KERNEL);
        if (!new_node) break;
        new_node->data = i;
        
        getrawmonotonic(&localclock[0]);
        
        // 2. Lock
        spin_lock(&slock);
        list_add_tail(&new_node->list, &my_list);
        spin_unlock(&slock);
        
        getrawmonotonic(&localclock[1]);
        ocalclock(localclock, &add_time, &add_cnt);
    }
    return NULL;
}

int search_list(int thread_id, void *data, int range_bound[])
{
    struct timespec localclock[2];
    struct my_node *cur;
    
    // USE LOCAL COUNTER!
    int local_found = 0;
    int expected = range_bound[1] - range_bound[0] + 1;
    
    spin_lock(&slock);
    getrawmonotonic(&localclock[0]);
    
    list_for_each_entry(cur, &my_list, list) {
        if (cur->data >= range_bound[0] && cur->data <= range_bound[1]) {
            local_found++;
        }
        
        // Optimization: Break early if we found all our nodes
        if (local_found >= expected) {
            break; 
        }
    }
    
    // Update global stat safely
    search_cnt += local_found;

    getrawmonotonic(&localclock[1]);
    spin_unlock(&slock); // <--- CRITICAL: MUST UNLOCK HERE
    
    ocalclock(localclock, &search_time, &search_cnt);
    
    printk(KERN_INFO "thread #%d searched: %d items\n", thread_id, local_found);
    return 0;
}

int delete_from_list(int thread_id, int range_bound[])
{
    struct my_node *cur, *tmp;
    struct timespec localclock[2];
    LIST_HEAD(temp_list); // Private list for safe deletion
    int local_del_cnt = 0;
    
    getrawmonotonic(&localclock[0]);
    spin_lock(&slock);
    
    // 1. Identify and Move (Fast, No kfree here)
    list_for_each_entry_safe(cur, tmp, &my_list, list) {
        if (cur->data >= range_bound[0] && cur->data <= range_bound[1]) {
            list_move(&cur->list, &temp_list);
            local_del_cnt++;
        }
    }
    
    // Update global stat
    del_cnt += local_del_cnt;
    
    spin_unlock(&slock);
    getrawmonotonic(&localclock[1]);
    ocalclock(localclock, &del_time, &del_cnt);
    
    // 2. Free Outside Lock (Safe to sleep)
    list_for_each_entry_safe(cur, tmp, &temp_list, list) {
        list_del(&cur->list);
        kfree(cur);
    }
    
    printk(KERN_INFO "thread #%d deleted: %d items\n", thread_id, local_del_cnt);
    return 0;
}

static int work_fn(void *data)
{
    int range_bound[2];
    // Cast back from long (safe for kthread_run)
    int thread_id = (int)(long)data; 
    
    set_iter_range(thread_id, range_bound);
    add_to_list(thread_id, range_bound);
    search_list(thread_id, NULL, range_bound);
    delete_from_list(thread_id, range_bound);
    
    while(!kthread_should_stop()) {
        msleep(100);
    }
    printk(KERN_INFO "thread #%d stopped work_fn\n", thread_id);
    return 0;
}

static int __init mod_init(void)
{   
    int i;
    
    // Initialize Globals
    add_time = 0; search_time = 0; del_time = 0;
    add_cnt = 0; search_cnt = 0; del_cnt = 0;
    
    printk(KERN_INFO "Entering spinlock module...\n");
    for (i = 0; i < NUM_THREAD; i++) {
        // Pass integer directly as void*
        threads[i] = kthread_run(work_fn, (void *)(long)(i+1), "T%d", i+1);
    }
    return 0;
}

static void __exit mod_exit(void) 
{
    int i;
    for (i = 0; i < NUM_THREAD; i++) {
        if (threads[i]) kthread_stop(threads[i]);
    }
    
    printk("spinlock_module_cleanup: Insert: %llu ns\n", add_time);    
    printk("spinlock_module_cleanup: Search: %llu ns\n", search_time);    
    printk("spinlock_module_cleanup: Delete: %llu ns\n", del_time);    
    
    printk("exiting spinlock module...\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("20212329 Keon Lee");

module_init(mod_init);
module_exit(mod_exit);
