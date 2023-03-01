/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: fsInit.c
*
* Description: Main driver for the file system assignment.
*  This file is where we will start and initialize our system.
*
**************************************************************/

#include "fsInit.h"
#include "mfs.h"

#define VCB_MAGIC_NUMBER 0xDEADED // used for checking if the VCB is already initialized
#define BLOCKS_TO_BYTES_DENOM 8 // 1 block = 1 bit = 1/8 bytes in the bitmap
#define BLOCKS_TO_INTS_DENOM 32 // 1 block = 1 bit = 1/32th of an integer

// The VCB and bitmap are shared by all files due to the extern keyword in the header.
VCB *vcb = NULL;
uint32_t *bitmap = NULL;

int initFileSystem(uint64_t numberOfBlocks, uint64_t blockSize) {
	// load up the vcb
	vcb = malloc(blockSize);
	if (!vcb) return ERROR;

	if (customLBAread(vcb, VCB_BLOCKS, VCB_START_BLOCK, "init VCB") == ERROR) {
		free(vcb);
		vcb = NULL;
		return ERROR;
	}

	// if signature matches, then the volume has already been initialized
	if (vcb->signature == VCB_MAGIC_NUMBER) {
		// initialize bitmap with what was written in disk
		bitmap = malloc(vcb->bitmap_blocks * vcb->block_size);
		if (!bitmap) {
			free(vcb);
			vcb = NULL;
			return ERROR;
		}

		if (customLBAread(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
			"init bitmap") == ERROR) {
			free(vcb);
			vcb = NULL;
			free(bitmap);
			bitmap = NULL;
			return ERROR;
		}
	} else { // initialize the volume
		// check that the volume can actually hold the VCB
		if (VCB_BLOCKS > numberOfBlocks) {
			printf("Error: Not enough blocks in the volume to hold the "
			       "volume control block.\n");
			free(vcb);
			vcb = NULL;
			return ERROR;
		}

		// initialize some of VCB's data members
		vcb->num_blocks = numberOfBlocks;
		vcb->num_free_blocks = numberOfBlocks;
		vcb->block_size = blockSize;
		vcb->free_space_start_block = 0;
		vcb->bitmap_start_block = VCB_BLOCKS; // bitmap starts after the VCB
		vcb->signature = VCB_MAGIC_NUMBER;
		
		// initialize bitmap and root directory, and the rest of vcb's data members
		// bitmap has been malloced at this point
		if (initBitmap() == ERROR) {
			free(vcb);
			vcb = NULL;
			free(bitmap);
			bitmap = NULL;
			return ERROR;
		}
		
		if (initRootDirectory() == ERROR) {
			free(vcb);
			vcb = NULL;
			free(bitmap);
			bitmap = NULL;
			return ERROR;
		}

		// once the root directory is initialized, free space starts after it
		vcb->free_space_start_block = vcb->root_dir_start_block + vcb->dir_blocks;

		// Write the VCB to disk
		if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK, "VCB first init") == ERROR) {
			free(vcb);
			vcb = NULL;
			free(bitmap);
			bitmap = NULL;
			return ERROR;
		}
	}

	// initialize CWD's start block with the root dir's start block
	setCWDstartBlock(vcb->root_dir_start_block);

	return SUCCESS;
}

int initBitmap() {
	// bytes needed for the bitmap
	uint64_t bitmap_bytes = ceilingDivide(vcb->num_blocks, BLOCKS_TO_BYTES_DENOM);

	// blocks needed for the bitmap
	uint64_t bitmap_blocks = ceilingDivide(bitmap_bytes, vcb->block_size);
	vcb->bitmap_blocks = bitmap_blocks;

	// check that the volume can actually hold the bitmap
	if (bitmap_blocks + VCB_BLOCKS > vcb->num_blocks) {
		printf("Error: Not enough free blocks in the volume to hold the bitmap.\n");
		return ERROR;
	}

	// numbers of ints needed to represent the bits in the bitmap
	uint64_t bitmap_ints = ceilingDivide(vcb->num_blocks, BLOCKS_TO_INTS_DENOM);

	bitmap = malloc(bitmap_blocks * vcb->block_size);
	if (!bitmap) return ERROR;

	// initialize all bits in the bitmap to be free
	for (int i = 0; i < bitmap_ints; i++) bitmap[i] = FREE;

	// mark the blocks the VCB and the bitmap will take up as used
	for (int i = 0; i < VCB_BLOCKS + bitmap_blocks; i++) markBlockUsed(bitmap, i);
	vcb->num_free_blocks -= VCB_BLOCKS + bitmap_blocks;

	// write the bitmap to disk
	if (customLBAwrite(bitmap, bitmap_blocks, vcb->bitmap_start_block,
		"write bitmap to LBA") == ERROR) {
		free(bitmap);
		bitmap = NULL;
		return ERROR;
	}

	return SUCCESS;
}

int initRootDirectory() {
	// bytes needed for a directory
	int dir_bytes = MAX_DIRECTORY_ENTRIES * sizeof(dir_entry);

	// blocks needed for a directory
	int dir_blocks = ceilingDivide(dir_bytes, vcb->block_size);
	vcb->dir_blocks = dir_blocks;

	// Try to get enough contiguous free blocks for the root directory
	uint64_t root_dir_start_block = getContiguousFreeBlocks(dir_blocks);
	if (root_dir_start_block == UNSIGNED_ERROR) {
		printf("Error: Not enough free blocks in the volume to hold the root directory.\n");
		return ERROR;
	}
	vcb->root_dir_start_block = root_dir_start_block;

	dir_entry *root_dir = calloc(dir_blocks, vcb->block_size);
	if (!root_dir) return ERROR;
	
	time_t curr_time = time(NULL);

	// initialize the '.' entry in the root directory
	strcpy(root_dir[0].name, ".");
	root_dir[0].start_block = root_dir_start_block;
	root_dir[0].size = dir_bytes;
	root_dir[0].type = DIRECTORY;
	root_dir[0].creation_date = curr_time;
	root_dir[0].last_modified = curr_time;
	root_dir[0].last_opened = curr_time;

	// initialize the '..' entry in the root directory
	// the '..' entry is the same as '.' save for the name in the root directory
	root_dir[1] = root_dir[0];
	strcpy(root_dir[1].name, "..");

	// initialize the remaining directory entries
	for (int i = 2; i < MAX_DIRECTORY_ENTRIES; i++) root_dir[i].type = FREE_ENTRY;

	// write the root directory to disk
	if (customLBAwrite(root_dir, dir_blocks, root_dir_start_block,
		"init root_dir") == ERROR) {
		free(root_dir);
		root_dir = NULL;
		return ERROR;
	}

	// mark the blocks the root directory took up as used
	for (int i = 0; i < dir_blocks; i++) markBlockUsed(bitmap, root_dir_start_block + i);
	vcb->num_free_blocks -= dir_blocks; // vcb written to disk later

	// write to disk the updated bitmap
	if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
		"root_dir bitmap") == ERROR) {
		free(root_dir);
		root_dir = NULL;
		return ERROR;
	}

	free(root_dir);
	root_dir = NULL;

	return SUCCESS;
}

int setCWDstartBlock(uint64_t start_block) {
	if (start_block >= vcb->num_blocks) {
		printf("Error: Attempted to set the cwd's start block to an illegal value.\n");
		return ERROR;
	}

	cwd_start_block = start_block;
	return SUCCESS;
}

uint64_t getCWDstartBlock() { return cwd_start_block; }

/* Frees the global pointers. */
void exitFileSystem() {
	free(vcb);
	vcb = NULL;
	free(bitmap);
	bitmap = NULL;

	printf("System exiting.\n");
}