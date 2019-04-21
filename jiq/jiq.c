/*
 * jiq.c -- the just-in-queue module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: jiq.c,v 1.7 2004/09/26 07:02:43 gregkh Exp $
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>     /* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>  /* error codes */
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <linux/interrupt.h> /* tasklets */
#include <linux/slab.h>
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

/*
 * The delay for the delayed workqueue timer file.
 */
static long delay = 1;
module_param(delay, long, 0);


/*
 * This module is a silly one: it only embeds short code fragments
 * that show how enqueued tasks `feel' the environment
 */
#define LIMIT	(PAGE_SIZE-128)	/* don't print any more after this size */

/*
 * Print information about the current environment. This is called from
 * within the task queues. If the limit is reched, awake the reading
 * process.
 */
static DECLARE_WAIT_QUEUE_HEAD (jiq_wait);

/* 共享队列 */
static struct work_struct jiq_work;
static struct delayed_work jiq_delay_work;

/*
 * Keep track of info we need between task queue runs.
 */
static struct clientdata {
	int len;
	char *kbuf;
	char *ubuf;
	unsigned long jiffies;
	long delay;
} jiq_data;

#define SCHEDULER_QUEUE ((task_queue *) 1)

static void jiq_print_tasklet(unsigned long);
static DECLARE_TASKLET(jiq_tasklet, jiq_print_tasklet, (unsigned long)&jiq_data);



/*
 * struct file_operations {
 *		struct module *owner;
 *		loff_t (*llseek) (struct file *, loff_t, int);
 *		ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
 *  	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
 *  	ssize_t (*aio_read) (struct kiocb *, const struct iovec *, unsigned long, loff_t);
 *  	ssize_t (*aio_write) (struct kiocb *, const struct iovec *, unsigned long, loff_t);
 *  	ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
 *  	ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
 *  	int (*iterate) (struct file *, struct dir_context *);
 *  	unsigned int (*poll) (struct file *, struct poll_table_struct *);
 *  	long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
 *  	long (*compat_ioctl) (struct file *, unsigned int, unsigned long);
 *  	int (*mmap) (struct file *, struct vm_area_struct *);
 *  	int (*open) (struct inode *, struct file *);
 *  	int (*flush) (struct file *, fl_owner_t id);
 *  	int (*release) (struct inode *, struct file *);
 *  	int (*fsync) (struct file *, loff_t, loff_t, int datasync);
 *  	int (*aio_fsync) (struct kiocb *, int datasync);
 *  	int (*fasync) (int, struct file *, int);
 *  	int (*lock) (struct file *, int, struct file_lock *);
 *  	ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
 *  	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
 *  	int (*check_flags)(int);
 *  	int (*flock) (struct file *, int, struct file_lock *);
 *  	ssize_t (*splice_write)(struct pipe_inode_info *, struct file *, loff_t *, size_t, unsigned int);
 *  	ssize_t (*splice_read)(struct file *, loff_t *, struct pipe_inode_info *, size_t, unsigned int);
 *  	int (*setlease)(struct file *, long, struct file_lock **);
 *  	long (*fallocate)(struct file *file, int mode, loff_t offset, loff_t len);
 *  	int (*show_fdinfo)(struct seq_file *m, struct file *f);
 * };
 */


/*
 * Do the printing; return non-zero if the task should be rescheduled.
 */
static int jiq_print(void *ptr) // ptr指向struct clientdata结构
{
	struct clientdata *data = ptr;
	int len = data->len;
	char *kbuf = data->kbuf;
	char *ubuf = data->ubuf;
	unsigned long j = jiffies;

	if (len > LIMIT) { 
		wake_up_interruptible(&jiq_wait); /* awake the process */
		return 0;
	}

	if (len == 0)
		len = sprintf(kbuf,"    time  delta preempt   pid cpu command\n");
	else
		len = 0;

  	/* intr_count is only exported since 1.3.5, but 1.99.4 is needed anyways */
	len += sprintf(kbuf+len, "%9li  %4li     %3i %5i %3i %s\n",
			j, j - data->jiffies,
			preempt_count(), current->pid, smp_processor_id(),
			current->comm);
	// copy_to_user()函数
	copy_to_user(ubuf, kbuf, len);

	data->len += len;
	data->kbuf += len;
	data->jiffies = j;
	return 1;
}


/*
 * Call jiq_print from a work queue
 */
static void jiq_print_wq(struct work_struct *ptr)
{
	struct clientdata *data = (struct clientdata *) ptr;
    
	if (! jiq_print (ptr)) // 唤醒
		return;
    
	if (data->delay)
		/*
		 * bool schedule_delayed_work(struct work_struct *work,
		 *                            unsigned long delay)
		 * -- put work task in gloabl workqueue after delay
		 */
		schedule_delayed_work(&jiq_delay_work, data->delay);
	else
		/*
		 * bool schedule_work(struct work_struct *work)
		 * -- put work task in global workqueue
		 */
		schedule_work(&jiq_work); //提交到共享队列
}

static int jiq_open_wq(struct inode *pinode, struct file *pfile)
{
	char* p;
	if ((p = kmalloc(LIMIT, GFP_KERNEL)) == NULL)
		return -1;
	jiq_data.kbuf = p;
	return 0;
}

static int jiq_release_wq(struct inode *pinode, struct file *pfile)
{
	char *p = jiq_data.kbuf;
	if (p)
		kfree(p);
	return 0;
}


/* ssize_t (*read) (struct file *, char __user *, size_t, loff_t *); */
static ssize_t jiq_read_wq(struct file *pfile, char __user *buf, size_t len, loff_t *poff)
{
	DEFINE_WAIT(wait);
	
	jiq_data.len = 0;                /* nothing printed, yet */
	jiq_data.ubuf = buf;              /* print in this place */
	jiq_data.jiffies = jiffies;      /* initial time */
	jiq_data.delay = 0;

	// 将当前进程状态设置为TASK_INTERRUPTIBLE，同时将wait任务加入到jiq_wait队列中
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_work(&jiq_work);
	// 开始调度
	schedule(); 
	// 将当前进程状态设置为TASK_RUNNING
	finish_wait(&jiq_wait, &wait);

	return jiq_data.len;
}

static ssize_t jiq_read_wq_delayed(struct file *pfile, char __user *buf, size_t len, loff_t *poff)
{
	// 定义一个wait事件
	DEFINE_WAIT(wait);
	
	jiq_data.len = 0;                /* nothing printed, yet */
	jiq_data.ubuf = buf;              /* print in this place */
	jiq_data.jiffies = jiffies;      /* initial time */
	jiq_data.delay = delay;
    
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_delayed_work(&jiq_delay_work, delay);
	schedule();
	finish_wait(&jiq_wait, &wait);

	return jiq_data.len;
}

/*
 * Call jiq_print from a tasklet
 */
static void jiq_print_tasklet(unsigned long ptr)
{
	if (jiq_print ((void *) ptr))
		tasklet_schedule (&jiq_tasklet);
}

static ssize_t jiq_read_tasklet(struct file *pfile, char __user *buf, size_t len, loff_t *poff)
{
    DEFINE_WAIT(wait);

	jiq_data.len = 0;                /* nothing printed, yet */
	jiq_data.ubuf = buf;              /* print in this place */
	jiq_data.jiffies = jiffies;      /* initial time */

	tasklet_schedule(&jiq_tasklet);
	//interruptible_sleep_on(&jiq_wait);    /* interruptible_sleep_on() is deprecated in Linux Kernel 3.16 */
    prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
    schedule();
    finish_wait(&jiq_wait, &wait);

	return jiq_data.len;
}

/*
 * This one, instead, tests out the timers.
 */

static struct timer_list jiq_timer;

static void jiq_timedout(unsigned long ptr)
{
	jiq_print((void *)ptr);            /* print a line */
	wake_up_interruptible(&jiq_wait);  /* awake the process */
}

static ssize_t jiq_read_run_timer(struct file *pfile, char __user *buf, size_t len, loff_t *poff)
{

	jiq_data.len = 0;           /* prepare the argument for jiq_print() */
	jiq_data.ubuf = buf;
	jiq_data.jiffies = jiffies;

	init_timer(&jiq_timer);              /* init the timer structure */
	jiq_timer.function = jiq_timedout;
	jiq_timer.data = (unsigned long)&jiq_data;
	jiq_timer.expires = jiffies + HZ; /* one second */

	jiq_print(&jiq_data);   /* print and go to sleep */
	add_timer(&jiq_timer);
	//interruptible_sleep_on(&jiq_wait);  /* RACE -- interruptible_sleep_on() is deprecated in Linux Kernel 3.16 */
	del_timer_sync(&jiq_timer);  /* in case a signal woke us up */
    
	return jiq_data.len;
}

struct file_operations proc_jiq_wq = 
{
	.owner = THIS_MODULE,
	.read = jiq_read_wq,
	.open = jiq_open_wq,
	.release = jiq_release_wq,
};

struct file_operations proc_jiq_wq_delay =
{
	.owner = THIS_MODULE,
	.read = jiq_read_wq_delayed,
};

struct file_operations proc_jiq_tasklet = 
{
	.owner = THIS_MODULE,
	.read = jiq_read_tasklet,
};

struct file_operations proc_jiq_timeout =
{
	.owner = THIS_MODULE,
	.read = jiq_read_run_timer,
};

/*
 * the init/clean material
 */

static int jiq_init(void)
{

	/* 初始化一个任务 */
	INIT_WORK(&jiq_work, jiq_print_wq);
    INIT_DELAYED_WORK(&jiq_delay_work, jiq_print_wq);

	/*
	 * struct proc_dir_entry *proc_create(const char *name, umode_t mode, 
	 *                                    struct proc_dir_entry *parent, const struct file_operations *proc_fops)
	 */
	proc_create("jiqwq", 0, NULL, &proc_jiq_wq);
	proc_create("jiqwqdelay", 0, NULL, &proc_jiq_wq_delay);
	proc_create("jiqtimer", 0, NULL, &proc_jiq_timeout);
	proc_create("jiqtasklet", 0, NULL, &proc_jiq_tasklet);
    /*
     * create_proc_read_entry() is deprecated
     */
	//create_proc_read_entry("jiqwq", 0, NULL, jiq_read_wq, NULL);
	//create_proc_read_entry("jiqwqdelay", 0, NULL, jiq_read_wq_delayed, NULL);
	//create_proc_read_entry("jitimer", 0, NULL, jiq_read_run_timer, NULL);
	//create_proc_read_entry("jiqtasklet", 0, NULL, jiq_read_tasklet, NULL);

	return 0; /* succeed */
}

static void jiq_cleanup(void)
{
	remove_proc_entry("jiqwq", NULL);
	remove_proc_entry("jiqwqdelay", NULL);
	remove_proc_entry("jitimer", NULL);
	remove_proc_entry("jiqtasklet", NULL);
}


module_init(jiq_init);
module_exit(jiq_cleanup);
