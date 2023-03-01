/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: fsInit.h
*
* Description: Header for the main driver of the file system assignment,
*  where we will start and initialize our system.
*
**************************************************************/

#ifndef _FS_INIT_H
#define _FS_INIT_H

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fsLow.h"
#include "helperFunctions.h"

#define VCB_START_BLOCK 0
#define VCB_BLOCKS 1 // size of the VCB in blocks

// For directory entry type:
#define DIRECTORY 1 // directory entry is a directory
#define FILE 0 // directory entry is a file
#define FREE_ENTRY -1 // directory entry has no type yet and is free to use

#define MAX_DIRECTORY_ENTRIES 52 // maximum directory entries in a directory
#define MAX_DE_NAME_LENGTH 64 // maximum length of a directory entry's name

typedef struct VCB {
    uint64_t num_blocks; // total number of blocks in volume
    uint64_t block_size; // size of a block in bytes
    uint64_t free_space_start_block; // block at which free space starts
    uint64_t num_free_blocks; // number of free blocks remaining

    uint64_t bitmap_start_block; // the block the bitmap starts on
    uint64_t bitmap_blocks; // size of the bitmap in blocks
    uint64_t root_dir_start_block; // the block the root directory starts on
    uint64_t dir_blocks; // size of a directory in blocks

    uint64_t signature; // magic number used to tell if the volume is initialized
} VCB;

#pragma pack(1) // remove the padding
typedef struct dir_entry {
	char name[MAX_DE_NAME_LENGTH]; // identifier for the entry
	uint64_t start_block; // the starting block of the entry
	uint64_t size; // size of entry in bytes
	int type; // 1 if entry is a directory, 0 if a file, -1 if entry is free
    
	time_t creation_date; // date the entry was created
	time_t last_modified; // date the entry was last modified
	time_t last_opened; // date the entry was last opened
} dir_entry;

// The VCB and bitmap will be accessible and shared by every file
extern VCB *vcb;
extern uint32_t *bitmap;

uint64_t cwd_start_block; // the start block of the cwd

/* Initializes the bitmap and some of vcb's data members related to the bitmap.
 * Returns ERROR if an error occurred when initializing the bitmap.
 * Returns SUCCESS otherwise. */
int initBitmap();

/* Initializes the root directory and some of vcb's data members related
 * to the root directory. Returns ERROR if an error occurred when initializing
 * the root directory. Returns SUCCESS otherwise. */
int initRootDirectory();

/* Sets cwd's start block. Returns ERROR if start_block is invalid.
 * Returns SUCCESS otherwise. */
int setCWDstartBlock(uint64_t start_block);

/* Returns cwd's start block. */
uint64_t getCWDstartBlock();

#endif