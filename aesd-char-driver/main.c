/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include "aesdchar.h"

#define FALSE 0
#define TRUE 1

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;
loff_t offset_backup = 0;
uint8_t ioctl_called = 0;
MODULE_AUTHOR("Vivek Tewari");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
struct class *aesd_class;
struct device *aesd_device_node;

int aesd_open(struct inode *inode, struct file *filp) {
    struct aesd_dev *ptr_aesd_dev;
    PDEBUG("open");

    ptr_aesd_dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = ptr_aesd_dev;

    if (ioctl_called) {
        ioctl_called = 0;
        filp->f_pos = offset_backup;
    }

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t retval = 0;
    ssize_t offset;
    struct aesd_buffer_entry *ret_entry = 0;
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

    PDEBUG("read %zu bytes with offset %lld\n", count, *f_pos);
    PDEBUG("filp->f_offs = %lld\n", filp->f_pos);

    if (mutex_lock_interruptible(&(dev->lock))) {
        PDEBUG("ERROR: Couldn't acquire lock\n");
        return -ERESTARTSYS;
    }

    ret_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&(dev->buffer), *f_pos, &offset);

    if (ret_entry == NULL) {
        PDEBUG("Not enough data written!\n");
        mutex_unlock(&(dev->lock));
        return 0;
    }

    retval = ret_entry->size - offset;
    if (copy_to_user(buf, (void *)(ret_entry->buffptr + offset), retval)) {
        PDEBUG("Error copying data to user buffer\n");
        mutex_unlock(&(dev->lock));
        return -EFAULT;
    }

    *f_pos += retval;

    PDEBUG("Data copied: %s", ret_entry->buffptr + offset);
    PDEBUG("new offset: %lld\n", *f_pos);
    PDEBUG("retval: %ld\n", retval);
    PDEBUG("-------------------------------\n");

    mutex_unlock(&(dev->lock));
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&(dev->lock))) {
        PDEBUG("ERROR: Couldn't acquire lock\n");
        return -ERESTARTSYS;
    }

    void *new_buffptr = krealloc(dev->data_buffer.buffptr, dev->data_buffer.size + count, GFP_KERNEL);
    if (new_buffptr == NULL) {
        PDEBUG("Error allocating memory!\n");
        mutex_unlock(&(dev->lock));
        return -ENOMEM;
    }

    dev->data_buffer.buffptr = new_buffptr;

    if (copy_from_user((char *)(dev->data_buffer.buffptr + dev->data_buffer.size), buf, count)) {
        PDEBUG("Error copying data from user buffer\n");
        mutex_unlock(&(dev->lock));
        return -EFAULT;
    }

    retval = count;
    dev->data_buffer.size += count;

    if (dev->data_buffer.buffptr[dev->data_buffer.size - 1] != '\n') {
        PDEBUG("WARNING: Buffer has no end line\n");
    } else {
        aesd_circular_buffer_add_entry(&(dev->buffer), &(dev->data_buffer));
        dev->data_buffer.size = 0;
        dev->data_buffer.buffptr = NULL;
    }

    *f_pos += count;
    mutex_unlock(&(dev->lock));
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t f_offs, int whence) {
    struct aesd_dev *dev = filp->private_data;
    loff_t retval;
    loff_t total_size = 0;
    uint8_t index;

    if (mutex_lock_interruptible(&(dev->lock))) {
        PDEBUG("ERROR: Couldn't acquire lock\n");
        return -ERESTARTSYS;
    }

    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++) {
        if (dev->buffer.entry[index].buffptr) {
            total_size += dev->buffer.entry[index].size;
        }
    }

    switch (whence) {
        case SEEK_SET:
            retval = f_offs;
            break;
        case SEEK_CUR:
            retval = filp->f_pos + f_offs;
            break;
        case SEEK_END:
            retval = total_size + f_offs;
            break;
        default:
            mutex_unlock(&(dev->lock));
            return -EINVAL;
    }

    if (retval < 0 || retval > total_size) {
        mutex_unlock(&(dev->lock));
        return -EINVAL;
    }

    filp->f_pos = retval;
    mutex_unlock(&(dev->lock));
    return retval;
}

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset) {
    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *buffer = &(dev->buffer);
    uint8_t index;
    long new_offset = 0;

    if (mutex_lock_interruptible(&(dev->lock))) {
        return -ERESTARTSYS;
    }

    if ((write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) || (buffer->entry[write_cmd].buffptr == NULL) || (buffer->entry[write_cmd].size <= write_cmd_offset)) {
        mutex_unlock(&(dev->lock));
        return -EINVAL;
    }

    for (index = 0; index < write_cmd; index++) {
        new_offset += buffer->entry[index].size;
    }

    new_offset += write_cmd_offset;
    filp->f_pos = new_offset;
    offset_backup = filp->f_pos;
    ioctl_called = 1;

    mutex_unlock(&(dev->lock));
    return 0;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    long retval;
    struct aesd_seekto seekto;

    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) {
                return -EFAULT;
            }
            retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            break;
        default:
            return -EINVAL;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl
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
        PDEBUG("Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    result = aesd_setup_cdev(&aesd_device);
    if (result) {
        unregister_chrdev_region(dev, 1);
        return result;
    }

    aesd_circular_buffer_init(&aesd_device.buffer);

    aesd_device.data_buffer.buffptr = kmalloc(0, GFP_KERNEL);
    aesd_device.data_buffer.size = 0;

    return 0;
}

void aesd_cleanup_module(void) {
    uint8_t index;
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++) {
        if (aesd_device.buffer.entry[index].buffptr != NULL) {
            kfree(aesd_device.buffer.entry[index].buffptr);
        }
    }
    if (aesd_device.data_buffer.buffptr != NULL) {
        kfree(aesd_device.data_buffer.buffptr);
    }
    cdev_del(&(aesd_device.cdev));
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
