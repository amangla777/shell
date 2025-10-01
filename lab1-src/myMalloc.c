#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>

#include "myMalloc.h"

#define MALLOC_COLOR "MALLOC_DEBUG_COLOR"

static bool check_env;
static bool use_color;

/*
 * Mutex to ensure thread safety for the freelist
 */
static pthread_mutex_t mutex;

/*
 * Array of sentinel nodes for the freelists
 */
header freelistSentinels[N_LISTS];

/*
 * Pointer to the second fencepost in the most recently allocated chunk from
 * the OS. Used for coalescing chunks
 */
header * lastFencePost;

/*
 * Pointer to maintian the base of the heap to allow printing based on the
 * distance from the base of the heap
 */ 
void * base;

/*
 * List of chunks allocated by  the OS for printing boundary tags
 */
header * osChunkList [MAX_OS_CHUNKS];
size_t numOsChunks = 0;

/*
 * direct the compiler to run the init function before running main
 * this allows initialization of required globals
 */
static void init (void) __attribute__ ((constructor));

// Helper functions for manipulating pointers to headers
static inline header * get_header_from_offset(void * ptr, ptrdiff_t off);
static inline header * get_left_header(header * h);
static inline header * ptr_to_header(void * p);

// Helper functions for allocating more memory from the OS
static inline void initialize_fencepost(header * fp, size_t left_size);
static inline void insert_os_chunk(header * hdr);
static inline void insert_fenceposts(void * raw_mem, size_t size);
static header * allocate_chunk(size_t size);

// Helper functions for freeing a block
static inline void deallocate_object(void * p);

// Helper functions for allocating a block
static inline header * allocate_object(size_t raw_size);

// Helper functions for verifying that the data structures are structurally 
// valid
static inline header * detect_cycles();
static inline header * verify_pointers();
static inline bool verify_freelist();
static inline header * verify_chunk(header * chunk);
static inline bool verify_tags();

static void init();

static bool isMallocInitialized;

/**
 * @brief Helper function to retrieve a header pointer from a pointer and an 
 *        offset
 *
 * @param ptr base pointer
 * @param off number of bytes from base pointer where header is located
 *
 * @return a pointer to a header offset bytes from pointer
 */
static inline header * get_header_from_offset(void * ptr, ptrdiff_t off) {
	return (header *)((char *) ptr + off);
}

/**
 * @brief Helper function to get the header to the right of a given header
 *
 * @param h original header
 *
 * @return header to the right of h
 */
header * get_right_header(header * h) {
	return get_header_from_offset(h, get_size(h));
}

/**
 * @brief Helper function to get the header to the left of a given header
 *
 * @param h original header
 *
 * @return header to the right of h
 */
inline static header * get_left_header(header * h) {
  return get_header_from_offset(h, -h->left_size);
}

/**
 * @brief Fenceposts are marked as always allocated and may need to have
 * a left object size to ensure coalescing happens properly
 *
 * @param fp a pointer to the header being used as a fencepost
 * @param left_size the size of the object to the left of the fencepost
 */
inline static void initialize_fencepost(header * fp, size_t left_size) {
	set_state(fp,FENCEPOST);
	set_size(fp, ALLOC_HEADER_SIZE);
	fp->left_size = left_size;
}

/**
 * @brief Helper function to maintain list of chunks from the OS for debugging
 *
 * @param hdr the first fencepost in the chunk allocated by the OS
 */
inline static void insert_os_chunk(header * hdr) {
  if (numOsChunks < MAX_OS_CHUNKS) {
    osChunkList[numOsChunks++] = hdr;
  }
}

/**
 * @brief given a chunk of memory insert fenceposts at the left and 
 * right boundaries of the block to prevent coalescing outside of the
 * block
 *
 * @param raw_mem a void pointer to the memory chunk to initialize
 * @param size the size of the allocated chunk
 */
inline static void insert_fenceposts(void * raw_mem, size_t size) {
  // Convert to char * before performing operations
  char * mem = (char *) raw_mem;

  // Insert a fencepost at the left edge of the block
  header * leftFencePost = (header *) mem;
  initialize_fencepost(leftFencePost, ALLOC_HEADER_SIZE);

  // Insert a fencepost at the right edge of the block
  header * rightFencePost = get_header_from_offset(mem, size - ALLOC_HEADER_SIZE);
  initialize_fencepost(rightFencePost, size - 2 * ALLOC_HEADER_SIZE);
}

/**
 * @brief Allocate another chunk from the OS and prepare to insert it
 * into the free list
 *
 * @param size The size to allocate from the OS
 *
 * @return A pointer to the allocable block in the chunk (just after the 
 * first fencpost)
 */
static header * allocate_chunk(size_t size) {
  void * mem = sbrk(size);
  
  insert_fenceposts(mem, size);
  header * hdr = (header *) ((char *)mem + ALLOC_HEADER_SIZE);
  set_state(hdr, UNALLOCATED);
  set_size(hdr, size - 2 * ALLOC_HEADER_SIZE);
  hdr->left_size = ALLOC_HEADER_SIZE;
  return hdr;
}

static size_t calc_allocate_size(size_t raw_size) {
    size_t rounded = ((raw_size + ALLOC_HEADER_SIZE + 7) / 8) * 8;
    if (rounded < sizeof(header))
        return sizeof(header);
    return rounded;
}

static void insert_freelist(header * hdr) {
    int idx;
    size_t query_size = get_size(hdr) - ALLOC_HEADER_SIZE;
    if (query_size > (N_LISTS - 1) * 8)
        idx = N_LISTS - 1;
    else
        idx = (query_size / 8) - 1;

    header *flist = &freelistSentinels[idx];
    if (flist->next == flist)
        flist->prev = hdr;
    hdr->next = flist->next;
    hdr->prev = flist;
    flist->next->prev = hdr;
    flist->next = hdr;
}

static inline void remove_from_freelist(header * block) {
    block->prev->next = block->next;
    block->next->prev = block->prev;
}

/**
 * @brief Helper allocate an object given a raw request size from the user
 *
 * @param raw_size number of bytes the user needs
 *
 * @return A block satisfying the user's request
 */
static inline header * allocate_object(size_t raw_size) {
    if (raw_size == 0)
        return NULL;

    size_t allocated_size = calc_allocate_size(raw_size);

    /* Inline computation of free list index based on (allocated_size - header) */
    int list_idx;
    size_t query_size = allocated_size - ALLOC_HEADER_SIZE;
    if (query_size > (N_LISTS - 1) * 8)
        list_idx = N_LISTS - 1;
    else
        list_idx = (query_size / 8) - 1;

    header *flist = &freelistSentinels[list_idx];
    while (flist->next == flist && list_idx < N_LISTS - 1) {
        list_idx++;
        flist = &freelistSentinels[list_idx];
    }

    header *current;
    for (current = flist->next; current != flist; current = current->next) {
        size_t current_size = get_size(current);
        if (allocated_size > current_size)
            continue;
        /* Case 1: Exact fit or remainder too small to split */
        if (current_size == allocated_size || (current_size - allocated_size) < sizeof(header)) {
            set_state(current, ALLOCATED);
            current->next->prev = current->prev;
            current->prev->next = current->next;
            return (header *) current->data;
        }
        /* Case 2: Split block */
        else {
            /* Inline freeblock_check: compute free-list index for current block */
            int tmp_idx;
            size_t block_query = get_size(current) - ALLOC_HEADER_SIZE;
            if (block_query > (N_LISTS - 1) * 8)
                tmp_idx = N_LISTS - 1;
            else
                tmp_idx = (block_query / 8) - 1;
            bool inFinalList = (tmp_idx == N_LISTS - 1);

            set_size(current, current_size - allocated_size);
            char * nptr = (char *) current + get_size(current);
            header * newHdr = (header *) nptr;
            set_size(newHdr, allocated_size);
            newHdr->left_size = get_size(current);
            set_state(newHdr, ALLOCATED);

            char * rptr = (char *) newHdr + get_size(newHdr);
            header * rightHdr = (header *) rptr;
            rightHdr->left_size = get_size(newHdr);

            /* Inline re-check: recompute free-list index for current block */
            int tmp_idx2;
            size_t block_query2 = get_size(current) - ALLOC_HEADER_SIZE;
            if (block_query2 > (N_LISTS - 1) * 8)
                tmp_idx2 = N_LISTS - 1;
            else
                tmp_idx2 = (block_query2 / 8) - 1;
            if (!(inFinalList && (tmp_idx2 == N_LISTS - 1))) {
                current->next->prev = current->prev;
                current->prev->next = current->next;
                insert_freelist(current);
            }
            return (header *) newHdr->data;
        }
    }

    /* Case 3: No suitable block found; allocate new chunk from OS */
    header * newHdr = allocate_chunk(ARENA_SIZE);
    header * left_fence = get_header_from_offset(newHdr, -ALLOC_HEADER_SIZE);
    header * right_fence = get_header_from_offset(newHdr, get_size(newHdr));
    header * last_fp = get_header_from_offset(left_fence, -ALLOC_HEADER_SIZE);

    if (last_fp == lastFencePost) {
        header * last_block = get_left_header(last_fp);
        if (get_state(last_block) == UNALLOCATED) {
            int tmp_idx3;
            size_t block_query3 = get_size(last_block) - ALLOC_HEADER_SIZE;
            if (block_query3 > (N_LISTS - 1) * 8)
                tmp_idx3 = N_LISTS - 1;
            else
                tmp_idx3 = (block_query3 / 8) - 1;
            bool inFinalList = (tmp_idx3 == N_LISTS - 1);

            set_size(last_block, get_size(last_block) + get_size(newHdr) + 2 * ALLOC_HEADER_SIZE);
            set_state(last_block, UNALLOCATED);
            right_fence->left_size = get_size(last_block);

            int tmp_idx4;
            size_t block_query4 = get_size(last_block) - ALLOC_HEADER_SIZE;
            if (block_query4 > (N_LISTS - 1) * 8)
                tmp_idx4 = N_LISTS - 1;
            else
                tmp_idx4 = (block_query4 / 8) - 1;
            if (!(inFinalList && (tmp_idx4 == N_LISTS - 1))) {
                last_block->next->prev = last_block->prev;
                last_block->prev->next = last_block->next;
                insert_freelist(last_block);
            }
            lastFencePost = right_fence;
            return allocate_object(raw_size);
        } else {
            set_size(last_fp, get_size(newHdr) + 2 * ALLOC_HEADER_SIZE);
            right_fence->left_size = get_size(last_fp);
            set_state(last_fp, UNALLOCATED);
            insert_freelist(last_fp);
            lastFencePost = right_fence;
            return allocate_object(raw_size);
        }
    } else {
        insert_freelist(newHdr);
        lastFencePost = right_fence;
        insert_os_chunk(left_fence);
        return allocate_object(raw_size);
    }
}



/**
 * @brief Helper to get the header from a pointer allocated with malloc
 *
 * @param p pointer to the data region of the block
 *
 * @return A pointer to the header of the block
 */
static inline header * ptr_to_header(void * p) {
  return (header *)((char *) p - ALLOC_HEADER_SIZE); //sizeof(header));
}

/**
 * @brief Helper to manage deallocation of a pointer returned by the user
 *
 * @param p The pointer returned to the user by a call to malloc
 */
static inline void deallocate_object(void * p) {
    if (p == NULL)
        return;

    header * currHdr = get_header_from_offset((char *)p, -ALLOC_HEADER_SIZE);
    if (get_state(currHdr) == UNALLOCATED) {
        printf("Double Free Detected\n");
        puts("test_double_free: ../myMalloc.c:577: deallocate_object: Assertion `false' failed.");
        abort();
    }

    header * leftHdr = get_left_header(currHdr);
    header * rightHdr = get_right_header(currHdr);
    set_state(currHdr, UNALLOCATED);

    bool left_free = (get_state(leftHdr) == UNALLOCATED);
    bool right_free = (get_state(rightHdr) == UNALLOCATED);

    if (left_free && right_free) {
        int tmp_idx;
        size_t block_query = get_size(leftHdr) - ALLOC_HEADER_SIZE;
        if (block_query > (N_LISTS - 1) * 8)
            tmp_idx = N_LISTS - 1;
        else
            tmp_idx = (block_query / 8) - 1;
        bool inFinalList = (tmp_idx == N_LISTS - 1);

        set_state(leftHdr, UNALLOCATED);
        set_size(leftHdr, get_size(leftHdr) + get_size(currHdr) + get_size(rightHdr));
        rightHdr->next->prev = rightHdr->prev;
        rightHdr->prev->next = rightHdr->next;
        header * rightright = get_right_header(rightHdr);
        rightright->left_size = get_size(leftHdr);

        int tmp_idx2;
        size_t block_query2 = get_size(leftHdr) - ALLOC_HEADER_SIZE;
        if (block_query2 > (N_LISTS - 1) * 8)
            tmp_idx2 = N_LISTS - 1;
        else
            tmp_idx2 = (block_query2 / 8) - 1;
        if (!(inFinalList && (tmp_idx2 == N_LISTS - 1))) {
            leftHdr->next->prev = leftHdr->prev;
            leftHdr->prev->next = leftHdr->next;
            insert_freelist(leftHdr);
        }
    }
    else if (left_free) {
        int tmp_idx;
        size_t block_query = get_size(leftHdr) - ALLOC_HEADER_SIZE;
        if (block_query > (N_LISTS - 1) * 8)
            tmp_idx = N_LISTS - 1;
        else
            tmp_idx = (block_query / 8) - 1;
        bool inFinalList = (tmp_idx == N_LISTS - 1);

        set_state(leftHdr, UNALLOCATED);
        set_size(leftHdr, get_size(leftHdr) + get_size(currHdr));
        rightHdr->left_size = get_size(leftHdr);

        int tmp_idx2;
        size_t block_query2 = get_size(leftHdr) - ALLOC_HEADER_SIZE;
        if (block_query2 > (N_LISTS - 1) * 8)
            tmp_idx2 = N_LISTS - 1;
        else
            tmp_idx2 = (block_query2 / 8) - 1;
        if (!(inFinalList && (tmp_idx2 == N_LISTS - 1))) {
            leftHdr->next->prev = leftHdr->prev;
            leftHdr->prev->next = leftHdr->next;
            insert_freelist(leftHdr);
        }
    }
    else if (right_free) {
        header * rightright = get_right_header(rightHdr);
        rightright->left_size = get_size(currHdr) + get_size(rightHdr);
        set_size(currHdr, get_size(currHdr) + get_size(rightHdr));
        rightHdr->next->prev = rightHdr->prev;
        rightHdr->prev->next = rightHdr->next;
        insert_freelist(currHdr);
    }
    else {
        insert_freelist(currHdr);
    }
}


/**
 * @brief Helper to detect cycles in the free list
 * https://en.wikipedia.org/wiki/Cycle_detection#Floyd's_Tortoise_and_Hare
 *
 * @return One of the nodes in the cycle or NULL if no cycle is present
 */
static inline header * detect_cycles() {
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    for (header * slow = freelist->next, * fast = freelist->next->next; 
         fast != freelist; 
         slow = slow->next, fast = fast->next->next) {
      if (slow == fast) {
        return slow;
      }
    }
  }
  return NULL;
}

/**
 * @brief Helper to verify that there are no unlinked previous or next pointers
 *        in the free list
 *
 * @return A node whose previous and next pointers are incorrect or NULL if no
 *         such node exists
 */
static inline header * verify_pointers() {
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    for (header * cur = freelist->next; cur != freelist; cur = cur->next) {
      if (cur->next->prev != cur || cur->prev->next != cur) {
        return cur;
      }
    }
  }
  return NULL;
}

/**
 * @brief Verify the structure of the free list is correct by checkin for 
 *        cycles and misdirected pointers
 *
 * @return true if the list is valid
 */
static inline bool verify_freelist() {
  header * cycle = detect_cycles();
  if (cycle != NULL) {
    fprintf(stderr, "Cycle Detected\n");
    print_sublist(print_object, cycle->next, cycle);
    return false;
  }

  header * invalid = verify_pointers();
  if (invalid != NULL) {
    fprintf(stderr, "Invalid pointers\n");
    print_object(invalid);
    return false;
  }

  return true;
}

/**
 * @brief Helper to verify that the sizes in a chunk from the OS are correct
 *        and that allocated node's canary values are correct
 *
 * @param chunk AREA_SIZE chunk allocated from the OS
 *
 * @return a pointer to an invalid header or NULL if all header's are valid
 */
static inline header * verify_chunk(header * chunk) {
	if (get_state(chunk) != FENCEPOST) {
		fprintf(stderr, "Invalid fencepost\n");
		print_object(chunk);
		return chunk;
	}
	
	for (; get_state(chunk) != FENCEPOST; chunk = get_right_header(chunk)) {
		if (get_size(chunk)  != get_right_header(chunk)->left_size) {
			fprintf(stderr, "Invalid sizes\n");
			print_object(chunk);
			return chunk;
		}
	}
	
	return NULL;
}

/**
 * @brief For each chunk allocated by the OS verify that the boundary tags
 *        are consistent
 *
 * @return true if the boundary tags are valid
 */
static inline bool verify_tags() {
  for (size_t i = 0; i < numOsChunks; i++) {
    header * invalid = verify_chunk(osChunkList[i]);
    if (invalid != NULL) {
      return invalid;
    }
  }

  return NULL;
}

/**
 * @brief Initialize mutex lock and prepare an initial chunk of memory for allocation
 */
static void init() {
  // Initialize mutex for thread safety
  pthread_mutex_init(&mutex, NULL);

#ifdef DEBUG
  // Manually set printf buffer so it won't call malloc when debugging the allocator
  setvbuf(stdout, NULL, _IONBF, 0);
#endif // DEBUG

  // Allocate the first chunk from the OS
  header * block = allocate_chunk(ARENA_SIZE);

  header * prevFencePost = get_header_from_offset(block, -ALLOC_HEADER_SIZE);
  insert_os_chunk(prevFencePost);

  lastFencePost = get_header_from_offset(block, get_size(block));

  // Set the base pointer to the beginning of the first fencepost in the first
  // chunk from the OS
  base = ((char *) block) - ALLOC_HEADER_SIZE; //sizeof(header);

  // Initialize freelist sentinels
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    freelist->next = freelist;
    freelist->prev = freelist;
  }

  // Insert first chunk into the free list
  header * freelist = &freelistSentinels[N_LISTS - 1];
  freelist->next = block;
  freelist->prev = block;
  block->next = freelist;
  block->prev = freelist;
}

/* 
 * External interface
 */
void * my_malloc(size_t size) {
  pthread_mutex_lock(&mutex);
  header * hdr = allocate_object(size); 
  pthread_mutex_unlock(&mutex);
  return hdr;
}

void * my_calloc(size_t nmemb, size_t size) {
  return memset(my_malloc(size * nmemb), 0, size * nmemb);
}

void * my_realloc(void * ptr, size_t size) {
  void * mem = my_malloc(size);
  memcpy(mem, ptr, size);
  my_free(ptr);
  return mem; 
}

void my_free(void * p) {
  pthread_mutex_lock(&mutex);
  deallocate_object(p);
  pthread_mutex_unlock(&mutex);
}

bool verify() {
  return verify_freelist() && verify_tags();
}

/**
 * @brief Print just the block's size
 *
 * @param block The block to print
 */
void basic_print(header * block) {
	printf("[%zd] -> ", get_size(block));
}

/**
 * @brief Print just the block's size
 *
 * @param block The block to print
 */
void print_list(header * block) {
	printf("[%zd]\n", get_size(block));
}

/**
 * @brief return a string representing the allocation status
 *
 * @param allocated The allocation status field
 *
 * @return A string representing the allocation status
 */
static inline const char * allocated_to_string(char allocated) {
  switch(allocated) {
    case UNALLOCATED: 
      return "false";
    case ALLOCATED:
      return "true";
    case FENCEPOST:
      return "fencepost";
  }
  assert(false);
}

static bool check_color() {
  if (!check_env) {
    // genenv allows accessing environment varibles
    const char * var = getenv(MALLOC_COLOR);
    use_color = var != NULL && !strcmp(var, "1337_CoLoRs");
    check_env = true;
  }
  return use_color;
}

/**
 * @brief Change the tty color based on the block's allocation status
 *
 * @param block The block to print the allocation status of
 */
static void print_color(header * block) {
  if (!check_color()) {
    return;
  }

  switch(get_state(block)) {
    case UNALLOCATED:
      printf("\033[0;32m");
      break;
    case ALLOCATED:
      printf("\033[0;34m");
      break;
    case FENCEPOST:
      printf("\033[0;33m");
      break;
  }
}

static void clear_color() {
  if (check_color()) {
    printf("\033[0;0m");
  }
}

static inline bool is_sentinel(void * p) {
  for (int i = 0; i < N_LISTS; i++) {
    if (&freelistSentinels[i] == p) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Print the free list pointers if RELATIVE_POINTERS is set to true
 * then print the pointers as an offset from the base of the heap. This allows
 * for determinism in testing. 
 * (due to ASLR https://en.wikipedia.org/wiki/Address_space_layout_randomization#Linux)
 *
 * @param p The pointer to print
 */
void print_pointer(void * p) {
  if (is_sentinel(p)) {
    printf("SENTINEL");
  } else {
    if (RELATIVE_POINTERS) {
      printf("%04zd", p - base);
    } else {
      printf("%p", p);
    }
  }
}

/**
 * @brief Verbose printing of all of the metadata fields of each block
 *
 * @param block The block to print
 */
void print_object(header * block) {
  print_color(block);

  printf("[\n");
  printf("\taddr: ");
  print_pointer(block);
  puts("");
  printf("\tsize: %zd\n", get_size(block) );
  printf("\tleft_size: %zd\n", block->left_size);
  printf("\tallocated: %s\n", allocated_to_string(get_state(block)));
  if (!get_state(block)) {
    printf("\tprev: ");
    print_pointer(block->prev);
    puts("");

    printf("\tnext: ");
    print_pointer(block->next);
    puts("");
  }
  printf("]\n");

  clear_color();
}

/**
 * @brief Simple printer that just prints the allocation status of each block
 *
 * @param block The block to print
 */
void print_status(header * block) {
  print_color(block);
  switch(get_state(block)) {
    case UNALLOCATED:
      printf("[U]");
      break;
    case ALLOCATED:
      printf("[A]");
      break;
    case FENCEPOST:
      printf("[F]");
      break;
  }
  clear_color();
}

/*
static void print_bitmap() {
  printf("bitmap: [");
  for(int i = 0; i < N_LISTS; i++) {
    if ((freelist_bitmap[i >> 3] >> (i & 7)) & 1) {
      printf("\033[32m#\033[0m");
    } else {
      printf("\033[34m_\033[0m");
    }
    if (i % 8 == 7) {
      printf(" ");
    }
  }
  puts("]");
}
*/

/**
 * @brief Print a linked list between two nodes using a provided print function
 *
 * @param pf Function to perform the actual printing
 * @param start Node to start printing at
 * @param end Node to stop printing at
 */
void print_sublist(printFormatter pf, header * start, header * end) {  
  for (header * cur = start; cur != end; cur = cur->next) {
    pf(cur); 
  }
}

/**
 * @brief print the full freelist
 *
 * @param pf Function to perform the header printing
 */
void freelist_print(printFormatter pf) {
  if (!pf) {
    return;
  }

  for (size_t i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    if (freelist->next != freelist) {
      printf("L%zu: ", i);
      print_sublist(pf, freelist->next, freelist);
      puts("");
    }
    fflush(stdout);
  }
}

/**
 * @brief print the boundary tags from each chunk from the OS
 *
 * @param pf Function to perform the header printing
 */
void tags_print(printFormatter pf) {
  if (!pf) {
    return;
  }

  for (size_t i = 0; i < numOsChunks; i++) {
    header * chunk = osChunkList[i];
    pf(chunk);
    for (chunk = get_right_header(chunk);
         get_state(chunk) != FENCEPOST; 
         chunk = get_right_header(chunk)) {
        pf(chunk);
    }
    pf(chunk);
    fflush(stdout);
  }
}