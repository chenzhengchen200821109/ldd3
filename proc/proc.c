#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chenzheng");
MODULE_DESCRIPTION("Simple module featuring proc read");

static struct file_operations fops;

static char *message;
static int pread;

int hello_proc_open(struct inode *pinode, struct file *pfile)
{
    printk(KERN_ALERT "proc called open\n");

    pread = 1;
    message = kmalloc(sizeof(char) * 20, __GFP_WAIT | __GFP_IO | __GFP_FS);
    if (message == NULL) {
        printk(KERN_ALERT "hello_proc_open failed\n");
        return -ENOMEM;
    }
    strcpy(message, "Hello World\n");
    return 0;
}

ssize_t hello_proc_read(struct file *pfile, char __user *buf, size_t size, loff_t *offset)
{
    size_t len = strlen(message);

    /* read loops until you return 0 */
    if (!pread)
        return 0;
    printk(KERN_ALERT "proc called read\n");
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

    if (!proc_create("hello_proc", 0, NULL, &fops)) {
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

