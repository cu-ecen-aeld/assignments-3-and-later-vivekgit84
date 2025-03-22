/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

/*
https://www.youtube.com/watch?v=uvD9_Wdtjtw
*/
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#include <stdlib.h>
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
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
			size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */
    
    //capture current status
    uint8_t curr_ref_position = buffer->out_offs;
    size_t curr_ref_position_size = buffer->entry[curr_ref_position].size;
    int prev_char_position = 0;
    
    //NULL check
    if( (buffer == NULL) || (entry_offset_byte_rtn == NULL))
    {
        return NULL;
    }
    
    //less than case
    //do
    while(1)
    {
      if(char_offset < curr_ref_position_size)
      {
        *entry_offset_byte_rtn = (char_offset - prev_char_position);
        return &buffer->entry[curr_ref_position];
      }
      //next location required to reference, as the index of 
      //the character requested is more than the size of the content is
      else
      {
        //take size of previous entry , number of characters pseudo traversed till now
        prev_char_position = curr_ref_position_size;
        
        //increment to the next location to make the proper return when hit
        curr_ref_position = ( (curr_ref_position + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
        
        //find the size for existing entry and add it to previous, to kniow if next
        //condition woudld pass or not
        curr_ref_position_size += buffer->entry[curr_ref_position].size;
         
      }
      
      //break when the traversing has reached to the same location
      if(curr_ref_position == buffer->out_offs)
      {
        break;
      }
      
    }
    //while(curr_ref_position == buffer->out_offs);
    
    return NULL;

}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char* aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description 
    */
    
    //null check
    const char *data_overflow = NULL;
    if((buffer == NULL) || (add_entry == NULL))
    {
      return data_overflow;
    }
    
    if(buffer->full)
    {
      data_overflow = buffer->entry[buffer->in_offs].buffptr;
    }
    
    //add content to the buffer as the data has passed error check 
    buffer->entry[buffer->in_offs] = *add_entry;
    buffer->in_offs = ( (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    
    //for the given entry addition , check the relative read(out) position
    if(buffer->full == true)
    {
      //updated as per review comments from Dan
      //buffer->out_offs++;
      buffer->out_offs = (  (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    }

    //check for buffer to be full now? by checking the read(out) and write(in) pointer locations
    if(buffer->in_offs == buffer->out_offs)
    {
      buffer->full = true;
    }
    else
    {
      buffer->full = false;
    }
    
    return data_overflow;
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

/* EOF */
