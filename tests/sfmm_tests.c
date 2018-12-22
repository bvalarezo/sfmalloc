#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"

#define MIN_BLOCK_SIZE (32)

sf_free_list_node *find_free_list_for_size(size_t size) {
    sf_free_list_node *fnp = sf_free_list_head.next;
    while(fnp != &sf_free_list_head && fnp->size < size)
	fnp = fnp->next;
    if(fnp == &sf_free_list_head || fnp->size != size)
	return NULL;
    return fnp;
}

int free_list_count(sf_header *ahp) {
    int count = 0;
    sf_header *hp = ahp->links.next;
    while(hp != ahp) {
	count++;
	hp = hp->links.next;
    }
    return count;
}

void assert_free_list_count(size_t size, int count) {
    sf_free_list_node *fnp = sf_free_list_head.next;
    while(fnp != &sf_free_list_head) {
	if(fnp->size == size)
	    break;
	fnp = fnp->next;
    }
    cr_assert(fnp != &sf_free_list_head && fnp->size == size,
	      "No free list of size %lu was found", size);
    int flc = free_list_count(&fnp->head);
    cr_assert_eq(flc, count,
		 "Wrong number of blocks in free list for size %lu (exp=%d, found=%d)",
		 size, flc);
}

void assert_free_block_count(int count) {
    int n = 0;
    sf_free_list_node *fnp = sf_free_list_head.next;
    while(fnp != &sf_free_list_head) {
	n += free_list_count(&fnp->head);
	fnp = fnp->next;
    }
    cr_assert_eq(n, count, "Wrong number of free blocks (exp=%d, found=%d)", count, n);
}

Test(sf_memsuite_student, malloc_an_Integer_check_freelist, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	int *x = sf_malloc(sizeof(int));

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(1);
	assert_free_list_count(PAGE_SZ - sizeof(sf_prologue) - sizeof(sf_epilogue) - MIN_BLOCK_SIZE, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sf_memsuite_student, malloc_three_pages, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	void *x = sf_malloc(3 * PAGE_SZ - sizeof(sf_prologue) - sizeof(sf_epilogue) - MIN_BLOCK_SIZE);

	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sf_memsuite_student, malloc_over_four_pages, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	void *x = sf_malloc(PAGE_SZ << 2);

	cr_assert_null(x, "x is not NULL!");
	assert_free_block_count(1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sf_memsuite_student, free_double_free, .init = sf_mem_init, .fini = sf_mem_fini, .signal = SIGABRT) {
	sf_errno = 0;
	void *x = sf_malloc(sizeof(int));
	sf_free(x);
	sf_free(x);
}

Test(sf_memsuite_student, free_no_coalesce, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *x = */ sf_malloc(sizeof(long));
	void *y = sf_malloc(sizeof(double) * 10);
	/* void *z = */ sf_malloc(sizeof(char));

	sf_free(y);

	assert_free_block_count(2);
	assert_free_list_count(96, 1);
	assert_free_list_count(3888, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, free_coalesce, .init = sf_mem_init, .fini = sf_mem_fini) {
	sf_errno = 0;
	/* void *w = */ sf_malloc(sizeof(long));
	void *x = sf_malloc(sizeof(double) * 11);
	void *y = sf_malloc(sizeof(char));
	/* void *z = */ sf_malloc(sizeof(int));

	sf_free(y);
	sf_free(x);

	assert_free_block_count(2);
	assert_free_list_count(128, 1);
	assert_free_list_count(3856, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sf_memsuite_student, freelist, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *u = sf_malloc(1); //32
	/* void *v = */ sf_malloc(40); //48
	void *w = sf_malloc(152); //160
	/* void *x = */ sf_malloc(536); //544
	void *y = sf_malloc(1); // 32
	/* void *z = */ sf_malloc(2072); //2080

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_free_block_count(4);
	assert_free_list_count(32, 2);
	assert_free_list_count(160, 1);
	assert_free_list_count(1152, 1);

	// First block in list should be the most recently freed block.
	sf_free_list_node *fnp = find_free_list_for_size(32);
	cr_assert_eq(fnp->head.links.next, (sf_header *)((char *)y - sizeof(sf_footer)),
		     "Wrong first block in free list (32): (found=%p, exp=%p)",
                     fnp->head.links.next, (sf_header *)((char *)y - sizeof(sf_footer)));
}

Test(sf_memsuite_student, realloc_larger_block, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(int));
	/* void *y = */ sf_malloc(10);
	x = sf_realloc(x, sizeof(int) * 10);

	cr_assert_not_null(x, "x is NULL!");
	sf_header *hp = (sf_header *)((char *)x - sizeof(sf_footer));
	cr_assert(hp->info.allocated == 1, "Allocated bit is not set!");
	cr_assert(hp->info.block_size << 4 == 48, "Realloc'ed block size not what was expected!");

	assert_free_block_count(2);
	assert_free_list_count(32, 1);
	assert_free_list_count(3936, 1);
}

Test(sf_memsuite_student, realloc_smaller_block_splinter, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(int) * 8);
	void *y = sf_realloc(x, sizeof(char));

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_header *hp = (sf_header *)((char*)y - sizeof(sf_footer));
	cr_assert(hp->info.allocated == 1, "Allocated bit is not set!");
	cr_assert(hp->info.block_size << 4 == 48, "Block size not what was expected!");
	cr_assert(hp->info.requested_size == 1, "Requested size not what was expected!");

	// There should be only one free block of size 4000.
	assert_free_block_count(1);
	assert_free_list_count(4000, 1);
}

Test(sf_memsuite_student, realloc_smaller_block_free_block, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(double) * 8);
	void *y = sf_realloc(x, sizeof(int));

	cr_assert_not_null(y, "y is NULL!");

	sf_header *hp = (sf_header *)((char*)y - sizeof(sf_footer));
	cr_assert(hp->info.allocated == 1, "Allocated bit is not set!");
	cr_assert(hp->info.block_size << 4 == 32, "Realloc'ed block size not what was expected!");
	cr_assert(hp->info.requested_size == 4, "Requested size not what was expected!");

	// After realloc'ing x, we can return a block of size 48 to the freelist.
	// This block will coalesce with the block of size 3968.
	assert_free_block_count(1);
	assert_free_list_count(4016, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

Test(sf_memsuite_student, validate_free_block_info, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(246); //256
	void *y = sf_malloc(sizeof(int)); //32
	sf_free(x);
	cr_assert_not_null(y, "y is NULL!");

	sf_header *hp = (sf_header *)((char*)x - sizeof(sf_block_info));
	sf_footer *fp = (sf_footer *)((char*)hp + (hp->info.block_size << 4) - sizeof(sf_footer));
	cr_assert(hp->info.allocated == 0, "Allocated bit is set!");
	cr_assert(hp->info.block_size << 4 == 256, "block size not what was expected!");
	cr_assert(hp->info.prev_allocated == 1, "Block directly after prologue, prev allocated bit not set!");
	cr_assert(hp->info.allocated == fp->info.allocated, "Header & Footer mismatch(allocated bit)");
	cr_assert(hp->info.block_size == fp->info.block_size, "Header & Footer mismatch(block size)");
	cr_assert(hp->info.prev_allocated == fp->info.prev_allocated, "Header & Footer mismatch(prev_allocated bit)");

	assert_free_block_count(2);
	assert_free_list_count(256, 1);
}

Test(sf_memsuite_student, malloc_then_free_coalsing, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *i = sf_malloc(sizeof(double)*1); //32
	void *j = sf_malloc(sizeof(double)*2); //32
	void *k = sf_malloc(sizeof(double)*3); //32
	void *l = sf_malloc(sizeof(double)*4); //48
	void *m = sf_malloc(sizeof(double)*5); //48
	void *n = sf_malloc(sizeof(double)*6); //64
	void *o = sf_malloc(sizeof(double)*7); //64
	void *p = sf_malloc(sizeof(double)*8); //80
	cr_assert_not_null(i, "i is NULL!");
	cr_assert_not_null(j, "j is NULL!");
	cr_assert_not_null(k, "k is NULL!");
	cr_assert_not_null(l, "l is NULL!");
	cr_assert_not_null(m, "m is NULL!");
	cr_assert_not_null(n, "n is NULL!");
	cr_assert_not_null(o, "o is NULL!");
	cr_assert_not_null(p, "p is NULL!");
	sf_free(i);
	sf_free(j);
	sf_free(k);
	sf_free(l);
	sf_free(m);
	sf_free(n);
	sf_free(o);
	sf_free(p);

	assert_free_block_count(1);
	assert_free_list_count(4048, 1);
}

Test(sf_memsuite_student, relloc_to_zero, .init = sf_mem_init, .fini = sf_mem_fini, .signal = SIGABRT) {
	void *x = sf_malloc(246); //256
	void *y = sf_realloc(x, 0); //free x
	sf_free(y); //double freeing should cause it to abort
}

Test(sf_memsuite_student, malloc_until_full, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_mem_end();//JUST TO INITIALIZE ITS VALUE
	do{
		x = sf_malloc(PAGE_SZ);//run malloc until we have run out of heap
	}
	while(x != NULL);
	cr_assert_null(x, "x is not NULL!");
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sf_memsuite_student, realloc_grow_then_shrink, .init = sf_mem_init, .fini = sf_mem_fini) {
	void *x = sf_malloc(sizeof(double)); //8 , 32
	x = sf_realloc(x, sizeof(double) * 10);// 80, 96

	cr_assert_not_null(x, "x is NULL!");
	sf_header *hp = (sf_header *)((char *)x - sizeof(sf_block_info));
	cr_assert(hp->info.allocated == 1, "Allocated bit is not set!");
	cr_assert(hp->info.block_size << 4 == 96, "Realloc'ed block size not what was expected!");

	x = sf_realloc(x, sizeof(double) * 5);

	cr_assert_not_null(x, "x is NULL!");
	hp = (sf_header *)((char *)x - sizeof(sf_block_info));
	cr_assert(hp->info.allocated == 1, "Allocated bit is not set!");
	cr_assert(hp->info.block_size << 4 == 48, "Realloc'ed block size not what was expected!");
}

