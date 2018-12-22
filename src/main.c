#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {
    sf_mem_init();

	void *x = sf_mem_end();//JUST TO INITIALIZE ITS VALUE
	do{
		x = sf_malloc(PAGE_SZ);//run malloc until we have run out of heap
	}
	while(x != NULL );

    // double* ptr = sf_malloc(4048);

    // *ptr = 320320320e-320;

    // printf("%e\n", *ptr);

    // sf_free(ptr);

    sf_mem_fini();

    return EXIT_SUCCESS;
}
