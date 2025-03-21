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
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Vivek Tewari"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
     
    // Retrieve the container structure from the inode
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    // Store the device pointer in the file's private data
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    /**
     * Typically, there's not much to do here unless you need to clean up
     * or release resources specific to this file descriptor.
     */

    // If there are resources associated with this file descriptor
    // (e.g., dynamically allocated memory), clean them up here.

    return 0; // Return success
}


ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0; // Number of bytes successfully read
    struct aesd_buffer_entry *entry;
    size_t entry_offset, bytes_to_copy, remaining_bytes;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    // Lock the device for thread-safe access
    mutex_lock(&dev->lock);

    // Retrieve the entry and offset based on the current file position
    entry = aesd_circular_buffer_find_entry_offset_for_pos(&dev->circular_buffer, *f_pos, &entry_offset);
    if (!entry) {
        // No data available for the current file position
        mutex_unlock(&dev->lock);
        return 0; // EOF
    }

    // Calculate the number of bytes remaining in the current entry
    remaining_bytes = entry->size - entry_offset;

    // Determine how many bytes to copy (the smaller of remaining bytes and count)
    bytes_to_copy = min(remaining_bytes, count);

    // Copy data from the circular buffer to user space
    if (copy_to_user(buf, entry->buffer + entry_offset, bytes_to_copy)) {
        mutex_unlock(&dev->lock);
        return -EFAULT; // Return error for failed copy
    }

    // Update file position based on the number of bytes copied
    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

    mutex_unlock(&dev->lock);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = count; // Return the number of bytes successfully written
    char *kern_buf = NULL;
    char *temp_buf = NULL;
    size_t remaining_space;
    struct aesd_buffer_entry *new_entry;
    bool found_newline = false;
    size_t i;

    PDEBUG("write %zu bytes", count);

    // Allocate kernel buffer for the write request
    kern_buf = kmalloc(count, GFP_KERNEL);
    if (!kern_buf) {
        return -ENOMEM;
    }

    // Copy data from user space to kernel space
    if (copy_from_user(kern_buf, buf, count)) {
        kfree(kern_buf);
        return -EFAULT;
    }

    // Lock the device to ensure thread-safe access
    mutex_lock(&dev->lock);

    // Append data to the partial buffer until terminated by '\n'
    for (i = 0; i < count; ++i) {
        if (kern_buf[i] == '\n') {
            found_newline = true;
            break;
        }
    }

    if (!found_newline) {
        // Append the entire buffer to the partial command
        dev->partial_command = krealloc(dev->partial_command, dev->partial_size + count, GFP_KERNEL);
        if (!dev->partial_command) {
            mutex_unlock(&dev->lock);
            kfree(kern_buf);
            return -ENOMEM;
        }
        memcpy(dev->partial_command + dev->partial_size, kern_buf, count);
        dev->partial_size += count;
    } else {
        // Process the command terminated by '\n'
        size_t newline_offset = i + 1;

        // Allocate memory for the complete command
        temp_buf = kmalloc(dev->partial_size + newline_offset, GFP_KERNEL);
        if (!temp_buf) {
            mutex_unlock(&dev->lock);
            kfree(kern_buf);
            return -ENOMEM;
        }

        // Copy the partial command and the new data
        memcpy(temp_buf, dev->partial_command, dev->partial_size);
        memcpy(temp_buf + dev->partial_size, kern_buf, newline_offset);

        // Free the old partial command
        kfree(dev->partial_command);
        dev->partial_command = NULL;
        dev->partial_size = 0;

        // Add the new entry to the circular buffer
        new_entry = aesd_circular_buffer_add_entry(&dev->circular_buffer, temp_buf, dev->partial_size + newline_offset);
        if (!new_entry) {
            kfree(temp_buf); // Free memory if adding to the buffer fails
        }

        // Remove old entries if there are more than 10
        if (dev->circular_buffer.size > 10) {
            aesd_circular_buffer_remove_oldest_entry(&dev->circular_buffer);
        }
    }

    mutex_unlock(&dev->lock);
    kfree(kern_buf);

    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
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
     aesd_circular_buffer_init(&aesd_device.circ_buffer);
     mutex_init(&aesd_device.device_lock);
	 

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
