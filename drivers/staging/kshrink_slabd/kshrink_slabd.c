// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 * Copyright (C) 2025 chickendrop89. All rights reserved.
 */

#define pr_fmt(fmt) "shrink_async: " fmt

#include <linux/module.h>
#include <trace/hooks/vmscan.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/memcontrol.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

extern unsigned long shrink_slab(gfp_t gfp_mask, int nid,
                 struct mem_cgroup *memcg,
                 int priority);

static struct workqueue_struct *shrink_async_wq;
static struct proc_dir_entry *shrink_async_proc_entry;

struct shrink_async_stats {
    atomic_t total_requests;
    atomic_t successful_queues;
    atomic_t failed_queues;
    atomic_t busy_bypasses;
    atomic_t tasks_in_worker;
    unsigned long long reclaimed_slabs;
    spinlock_t lock; /* for reclaimed_slabs */
};
static struct shrink_async_stats stats;

struct shrink_async_work {
    struct work_struct work;
    struct mem_cgroup *memcg;
    gfp_t gfp_mask;
    int nid;
    int priority;
};

static void shrink_async_worker(struct work_struct *work)
{
    struct shrink_async_work *saw = container_of(work, struct shrink_async_work, work);
    struct reclaim_state async_reclaim_state = {
        .reclaimed_slab = 0,
    };
    unsigned long reclaimed_before;
    unsigned long reclaimed_after;
    unsigned long flags;

    atomic_inc(&stats.tasks_in_worker);

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
    current->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;
    current->reclaim_state = &async_reclaim_state;

    reclaimed_before = async_reclaim_state.reclaimed_slab;
    shrink_slab(saw->gfp_mask, saw->nid, saw->memcg, saw->priority);
    reclaimed_after = async_reclaim_state.reclaimed_slab;

    current->reclaim_state = NULL;
    current->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD);

    spin_lock_irqsave(&stats.lock, flags);
    stats.reclaimed_slabs += reclaimed_after - reclaimed_before;
    spin_unlock_irqrestore(&stats.lock, flags);

    css_put(&saw->memcg->css);
    kfree(saw);
    atomic_dec(&stats.tasks_in_worker);
}

void should_shrink_async(void *data, gfp_t gfp_mask, int nid,
            struct mem_cgroup *memcg, int priority, bool *bypass)
{
    struct shrink_async_work *saw;

    atomic_inc(&stats.total_requests);

    /* Don't try to async shrink from a worker thread to avoid recursion */
    if (in_task() && (current->flags & PF_KSWAPD)) {
        atomic_inc(&stats.busy_bypasses);
        *bypass = false;
        return;
    }

    saw = kmalloc(sizeof(*saw), GFP_ATOMIC);
    if (!saw) {
        atomic_inc(&stats.failed_queues);
        *bypass = false;
        return;
    }

    INIT_WORK(&saw->work, shrink_async_worker);

    saw->gfp_mask = gfp_mask;
    saw->nid = nid;
    saw->priority = priority;
    saw->memcg = memcg;

    css_get(&memcg->css);
    queue_work(shrink_async_wq, &saw->work);

    atomic_inc(&stats.successful_queues);
    *bypass = true;
}

static int shrink_async_proc_show(struct seq_file *m, void *v)
{
    unsigned long long reclaimed_slabs;
    unsigned long flags;

    spin_lock_irqsave(&stats.lock, flags);
    reclaimed_slabs = stats.reclaimed_slabs;
    spin_unlock_irqrestore(&stats.lock, flags);

    seq_printf(m, "total_requests: %d\n", atomic_read(&stats.total_requests));
    seq_printf(m, "successful_queues: %d\n", atomic_read(&stats.successful_queues));
    seq_printf(m, "failed_queues: %d\n", atomic_read(&stats.failed_queues));
    seq_printf(m, "busy_bypasses: %d\n", atomic_read(&stats.busy_bypasses));
    seq_printf(m, "tasks_in_worker: %d\n", atomic_read(&stats.tasks_in_worker));
    seq_printf(m, "reclaimed_slabs (pages): %llu\n", reclaimed_slabs);
    return 0;
}

static int shrink_async_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, shrink_async_proc_show, NULL);
}

static const struct proc_ops shrink_async_proc_ops = {
    .proc_open = shrink_async_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int register_shrink_async_vendor_hooks(void)
{
    return register_trace_android_vh_shrink_slab_bypass(should_shrink_async, NULL);
}

static void unregister_shrink_async_vendor_hooks(void)
{
    unregister_trace_android_vh_shrink_slab_bypass(should_shrink_async, NULL);
}

static int __init shrink_async_init(void)
{
    int ret = 0;

    spin_lock_init(&stats.lock);

    shrink_async_wq = alloc_workqueue("shrink_async_wq", WQ_POWER_EFFICIENT | WQ_MEM_RECLAIM | WQ_FREEZABLE, 0);
    if (!shrink_async_wq) {
        pr_err("Failed to create shrink_async_wq\n");
        return -ENOMEM;
    }

    ret = register_shrink_async_vendor_hooks();
    if (ret != 0) {
        pr_err("register_trace_android_vh_shrink_slab_bypass failed! ret=%d\n", ret);
        destroy_workqueue(shrink_async_wq);
        return ret;
    }
    
    shrink_async_proc_entry = proc_create("shrink_async", 0444, NULL, &shrink_async_proc_ops);
    if (!shrink_async_proc_entry) {
        pr_err("Failed to create procfs entry\n");
        unregister_shrink_async_vendor_hooks();
        destroy_workqueue(shrink_async_wq);
        return -ENOMEM;
    }

    pr_info("kshrink_async succeed!\n");
    return 0;
}

static void __exit shrink_async_exit(void)
{
    if (shrink_async_proc_entry)
        proc_remove(shrink_async_proc_entry);

    unregister_shrink_async_vendor_hooks();

    if (shrink_async_wq) {
        flush_workqueue(shrink_async_wq);
        destroy_workqueue(shrink_async_wq);
    }

    pr_info("shrink_async exit succeed!\n");
}

module_init(shrink_async_init);
module_exit(shrink_async_exit);

MODULE_LICENSE("GPL v2");
