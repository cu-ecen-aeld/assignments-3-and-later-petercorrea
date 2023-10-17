/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; 
int aesd_minor =   0;

MODULE_AUTHOR("Peter Correa");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp) {
    struct aesd_dev *dev;
    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos) {
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *read_buffer;
    int count_remaining;
    size_t offset;
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    //mutex lock
    mutex_lock(&dev->read_write_mutex);
    read_buffer = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &offset);
    if (read_buffer == NULL) {
        mutex_unlock(&dev->read_write_mutex);
        return retval;
    }
    retval = (count < (read_buffer->size - offset)) ? count : read_buffer->size - offset;
    count_remaining = copy_to_user(buf, read_buffer->buffptr + offset, retval);
    if (count_remaining != 0) {
        mutex_unlock(&dev->read_write_mutex);
        return -EFAULT;
    }
   
    *f_pos = *f_pos + retval;
    
    mutex_unlock(&dev->read_write_mutex);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    size_t size = dev->entry.size;
    ssize_t count_remaining;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    //mutex
    mutex_lock(&dev->read_write_mutex);
    if (size == 0) {
        dev->entry.buffptr = kzalloc(count, GFP_KERNEL);
    }
    else {
        int new_size = size + count;
        dev->entry.buffptr = krealloc(dev->entry.buffptr, new_size, GFP_KERNEL);
    }
    if (dev->entry.buffptr == NULL) {
        mutex_unlock(&dev->read_write_mutex);
        return retval;
    }

    count_remaining = copy_from_user((void*)&dev->entry.buffptr[size], buf, count);
    retval = count - count_remaining;
    dev->entry.size = size + retval;

    *f_pos = *f_pos + retval;

    if (strchr((char*)dev->entry.buffptr, '\n')) {
        const char* buffptr_to_free = aesd_circular_buffer_add_entry(&dev->buffer,&dev->entry);
        kfree(buffptr_to_free);
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }

    mutex_unlock(&dev->read_write_mutex);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    struct aesd_dev *dev = filp->private_data;
    loff_t retval = -EINVAL;
    PDEBUG("Attempting to adjust offset by: %lld", offset);

    mutex_lock(&dev->read_write_mutex);
    retval = fixed_size_llseek(filp, offset, whence, dev->buffer.total_size);
    mutex_unlock(&dev->read_write_mutex);

    return retval;
}
long aesd_adjust_file_offset(struct file *filp, unsigned int cmd, unsigned int offset) {
    struct aesd_dev *dev = filp->private_data;
    int proposed_cmd = (dev->buffer.out_offs + cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    int new_fpos = 0;
    PDEBUG("cmd: %d offset: %d", cmd, offset);
    PDEBUG("current buffer: %d proposed buffer: %d", dev->buffer.out_offs, proposed_cmd);
    if (cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        return -EINVAL;
    }
    else if (dev->buffer.entry[proposed_cmd].buffptr == NULL) {
        return -EINVAL;
    }
    else if (dev->buffer.entry[proposed_cmd].size < offset) {
        return -EINVAL;
    }
    else {
        int index;
        for (index = 0; index < cmd; index++) { 
            int entry_index = (index + dev->buffer.out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            new_fpos += dev->buffer.entry[entry_index].size;
        }
        new_fpos += offset;
    }
    return new_fpos;
}
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    long retval = 0;
    struct aesd_seekto seekto;

    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) {
                retval = EFAULT;
            }
            else {
                retval = aesd_adjust_file_offset(filp,seekto.write_cmd, seekto.write_cmd_offset);
                filp->f_pos = retval;
            }
            break;
        default:
           break; 
    }

    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek  = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.read_write_mutex);
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    struct aesd_buffer_entry *entry;
    int index;
    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        kfree(entry->buffptr);
    }
    kfree(aesd_device.entry.buffptr);
    mutex_destroy(&aesd_device.read_write_mutex);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);