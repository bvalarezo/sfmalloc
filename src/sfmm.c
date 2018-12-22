/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"
#include "helper.h"

/*
 * This is your implementation of sf_malloc. It acquires uninitialized memory that
 * is aligned and padded properly for the underlying system.
 *
 * @param size The number of bytes requested to be allocated.
 *
 * @return If size is 0, then NULL is returned without setting sf_errno.
 * If size is nonzero, then if the allocation is successful a pointer to a valid region of
 * memory of the requested size is returned.  If the allocation is not successful, then
 * NULL is returned and sf_errno is set to ENOMEM.
 */
void *sf_malloc(size_t size) {
	if(size <= 0){//If size is 0, there is no memory to allocate
		return NULL;
	}
	size_t rounded_size = round_up(size); //round UP to actual block size (MIN_BLK_SIZE + size + padding)

	if(sf_mem_start() == sf_mem_end()){ //THE HEAP has not been initialized, we should grow the heap
		initialize_heap();
		if(sf_errno == ENOMEM){ //OUT OF MEMORY
			return NULL;//EXIT
		}
		//prologue and epilogue created
		//free block of size PAGE_SZ made and free list added to seg list of class size PAGE_SZ
	}
	//We have to find a free block so we can then allocate it
	sf_header *block_to_allocate = NULL;
	sf_free_list_node *index_ptr = sf_free_list_head.next;
	sf_free_list_node *best_fit_ptr = NULL;
	//loop through the list of free lists
	while(index_ptr != &sf_free_list_head){//while the next does not point back to the head
		//we have to search for a free list of size "size"
		if(index_ptr->size == rounded_size && (index_ptr->head.links.prev != &index_ptr->head)){ //meaning we found the matched size class AND it has a free block avaliable
			//WE FOUND the seg list of size "size" and it has a free block avaliable
			best_fit_ptr = index_ptr;
		}
		index_ptr = index_ptr->next;
		//otherwise we keep looping until it ends
	}
	//interestingly enough, index_ptr points to the head again
	if(best_fit_ptr == NULL){//this means that we exhausted the whole list couldnt find a match of size "size", Gotta get memory by splitting
		//we have to split
		//get the free list to of best fit size
		best_fit_ptr = best_fit(rounded_size);
		if(best_fit_ptr == NULL){
			//nothing came back so there are no free blocks to split
			//extend heap
			best_fit_ptr = extend_heap(rounded_size);
			if(sf_errno == ENOMEM){ //OUT OF MEMORY
				return NULL;//EXIT
			}
		}
		//SPLITING
			//we now have to split the last free block from best_fit_free_list
			//BUT FIRST CHECK IF IT CAUSES A SPLINTER
			sf_header *block_to_split = best_fit_ptr->head.links.prev; //BLOCK WE WANT TO SPLIT
			if(splinter_check(rounded_size, block_to_split)){
				//Splinter will happen, so don't split just allocate the whole block
				block_to_allocate = block_to_split;
				remove_free_block_from_free_list(block_to_split);
			}
			else{
				//split block
				block_to_allocate = split_free_block(size, rounded_size, block_to_split);
			}
	}
	else{
		//best_fit_ptr points to a free list with a free block avaliable to just grab and allocate
		block_to_allocate = best_fit_ptr->head.links.prev;
		remove_free_block_from_free_list(best_fit_ptr->head.links.prev);
	}
	//we have the pointer to the block we want to allocate
	//allocate it now
	void *result = allocate(block_to_allocate, size);
    return result; //return payload address of the allocated block
}


/*
 * Resizes the memory pointed to by ptr to size bytes.
 *
 * @param ptr Address of the memory region to resize.
 * @param size The minimum size to resize the memory to.
 *
 * @return If successful, the pointer to a valid region of memory is
 * returned, else NULL is returned and sf_errno is set appropriately.
 *
 *   If sf_realloc is called with an invalid pointer sf_errno should be set to EINVAL.
 *   If there is no memory available sf_realloc should set sf_errno to ENOMEM.
 *
 * If sf_realloc is called with a valid pointer and a size of 0 it should free
 * the allocated block and return NULL without setting sf_errno.
 */
void *sf_realloc(void *pp, size_t rsize) {
	if(invalid_pointer_check(pp)){
		//ITS INVALID
		abort();
	}
	if(rsize == 0){ //If reallocing with with a size of 0, free the block
		sf_free(pp);
		return NULL;
	}
	//At this point we can assume that the rsize is a valid size and the pointer is valid
	//Get the header
	sf_header *block_to_realloc = (sf_header*)((char *) pp - sizeof(sf_block_info));
	//cases when reallocating
	//Reallocating to a larger Size
	if((block_to_realloc->info.block_size << 4) < round_up(rsize)){
		//Call sf_malloc to obtain a larger block
		void *larger_block = sf_malloc(rsize);
		if(larger_block == NULL){
			return NULL;
		}
		//Call memcpy to copy the data in pp to larger_block
		memcpy(larger_block, pp, block_to_realloc->info.requested_size);
		//Call sf_free on pp given by the client(coalescing if nescessary)
		sf_free(pp);
		//Return larger_block to the client
		return larger_block;
	}
	//Reallocating to a smaller size
	else if((block_to_realloc->info.block_size << 4) > round_up(rsize)){
		//Split the block
		//Check if a splinter will happen
		if(splinter_check(round_up(rsize), block_to_realloc)){
			//Splinter will happen
			//Don't split, just update the block_to_realloc's requested size
			block_to_realloc->info.requested_size = rsize;
		}
		else{
			//split the block to realloc
			block_to_realloc = split_allocated_block(rsize, round_up(rsize), block_to_realloc);
			sf_header *new_next_free = (sf_header *) ((char *) block_to_realloc + (block_to_realloc->info.block_size << 4));
			//We have to coalese the free block made after the spliter
			new_next_free = coalese(new_next_free);
			//we have to set up the free block
			sf_free_list_node *best_fit_ptr = find_free_list_match(new_next_free->info.block_size << 4);
			if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
				//Make a new Free list
				best_fit_ptr = create_free_list(new_next_free->info.block_size << 4);
			}
			//Okay at this point best_fit_ptr has to point to the free list to add the new free block
			//Link the free block
			sf_add_free_block(best_fit_ptr, new_next_free);
			}
		return allocate(block_to_realloc, rsize);
	}
	else{
		//If reallocating to equal size, we just return the block
		return pp;
	}
}

/*
 * Marks a dynamically allocated region as no longer in use.
 * Adds the newly freed block to the free list.
 *
 * @param ptr Address of memory returned by the function sf_malloc.
 *
 * If ptr is invalid, the function calls abort() to exit the program.
 */
void sf_free(void *pp) {
	//First determine if the pp is valid
	if(invalid_pointer_check(pp)){
		//ITS INVALID
		abort();
	}
	sf_free_list_node *best_fit_ptr = NULL;
	sf_header *block_to_free = (sf_header*)((char *) pp - sizeof(sf_block_info));
	sf_header *next_block =(sf_header *) ((char *) block_to_free + (block_to_free->info.block_size << 4));
	//free it
	block_to_free->info.allocated = 0;
	//We should set the next block's prev allocated to 0
	next_block->info.prev_allocated = 0;
	//add free block back to free list
	best_fit_ptr = find_free_list_match(block_to_free->info.block_size << 4);
	if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
		//Make a new Free list
		best_fit_ptr = create_free_list(block_to_free->info.block_size << 4);
	}
	block_to_free = sf_add_free_block(best_fit_ptr, block_to_free);
	//coalese it
	size_t old_block_to_free_size = block_to_free->info.block_size;
	block_to_free = coalese(block_to_free); //Get the header of the coalesed block
	//coalesing happened if the block size is different than block to free
	if(old_block_to_free_size != block_to_free->info.block_size){ //coalesing happened, fix the whole thing again
		//Readd the free block to the list of free lists
		best_fit_ptr = find_free_list_match(block_to_free->info.block_size << 4);
		if(best_fit_ptr == NULL){ //MEANING WE DID NOT FIND A MATCH
			//Make a new Free list
			best_fit_ptr = create_free_list(block_to_free->info.block_size << 4);
		}
		sf_add_free_block(best_fit_ptr, block_to_free);
	}
    return;
}

