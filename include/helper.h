#define MIN_BLK_SIZE 32

void initialize_heap();
sf_header *sf_add_free_block(sf_free_list_node *current_free_list, sf_header *location_of_header);//, sf_footer *location_of_footer);
sf_free_list_node *create_free_list(size_t size);
sf_free_list_node *best_fit(size_t size);
int round_up(size_t size);
int splinter_check(size_t rounded_size, sf_header *block_to_split);
sf_header *split_free_block(size_t requested_size, size_t rounded_size, sf_header *block_to_split);
sf_header *split_allocated_block(size_t requested_size, size_t rounded_size, sf_header *block_to_split);
void remove_free_block_from_free_list(sf_header *x);
sf_free_list_node *find_free_list_match(size_t size_class);
void *allocate(sf_header *block_to_allocate, size_t requested_size);
sf_free_list_node *extend_heap(size_t rounded_size);
sf_header *coalese(sf_header *free_block);
int invalid_pointer_check(void *pp);
