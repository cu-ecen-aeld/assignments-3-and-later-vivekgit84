/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#else
#include <string.h>
#include <stdio.h>
#include "unity.h" // Include only in user space
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer,
    size_t char_offset,
    size_t *entry_offset_byte_rtn)
{
    size_t cumulative_offset = 0;
    size_t next_offset;
    uint8_t index;

    // Validate input
    if (!buffer || !entry_offset_byte_rtn) 
    {
        return NULL;
    }

   int counter = 0;

    // Traverse the buffer starting from out_offs
    for (index = buffer->out_offs; counter < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED, counter++) {
        struct aesd_buffer_entry *entry = &buffer->entry[index];

        // Break if we reach in_offs and buffer is not full
        if (!(buffer->full) && index == buffer->in_offs) {
            break;
        }

        // Compute the next offset
        next_offset = cumulative_offset + entry->size;

        // Check if char_offset lies within this entry
        if (char_offset < next_offset) {
            *entry_offset_byte_rtn = char_offset - cumulative_offset;
            return entry;
        }

        // Stop condition for wrap-around in full buffer
        if (index == buffer->in_offs && buffer->full && cumulative_offset !=0 ) {
            break;
        }
        
        cumulative_offset = next_offset;
        
   
    }

    // If no valid entry found, return NULL
    return NULL;
  }



/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // Validate input
    if (!buffer || !add_entry) {
        return;
    }

    // Overwrite the oldest entry if the buffer is full
    if (buffer->full) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Insert the new entry
    buffer->entry[buffer->in_offs] = *add_entry;

    // Advance the in_offs pointer
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // Update the full flag
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

/**
* releases memory
*/
void aesd_circular_buffer_exit_cleanup(struct aesd_circular_buffer *buffer)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    
    AESD_CIRCULAR_BUFFER_FOREACH(entry,buffer,index)
    {
      if(entry->buffptr != NULL)
      {
    
#ifdef __KERNEL__
	    kfree(entry->buffptr);
#else
	    free((char *)entry->buffptr);
#endif
      }
      
    }
}
