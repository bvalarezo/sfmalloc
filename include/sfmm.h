/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them
 * in another header file.
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */
#ifndef SFMM_H
#define SFMM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/*

                                      Format of an allocated memory block
    +-----------------------------------------------------------------------------------------+
    |                                       64-bits wide                                      |
    +-----------------------------------------------------------------------------------------+

    +--------------------------------------------+------------------+-------+-------+---------+ <- header
    |              requested_size                |   block_size     |       | prev  |  alloc  |
    |                                            |                  |  00   | alloc |    1    |
    |                 32 bits                    |     28 bits      |       | 1 bit |   bit   |
    +--------------------------------------------+------------------+-------+-------+---------+ <- (double-row
    |                                                                                         |     aligned)
    |                                   Payload and Padding                                   |
    |                                     (N Memory Rows)                                     |
    |                                                                                         |
    |                                                                                         |
    +--------------------------------------------+------------------+-------+-------+---------+

*/

/*
                                      Format of a free memory block
    +--------------------------------------------+------------------+-------+-------+---------+ <- header
    |                 unused                     |   block_size     |       | prev  |  alloc  |
    |                                            |                  |  00   | alloc |    1    |
    |                 32 bits                    |     28 bits      |       | 1 bit |   bit   |
    +--------------------------------------------+------------------+-------+-------+---------+ <- (double-row
    |                                                                                         |     aligned)
    |                                Pointer to next free block                               |
    |                                                                                         |
    +--------------------------------------------+------------------+-------+-------+---------+
    |                                                                                         |
    |                               Pointer to previous free block                            |
    |                                                                                         |
    +--------------------------------------------+------------------+-------+-------+---------+
    |                                                                                         |
    |                                         Unused                                          |
    |                                     (N Memory Rows)                                     |
    |                                                                                         |
    |                                                                                         |
    +--------------------------------------------+------------------+-------+-------+---------+ <- footer
    |                 unused                     |   block_size     |       | prev  |  alloc  |
    |                                            |                  |  00   | alloc |    1    |
    |                 32 bits                    |     28 bits      |       | 1 bit |   bit   |
    +--------------------------------------------+------------------+-------+-------+---------+

*/

/* Struct for the common part of block header and block footer. */
typedef struct {
    unsigned      allocated :  1;
    unsigned prev_allocated :  1;
    unsigned     two_zeroes :  2;
    unsigned     block_size : 28;  // Note: value is size>>4
    unsigned requested_size : 32;
} __attribute__((packed)) sf_block_info;

/* Struct for a block header */
typedef struct sf_header {
    sf_block_info info;
    /*
     * A free block has pointers to the next and previous free block of the same size.
     * An allocated block has a payload, and since the next and previous pointers are
     * only needed for a free block, the payload can use this space when the block is
     * allocated.  We use a union to reflect this idea.
     */
    union {
        uint64_t payload;  /* First word of payload (aligned). */
        struct {
            struct sf_header *next;
            struct sf_header *prev;
        } links;           /* Pointers to next and previous free blocks. */
    };
} sf_header;

/* Struct for a block footer (footers are only present in free blocks) */
typedef struct {
    sf_block_info info;
} sf_footer;

/*
 * The heap is designed to keep the payload area of each block aligned to a double-word (16-byte)
 * boundary.  The information word of block header precedes the payload area, and is only
 * single-word aligned.  The heap starts with a "prologue" that consists of padding (to achieve
 * the desired alignment) and an allocated block with just a header and a footer and a minimum-size
 * payload area (which is unused).  The heap ends with an "epilogue" that consists only of an
 * allocated footer.  The prologue and epilogue are never freed, and they serve as sentinels that
 * eliminate edge cases in coalescing that would otherwise have to be treated.
 */

/*
                                         Format of the heap
    +-----------------------------------------------------------------------------------------+
    |                                       64-bits wide                                      |
    +-----------------------------------------------------------------------------------------+

                                                                                                   heap start
    +-----------------------------------------------------------------------------------------+ <- (aligned)
    |                                                                                         |
    |                                            0                                            | padding
    |                                         64 bits                                         |
    +-----------------------------------------------------------------------------------------+
    |                                            |                  |       |       |         |
    |                    0                       |        0         |  00   |   0   |    1    | prologue
    |                 32 bits                    |     28 bits      |       |       |         | header
    +--------------------------------------------+------------------+-------+-------+---------+ <- (aligned)
    |                                                                                         |
    |                                            0                                            | padding
    |                                         64 bits                                         |
    +--------------------------------------------+------------------+-------+-------+---------+
    |                                                                                         |
    |                                            0                                            | padding
    |                                         64 bits                                         |
    +--------------------------------------------+------------------+-------+-------+---------+ <- (aligned)
    |                                            |                  |       |       |         |
    |                    0                       |        0         |  00   |   0   |    1    | prologue
    |                 32 bits                    |     28 bits      |       |       |         | footer
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    |                                 Allocated and free blocks                               |
    |                                                                                         |
    |                                                                                         |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
    |                                            |                  |       | prev  |         |
    |                    0                       |        0         |  00   | alloc |    1    | epilogue
    |                 32 bits                    |     28 bits      |       | 1 bit |         |
    +-----------------------------------------------------------------------------------------+ <- heap end
                                                                                                   (aligned)
*/

/* Struct for heap prologue */
typedef struct {
    uint64_t padding;
    sf_header header;
    sf_footer footer;
} sf_prologue;

/* Struct for heap epilogue */
typedef struct {
    sf_footer footer;
} sf_epilogue;

/*
 * Free blocks are maintained in a free list that consists of blocks all of the same size.
 * There is one free list for each block size that has ever been created.  The several
 * free lists are themselves maintained in a "list of free lists", in which each node
 * contains the size of the blocks in that list and a header node.
 *
 * The individual freelists, as well as the "list of free lists", are maintained as
 * circular, doubly linked lists that have a "dummy" header node between the beginning
 * and the end of the list.  In an empty list, the next and free pointers of the header
 * node point back to itself.  In a list with something in it, the next pointer of the
 * header points to the first node in the list and the free pointer of the header points
 * to the last thing in the list.  The header itself is never removed from the list and
 * it contains no data.  The reason for doing things this way is to avoid edge cases
 * in insertion and deletion of nodes from the list.
 */

typedef struct sf_free_list_node {
    size_t size;
    sf_header head;             /* Head of list of free nodes of same size. */
    struct sf_free_list_node *next;
    struct sf_free_list_node *prev;
} sf_free_list_node;

/*
 * Header for the "list of free lists".  This list is maintained in increasing order
 * of size.
 */
sf_free_list_node sf_free_list_head;

/* sf_errno: will be set on error */
int sf_errno;

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
void *sf_malloc(size_t size);

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
void *sf_realloc(void *ptr, size_t size);

/*
 * Marks a dynamically allocated region as no longer in use.
 * Adds the newly freed block to the free list.
 *
 * @param ptr Address of memory returned by the function sf_malloc.
 *
 * If ptr is invalid, the function calls abort() to exit the program.
 */
void sf_free(void *ptr);

/* sfutil.c: Helper functions already created for this assignment. */

/*
 * Any program using the sfmm library must call this function ONCE
 * before issuing any allocation requests. This function DOES NOT
 * allocate any space to your allocator.
 */
void sf_mem_init();

/*
 * Any program using the sfmm library must call this function ONCE
 * after all allocation requests are complete. If implemented cleanly,
 * your program should have no memory leaks in valgrind after this function
 * is called.
 */
void sf_mem_fini();

/*
 * This function increases the size of your heap by adding one page of
 * memory to the end.
 *
 * @return On success, this function returns a pointer to the start of the
 * additional page, which is the same as the value that would have been returned
 * by get_heap_end() before the size increase.  On error, NULL is returned
 * and sf_errno is set to ENOMEM.
 */
void *sf_mem_grow();

/* The size of a page of memory returned by sf_mem_grow(). */
#define PAGE_SZ 4096

/*
 * @return The starting address of the heap for your allocator.
 */
void *sf_mem_start();

/*
 * @return The ending address of the heap for your allocator.
 */
void *sf_mem_end();

/*
 * Create a new free list node for blocks of a specified size and
 * insert the new node into the "list of free lists", just before a
 * specified node.  Note that it is the caller's responsibility to
 * ensure that that the list is maintained in increasing order of size.
 *
 * @param size  Size of the blocks in the new free list.
 * @param next  Node before which to insert the new node.
 * @return A pointer to the newly inserted sf_free_list_node, or NULL
 * if it was not possible to allocate the new free node.
 */
sf_free_list_node *sf_add_free_list(size_t size, sf_free_list_node *next);

/*
 * Display the contents of the heap in a human-readable form.
 */
void sf_show_block_info(sf_block_info *ip);
void sf_show_blocks();
void sf_show_free_lists();
void sf_show_heap();

#endif