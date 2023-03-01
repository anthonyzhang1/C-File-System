/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: helperFunctions.c
*
* Description: This is the source code file for the general helper functions
*  used throughout the file system.
*
**************************************************************/

#include "helperFunctions.h"

/* We search for free blocks in the bitmap at block number start_block_index.
 * start_block_index is incremented to the last block checked whenever we try
 * to get continuous free blocks for a file. We do this to avoid having to start
 * the bitmap search from 0 every time we want to get some free blocks.
 * Upon launching the program, start_block_index is reset to 0, so we will fill in
 * any gaps from deleted files when we search the bitmap for free blocks again. */
uint64_t start_block_index = 0;

int ceilingDivide(int numerator, int denominator) {
    return (numerator + denominator - 1) / denominator;
}

/* Sources: stackoverflow.com/questions/2525310/how-to-define-and-work-with-an-array-of-bits-in-c
 *
 * Bit n is stored in the n/32th integer, rounded down. Hence, bitmap[block_num / 32].
 * Bit n is the n % 32th bit in the n/32th integer. Hence, block_num % 32.
 *
 * We bit shift the value 1u, n % 32 places, which results in a value something like: 000...1000.
 * We then take the n/32th integer and perform a bitwise OR on it with the bit shifted 1u,
 * then assign the result to the n/32th integer.
 * Only the n % 32th bit should have been changed into a 1, if it was not 1 already. */
void markBlockUsed(uint32_t *bitmap, uint64_t block_num) {
    if (block_num >= vcb->num_blocks) { // check for out of bounds bitmap access
        printf("Out of bounds array access stopped in markBlockUsed.\n");
        return;
    }

    bitmap[block_num / 32] |= 1u << (block_num % 32);
}

/* Sources: stackoverflow.com/questions/2525310/how-to-define-and-work-with-an-array-of-bits-in-c
 *
 * Bit n is stored in the n/32th integer, rounded down. Hence, bitmap[block_num / 32].
 * Bit n is the n % 32th bit in the n/32th integer. Hence, block_num % 32.
 * 
 * We bit shift the value 1u, n % 32 places, which results in a value something like: 000...1000.
 * We do a bitwise NOT on the bit shifted value, so it becomes something like: 111...0111.
 * We then take the n/32th integer and perform a bitwise AND on it with the 111...0111 value,
 * then assign the result to the n/32th integer.
 * Only the n % 32th bit should have been changed into a 0, if it was not 0 already. */
void markBlockFree(uint32_t *bitmap, uint64_t block_num) {
    if (block_num >= vcb->num_blocks) { // check for out of bounds bitmap access
        printf("Out of bounds array access stopped in markBlockFree.\n");
        return;
    }

    bitmap[block_num / 32] &= ~(1u << (block_num % 32));
}

/* Sources: stackoverflow.com/questions/2525310/how-to-define-and-work-with-an-array-of-bits-in-c
 *
 * Bit n is stored in the n/32th integer, rounded down. Hence, bitmap[block_num / 32].
 * Bit n is the n % 32th bit in the n/32th integer. Hence, block_num % 32.
 * We bit shift the value 1u, n % 32 places, which results in a value something like: 000...1000.
 * We then take the n/32th integer and perform a bitwise AND on it with the bit shifted 1u.
 * 
 * If the result of the bitwise AND is 0, then return 0 (free).
 * If the result of the bitwise AND is non-0, then return 1 (used). */
int getBlockStatus(uint32_t *bitmap, uint64_t block_num) {
    if (block_num >= vcb->num_blocks) return USED; // do not modify past the disk/bitmap bounds

    return (bitmap[block_num / 32] & (1u << (block_num % 32))) != 0;
}

/* The search for free blocks starts at start_block_index, which is a global variable.
 * start_block_index is not reset to 0 after a search, so we essentially search the bitmap
 * for free blocks from where we stopped searching last time. start_block_index is reset to 0
 * after restarting the program, which allows us to fill in any holes left by deleted files
 * that start_block_index had already moved past. */
uint64_t getContiguousFreeBlocks(uint64_t num_blocks_wanted) {
    if (vcb->num_free_blocks < num_blocks_wanted) return UNSIGNED_ERROR;

    if (start_block_index < vcb->free_space_start_block) {
        start_block_index = vcb->free_space_start_block;
    }

    int cont_free_blocks_count = 0; // keeps track of contiguous free blocks
    for (int iterations = 0; iterations < vcb->num_blocks; iterations++, start_block_index++) {
        // loop back around if start_block_index reaches the end of the bitmap
        if (start_block_index >= vcb->num_blocks) {
            start_block_index = vcb->free_space_start_block;
            cont_free_blocks_count = 0;
        }

        // free block
        if (getBlockStatus(bitmap, start_block_index) == FREE) cont_free_blocks_count++;
        else cont_free_blocks_count = 0; // used block

        // found a set of contiguous free blocks that can fit num_blocks_wanted blocks
        if (cont_free_blocks_count >= num_blocks_wanted) {
            start_block_index++;
            return start_block_index - cont_free_blocks_count;
        }
    }

    // Looked through the entire bitmap without finding enough contiguous free blocks
    return UNSIGNED_ERROR;
}

uint64_t modStartBlockIndex(long long num_blocks) {
    start_block_index += num_blocks; // can be negative or positive

    if ((start_block_index < vcb->free_space_start_block)
        || (start_block_index >= vcb->num_blocks)) {
        start_block_index = vcb->free_space_start_block;
    }

    return start_block_index;
}

long long customLBAread(void *buf, uint64_t blocks_to_read, uint64_t start_block, char *msg) {
    // LBAread returns the number of blocks read into the buffer.
    uint64_t blocks_read = LBAread(buf, blocks_to_read, start_block);
    
    if (blocks_read == blocks_to_read) return blocks_read;
    else {
        printf("An error occurred with LBAread(). Error Message: %s\n", msg);
        return ERROR;
    }
}

long long customLBAwrite(void *buf, uint64_t blocks_to_write, uint64_t start_block, char *msg) {
    // LBAwrite returns the number of blocks written to the disk.
    uint64_t blocks_written = LBAwrite(buf, blocks_to_write, start_block);

    if (blocks_written == blocks_to_write) return blocks_written;
    else {
        printf("An error occurred with LBAwrite(). Error Message: %s\n", msg);
        return ERROR;
    }
}

void printVCBcontents() {
	printf("\nVCB Contents:\n"
		   "vcb->num_blocks: %ld\n"
		   "vcb->block_size: %ld\n"
		   "vcb->free_space_start_block: %ld\n"
		   "vcb->num_free_blocks: %ld\n\n"

		   "vcb->bitmap_start_block: %ld\n"
		   "vcb->bitmap_blocks: %ld\n"
		   "vcb->root_dir_start_block: %ld\n"
		   "vcb->dir_blocks: %ld\n\n"

		   "vcb->signature: %lX\n\n",
		   vcb->num_blocks, vcb->block_size, vcb->free_space_start_block,
           vcb->num_free_blocks, vcb->bitmap_start_block, vcb->bitmap_blocks,
           vcb->root_dir_start_block, vcb->dir_blocks, vcb->signature);
}