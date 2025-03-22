
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

#include "aesdchar.h"

#define FALSE 0
#define TRUE 1

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Piistachyoo");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
struct class *aesd_class;
struct device *aesd_device_node;
int aesd_open(struct inode *inode, struct file *filp) {
    struct aesd_dev *ptr_aesd_dev;
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    ptr_aesd_dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = ptr_aesd_dev;
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
    ssize_t retval = 0;
    struct aesd_buffer_entry *ret_entry = 0;
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle read
     */
    if (mutex_lock_interruptible(&(dev->lock))) {
        PDEBUG("ERROR: Couldn't acquire lock\n");
        retval = -ERESTARTSYS;
        goto inCaseOfFailure;
    }

    ret_entry = aesd_circular_buffer_find_entry_offset_for_fpos(
        &(dev->buffer), *f_pos, &retval);
    if (ret_entry == NULL) {
        PDEBUG("Not enough data written!\n");
        retval = 0;
        goto inCaseOfFailure;
    }
    PDEBUG("Readed data\n");
    retval = ret_entry->size;
    if (copy_to_user(buf, ret_entry->buffptr, retval)) {
        PDEBUG("Error copying data to user buffer\n");
        retval = -EFAULT;
        goto inCaseOfFailure;
    } else {
        *f_pos += retval;
    }

    PDEBUG("Data copied: %s \n", ret_entry->buffptr);
    PDEBUG("new offset: %lld\n", *f_pos);
    PDEBUG("reval: %ld\n", retval);

inCaseOfFailure:
    mutex_unlock(&(dev->lock));
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos) {
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle write
     */
    if (mutex_lock_interruptible(&(dev->lock))) {
        PDEBUG("ERROR: Couldn't acquire lock\n");
        retval = -ERESTARTSYS;
        goto inCaseOfFailure;
    }

    dev->data_buffer.buffptr = krealloc(
        dev->data_buffer.buffptr, dev->data_buffer.size + count, GFP_KERNEL);
    if (dev->data_buffer.buffptr == NULL) {
        PDEBUG("Error allocating memory!\n");
        retval = -ENOMEM;
        goto inCaseOfFailure;
    }

    /* Copy data from user space to kernel space */
    if (copy_from_user(
            (char *)(dev->data_buffer.buffptr + dev->data_buffer.size), buf,
            count)) {
        PDEBUG("Error copying data from user buffer\n");
        retval = -EFAULT;
        goto inCaseOfFailure;
    }

    PDEBUG("Data in buffer is: %s", dev->data_buffer.buffptr);
    retval = count;
    dev->data_buffer.size += count;

    /* Check if it was ended with endline or not */
    if (dev->data_buffer.buffptr[dev->data_buffer.size - 1] != '\n') {
        PDEBUG("WARNING: Buffer has no end line\n");
        PDEBUG("Skipping data entry\n");
    } else {
        PDEBUG("Data in buffer: %s\n", dev->data_buffer.buffptr);
        PDEBUG("Size of buffer: %zu\n", dev->data_buffer.size);
        aesd_circular_buffer_add_entry(&(dev->buffer), &(dev->data_buffer));
        PDEBUG("Entry added\n");
        dev->data_buffer.size = 0;
        dev->data_buffer.buffptr = NULL;
    }

inCaseOfFailure:
    mutex_unlock(&(dev->lock));
    return retval;
}
struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev) {
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        PDEBUG("Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void) {
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        // printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        PDEBUG("Can't get major %d\n", aesd_major);
        goto deviceNumberFail;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    result = aesd_setup_cdev(&aesd_device);
    if (result) {
        goto cdevRegisterFail;
    }

    aesd_circular_buffer_init(&aesd_device.buffer);

    aesd_device.data_buffer.buffptr = kmalloc(0, GFP_KERNEL);
    aesd_device.data_buffer.size = 0;

    return 0;

cdevRegisterFail:
    unregister_chrdev_region(dev, 1);
deviceNumberFail:
    return result;
}

void aesd_cleanup_module(void) {
    uint8_t index;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    // for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++)
    // {
    //     if (aesd_device.buffer.entry[index].buffptr) {
    //         kfree(aesd_device.buffer.entry[index].buffptr);
    //     }
    // }

    // if (aesd_device.data_buffer.buffptr)
    //     kfree(aesd_device.data_buffer.buffptr);

    cdev_del(&(aesd_device.cdev));
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

