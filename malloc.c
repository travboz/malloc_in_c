// sbrk(0) returns a pointer to the current top of the heap
// sbrk(val) increments the heap size by val and returns a pointer to the previous top of the heap

// sbrk: https://man7.org/linux/man-pages/man2/sbrk.2.html
/*
sbrk allows the program to increase or decrease the amount of memory allocated
to *its* heap at runtime.
It asks the OS to move the `break` point - or the boundary between allocated and
unallocated memory. Using a positive val in sbrk(val) increases the heap size,
while using a negative val decreases it.
sbrk() returns the *address* of the new break point - indicating the start
of the newly allocated memory.
*/

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>

/*
Simple implementation of malloc:
*/

/**
 * When a program asks malloc for space, malloc asks sbrk to increment the heap
 * size and returns a pointer to the start of the new region on the heap.
 */
void *malloc(size_t size)
{
    void *p = sbrk(0);          // get the current top of the heap
    void *request = sbrk(size); // set aside space of size equal to size and get the
    // previous top of the heap

    if (request == (void *)-1)
    {                // if sbrk fails it returns "(void*) -1"
        return NULL; // sbrk failed
    }
    else
    {
        assert(p == request); // assert the addresses point to the same address (top of the original heap) - but this is not thread safe
        return p;             // return our new address
    }
}

/*
Implementation of malloc
*/
struct block_meta
{
    size_t size;             // size of the block we need
    struct block_meta *next; // the block after it
    int free;                // marker for if this block of memory is considered free so we can write over it
    int magic;               // for debugging
};

#define META_SIZE sizeof(struct block_meta) // to get the size of some block

// the "head" of our "linked list" of memory blocks
void *global_base = NULL;

// we want to re-use free space if possible - only allocating new space
// if we can't reuse existing space.
// we use a linked list to iterate through and search for a block that's
// large enough.

/*
We iterate through our linked list of memory blocks searching for a block
that is both FREE and IS BIG ENOUGH to hold the data we have.
*/
struct block_meta *find_free_block(struct block_meta **last, size_t size)
{
    struct block_meta *current = global_base; // start from the bottom of our heap

    // while current is not null and:
    // we not at a point where current is both free to use and it's big enough
    while (current && !(current->free && current->size >= size))
    {
        *last = current;         // move last to point to our current block
        current = current->next; // take a step to the next block
    }

    return current;
}

/*
If we don't find a free block, we request it from the OS using sbrk and add
our new block to the end of the list.
*/
struct block_meta *request_space(struct block_meta *last, size_t size)
{
    struct block_meta *block;
    block = sbrk(0);                        // we grab the current top of the heap
    void *request = sbrk(size + META_SIZE); // alocate enough space for our new data + the meta data associated with it

    assert((void *)block == request); // not thread safe - assert they're pointing to the same top of heap
    if (request == (void *)-1)
    {
        return NULL; // sbrk failed because (void*) -1
    }

    // ### Adding our new block to the end of the list
    // if this is not the first allocation
    if (last)
    { // NULL on first request
        last->next = block;
    }
    block->size = size;
    block->next = NULL;
    block->free = 0; // marker for "using" space
    block->magic = 0x12345678;
    return block;
}

void *malloc(size_t size)
{
    struct block_meta *block;

    if (size <= 0)
    {
        return NULL;
    }
    // if our global base pointer is NULL, we need to request space
    // and set the base pointer to our new block
    if (!global_base)
    {                                      // if global_base is NULL -> if this is the first call
        block = request_space(NULL, size); // the last block is NULL
        if (!block)
        {
            return NULL;
        }

        global_base = block;
    }
    else // global_base wasn't NULL and so we search for existing space to reuse
    {
        // if we can find
        struct block_meta *last = global_base;
        block = find_free_block(&last, size);

        if (!block) // Failed to find free block.
        {
            // so we request for some space
            block = request_space(last, size);
            if (!block)
            {
                return NULL;
            }
        }
        else // we found a free block
        {
            block->free = 0; // mark it as allocated
            block->magic = 0x77777777;
        }
    }

    return (block + 1); // we want to return a pointer to the region after block_meta. Since block is a pointer of type struct block_meta, +1 increments the address by one sizeof(struct block_meta).
}

/**
 * @brief implementation of free()
 * This takes a ptr and returns a pointer to the metadata associated with the memory block it points to.
 */
struct block_meta *get_block_ptr(void *ptr)
{
    return (struct block_meta *)ptr - 1;
}

/**
 * @brief
 *
 */
void free(void *ptr)
{
    if (!ptr)
    {
        return;
    }

    struct block_meta *block_ptr = get_block_ptr(ptr);
    assert(block_ptr->free == 0);                                             // assert it's being used
    assert(block_ptr->magic == 0x77777777 || block_ptr->magic == 0x12345678); // assert it's either the start of our list or end

    block_ptr->free = 1;
    block_ptr->magic = 0x55555555;
}

/**
 * @brief Realloc
 *
 */
void *realloc(void *ptr, size_t size)
{
    if (!ptr)
    {
        // NULL ptr. realloc should act like malloc
        return malloc(size);
    }

    struct block_meta *block_ptr = get_block_ptr(ptr);
    if (block_ptr->size >= size)
    {
        // we have enough space
        return ptr;
    }

    // we need t really realloc space - and so we malloc new space
    // and free old space. then we copy over the old data to the
    // new space.
    void *new_ptr;
    new_ptr = malloc(size);

    if (!new_ptr)
    {
        return NULL; // weren't able to allocate for some reason
    }

    memcpy(new_ptr, ptr, block_ptr->size);
    free(ptr);
    return new_ptr;
}

/**
 * @brief Calloc implementation
 *
 */
void *calloc(size_t nelem, size_t elsize)
{
    // checking for overflow
    if (nelem > 0 && SIZE_MAX / nelem < elsize)
    {
        // Overflow occurred
        return NULL;
    }
    size_t size = nelem * elsize; // TODO: check for overflow.
    void *ptr = malloc(size);
    if (ptr != NULL)
    {
        memset(ptr, 0, size);
    }
    return ptr;
}