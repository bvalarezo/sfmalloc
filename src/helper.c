#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"
#include "helper.h"


void initialize_heap(){
	//we have to initialize the heap, grow the page size
	if(sf_mem_grow() == NULL){
		sf_errno = ENOMEM;
		return;
	}
	//lets get the locations for the prologue and the epilogue
	sf_prologue *my_prologue = sf_mem_start();
	sf_epilogue *my_epilogue = sf_mem_end() - sizeof(sf_epilogue);
	//both of these structs are filled with garbage, we need to set them all to zeros
	memset(my_prologue, 0, sizeof(sf_prologue)); //size is 40
	memset(my_epilogue, 0, sizeof(sf_epilogue));
	//PROLOGUE
	my_prologue->header.info.allocated = 1;
	my_prologue->header.info.block_size = 0;
	my_prologue->footer.info = my_prologue->header.info;
	//EPILOGUE
	my_epilogue->footer.info = my_prologue->header.info;
	sf_free_list_node *seg_list_ptr = create_free_list(PAGE_SZ - (sizeof(sf_prologue) + sizeof(sf_epilogue)));
	//sf_header
	sf_header *hp = sf_mem_start() + sizeof(sf_prologue);
	sf_add_free_block(seg_list_ptr, hp);
	hp->info.prev_allocated = 1;
	sf_footer *fp = (sf_footer*)((char *)hp + (hp->info.block_size << 4) - sizeof(sf_footer));
	fp->info = hp->info;
}

//FIRST IN LAST OUT
/*
* This function wil add the free block to the input free list
* This funciton will also set up the header info and the footer info and location
*/
sf_header *sf_add_free_block(sf_free_list_node *current_free_list, sf_header *location_of_header){
	//We have to link the sentinal HEAD.link.next(not node don't get confused) with the free block's header
	sf_header *sentinal_head = &current_free_list->head; //SENTINAL HEAD
	sf_header *new_header = location_of_header;
	sf_footer *new_footer = (sf_footer*)((char *)new_header + current_free_list->size - sizeof(sf_footer));
	//linking FIRST IN
	sf_header *old_head_next = sentinal_head->links.next;
	sentinal_head->links.next = new_header;
	new_header->links.next = old_head_next;
	new_header->links.prev = sentinal_head;
	old_head_next->links.prev = new_header;

	new_header->info.allocated = 0;
	new_header->info.block_size = current_free_list->size >> 4;
	//new_header->info.prev_allocated = 1; //does this make sense?
	new_footer->info = new_header->info; //Footer matches header
	return new_header;
}


/* In this function we will create a free list of with block size "size"
 *This will handle adding it to the list of lists in the correct position
 *Is to be used if seglist does not have a free list of the size
 *
 *@return	the pointer of the free_list_node we just created
 */
sf_free_list_node *create_free_list(size_t size){ //fix this
	//we have to make a while loop to place the free list in the correct position
	sf_free_list_node *current_free_list = sf_free_list_head.next; //start after head
	sf_free_list_node *new_free_list;
	while(current_free_list != &sf_free_list_head){//while the next does not point back to the head
		//we have to find the free list with the size < "size"
		if((current_free_list->head.info.block_size << 4)> size){
			//found where to place the new free list
			break;
		}
		current_free_list = current_free_list->next;

	}
	//current_free_list is the free list before the new one
	new_free_list = sf_add_free_list(size, current_free_list); //adding the free list with block size "size"
	//new_free_list is now the new free list, //next and prev have been set

	new_free_list->head.info.requested_size = 0;// = current_free_list->next;
	new_free_list->head.info.block_size = size >> 4; //proper block size
	new_free_list->head.info.two_zeroes = 0;
	new_free_list->head.info.prev_allocated = 0;
	new_free_list->head.info.allocated = 0;

	return new_free_list;
}
/*In this function, we will loop through the entire free list and
 * find a free list that has a free block is best to allocate from based on the size
 *
 *@return pinter to free_list_node that satisfies best fit
 */
sf_free_list_node *best_fit(size_t rounded_size){
		sf_free_list_node *index_ptr = sf_free_list_head.next;
		sf_free_list_node *best_fit_free_list = NULL;
		size_t min = index_ptr->prev->prev->size; //THE MAX
		size_t comparable = min;
		while(index_ptr != &sf_free_list_head){ //loop through the whole thing, check to see we haven't reached the head
			if(index_ptr->size < rounded_size){
				//keep going
				index_ptr = index_ptr->next;
			}
			else{ //current_free_list->size >= rounded_size
				//since its ordered we find our best fit right off the bat, but we must do full search sooooo
				comparable = index_ptr->size;
				if(comparable <= min && (index_ptr->head.links.prev != &index_ptr->head)){
					min = comparable; //set the new min
					best_fit_free_list = index_ptr;
				}
				index_ptr = index_ptr->next;
			}
		}
		return best_fit_free_list;
	}

/*This just rounds the size to a multiple of 16 + Min Block Size
* Should be okay now
*/
int round_up(size_t size){
	int n = size + sizeof(sf_block_info);
	int multiple = 16;
    if (n % multiple != 0) {
        n = ((n + multiple) / multiple) * multiple;
    }
    if(n < MIN_BLK_SIZE){
    	n = MIN_BLK_SIZE;
    }
    return n;
}


/*This function checks to see if a splinter will happen or not
 *at the free block of the free list
 *
 *@return 1 if splinter WILL happen
 *@return 0 if no splinter will happen
 */
int splinter_check(size_t rounded_size, sf_header *block_to_split){
	size_t free_block_size = block_to_split->info.block_size << 4;
	//we have to check if the free_block_size - rounded_size is not less that 32
	if(free_block_size - rounded_size < MIN_BLK_SIZE){
		//SPLINTER WILL HAPPEN
		return 1;
	}
	else
	{
		return 0;
		//NO Splinter
	}

}

/*This function will split a free block from the free list and allocate
 *the rounded size amount of bytes.
 *
 *returns the address of the header block we want to allocate (The split block from the OG)
 */
sf_header *split_free_block(size_t requested_size, size_t rounded_size, sf_header *block_to_split){
	//first in LAST OUT
	//Get the last free block of the free_list
	sf_header *new_header = (sf_header*)((char *) block_to_split + rounded_size); //location of the new header
	sf_free_list_node *best_fit_ptr = NULL; //free list to place the split block
	//(sf_footer*)((char *)header + block_size - 8);
	//Unlink the next and prev of the block from the free list
	remove_free_block_from_free_list(block_to_split);
	//we need to reduce the block_to_split by round_size amount of bytes
	sf_block_info new_info = block_to_split->info; //A copy
	new_info.block_size = ((block_to_split->info.block_size << 4) - rounded_size) >> 4;
	//new_info.prev_allocated = 1; //Because we are splitting DO THIS IN ALLOCATE
	//new info for new header of split block
	new_header->info = new_info;
	//We have to find a Free List to add this new free block
	best_fit_ptr = find_free_list_match(new_header->info.block_size << 4);
	if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
		//Make a new Free list
		best_fit_ptr = create_free_list(new_header->info.block_size << 4);
	}
	//Okay at this point best_fit_ptr has to point to the free list to add the new free block
	//Link the free block
	sf_add_free_block(best_fit_ptr, new_header);
	//The new free block has been dealt with
	//Fix the header of the other block we want to allocate
	block_to_split->info.block_size = rounded_size >> 4;
	block_to_split->info.requested_size = requested_size;
	return block_to_split;
}


/*This function will split an allocated block from input and write the new requested size to the allocated block header
 *
 *returns the address of the header block we just split (The split block from the OG)
 */
sf_header *split_allocated_block(size_t requested_size, size_t rounded_size, sf_header *block_to_split){
	sf_header *new_header = (sf_header*)((char *) block_to_split + rounded_size); //location of the new header
	//we need to reduce the block_to_split by round_size amount of bytes
	sf_block_info new_info = block_to_split->info; //A copy
	new_info.block_size = ((block_to_split->info.block_size << 4) - rounded_size) >> 4;
	new_info.prev_allocated = 1;
	//new info for new header of split block
	new_header->info = new_info;
	//We have to find a Free List to add this new free block
	sf_free_list_node *best_fit_ptr = NULL;
	best_fit_ptr = find_free_list_match(new_header->info.block_size << 4);
	if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
		//Make a new Free list
		best_fit_ptr = create_free_list(new_header->info.block_size << 4);
	}
	//Okay at this point best_fit_ptr has to point to the free list to add the new free block
	//Link the free block
	sf_add_free_block(best_fit_ptr, new_header);
	//The new free block has been dealt with
	//Fix the header of the other block we want
	block_to_split->info.block_size = rounded_size >> 4;
	block_to_split->info.requested_size = requested_size;
	return block_to_split;
}

/* Removes a free block from a free list and fixes the linked list .next & .prev
*/
//LAST OUT
void remove_free_block_from_free_list(sf_header *x){
	x->links.prev->links.next = x->links.next;
	x->links.next->links.prev = x->links.prev;
	x->links.next = NULL;
	x->links.prev = NULL;
}


/* This functions finds the free list of the inputted size class
*
*@returns	the pointer to the free list of size class.
*			If there is no match it will return NULL
*/
sf_free_list_node *find_free_list_match(size_t size_class){
	//We have to find a free block to allocate the size
	sf_free_list_node *index_ptr = sf_free_list_head.next;
	sf_free_list_node *best_fit_ptr = NULL;
	//loop through the list of free lists
	while(index_ptr != &sf_free_list_head){//while the next does not point back to the head
		//we have to search for a free list of size "size"
		if(index_ptr->size == size_class){ //meaning we found the matched size class AND it has a free block avaliable
			//WE FOUND the seg list of size "size" and it has a free block avaliable
			best_fit_ptr = index_ptr;
		}
		index_ptr = index_ptr->next;
		//otherwise we keep looping until it ends
	}
	return best_fit_ptr;
}

void *allocate(sf_header *block_to_allocate, size_t requested_size){
	block_to_allocate->info.allocated = 1;
	block_to_allocate->info.requested_size = requested_size;
	//We have to tell the next block that its prev is allocated
	sf_block_info *info_of_next_block = (sf_block_info*)((char *)block_to_allocate + (block_to_allocate->info.block_size << 4));
	info_of_next_block->prev_allocated = 1;

	return &block_to_allocate->payload;
}

/* This function's main purpose is to extend the heap
 * Put the new free block from extending the heap in a free list
 * and return the free list
 */
sf_free_list_node *extend_heap(size_t rounded_size){
	//so we have to extend the heap
	//Lets get the epilogue
	int i;
	sf_epilogue *my_epilogue = sf_mem_end() - sizeof(sf_epilogue);
	sf_header *new_header = NULL;
	sf_header *next_header = NULL;
	sf_free_list_node *best_fit_ptr = NULL;
	sf_footer *footer_info = NULL;
	if(my_epilogue->footer.info.prev_allocated == 1){
		//There is no free block at the end, tight fit
		//Extend the heap and new free block is size PAGE_SZ
		//amount that is free = 0
		//increment by PAGE_SZ - size of epilogue
		new_header = sf_mem_end();
		for(i = 0; rounded_size > i; i += PAGE_SZ){
		 	if(sf_mem_grow() == NULL){
				sf_errno = ENOMEM;
				return NULL;
			}
		}
		//location for new header is where the epilogue was at
		//new_header = (sf_header *)((char *) sf_mem_end() - i); //address of header for new free
		//have to set up the epilogue again
		my_epilogue = sf_mem_end() - sizeof(sf_epilogue);
		my_epilogue->footer.info.allocated = 1;
		new_header->info.block_size = (i - sizeof(sf_epilogue)) >> 4;
	}
	else{
		//my_epilogue->footer.info.prev_allocated == 0
		//There is a free block before the epilogue
		footer_info = (sf_footer *) ((char *) my_epilogue - sizeof(sf_footer)); //location of footer of free block prev
		new_header = (sf_header *) ((char *) my_epilogue - (footer_info->info.block_size << 4)); //start location of the prev free block
		next_header = sf_mem_end() - sizeof(sf_epilogue);
		//There is some amount of free space to account for, make that i
		for(i = (footer_info->info.block_size << 4) ; rounded_size > i; i+= PAGE_SZ){
		 	if(sf_mem_grow() == NULL){
				sf_errno = ENOMEM;
				return NULL;
			}
		}
		//need to setup the free block made after extending the heap
		//need to setup the footer
		//next_header = (sf_header *)((char *) sf_mem_end() - (i - (footer_info->info.block_size << 4))); //location of the next free block
		next_header->info.block_size = (i + sizeof(sf_epilogue) - (footer_info->info.block_size << 4)) >> 4;
		next_header->info.allocated = 0;
		//overwrite the epilogue
		remove_free_block_from_free_list(new_header); //remove from the free list
		new_header->info.block_size = ((new_header->info.block_size << 4) + sizeof(sf_epilogue)) >> 4;
		footer_info->info = new_header->info; //Footer updated
		//put block in a new free list
		best_fit_ptr = find_free_list_match(new_header->info.block_size << 4);
		if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
		//Make a new Free list
			best_fit_ptr = create_free_list(new_header->info.block_size << 4);
		}
		sf_add_free_block(best_fit_ptr, new_header);
		//and again for next
		best_fit_ptr = find_free_list_match(next_header->info.block_size << 4);
		if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
		//Make a new Free list
			best_fit_ptr = create_free_list(next_header->info.block_size << 4);
		}
		sf_add_free_block(best_fit_ptr, next_header);
		next_header->info.prev_allocated = 0;
		//fix the epilogue
		my_epilogue = sf_mem_end() - sizeof(sf_epilogue);
		my_epilogue->footer.info.allocated = 1;;
		//COALESE it with the new free block made by extending the heap
		new_header = coalese(new_header);
	}

	//we have to set up the free block
	best_fit_ptr = find_free_list_match(new_header->info.block_size << 4);
	if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
		//Make a new Free list
		best_fit_ptr = create_free_list(new_header->info.block_size << 4);
	}
	//Okay at this point best_fit_ptr has to point to the free list to add the new free block
	//Link the free block
	sf_add_free_block(best_fit_ptr, new_header);

	return best_fit_ptr;
}

/* This coaleses a free block (update the sf_header)
 * @returns		the new header of the free block
 */
sf_header *coalese(sf_header *free_block){
	//A couple of edge cases to account for when coalesing
	//(sf_footer*)((char *)new_header + current_free_list->size - sizeof(sf_footer));
	sf_block_info *next_free_block_info = (sf_block_info *) ((char *)free_block + (free_block->info.block_size << 4));
	sf_header *new_header = NULL;
	sf_header *prev_free_block= NULL;
	sf_header *next_free_block = NULL;
	//When the direct prev and direct next free block allocated
	if(free_block->info.prev_allocated == 1 && next_free_block_info->allocated == 1){ //both prev and next is allocated
		//Don't coalese
		return free_block;
	}
	//When the direct prev is free but the next is allocated
	else if(free_block->info.prev_allocated == 0 && next_free_block_info->allocated == 1){
		//Coalese the prev and current
		//get the footer of prev for info
		sf_footer *temp = (sf_footer *) ((char *) free_block - sizeof(sf_footer)); //footer of prev
		prev_free_block = (sf_header *) ((char *) free_block - (temp->info.block_size << 4)); //location of prev
		remove_free_block_from_free_list(free_block);
		remove_free_block_from_free_list(prev_free_block);
		//need to get prev for info
		new_header = prev_free_block; //new header location is where the header of the prev was
		new_header->info.block_size = ((free_block->info.block_size << 4) + (prev_free_block->info.block_size << 4)) >> 4; //fix the block size info
		return new_header;
	}
	//When the direct prev is allocated but the next is free
	else if(free_block->info.prev_allocated == 1 && next_free_block_info->allocated == 0){
		//Coalese the current and next
		//next_free_block has the size we need
		next_free_block = (sf_header *) next_free_block_info; //location of next
		remove_free_block_from_free_list(free_block);
		remove_free_block_from_free_list(next_free_block);
		new_header = free_block;//get the header of the current block
		new_header->info.block_size = ((free_block->info.block_size << 4) + (next_free_block_info->block_size << 4)) >> 4; //fix the block size
		//free block's footer needs to be eliminated
		return new_header;
	}
	else{
		//coalese the prev, current, and next
		sf_footer *temp = (sf_footer *) ((char *) free_block - sizeof(sf_footer));
		prev_free_block = (sf_header *) ((char *) free_block - (temp->info.block_size << 4)); //location of prev
		next_free_block = (sf_header *) next_free_block_info; //location of next
		remove_free_block_from_free_list(prev_free_block);
		remove_free_block_from_free_list(free_block);
		remove_free_block_from_free_list(next_free_block);
		//Unlinking done
		new_header = prev_free_block; //new header location is where the header of the prev was
		new_header->info.block_size = ((free_block->info.block_size << 4) + (prev_free_block->info.block_size << 4) + (next_free_block_info->block_size << 4)) >> 4;
		//Delete old headers
		return new_header;
	}

}

/* Validates whether the pointer is a valid sf_header to free
 * @returns		1 if invalid
 * @returns		0 if valid
 */
int invalid_pointer_check(void *pp){
	sf_header *header_of_pp = (sf_header *) ((char *)pp - sizeof(sf_block_info));
	sf_prologue *my_prologue_end = sf_mem_start() + sizeof(sf_prologue); //end of prologue
	sf_epilogue *my_epilogue = sf_mem_end() - sizeof(sf_epilogue); //start of epilogue
	if(pp == NULL){ //The pointer is NULL
		return 1;
	}
	if((void *)header_of_pp < (void *)my_prologue_end ||(void *)header_of_pp > (void *)my_epilogue){//Header of block is before the prologue or after the epilogue. Out of Bounds
		return 1;
	}
	if(header_of_pp->info.allocated == 0){//The allocated bit is 0
		return 1;
	}
	if((header_of_pp->info.block_size << 4) % 16 != 0 || ((header_of_pp->info.block_size << 4) < MIN_BLK_SIZE)){//block_size field is not a multiple of 16 or block_size is less than MIN_BLK_SIZE
		return 1;
	}
	if(round_up(header_of_pp->info.requested_size) > header_of_pp->info.block_size << 4){//requested size + padding != blocksize
		return 1;
	}
	if(header_of_pp->info.prev_allocated == 0){//previous block is free
		//check the prev block header and footer to see if it is INDEED free
		sf_footer *prev_footer_of_pp = (sf_footer *)((char *) header_of_pp - sizeof(sf_footer));
		sf_header *prev_header_of_pp = (sf_header *)((char *) header_of_pp - (prev_footer_of_pp->info.block_size << 4));
		if(prev_header_of_pp->info.allocated != 0 || prev_footer_of_pp->info.allocated != 0){
			return 1;
		}
	}
	return 0;
}
