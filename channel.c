#include "channel.h"

// Creates a new channel with the provided size and returns it to the caller
channel_t* channel_create(size_t size)
{
    // create a channel object on the heap
    channel_t* new_channel = malloc(sizeof(channel_t));
    // initialize the buffer of this channel
    buffer_t* buff = buffer_create(size);
    // update the channel's buffer
    new_channel->buffer = buff;
    // initialize the channel's semaphores
    /*
    sem_t mutex_buff; // lock for the buffer add and remove operations
    sem_t empty; // for synchronizing buffer add across multiple producers
    sem_t full; // for synchronizing buffer remove across multiple consumers
    bool open_status; // for channel open/close status. 1 for open, 0 for closed
    sem_t mutex_status; // lock for the status variable
    */
    pthread_mutex_init(&new_channel->channel_lock, NULL);
    pthread_cond_init(&new_channel->full, NULL);
    pthread_cond_init(&new_channel->empty, NULL);
    new_channel->channel_status = true;
    // initialize the sender, receiver lists;
    new_channel->sel_sends = list_create();
    new_channel->sel_recvs = list_create();
    return new_channel;
}

enum channel_status channel_send_core(channel_t *channel, void* data){
  // assume the calling process already holds the lock, and the buffer has adequate size, so just send it
  // and then notify the waiting consumers and send/receiver lists
  // write to the buffer
  if (buffer_add(channel->buffer, data) == BUFFER_ERROR){
    return GENERIC_ERROR;
  }
  // signal to a consumer thread to consume data if required
  pthread_cond_signal(&channel->full);       
  // acquire that local lock defined in select

  // notify all select receives on this channel
  size_t num_recvs = list_count(channel->sel_recvs);
  if (num_recvs != 0){
    // iterate over the list
    list_node_t* head = list_head(channel->sel_recvs);
    while (head != NULL){
      // lock the corresponding select lock pointer
      pthread_mutex_lock(((sel_sync_t*)head->data)->sel_lock);
      // signal the thread
      pthread_cond_signal(((sel_sync_t*)head->data)->sel_cond);
      pthread_mutex_unlock(((sel_sync_t*)head->data)->sel_lock);
      head = head->next;
    }
  }
  return SUCCESS;
}

enum channel_status channel_receive_core(channel_t *channel, void** data){
  // assume the calling process already holds the lock, and the buffer has > 0 size, so just receive from it
  // and then notify the waiting consumers and send/receiver lists
  // remove from the buffer
  if (buffer_remove(channel->buffer, data) == BUFFER_ERROR){
    pthread_mutex_unlock(&channel->channel_lock);
    return GENERIC_ERROR;
  }
  // signal to a prdoducer thread to produce data if required
  pthread_cond_signal(&channel->empty);
  
  // notify all select sends on this channel
  size_t num_sends = list_count(channel->sel_sends);
  if (num_sends != 0){
    // iterate over the list
    list_node_t* head = list_head(channel->sel_sends);
    while (head != NULL){
      // lock the corresponding select lock pointer
      pthread_mutex_lock(((sel_sync_t*)head->data)->sel_lock);
      // signal the thread
      pthread_cond_signal(((sel_sync_t*)head->data)->sel_cond);
      pthread_mutex_unlock(((sel_sync_t*)head->data)->sel_lock);
      head = head->next;
    }
  }
  return SUCCESS;
}


// Writes data to the given channel
// This is a blocking call i.e., the function only returns on a successful completion of send
// In case the channel is full, the function waits till the channel has space to write the new data
// Returns SUCCESS for successfully writing data to the channel,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_send(channel_t *channel, void* data)
{
    // acquire lock
    pthread_mutex_lock(&channel->channel_lock);
    // thread woke up now, and guaranteed that buffer is not full, just now check if channel is open, still hold the lock, so no closer thread can close the channel
    if (channel->channel_status == false){
      pthread_mutex_unlock(&channel->channel_lock);
      return CLOSED_ERROR;
    }
    // see if buffer is not full
    size_t cap = buffer_capacity(channel->buffer);
    while (buffer_current_size(channel->buffer) == cap)
    {
      // wait for a consumer thread
      if (pthread_cond_wait(&channel->empty, &channel->channel_lock) != 0){
        pthread_mutex_unlock(&channel->channel_lock);
        return GENERIC_ERROR;
      }
      if (channel->channel_status == false){
	pthread_mutex_unlock(&channel->channel_lock);
        return CLOSED_ERROR;
      }
    }
    
    enum channel_status stat = channel_send_core(channel, data);
    // unlock the channel_lock
    pthread_mutex_unlock(&channel->channel_lock);
    return stat;
}
// Reads data from the given channel and stores it in the function's input parameter, data (Note that it is a double pointer)
// This is a blocking call i.e., the function only returns on a successful completion of receive
// In case the channel is empty, the function waits till the channel has some data to read
// Returns SUCCESS for successful retrieval of data,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_receive(channel_t* channel, void** data)
{
    // acquire lock
    pthread_mutex_lock(&channel->channel_lock);
    // thread woke up now, and guaranteed that buffer is not empty, just now check if channel is open, still hold the lock, so no closer thread can close the channel
    if (channel->channel_status == false){
      pthread_mutex_unlock(&channel->channel_lock);
      return CLOSED_ERROR;
    }

    // see if buffer is not empty
    while (buffer_current_size(channel->buffer) == 0)
    {
      // wait for a producer thread
      if (pthread_cond_wait(&channel->full, &channel->channel_lock) != 0){
        pthread_mutex_unlock(&channel->channel_lock);
        return GENERIC_ERROR;
      }
        // thread woke up now, and guaranteed that buffer is not empty, just now check if channel is open, still hold the lock, so no closer thread can close the channel
      if (channel->channel_status == false){
        pthread_mutex_unlock(&channel->channel_lock);
        return CLOSED_ERROR;
      }
    }
    enum channel_status stat = channel_receive_core(channel, data);
    // unlock the channel_lock
    pthread_mutex_unlock(&channel->channel_lock);
    return stat;
}


// Writes data to the given channel
// This is a non-blocking call i.e., the function simply returns if the channel is full
// Returns SUCCESS for successfully writing data to the channel,
// CHANNEL_FULL if the channel is full and the data was not added to the buffer,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_non_blocking_send(channel_t* channel, void* data)
{
    // first detect if the buffer is full, acquire lock first
    pthread_mutex_lock(&channel->channel_lock);
    
    // check if channel is not closed
    if (channel->channel_status == false){
        pthread_mutex_unlock(&channel->channel_lock);
        return CLOSED_ERROR;
    }
    size_t cap = buffer_capacity(channel->buffer);
    if (buffer_current_size(channel->buffer) == cap){
        // buffer is now full, release the lock first, and then return CHANNEL_FULL status
        pthread_mutex_unlock(&channel->channel_lock);
        return CHANNEL_FULL;
    }
    // if channel is not full, blocking send should work
    // lock should be still held
    // do things like adding to buffer and notifying threads only
    enum channel_status stat = channel_send_core(channel, data);
    // release the lock on the buffer
    pthread_mutex_unlock(&channel->channel_lock);
    return stat;
}

// Reads data from the given channel and stores it in the function's input parameter data (Note that it is a double pointer)
// This is a non-blocking call i.e., the function simply returns if the channel is empty
// Returns SUCCESS for successful retrieval of data,
// CHANNEL_EMPTY if the channel is empty and nothing was stored in data,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_non_blocking_receive(channel_t* channel, void** data)
{
    // first detect if the buffer is empty, acquire lock first
    pthread_mutex_lock(&channel->channel_lock);
    
    // check if channel is not closed
    if (channel->channel_status == false){
        pthread_mutex_unlock(&channel->channel_lock);
        return CLOSED_ERROR;
    }
    if (buffer_current_size(channel->buffer) == 0){
        // buffer is now empty, release the lock first, and then return CHANNEL_EMPTY status
        pthread_mutex_unlock(&channel->channel_lock);
        return CHANNEL_EMPTY;
    }
    // if channel is not empty, blocking receive should work
    enum channel_status stat = channel_receive_core(channel, data);
    // release the lock on the buffer
    pthread_mutex_unlock(&channel->channel_lock);
    return stat;
}

// Closes the channel and informs all the blocking send/receive/select calls to return with CLOSED_ERROR
// Once the channel is closed, send/receive/select operations will cease to function and just return CLOSED_ERROR
// Returns SUCCESS if close is successful,
// CLOSED_ERROR if the channel is already closed, and
// GENERIC_ERROR in any other error case
enum channel_status channel_close(channel_t* channel)
{
    // try to acquire the status lock first
    // acquire the channel lock first
    pthread_mutex_lock(&channel->channel_lock);
    //update the status to closed
    if (channel->channel_status == false){
        pthread_mutex_unlock(&channel->channel_lock);
        return CLOSED_ERROR;
    }
    else{
        channel->channel_status = false;
        // need to wake up all threads (senders and receivers)
        pthread_cond_broadcast(&channel->full);
        pthread_cond_broadcast(&channel->empty);
        
        // notify all sender and receiver threads on this channel
        size_t num_recvs = list_count(channel->sel_recvs);
	    if (num_recvs != 0){
	      // iterate over the list
	      list_node_t* head = list_head(channel->sel_recvs);
	      while (head != NULL){
		// lock the corresponding select lock pointer
		pthread_mutex_lock(((sel_sync_t*)head->data)->sel_lock);
		// signal the thread
		pthread_cond_signal(((sel_sync_t*)head->data)->sel_cond);
		pthread_mutex_unlock(((sel_sync_t*)head->data)->sel_lock);
		head = head->next;
	      }
	    }
	
	size_t num_sends = list_count(channel->sel_sends);
	    if (num_sends != 0){
	      // iterate over the list
	      list_node_t* head = list_head(channel->sel_sends);
	      while (head != NULL){
		// lock the corresponding select lock pointer
		pthread_mutex_lock(((sel_sync_t*)head->data)->sel_lock);
		// signal the thread
		pthread_cond_signal(((sel_sync_t*)head->data)->sel_cond);
		pthread_mutex_unlock(((sel_sync_t*)head->data)->sel_lock);
		head = head->next;
	      }
	    }    
        pthread_mutex_unlock(&channel->channel_lock);
        
        //if (channel->select_lock != NULL){
	//	pthread_mutex_lock(channel->select_lock);
	//	pthread_cond_signal(channel->select_cond);
	//	pthread_mutex_unlock(channel->select_lock);
	//}        
    }
    return SUCCESS;
}

// Frees all the memory allocated to the channel
// The caller is responsible for calling channel_close and waiting for all threads to finish their tasks before calling channel_destroy
// Returns SUCCESS if destroy is successful,
// DESTROY_ERROR if channel_destroy is called on an open channel, and
// GENERIC_ERROR in any other error case
enum channel_status channel_destroy(channel_t* channel)
{
    
    pthread_mutex_lock(&channel->channel_lock);
    // check if channel is open
    if (channel->channel_status == true)
    {
        pthread_mutex_unlock(&channel->channel_lock);
        return DESTROY_ERROR;
    }
    // free the buffer and the channel
    buffer_free(channel->buffer);
    pthread_mutex_unlock(&channel->channel_lock);
    // free the lists
    list_destroy(channel->sel_sends);
    list_destroy(channel->sel_recvs);
    free(channel);
    /* IMPLEMENT THIS */
    return SUCCESS;
}

// Takes an array of channels (channel_list) of type select_t and the array length (channel_count) as inputs
// This API iterates over the provided list and finds the set of possible channels which can be used to invoke the required operation (send or receive) specified in select_t
// If multiple options are available, it selects the first option and performs its corresponding action
// If no channel is available, the call is blocked and waits till it finds a channel which supports its required operation
// Once an operation has been successfully performed, select should set selected_index to the index of the channel that performed the operation and then return SUCCESS
// In the event that a channel is closed or encounters any error, the error should be propagated and returned through select
// Additionally, selected_index is set to the index of the channel that generated the error
enum channel_status channel_select(select_t* channel_list, size_t channel_count, size_t* selected_index)
{
    /* IMPLEMENT THIS */
    // check status of thread A
    // if status is not valid move to B
    
    // create a lock and a condition variable
    pthread_mutex_t local_lock;
    pthread_cond_t local_cond;
    pthread_mutex_init(&local_lock, NULL);
    pthread_cond_init(&local_cond, NULL);
    // pack the two sync constructs into a struct pointer
    sel_sync_t sel_sync;
    sel_sync.sel_lock = &local_lock;
    sel_sync.sel_cond = &local_cond;

    while (true){
      // try to lock all channels first, so that the status doesn't change
      for (size_t i = 0; i < channel_count; i++)
      {
          // should only lock if channel[i] is not duplicate
          bool dup = false;
          for(size_t j = 0; j < i; j++){
              if (channel_list[j].channel == channel_list[i].channel){
                dup = true;
                break;
              }
          }
          if (dup == false)
          	pthread_mutex_lock(&(channel_list[i].channel->channel_lock));  
      }
      // remove this lock from all the channel (sender/receiver lists)
      for (size_t i = 0; i < channel_count; i++){
      	// try to remove the sync struct from the sender list if not already removed
      	if (channel_list[i].dir == SEND){
        	list_node_t* node = list_find(channel_list[i].channel->sel_sends, &sel_sync);
        	if (node != NULL){
           		list_remove(channel_list[i].channel->sel_sends, node);
        	}
        }
        else{
        	list_node_t* node = list_find(channel_list[i].channel->sel_recvs, &sel_sync);
            	if (node != NULL){
              		list_remove(channel_list[i].channel->sel_recvs, node);
            	}
        }
      }
      
      for (size_t i = 0; i < channel_count; i++){
        //select_t* st = (select_t *)channel_list[i];
        channel_t* channel = channel_list[i].channel;
//        void* data = channel_list[i].data;
        enum direction dir = channel_list[i].dir;
        // figure out if dir can be done on channel
        if (dir == SEND){
          // check if the channel is closed first
          if (channel->channel_status == false){
          	// channel is closed, so release all the locks and return with closed error
          	// unlock all channels before making the call
	        for (size_t i = 0; i < channel_count; i++)
	        {
	        // should only unlock if channel[i] is not duplicate
	          bool dup = false;
	          for(size_t j = 0; j < i; j++){
	            if (channel_list[j].channel == channel_list[i].channel){
	              dup = true;
	              break;
	            }
	          }
	          if (dup == false)
	            pthread_mutex_unlock(&(channel_list[i].channel->channel_lock));
	        }
	    // return
	    return CLOSED_ERROR;
            }

            size_t cap = buffer_capacity(channel->buffer);
            // check size is full
            if (buffer_current_size(channel->buffer) < cap){
              // do send core (with channel lock held), then release all channel locks and return
              enum channel_status stat = channel_send_core(channel, channel_list[i].data);
              // cannot send to the buffer, so would have to wait, eventually till this buffer gets emptied out
              // record this to some variable, so that you can recheck that later
              // set the selected_index to point to this channel
              *selected_index = i;
              // unlock all channels before making the call
              for (size_t i = 0; i < channel_count; i++)
              {
              // should only unlock if channel[i] is not duplicate
                bool dup = false;
                for(size_t j = 0; j < i; j++){
                  if (channel_list[j].channel == channel_list[i].channel){
                    dup = true;
                    break;
                  }
                }
                if (dup == false)
                  pthread_mutex_unlock(&(channel_list[i].channel->channel_lock));
              }
              return stat;
            }
          }
          else{
            // check if the channel is closed first
            if (channel->channel_status == false){
            	// channel is closed, so release all the locks and return with closed error
            	// unlock all channels before making the call
	          for (size_t i = 0; i < channel_count; i++)
	          {
	          // should only unlock if channel[i] is not duplicate
	            bool dup = false;
	            for(size_t j = 0; j < i; j++){
	              if (channel_list[j].channel == channel_list[i].channel){
	                dup = true;
	                break;
	              }
	            }
	            if (dup == false)
	              pthread_mutex_unlock(&(channel_list[i].channel->channel_lock));
	          }
	      // return
	      return CLOSED_ERROR;
            }
          
            // it's a receive operation to the channel
            // check size is 0
            if (buffer_current_size(channel->buffer) > 0){
              // cannot receive from the buffer, so would have to wait, eventually till this buffer gets full
              // record this to some variable, so that you can recheck that later
              // set the selected_index to point to this channel
              enum channel_status stat = channel_receive_core(channel, &channel_list[i].data);
              *selected_index = i;
              // unlock all channels before making the receive call
              for (size_t i = 0; i < channel_count; i++)
              {
                // should only lock if channel[i] is not duplicate
                bool dup = false;
                for(size_t j = 0; j < i; j++){
                  if (channel_list[j].channel == channel_list[i].channel){
                    dup = true;
                    break;
                  }
                }
                if (dup == false)
                  pthread_mutex_unlock(&(channel_list[i].channel->channel_lock));
              }
              return stat;
            }
          }
        }
        // guaranteed to wait on all channels
        // wait here on all channels
        // let all the channels' accessors know that this select is sleeping, before going to sleep
        // acquire the local lock and assign the lock/condition variable to all channels
        pthread_mutex_lock(&local_lock);
        // should insert only if non duplicate (same channel and same operation not allowed more than once)
        for (size_t i = 0; i < channel_count; i++){
          // still have the channel lock
          // add to the list
          bool dup = false;
          // check if this is a duplicate operation
          for (size_t j = 0; j < i; j++){
            if ((channel_list[j].channel == channel_list[i].channel) && (channel_list[j].dir == channel_list[i].dir)){
              dup = true;
              break;
            }
          }
          if (dup == false && channel_list[i].dir == SEND){
            list_insert(channel_list[i].channel->sel_sends, &sel_sync);
          }
          else if (dup == false && channel_list[i].dir == RECV){
            list_insert(channel_list[i].channel->sel_recvs, &sel_sync);
          }
          // release the channel lock
          // should only lock if channel[i] is not duplicate
          dup = false;
          for(size_t j = 0; j < i; j++){
            if (channel_list[j].channel == channel_list[i].channel){
              dup = true;
              break;
            }
          }
          if (dup == false)
            pthread_mutex_unlock(&(channel_list[i].channel->channel_lock));
        }
        pthread_cond_wait(&local_cond, &local_lock);
        pthread_mutex_unlock(&local_lock);
      }
    return SUCCESS;
}
