/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
* 
* File: b_io.c
*
* Description: Functions for the key file I/O operations.
*
**************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include "b_io.h"

#define MIN_FREE_CONT_BLOCKS 5 // minimum free contiguous blocks to do a file write
#define MAX_FCBS 20 // maximum number of file descriptors allowed concurrently

typedef struct b_fcb {
	char *buf; // holds the open file buffer. buf is NULL when the fcb element is free
	int buf_index; // holds the current position in the buffer
	int buf_valid_bytes; // holds how many valid bytes there are in the buffer

	// the file pointer. it dictates where read/writes are done.
	// measured in bytes from the start of the file
	uint64_t file_offset;
	uint64_t file_bytes; // size of the file in bytes
	uint64_t file_start_block; // starting block of the file
	uint64_t file_num_blocks; // size of the file in blocks

	uint64_t parent_dir_start_block; // start block of the parent directory of the file
	// the directory entry index of the file in the parent directory. also used as a
	// flag for a read error in b_write. set to ERROR when a read error occurs.
	int entry_index;
	char filename[MAX_DE_NAME_LENGTH]; // the name of the file

	int flags; // flag for whether we are reading, writing, etc.
	int is_new_file; // flag for whether the file existed previously
	
	// flag that indicates whether to stop reading or writing,
	// e.g. end of file, end of free space, or an error occurred
	int stop;
} b_fcb;

/* Prints the data members in the FCB. Used for debugging. */
void printFCBcontents(b_fcb *fcb);

b_fcb fcb_array[MAX_FCBS];
int startup = FALSE; // whether the FCB has been initialized
uint64_t block_size; // size of a block in bytes, not necessarily 512

/* Initializes our file system. */
void b_init() {
	// initialize fcb_array to all free
	for (int i = 0; i < MAX_FCBS; i++) fcb_array[i].buf = NULL;

	block_size = vcb->block_size; // init block size
	startup = TRUE;
}

/* Gets a free FCB element. Returns the free FCB element's index,
 * or returns ERROR if all FCB elements are in use.
 * Not thread safe, but do not worry about it for this assignment. */
b_io_fd b_getFCB() {
	for (int i = 0; i < MAX_FCBS; i++) {
		if (fcb_array[i].buf == NULL) return i;
	}

	return ERROR; // all FCB elements are in use
}

/* Modification of interface for this assignment, flags match the Linux flags for open:
 * O_RDONLY, O_WRONLY, or O_RDWR. Also O_APPEND, O_CREAT, and O_TRUNC. */
b_io_fd b_open(char *filename, int flags) {
	if (startup == FALSE) b_init(); // initialize our system
	
	dir_entry *parent_dir = NULL; // holds the parent/temp directory
	char *basename = NULL; // holds the name of the file

	char *buf[block_size]; // holds the current block's contents. used for O_APPEND
	int buf_index; // the current position in buf
	int buf_valid_bytes; // how many valid bytes are in the buffer

	uint64_t file_offset; // where the reading/writing will start from
	uint64_t file_bytes; // size of the file in bytes
	uint64_t file_start_block; // start block for the file
	uint64_t file_num_blocks; // number of blocks the file takes up

	long long parent_dir_start_block; // the start block of the file's parent directory
	int entry_index; // the directory entry index in parent_dir that refers to the file
	int valid_flag = FALSE; // flag for whether the required flags were entered
	int is_new_file = FALSE; // flag for whether the file is new

	parent_dir_start_block = getParentBasenameStartBlock(filename);
	if (parent_dir_start_block == ERROR) {
		printf ("Error getting the parent's start block. ");
		goto free_and_return_error;
	}

	// read into parent_dir
	parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "b_open init parent_dir") == ERROR) {
		goto free_and_return_error;
	}

	if ((flags & O_WRONLY) || (flags & O_RDWR)) { // write mode
		valid_flag = TRUE;

		// get the name of the new file. basename must be freed.
		basename = getBasename(filename);
		if (!basename) { // no basename, i.e. path was "/"
			printf("You can only write to/overwrite files. ");
			goto free_and_return_error;
		}

		// check length of the new file's name
		if (strnlen(basename, MAX_DE_NAME_LENGTH) >= MAX_DE_NAME_LENGTH) {
			printf("A filename can be at most %d characters long. ", MAX_DE_NAME_LENGTH - 1);
			goto free_and_return_error;
		}

		int file_already_exists = FALSE; // flag for if a file already exists

		// searches in the parent dir and checks if a file with the same name already exists
		entry_index = getDirEntryIndexByName(parent_dir, basename);
		if (entry_index == ERROR) { // error
			printf("Error getting the directory entry index. ");
			goto free_and_return_error;
		} else if (entry_index != NOT_FOUND) { // file already exists
			if (parent_dir[entry_index].type != FILE) { // can only write to files
				printf("You can only write to/overwrite files. ");
				goto free_and_return_error;
			}

			file_already_exists = TRUE;

			file_bytes = parent_dir[entry_index].size;
			file_start_block = parent_dir[entry_index].start_block;
			file_num_blocks = ceilingDivide(file_bytes, block_size);
		}

		buf_valid_bytes = block_size;

		// append to the end of the file
		if (file_already_exists && (flags & O_APPEND)) {
			if (file_bytes <= 0) { // if appending to an empty file
				// find some free blocks for the file to be written to,
				// since empty files are not stored on disk
				file_start_block = getContiguousFreeBlocks(MIN_FREE_CONT_BLOCKS);
				if (file_start_block == UNSIGNED_ERROR) {
					printf("Not enough contiguous free blocks on disk for a new file. ");
					goto free_and_return_error;
				}

				buf_index = 0;
				file_offset = 0;
				file_bytes = 0;
				file_num_blocks = 0;

				// next time we search for free space we should try to fill in any gaps
				// that appeared if file_num_blocks < MIN_FREE_CONT_BLOCKS
				modStartBlockIndex(-MIN_FREE_CONT_BLOCKS);
			} else { // appending to a non-empty file
				uint64_t cur_block_in_vol = file_start_block + (file_bytes / block_size);
				buf_index = file_bytes % block_size;
				file_offset = file_bytes;

				// read the last block of the file into buf, if its not empty.
				// if we do not do this, we will write garbage data into the disk.
				if (buf_index > 0) {
					if (customLBAread(buf, 1, cur_block_in_vol, "b_open append") == ERROR) {
						goto free_and_return_error;
					}
				}
			}
		} // truncate the old file and write over it
		else if (file_already_exists && (flags & O_TRUNC)) {
			// we truncate the file to be overwritten first by making 
			// its size and start block 0. then we free its blocks on disk.
			parent_dir[entry_index].start_block = 0;
			parent_dir[entry_index].size = 0;

			// after modifying parent_dir, update it in disk
			if (customLBAwrite(parent_dir, vcb->dir_blocks, parent_dir[0].start_block,
                "b_open O_TRUNC update parent_dir") == ERROR) {
				goto free_and_return_error;
			}

			// only modify the bitmap if the file took up space on disk
			if (file_num_blocks > 0) {
				for (int i = 0; i < file_num_blocks; i++) {
					markBlockFree(bitmap, file_start_block + i);
				}
				vcb->num_free_blocks += file_num_blocks;

				// Write vcb to disk after updating vcb->num_free_blocks.
				if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK,
				    "b_open O_TRUNC update VCB") == ERROR) {
					goto free_and_return_error;
				}

				// write to disk the updated bitmap
				if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
                    "b_open O_TRUNC update bitmap") == ERROR) {
					goto free_and_return_error;
				}
			}

			// find some free blocks for the file to be written to
			file_start_block = getContiguousFreeBlocks(MIN_FREE_CONT_BLOCKS);
			if (file_start_block == UNSIGNED_ERROR) {
				printf("Not enough contiguous free blocks on disk for a new file. ");
				goto free_and_return_error;
			}

			buf_index = 0;
			file_offset = 0;
			file_bytes = 0;
			file_num_blocks = 0;

			// next time we search for free space we should try to fill in any gaps
			// that appeared if file_num_blocks < MIN_FREE_CONT_BLOCKS
			modStartBlockIndex(-MIN_FREE_CONT_BLOCKS);
		} else if (file_already_exists) { // write to existing file starting at file offset 0
			if (file_bytes <= 0) { // if writing to an empty file
				// find some free blocks for the file to be written to,
				// since empty files are not stored on disk
				file_start_block = getContiguousFreeBlocks(MIN_FREE_CONT_BLOCKS);
				if (file_start_block == UNSIGNED_ERROR) {
					printf("Not enough contiguous free blocks on disk for a new file. ");
					goto free_and_return_error;
				}

				buf_index = 0;
				file_offset = 0;
				file_bytes = 0;
				file_num_blocks = 0;

				// next time we search for free space we should try to fill in any gaps
				// that appeared if file_num_blocks < MIN_FREE_CONT_BLOCKS
				modStartBlockIndex(-MIN_FREE_CONT_BLOCKS);
			} else { // if writing to a non-empty file
				buf_index = 0;
				file_offset = 0;
			}
		} else { // file does not exist. so make a new one
			if (!(flags & O_CREAT)) {
				printf("You cannot create a new file without O_CREAT set. ");
				goto free_and_return_error;
			}

			// find a free directory entry in the parent directory
 			entry_index = getDirFreeEntryIndex(parent_dir);
			if (entry_index == ERROR) { // error
				printf("Error getting the directory entry index. ");
				goto free_and_return_error;
			} else if (entry_index == NOT_FOUND) { // parent dir full
				printf("The directory you are trying to make the new file in is full. ");
				goto free_and_return_error;
			}

			// find some free blocks for the file to be written to
			file_start_block = getContiguousFreeBlocks(MIN_FREE_CONT_BLOCKS);
			if (file_start_block == UNSIGNED_ERROR) {
				printf("Not enough contiguous free blocks on disk for a new file. ");
				goto free_and_return_error;
			}

			buf_index = 0;
			file_offset = 0;
			file_bytes = 0;
			file_num_blocks = 0;
			is_new_file = TRUE;

			// next time we search for free space we should try to fill in any gaps
			// that appeared if file_num_blocks < MIN_FREE_CONT_BLOCKS
			modStartBlockIndex(-MIN_FREE_CONT_BLOCKS);
		}
	}
	
	if ((flags == O_RDONLY) || (flags & O_RDWR)) { // read mode
		valid_flag = TRUE;

		// get the name of the file to be read. basename must be freed.
		basename = getBasename(filename);
		if (!basename) { // no basename, i.e. path was "/"
			printf("You can only read from files. ");
			goto free_and_return_error;
		}

		// searches in the parent dir and looks for the file to read
		entry_index = getDirEntryIndexByName(parent_dir, basename);
		if (entry_index == ERROR || entry_index == NOT_FOUND) {  // file not found
			printf("The file '%s' could not be found. ", basename);
			goto free_and_return_error;
		}

		if (parent_dir[entry_index].type != FILE) { // can only read from files
			printf("You can only read from files. ");
			goto free_and_return_error;
		}
		
		buf_index = 0;
		buf_valid_bytes = 0;
		file_offset = 0;
		file_bytes = parent_dir[entry_index].size;
		file_start_block = parent_dir[entry_index].start_block;
		file_num_blocks = ceilingDivide(file_bytes, block_size);
	}

	if (!valid_flag) { // if flags do not include either read, write, or read/write
		printf("Incorrect flags set. ");
		goto free_and_return_error;
	}

	b_io_fd fd = b_getFCB(); // get a free file descriptor
	if (fd == ERROR) { // if all FCBs are used
		printf("Maximum open file limit reached. ");
		goto free_and_return_error;
	}

	fcb_array[fd].buf = malloc(block_size);
	memcpy(fcb_array[fd].buf, buf, block_size);
	fcb_array[fd].buf_index = buf_index;
	fcb_array[fd].buf_valid_bytes = buf_valid_bytes;

	fcb_array[fd].file_offset = file_offset;
	fcb_array[fd].file_bytes = file_bytes;
	fcb_array[fd].file_start_block = file_start_block;
	fcb_array[fd].file_num_blocks = file_num_blocks;

	fcb_array[fd].parent_dir_start_block = parent_dir_start_block;
	fcb_array[fd].entry_index = entry_index;
	strcpy(fcb_array[fd].filename, basename);

	fcb_array[fd].flags = flags;
	fcb_array[fd].is_new_file = is_new_file;
	fcb_array[fd].stop = FALSE;

	free(parent_dir);
	parent_dir = NULL;
	free(basename);
	basename = NULL;

	return fd; // success

	free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
	free(parent_dir);
	parent_dir = NULL;
	free(basename);
	basename = NULL;

	printf("File open failed.\n");
	return ERROR;
}

int b_seek(b_io_fd fd, off_t offset, int whence) {
	if (startup == FALSE) b_init(); // initialize our system

	if (fd < 0 || fd >= MAX_FCBS) return ERROR; // invalid file descriptor

	long long file_offset = fcb_array[fd].file_offset;

	if (whence == SEEK_SET) { // the file offset is set to offset
		file_offset = offset;
	} else if (whence == SEEK_CUR) { // the file offset is modified by offset
		file_offset += offset;
	} else if (whence == SEEK_END) { // the file offset is set to the file size plus offset
		file_offset = fcb_array[fd].file_bytes + offset;
	} else { // invalid or unsupported whence directive
		printf("An invalid or unsupported whence directive was given. ");
		goto free_and_return_error;
	}

	if (file_offset < 0) {
		printf("The resulting file offset cannot be negative. ");
		goto free_and_return_error;
	} // if offset is past end of volume
	else if (fcb_array[fd].file_start_block + (file_offset / block_size) >= vcb->num_blocks) {
		printf("The resulting file offset goes past the end of the volume. ");
		goto free_and_return_error;
	} else if (file_offset > INT_MAX) { // overflow
		printf("The resulting offset cannot cannot be represented in a 32-bit integer. ");
		goto free_and_return_error;
	}

	fcb_array[fd].file_offset = file_offset;

	// read the current block in volume into the fcb buf after seeking,
	// if it is safe to do so
	if (fcb_array[fd].file_offset <= fcb_array[fd].file_bytes) {
		fcb_array[fd].buf_index = fcb_array[fd].file_offset % block_size;

		if (fcb_array[fd].buf_index > 0) { // do not read if there is nothing to read
			uint64_t cur_vol_block = fcb_array[fd].file_start_block
		                           + (fcb_array[fd].file_offset / block_size);

			if (customLBAread(fcb_array[fd].buf, 1, cur_vol_block, "b_seek") == ERROR) {
				goto free_and_return_error;
			}

			fcb_array[fd].buf_valid_bytes = block_size;
		}
	}

	return file_offset; // guaranteed to fit in a 32-bit integer

	free_and_return_error: // Label for error handling. Return ERROR.

	printf("Seek failed.\n");
	return ERROR;
}

/* Diagram for both b_write and b_read:
 *
 * Filling the callers request is broken into three parts:
 * Part 1 is what can be filled from the current buffer, which may or may not be enough
 * Part 2 is after using what was left in our buffer there is still 1 or more block
 *        size chunks needed to fill the callers request. This represents the number of
 *        bytes in multiples of the blocksize.
 * Part 3 is a value less than blocksize which is what remains to copy to the callers buffer
 *        after fulfilling part 1 and part 2. This would always be filled from a refill 
 *        of our buffer.
 *  +-------------+------------------------------------------------+--------+
 *  |             |                                                |        |
 *  | filled from |  filled direct in multiples of the block size  | filled |
 *  | existing    |                                                | from   |
 *  | buffer      |                                                |refilled|
 *  |             |                                                | buffer |
 *  |             |                                                |        |
 *  | Part1       |  Part 2                                        | Part3  |
 *  +-------------+------------------------------------------------+--------+
*/

/* Sources: Robert Bierman's explanation of Assignment 2b. */
int b_write(b_io_fd fd, char *buffer, int count) {
	if (startup == FALSE) b_init(); // Initialize our system

	if (fd < 0 || fd >= MAX_FCBS) return ERROR; // invalid file descriptor
	else if (fcb_array[fd].buf == NULL) { // write called before open
		printf("File not open for this descriptor. File write failed.\n");
		return ERROR;
	} else if (!((fcb_array[fd].flags & O_WRONLY) || (fcb_array[fd].flags & O_RDWR))) {
		printf("The flags were not set to write mode. File write failed.\n");
		return ERROR;
	} else if (count < 0) { // error with read. do not write anything
		printf("Could not read from the file. ");
		fcb_array[fd].entry_index = ERROR; // tells b_close not to make a new file
		fcb_array[fd].stop = TRUE;
		return ERROR;
	} else if (count == 0) return 0; // no bytes to write
	// end of free space for the file or an error occurred. do not write any more
	else if (fcb_array[fd].stop) {
		printf("Warning: The file was only partially written to disk. This happened "
		       "either because the next contiguous block was marked as used, "
			   "or an error has occurred.\n");
		return 0;
	}

	// repositions the file pointer if it exceeded the size of the file due to a seek.
	// recalculate the fcb buf index and load in the file's last block into the fcb buf
	if (fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
		fcb_array[fd].file_offset = fcb_array[fd].file_bytes;
		fcb_array[fd].buf_index = fcb_array[fd].file_offset % block_size;

		if (fcb_array[fd].buf_index > 0) { // do not read if there is nothing to read
			uint64_t cur_vol_block = fcb_array[fd].file_start_block
		                           + (fcb_array[fd].file_offset / block_size);

			if (customLBAread(fcb_array[fd].buf, 1, cur_vol_block,
			    "b_write reposition offset") == ERROR) {
				goto free_and_return_error;
			}
		}
	}

	int bytes_transferred = 0; // bytes transferred to the fcb buffer
	int part1, part2, part3; // the three potential copy lengths, in bytes
	int num_blocks_to_copy; // how many blocks to copy in part2

	// free bytes left in the fcb buffer
	int fcb_buf_rem_bytes = block_size - fcb_array[fd].buf_index;

	// the current block in the volume we are writing to
	uint64_t cur_vol_block = fcb_array[fd].file_start_block
	                       + (fcb_array[fd].file_offset / block_size);

	uint64_t file_end_block; // the last block of the file
	if (fcb_array[fd].file_num_blocks > 0) {
		file_end_block = fcb_array[fd].file_start_block + fcb_array[fd].file_num_blocks - 1;
	} else file_end_block = fcb_array[fd].file_start_block;

	// handle the case where file_offset is a multiple of block_size. cur_vol_block can
	// be greater than file_end_block, and if cur_vol_block is used by some other file,
	// we should not write anything to cur_vol_block. therefore we abort and return 0.
	if ((cur_vol_block > file_end_block) && (getBlockStatus(bitmap, cur_vol_block) == USED)) {
		// mark the buffer as empty if it was not empty already to stop
		// b_close from writing cur_vol_block to disk
		fcb_array[fd].buf_index = 0;
		fcb_array[fd].stop = TRUE;
		return 0;
	} // if the next block after the file's end block is used,
	// we should check that count does not result in us
	// writing into the next block. if it does, set the stop flag.
	else if ((cur_vol_block == file_end_block)
	    && (getBlockStatus(bitmap, file_end_block + 1) == USED)
	    && (count > fcb_buf_rem_bytes)) {
		fcb_array[fd].stop = TRUE;
	}

	// fill up the fcb buffer to its capacity and write it to disk.
	// since count > fcb_buf_rem_bytes, the caller's buffer will have enough data to copy.
	// Return bytes_transferred and stop any further writes because the next block is used
	if (fcb_array[fd].stop) {
		memcpy(fcb_array[fd].buf + fcb_array[fd].buf_index, buffer, fcb_buf_rem_bytes);
		fcb_array[fd].buf_index += fcb_buf_rem_bytes;
		bytes_transferred += fcb_buf_rem_bytes;
		
		if (customLBAwrite(fcb_array[fd].buf, 1, cur_vol_block,
		    "b_write stop flag") == ERROR) {
			goto free_and_return_error;
		}

		fcb_array[fd].file_offset += block_size;
		cur_vol_block = fcb_array[fd].file_start_block
	                  + (fcb_array[fd].file_offset / block_size);
		fcb_array[fd].buf_index = 0;

		// if we wrote past the size of the file, we update our file size accordingly
		if (fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
			fcb_array[fd].file_bytes = fcb_array[fd].file_offset;
		}

		// check if we filled up a free block. if we did, increase our file block count
		if ((cur_vol_block > file_end_block)
		    && (getBlockStatus(bitmap, cur_vol_block - 1) == FREE)) {
			fcb_array[fd].file_num_blocks++;
			file_end_block++;
		}

		return bytes_transferred;
	}

	if (count <= fcb_buf_rem_bytes) { // fcb buffer can fit count
		part1 = count;
		part2 = 0;
		part3 = 0;
	} else { // fcb buffer cannot fit count
		part1 = fcb_buf_rem_bytes;
		part3 = count - fcb_buf_rem_bytes;

		num_blocks_to_copy = part3 / block_size;
		part2 = num_blocks_to_copy * block_size; // bytes directly writable
		part3 -= part2; // part3 % block_size, the residue after part2
	}

	if (part1 > 0) {
		memcpy(fcb_array[fd].buf + fcb_array[fd].buf_index, buffer, part1);
		fcb_array[fd].buf_index += part1;
		bytes_transferred += part1;
	}

	// fcb buffer completely full upon entering part 2
	if (part2 > 0) {
		// write the full buffer after part 1
		if (customLBAwrite(fcb_array[fd].buf, 1, cur_vol_block,
			"b_write in part 2, from part 1's buffer") == ERROR) {
			goto free_and_return_error;
		}

		fcb_array[fd].file_offset += block_size;
		cur_vol_block = fcb_array[fd].file_start_block
	                  + (fcb_array[fd].file_offset / block_size);
		fcb_array[fd].buf_index = 0;
		
		if (fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
			fcb_array[fd].file_bytes = fcb_array[fd].file_offset;
		}

		if ((cur_vol_block > file_end_block)
		    && (getBlockStatus(bitmap, cur_vol_block - 1) == FREE)) {
			fcb_array[fd].file_num_blocks++;
			file_end_block++;
		}

		int temp_part2 = 0;
		for (int i = 0; i < num_blocks_to_copy; i++) {
			// if this block is used by some other file, stop writing
			if ((cur_vol_block > file_end_block)
			    && (getBlockStatus(bitmap, cur_vol_block) == USED)) {
				fcb_array[fd].buf_index = 0;
				fcb_array[fd].stop = TRUE;
				return bytes_transferred;
			}

			if (customLBAwrite(buffer + part1 + temp_part2, 1, cur_vol_block,
			    "b_write part 2, direct writes") == ERROR) {
				goto free_and_return_error;
			}
			
			fcb_array[fd].file_offset += block_size;
			cur_vol_block = fcb_array[fd].file_start_block
	                      + (fcb_array[fd].file_offset / block_size);

			if (fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
				fcb_array[fd].file_bytes = fcb_array[fd].file_offset;
			}

			if ((cur_vol_block > file_end_block)
		    	&& (getBlockStatus(bitmap, cur_vol_block - 1) == FREE)) {
				fcb_array[fd].file_num_blocks++;
				file_end_block++;
			}

			bytes_transferred += block_size;
			temp_part2 += block_size;
		}

		part2 = temp_part2;
	}

	// part3 will be less than block_size
	if (part3 > 0) {
		// if part2 > 0, the fcb buffer will be empty.
		// otherwise, the fcb buffer will be full coming in.
		if (part2 <= 0) {
			if (customLBAwrite(fcb_array[fd].buf, 1, cur_vol_block,
			    "b_write part3 where part2 <= 0") == ERROR) {
				goto free_and_return_error;
			}
			
			fcb_array[fd].file_offset += block_size;
			cur_vol_block = fcb_array[fd].file_start_block
	        	          + (fcb_array[fd].file_offset / block_size);
			fcb_array[fd].buf_index = 0;

			if (fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
				fcb_array[fd].file_bytes = fcb_array[fd].file_offset;
			}

			if ((cur_vol_block > file_end_block)
			    && (getBlockStatus(bitmap, cur_vol_block - 1) == FREE)) {
				fcb_array[fd].file_num_blocks++;
				file_end_block++;
			}
		}
		
		// no guarantees that cur_vol_block is not used by some other file
		if ((cur_vol_block > file_end_block)
			&& (getBlockStatus(bitmap, cur_vol_block) == USED)) {
			fcb_array[fd].buf_index = 0;
			fcb_array[fd].stop = TRUE;
			return bytes_transferred;
		}

		// at this point, the fcb buffer is empty
		memcpy(fcb_array[fd].buf + fcb_array[fd].buf_index, buffer + part1 + part2, part3);
		fcb_array[fd].buf_index += part3;
		bytes_transferred += part3;
	}

	return bytes_transferred; // success

	free_and_return_error: // Label for error handling. Set stop to TRUE and return ERROR.
	fcb_array[fd].stop = TRUE; // stop any further writes

	printf("Aborting file write.\n");
	return ERROR;
}

/* Sources: Robert Bierman's explanation of Assignment 2b */
int b_read(b_io_fd fd, char *buffer, int count) {
	if (startup == FALSE) b_init(); // Initialize our system

	if (fd < 0 || fd >= MAX_FCBS) return ERROR; // if invalid file descriptor
	else if (fcb_array[fd].buf == NULL) { // read called before open
		printf("File not open for this descriptor. File read failed.\n");
		return ERROR;
	} // check if the read flag is on
	else if (!((fcb_array[fd].flags == O_RDONLY) || (fcb_array[fd].flags & O_RDWR))) {
		printf("The flags were not set to read mode. File read failed.\n");
		return ERROR;
	} else if (count <= 0) return 0; // if no bytes to read
	// if the file offset is at end of file, there is nothing to read
	else if (fcb_array[fd].file_offset >= fcb_array[fd].file_bytes) return 0;
	else if (fcb_array[fd].stop) return 0; // end of file or an error occurred. stop reading

	int part1, part2, part3; // the three potential copy lengths, in bytes
	int num_blocks_to_copy; // how many blocks to copy in part 2

	// bytes available to copy from fcb buffer
	int fcb_buf_rem_bytes = fcb_array[fd].buf_valid_bytes - fcb_array[fd].buf_index;
	// the current block in the volume we are reading from
	uint64_t cur_vol_block = fcb_array[fd].file_start_block
	                       + (fcb_array[fd].file_offset / block_size);

	// trims down count such that it will not read past the end of the file.
	// if the trim occurs, set the stop flag so we do not read past the end of file.
	// setting the stop flag is not necessary, it is just an extra safety measure
	if (count + fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
		fcb_array[fd].stop = TRUE;
		count = fcb_array[fd].file_bytes - fcb_array[fd].file_offset;
	}

	if (count <= fcb_buf_rem_bytes) { // if we can fill directly from fcb's buffer
		part1 = count;
		part2 = 0;
		part3 = 0;
	} else { // if the caller needs more bytes than what was in the fcb buffer
		part1 = fcb_buf_rem_bytes;
		part3 = count - fcb_buf_rem_bytes;

		num_blocks_to_copy = part3 / block_size;
		part2 = num_blocks_to_copy * block_size; // bytes directly readable
		part3 -= part2; // part3 % block_size, the residue after part2
	}

	if (part1 > 0) {
		memcpy(buffer, fcb_array[fd].buf + fcb_array[fd].buf_index, part1);
		fcb_array[fd].buf_index += part1;
		fcb_array[fd].file_offset += part1;
		cur_vol_block = fcb_array[fd].file_start_block
	                  + (fcb_array[fd].file_offset / block_size);
	}

	// fcb buffer completely copied upon entering part 2
	if (part2 > 0) {
		// directly copy num_blocks_to_copy to the caller's buffer
		if (customLBAread(buffer + part1, num_blocks_to_copy, cur_vol_block,
		    "b_read part2") == ERROR) {
			goto free_and_return_error;
		}

		fcb_array[fd].file_offset += num_blocks_to_copy * block_size;
		cur_vol_block = fcb_array[fd].file_start_block
	                  + (fcb_array[fd].file_offset / block_size);
	}

	// fcb buffer empty or completely copied upon entering part 3.
	// part3 will be less than block_size
	if (part3 > 0) {
		// caller still needs more bytes, so we refill the fcb buffer
		if (customLBAread(fcb_array[fd].buf, 1, cur_vol_block,
		    "b_read part3") == ERROR) {
			goto free_and_return_error;
		}

		fcb_array[fd].buf_index = 0;
		fcb_array[fd].buf_valid_bytes = block_size;

		// fcb buffer has content so we copy some to the caller's buffer
		memcpy(buffer + part1 + part2, fcb_array[fd].buf + fcb_array[fd].buf_index, part3);
		fcb_array[fd].buf_index += part3;
		fcb_array[fd].file_offset += part3;
		cur_vol_block = fcb_array[fd].file_start_block
	                  + (fcb_array[fd].file_offset / block_size);
	}

	return part1 + part2 + part3; // success

	free_and_return_error: // Label for error handling. Set stop to TRUE and return ERROR.
	fcb_array[fd].stop = TRUE; // stop any further reads

	printf("Aborting file read.\n");
	return ERROR;
}

void b_close(b_io_fd fd) {
	if (startup == FALSE) { // FCB not initialized
		printf("Error: Close called before the file control block was initialized. "
		       "File close failed.\n");
		return;
	} else if (fd < 0 || fd >= MAX_FCBS) return; // invalid file descriptor
	else if (fcb_array[fd].buf == NULL) { // close called before open
		printf("File not open for this descriptor. File close failed.\n");
		return;
	}

	dir_entry *parent_dir = NULL; // parent_dir of file

	if ((fcb_array[fd].flags & O_WRONLY) || (fcb_array[fd].flags & O_RDWR)) { // write mode
		// if an error occurred when reading the file, do not write anything
		if (fcb_array[fd].entry_index == ERROR) {
			if (fcb_array[fd].is_new_file) printf("No files were created.\n");
			else printf("No files were modified.\n");

			free(fcb_array[fd].buf);
			fcb_array[fd].buf = NULL;
			return;
		} else if (fcb_array[fd].buf_index > 0) { // if buffer has content in it
			uint64_t file_end_block; // the last block of the file
			if (fcb_array[fd].file_num_blocks > 0) {
				file_end_block = fcb_array[fd].file_start_block
				               + fcb_array[fd].file_num_blocks - 1;
			} else file_end_block = fcb_array[fd].file_start_block;

			// cur_vol_block is guaranteed to not overwrite another file's block in this case
			uint64_t cur_vol_block = fcb_array[fd].file_start_block
	                               + (fcb_array[fd].file_offset / block_size);

			// if the file pointer points to a new block, then we write the buffer's
			// contents to a new block on disk, at cur_vol_block
			if (cur_vol_block > file_end_block) {
				if (customLBAwrite(fcb_array[fd].buf, 1, cur_vol_block,
			    	"b_close write") == ERROR) {
					goto free_and_print_error;
				}

				fcb_array[fd].file_offset += fcb_array[fd].buf_index;
				cur_vol_block = fcb_array[fd].file_start_block
	                          + (fcb_array[fd].file_offset / block_size);
		        fcb_array[fd].buf_index = 0;

				if (fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
					fcb_array[fd].file_bytes = fcb_array[fd].file_offset;
				}

				fcb_array[fd].file_num_blocks++;
				file_end_block++;
			} // current vol block already has some of the file's contents.
			// should only overwrite what is needed and not add garbage to the file,
			// thus we need to read from disk to save the data we need to keep.
			//
			// or, this is the first block of a new file. its fine to read
			// from disk first since we were writing there anyway.
			else {
				char *buf[block_size];
				if (customLBAread(buf, 1, cur_vol_block, "b_close buf") == ERROR) {
					goto free_and_print_error;
				}

				// where we start overwriting in the block
				int start = fcb_array[fd].file_offset % block_size;
				int end = fcb_array[fd].buf_index; // where we stop overwriting in the block

				memcpy(buf + start, fcb_array[fd].buf + start, end - start);
				
				// write the updated buf to disk
				if (customLBAwrite(buf, 1, cur_vol_block, "b_close buffered write") == ERROR) {
					goto free_and_print_error;
				}

				fcb_array[fd].file_offset += end - start;
				cur_vol_block = fcb_array[fd].file_start_block
	                          + (fcb_array[fd].file_offset / block_size);
		        fcb_array[fd].buf_index = 0;

				if (fcb_array[fd].file_offset > fcb_array[fd].file_bytes) {
					fcb_array[fd].file_bytes = fcb_array[fd].file_offset;
				}

				// if we wrote to a free block, we increase our file's block count
				if (getBlockStatus(bitmap, cur_vol_block) == FREE) {
					fcb_array[fd].file_num_blocks++;
				}
			}
		}

		// buffer is empty now; all data written to disk. load up parent_dir
		parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
		if (customLBAread(parent_dir, vcb->dir_blocks, fcb_array[fd].parent_dir_start_block,
		    "b_close write mode parent_dir") == ERROR) {
			goto free_and_print_error;
		}

		// used for determining if any new blocks were used in a write.
		// can contain garbage, so this value is only used for checking if an existing
		// file was modified.
		uint64_t previous_size = parent_dir[fcb_array[fd].entry_index].size;

		// update parent_dir. if the file is empty, i.e. file_bytes <= 0,
		// set its start block to 0.
		time_t curr_time = time(NULL);
		strcpy(parent_dir[fcb_array[fd].entry_index].name, fcb_array[fd].filename);
		parent_dir[fcb_array[fd].entry_index].start_block = (fcb_array[fd].file_bytes > 0)
														  ? fcb_array[fd].file_start_block : 0;
		parent_dir[fcb_array[fd].entry_index].size = fcb_array[fd].file_bytes;
		parent_dir[fcb_array[fd].entry_index].type = FILE;

		if (fcb_array[fd].is_new_file) {
			parent_dir[fcb_array[fd].entry_index].creation_date = curr_time;
		}
		
		parent_dir[fcb_array[fd].entry_index].last_modified = curr_time;
		parent_dir[fcb_array[fd].entry_index].last_opened = curr_time;

		// only update parent's last modified date if we created a new file
		if (fcb_array[fd].is_new_file) { 
			parent_dir[0].last_modified = curr_time;

			// if parent is root_dir, then also update root_dir[1] since root is its own parent
			if (parent_dir[0].start_block == vcb->root_dir_start_block) {
				parent_dir[1].last_modified = curr_time;
			}
		}

		// after modifying parent_dir, update it in disk
		if (customLBAwrite(parent_dir, vcb->dir_blocks, fcb_array[fd].parent_dir_start_block,
		    "b_close write mode parent_dir") == ERROR) {
			goto free_and_print_error;
		}

		// only modify the bitmap if the file took up space on disk
		if (fcb_array[fd].file_num_blocks > 0) {
			for (int i = 0; i < fcb_array[fd].file_num_blocks; i++) {
				markBlockUsed(bitmap, fcb_array[fd].file_start_block + i);
			}
			
			if (fcb_array[fd].is_new_file) {
				vcb->num_free_blocks -= fcb_array[fd].file_num_blocks;
			} else { // only modify num_free_blocks by how many free blocks were used
				uint64_t previous_num_blocks = ceilingDivide(previous_size, block_size);
				vcb->num_free_blocks -= fcb_array[fd].file_num_blocks - previous_num_blocks;
			}

			// Write vcb to disk after updating vcb->num_free_blocks
			if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK, "b_close vcb") == ERROR) {
				goto free_and_print_error;
			}

			// write to disk the updated bitmap
			if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
                "b_close bitmap") == ERROR) {
				goto free_and_print_error;
			}
		}

		if (fcb_array[fd].is_new_file) {
			printf("The %lu-byte file '%s' was created.\n",
		           fcb_array[fd].file_bytes, fcb_array[fd].filename);
		} else {
			printf("The %lu-byte file '%s' was modified.\n",
		           fcb_array[fd].file_bytes, fcb_array[fd].filename);
		}
	}

	if ((fcb_array[fd].flags == O_RDONLY) || (fcb_array[fd].flags & O_RDWR)) { // read mode
		// load up parent_dir so we can update the last opened date for the file
		parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
		if (customLBAread(parent_dir, vcb->dir_blocks, fcb_array[fd].parent_dir_start_block,
		    "b_close read mode parent_dir") == ERROR) {
			goto free_and_print_error;
		}

		parent_dir[fcb_array[fd].entry_index].last_opened = time(NULL);

		// after modifying parent_dir, update it in disk
		if (customLBAwrite(parent_dir, vcb->dir_blocks, fcb_array[fd].parent_dir_start_block,
		    "b_close read mode parent_dir") == ERROR) {
			goto free_and_print_error;
		}
	}

	free(parent_dir);
	parent_dir = NULL;
	free(fcb_array[fd].buf);
	fcb_array[fd].buf = NULL;

	return; // success

	free_and_print_error: // Label for error handling. Free mallocs and close the file.
	free(parent_dir);
	parent_dir = NULL;
	free(fcb_array[fd].buf);
	fcb_array[fd].buf = NULL;

	printf("File close aborted.\n");
	return;
}

void printFCBcontents(b_fcb *fcb) {
	printf("\nFCB contents:\n"
	       "buf:\n%.*s\n"
		   "buf_index: %d\n"
		   "buf_valid_bytes: %d\n\n"
		   
		   "file_bytes: %lu\n"
		   "file_start_block: %lu\n"
		   "file_num_blocks: %lu\n"
		   "file_offset: %lu\n\n"

		   "parent_dir_start_block: %lu\n"
		   "entry_index: %d\n"
		   "filename: %s\n\n"
		   
		   "flags: 0x%x\n"
		   "is_new_file: %d\n"
		   "stop: %d\n\n",
		   fcb->buf_index, fcb->buf, fcb->buf_index, fcb->buf_valid_bytes,
		   fcb->file_bytes, fcb->file_start_block, fcb->file_num_blocks, fcb->file_offset,
		   fcb->parent_dir_start_block, fcb->entry_index, fcb->filename,
		   fcb->flags, fcb->is_new_file, fcb->stop);
}