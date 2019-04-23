#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chenzheng");
MODULE_DESCRIPTION("Simple module featuring proc read and workqueue");

#define BUFLEN 128

static struct work_struct wq; // a work
static DECLARE_WAIT_QUEUE_HEAD (mywait);

static struct file_operations fops;

static char *message;
static int pread;
static unsigned long jiffies_save; // saved jiffies value

static void workqueue_fn(struct work_struct *work)
{
    size_t len;
    unsigned long j = jiffies;
    printk(KERN_ALERT "workqueue_fn is executing\n");
    //len = strlen(message);
    //strcpy(message + len, "workqueue_fn has been called\n");
    len = sprintf(message, "    time  delta preempt  pid cpu commd\n");
    sprintf(message + len, "%9li  %4li    %3i %5i %3i %s\n",
            j, j - jiffies_save, preempt_count(), current->pid, smp_processor_id(), current->comm);
	// 唤醒休眠的线程
    wake_up_interruptible(&mywait);
}

int hello_proc_open(struct inode *pinode, struct file *pfile)
{
    printk(KERN_ALERT "proc called open\n");

    pread = 1;
    message = kmalloc(sizeof(char) * BUFLEN, __GFP_WAIT | __GFP_IO | __GFP_FS);
    if (message == NULL) {
        printk(KERN_ALERT "hello_proc_open failed\n");
        return -ENOMEM;
    }
    //strcpy(message, "Hello World\n");
    return 0;
}

ssize_t hello_proc_read(struct file *pfile, char __user *buf, size_t size, loff_t *offset)
{
    size_t len;
    DEFINE_WAIT(wait);

    jiffies_save = jiffies;
    printk(KERN_ALERT "proc called read\n");
    /* read loops until you return 0 */
    if (!pread) {
        pread = 1;
        return 0;
    }

	// 自我休眠，等待其他线程将我唤醒
    prepare_to_wait(&mywait, &wait, TASK_INTERRUPTIBLE);
    schedule_work(&wq);
    schedule();
    finish_wait(&mywait, &wait);

	// 被其他线程唤醒，开始执行余下代码
    len = strlen(message);
    copy_to_user(buf, message, len);
    pread = 0; /* end loops by set flag pread */
    return len;
}

int hello_proc_release(struct inode *pinode, struct file *pfile)
{
    printk(KERN_ALERT "proc called release\n");
    kfree(message);
    message = 0;
    return 0;
}

static int hello_init(void)
{
    printk(KERN_ALERT "hello init\n");
    fops.open = hello_proc_open;
    fops.read = hello_proc_read;
    fops.release = hello_proc_release;
    fops.owner = THIS_MODULE;

    INIT_WORK(&wq, workqueue_fn);

    if (!proc_create("mywq", 0, NULL, &fops)) {
        printk(KERN_ALERT "proc_create failed\n");
        remove_proc_entry("hello_proc", NULL);
        return -ENOMEM;
    }
    return 0;
}

static void hello_exit(void)
{
    printk(KERN_ALERT "hello_exit\n");
    remove_proc_entry("hello_proc", NULL);
}

module_init(hello_init);
module_exit(hello_exit);

