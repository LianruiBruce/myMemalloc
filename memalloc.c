/**
 * This is a personal project following the instruction https://arjunsreedharan.org/post/148675821737/memory-allocators-101-write-a-simple-memory
 * The Original Github: https://github.com/arjun024/memalloc
 * Author: Lianrui Geng
 * Content: Practice for C language and memory allocators
 */

/**
 * For the virtual address spaces:
 *  Text section: The part that contains the binary instructions to be executed by the processor.
	Data section: Contains non-zero initialized static data.
	BSS (Block Started by Symbol) : Contains zero-initialized static data. Static data uninitialized in program is initialized 0 and goes here.
	Heap: Contains the dynamically allocated data.
	Stack: Contains your automatic variables, function arguments, copy of base pointer etc.
 *
*/

#include <unistd.h>
#include <string.h>
#include <pthread.h>
/* Only for the debug printf */
#include <stdio.h>

typedef char ALIGN[16];

// I will use the orginal union.
union header
{
	struct
	{
		size_t size;
		unsigned is_free;
		union header *next;
	} s;
	/* force the header to be aligned to 16 bytes */
	ALIGN stub;
};
typedef union header header_t;

header_t *head = NULL, *tail = NULL;
pthread_mutex_t global_malloc_lock;

header_t *get_free_block(size_t size)
{
	header_t *curr = head;
	while (curr)
	{
		/* see if there's a free block that can accomodate requested size */
		if (curr->s.is_free && curr->s.size >= size)
			return curr;
		curr = curr->s.next;
	}
	return NULL;
}

void free(void *block)
{
	header_t *header, *tmp;
	/* program break is the end of the process's data segment */
	void *programbreak;

	if (!block)
		return;
	pthread_mutex_lock(&global_malloc_lock);
	header = (header_t *)block - 1;
	/* sbrk(0) gives the current program break address */
	programbreak = sbrk(0);

	/*
	   Check if the block to be freed is the last one in the
	   linked list. If it is, then we could shrink the size of the
	   heap and release memory to OS. Else, we will keep the block
	   but mark it as free.
	 */
	if ((char *)block + header->s.size == programbreak)
	{
		if (head == tail)
		{
			head = tail = NULL;
		}
		else
		{
			tmp = head;
			while (tmp)
			{
				if (tmp->s.next == tail)
				{
					tmp->s.next = NULL;
					tail = tmp;
				}
				tmp = tmp->s.next;
			}
		}
		/*
		   sbrk() with a negative argument decrements the program break.
		   So memory is released by the program to OS.
		*/
		sbrk(0 - header->s.size - sizeof(header_t));
		/* Note: This lock does not really assure thread
		   safety, because sbrk() itself is not really
		   thread safe. Suppose there occurs a foregin sbrk(N)
		   after we find the program break and before we decrement
		   it, then we end up realeasing the memory obtained by
		   the foreign sbrk().
		*/
		pthread_mutex_unlock(&global_malloc_lock);
		return;
	}
	header->s.is_free = 1;
	pthread_mutex_unlock(&global_malloc_lock);
}

void *malloc(size_t size)
{
	// check if the size is empty/zero
	if (!size)
	{
		return NULL;
	}
	// lock this part of memory to ensure thread-safety
	pthread_mutex_lock(&global_malloc_lock);
	// check if we could find one large memory which is enough for our request
	header_t *newHeader = get_free_block(size);
	// if we found one
	if (newHeader)
	{
		// now we set this part memory is not free
		newHeader->s.is_free = 0;
		// unlock the memory before we run it.
		pthread_mutex_unlock(&global_malloc_lock);
		// directly return this
		return (void *)(newHeader + 1);
	}
	// if we don't have this luckly memory, we need to find the total size first
	size_t totalSize;
	totalSize = sizeof(header_t) + size;
	// now we have to ask for more memory from system
	// first we use sbrk to send this request
	void *newBlock = sbrk(totalSize);

	// if we meet one error when we use sbrk
	if (newBlock == (void *)-1)
	{
		pthread_mutex_unlock(&global_malloc_lock);
		return NULL;
	}

	// now we are good to allocate the memory
	newHeader = newBlock;
	newHeader->s.is_free = 0;
	newHeader->s.size = size;
	newHeader->s.next = NULL;

	// if this new header is the first head
	if (!head)
	{
		head = newHeader;
	}
	// if we have the tail already
	if (tail)
	{
		tail->s.next = newHeader;
	}

	// now update the tail be the head
	tail = newHeader;

	// now we unlock the memory
	pthread_mutex_unlock(&global_malloc_lock);
	return (void *)(newHeader + 1);
}

void *calloc(size_t num, size_t nsize)
{
	size_t size;
	void *block;
	if (!num || !nsize)
		return NULL;
	size = num * nsize;
	/* check mul overflow */
	if (nsize != size / num)
		return NULL;
	block = malloc(size);
	if (!block)
		return NULL;
	memset(block, 0, size);
	return block;
}

void *realloc(void *block, size_t size)
{
	header_t *header;
	void *ret;
	if (!block || !size)
		return malloc(size);
	header = (header_t *)block - 1;
	if (header->s.size >= size)
		return block;
	ret = malloc(size);
	if (ret)
	{
		/* Relocate contents to the new bigger block */
		memcpy(ret, block, header->s.size);
		/* Free the old memory block */
		free(block);
	}
	return ret;
}

/* A debug function to print the entire link list */
void print_mem_list()
{
	header_t *curr = head;
	printf("head = %p, tail = %p \n", (void *)head, (void *)tail);
	while (curr)
	{
		printf("addr = %p, size = %zu, is_free=%u, next=%p\n",
			   (void *)curr, curr->s.size, curr->s.is_free, (void *)curr->s.next);
		curr = curr->s.next;
	}
}

int main()
{
	void *block1, *block2;

	// Test malloc
	block1 = malloc(100); // Allocate 100 bytes
	if (block1 == NULL)
	{
		printf("malloc failed\n");
		return 1;
	}
	strcpy(block1, "Test string"); // Use the allocated memory
	printf("block1: %s\n", (char *)block1);

	// Test realloc
	block1 = realloc(block1, 200); // Increase size to 200 bytes
	if (block1 == NULL)
	{
		printf("realloc failed\n");
		return 1;
	}
	strcat(block1, " with more data");
	printf("block1 after realloc: %s\n", (char *)block1);

	// Test calloc
	block2 = calloc(25, 4); // Allocate an array of 25 ints (4 bytes each)
	if (block2 == NULL)
	{
		printf("calloc failed\n");
		return 1;
	}
	// Use the allocated array

	// Test free
	free(block1);
	free(block2);

	// Optional: Print memory list
	print_mem_list();

	return 0;
}
