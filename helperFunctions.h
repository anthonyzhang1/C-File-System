/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: helperFunctions.h
*
* Description: This is the header file for some of the
*  miscellaneous functions and constants used throughout the file system.
*
**************************************************************/

#ifndef _HELPER_FUNCTIONS_H
#define _HELPER_FUNCTIONS_H

#include <stdio.h>
#include <sys/types.h>
#include "fsInit.h"

#define TRUE 1 // the boolean value TRUE
#define FALSE 0 // the boolean value FALSE

#define SUCCESS 0 // signifies there was no error
#define UNSIGNED_ERROR 0 // signifies an error for an unsigned return value, sometimes
#define ERROR -1 // signifies an error for a signed return value

#define FREE 0u // a 0 in the bitmap means that the corresponding block is free
#define USED 1u // a 1 in the bitmap means that the corresponding block is used

/* Returns the numerator divided by the denominator, rounded up. */
int ceilingDivide(int numerator, int denominator);

/* Marks a block as used in the bitmap.
 * Does not modify the bitmap if block_num is out of bounds. */
void markBlockUsed(uint32_t *bitmap, uint64_t block_num);

/* Marks a block as free in the bitmap.
 * Does not modify the bitmap if block_num is out of bounds. */
void markBlockFree(uint32_t *bitmap, uint64_t block_num);

/* Returns FREE if the block is free, or USED if the block is used.
 * If block_num is out of bounds, return USED. */
int getBlockStatus(uint32_t *bitmap, uint64_t block_num);

/* Gets num_blocks_wanted contiguous blocks in the bitmap.
 * Returns the starting block of these contiguous blocks.
 * Returns UNSIGNED_ERROR if num_blocks_wanted contiguous free blocks could not be found. */
uint64_t getContiguousFreeBlocks(uint64_t num_blocks_wanted);

/* Modify which block the bitmap starts searching from, denoted start_block_index.
 * start_block_index will be modified by num_blocks. If start_block_index will take
 * on an invalid start block, it will be reset back to vcb->free_space_start_block.
 * Returns the value of start_block_index. */
uint64_t modStartBlockIndex(long long num_blocks);

/* Same as LBAread but can take a message to help identify which function caused the error.
 * Returns the number of blocks read into the buffer, or returns ERROR
 * if the number of blocks read into the buffer is not the same as blocks_to_read. */
long long customLBAread(void *buf, uint64_t blocks_to_read, uint64_t start_block, char *msg);

/* Same as LBAwrite but can take a message to help identify which function caused the error.
 * Returns the number of blocks written to the disk, or returns ERROR
 * if the number of blocks written to the disk is not the same as blocks_to_write. */
long long customLBAwrite(void *buf, uint64_t blocks_to_write, uint64_t start_block, char *msg);

/* Prints the data members in the VCB. Used for debugging. */
void printVCBcontents();

#endif