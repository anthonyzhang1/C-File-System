/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: fsDirEntryOps.c
*
* Description: Functions for the shell commands pertaining to directory entries,
*  i.e. remove (rm), make directory (md), change directory (cd),
*  print working directory (pwd), and move (mv).
*
**************************************************************/

#include "fsInit.h"
#include "mfs.h"

int fs_mkdir(const char *pathname, mode_t mode) {
	dir_entry *parent_dir = NULL; // holds the parent/temp directory
	char *new_dir_name = NULL; // name of the new directory
	dir_entry *new_dir = NULL; // holds the new directory

	// get the start block of the new file's parent directory
	long long parent_dir_start_block = getParentBasenameStartBlock(pathname);
	if (parent_dir_start_block == ERROR) {
		printf ("Error getting the parent's start block. ");
		goto free_and_return_error;
	}

	// read from disk into parent_dir
	parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_mkdir init parent_dir") == ERROR) {
		goto free_and_return_error;
	}

	// get the name of the new directory. new_dir_name must be freed.
	new_dir_name = getBasename(pathname);
	if (!new_dir_name) { // path was "/"
		printf("The root directory already exists. ");
		goto free_and_return_error;
	}

	// check length of the new directory name
	if (strnlen(new_dir_name, MAX_DE_NAME_LENGTH) >= MAX_DE_NAME_LENGTH) {
		printf("Your directory name can be at most %d characters long. ",
		       MAX_DE_NAME_LENGTH - 1);
		goto free_and_return_error;
	}

	// searches in the parent dir and checks if a file with the same name already exists
	int entry_index = getDirEntryIndexByName(parent_dir, new_dir_name);
	if (entry_index == ERROR) { // error
		printf("Error getting the directory entry index. ");
		goto free_and_return_error;
	} else if (entry_index != NOT_FOUND) { // name already used by some other file
		printf("'%s' already exists. ", new_dir_name);
		goto free_and_return_error;
	}

	// at this point, we know that there are no name clashes within the parent directory
	// so, we search for a free entry to make our new directory in
	entry_index = getDirFreeEntryIndex(parent_dir);
	if (entry_index == ERROR) { // error
		printf("Error getting a free directory entry index. ");
		goto free_and_return_error;
	} else if (entry_index == NOT_FOUND) { // parent_dir is full
		printf("The directory you are trying to make the new directory in is full. ");
		goto free_and_return_error;
	}

	// Try to get enough contiguous free blocks in the volume for the directory.
	uint64_t dir_start_block = getContiguousFreeBlocks(vcb->dir_blocks);
	if (dir_start_block == UNSIGNED_ERROR) {
		printf("Not enough contiguous free blocks on disk for the new directory. ");
		goto free_and_return_error;
	}

	// at this point, there will be no name clashes and we know where the new entry will go.
	// use calloc instead of malloc because we dont want to accidentally read garbage data
	// as if it were valid data
	new_dir = calloc(vcb->dir_blocks, vcb->block_size);
	time_t curr_time = time(NULL);

	// initialize the '.' entry in the new directory
	strcpy(new_dir[0].name, ".");
	new_dir[0].start_block = dir_start_block;
	new_dir[0].size = MAX_DIRECTORY_ENTRIES * sizeof(dir_entry);
	new_dir[0].type = DIRECTORY;
	new_dir[0].creation_date = curr_time;
	new_dir[0].last_modified = curr_time;
	new_dir[0].last_opened = curr_time;

	// initialize the '..' entry in the new directory
	new_dir[1] = parent_dir[0];
	strcpy(new_dir[1].name, "..");
	new_dir[1].last_modified = curr_time;

    // initialize the remaining directory entries
	for (int i = 2; i < MAX_DIRECTORY_ENTRIES; i++) new_dir[i].type = FREE_ENTRY;

	// write the new directory to disk
	if (customLBAwrite(new_dir, vcb->dir_blocks, dir_start_block,
		"new directory make dir") == ERROR) {
		goto free_and_return_error;
	}

	// put new_dir into parent_dir and update parent_dir's last modified time
	parent_dir[entry_index] = new_dir[0];
	strcpy(parent_dir[entry_index].name, new_dir_name);
	parent_dir[0].last_modified = curr_time;
	
	// if parent is root_dir, then also update root_dir[1] since root is its own parent
	if (parent_dir[0].start_block == vcb->root_dir_start_block) {
		parent_dir[1].last_modified = curr_time;
	}

	// after modifying parent_dir, update it in the disk
	if (customLBAwrite(parent_dir, vcb->dir_blocks, parent_dir[0].start_block,
	    "writing parent dir in make dir") == ERROR) {
		goto free_and_return_error;
	}

	// mark the blocks the new directory will take up as used
	for (int i = 0; i < vcb->dir_blocks; i++) markBlockUsed(bitmap, dir_start_block + i);
	vcb->num_free_blocks -= vcb->dir_blocks;

	// Write vcb to disk after updating vcb->num_free_blocks.
	if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK, "fs_mkdir update VCB") == ERROR) {
		goto free_and_return_error;
	}

	// write to disk the updated bitmap
	if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
		"fs_mkdir update bitmap") == ERROR) {
		goto free_and_return_error;
	}

	free(parent_dir);
	parent_dir = NULL;
	free(new_dir_name);
	new_dir_name = NULL;
	free(new_dir);
	new_dir = NULL;
	
	return SUCCESS;
 	
	free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
	free(parent_dir);
	parent_dir = NULL;
	free(new_dir_name);
	new_dir_name = NULL;
	free(new_dir);
	new_dir = NULL;

	printf("Make directory failed.\n");
	return ERROR;
}

char* fs_getcwd(char *buf, size_t size) {
	dir_entry *curr_dir = NULL; // holds the current working directory

    curr_dir = malloc(vcb->dir_blocks * vcb->block_size);
    if (customLBAread(curr_dir, vcb->dir_blocks, getCWDstartBlock(),
        "fs_getcwd curr_dir init") == ERROR) {
		goto free_and_return_null;
    }

    // write cwd's absolute path into buf
    if (!getDirAbsPath(curr_dir, buf, size)) {
        printf("Error getting the absolute path of the current directory. ");
        goto free_and_return_null;
    }

    if (strlen(buf) >= size) {
        printf("The absolute path exceeded the buffer's size. "); 
        goto free_and_return_null;
    }

    free(curr_dir);
	curr_dir = NULL;

    return buf; // success

	free_and_return_null: // Label for error handling. Free the mallocs and return NULL.
	free(curr_dir);
	curr_dir = NULL;

	printf("Failed to get the current working directory.\n");
	return NULL;
}

int fs_setcwd(char *buf) {
	dir_entry *parent_dir = NULL; // holds the parent/temp directory
	char *basename = NULL; // name of the basename directory

    long long parent_dir_start_block = getParentBasenameStartBlock(buf);
	if (parent_dir_start_block == ERROR) {
		printf("Error getting the parent directory's start block. ");
		goto free_and_return_error;
	}

    // read from disk into parent_dir
	parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_setcwd init parent dir") == ERROR) {
		goto free_and_return_error;
	}

	// Gets the basename. basename must be freed.
	basename = getBasename(buf);
	if (!basename) { // path was "/", set cwd to root dir
		setCWDstartBlock(vcb->root_dir_start_block);
		return SUCCESS;
	}

	// searches in the parent dir and searches for an entry with the matching name
	int entry_index = getDirEntryIndexByName(parent_dir, basename);
	if (entry_index == ERROR || entry_index == NOT_FOUND) { // dir not found
		printf("'%s' not found. ", basename);
		goto free_and_return_error;
	}

    // now we have a valid path
    if (parent_dir[entry_index].type != DIRECTORY) { // cannot cd into non-directories
        printf("'%s' is not a directory. ", basename);
        goto free_and_return_error;
    }

    // the path is valid and leads to a directory. we can set cwd's start block
    if (setCWDstartBlock(parent_dir[entry_index].start_block) == ERROR) {
        printf("Error setting the current directory start block. ");
        goto free_and_return_error;
    }
    
	free(parent_dir);
	parent_dir = NULL;
    free(basename);
	basename = NULL;

    return SUCCESS;

	free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
	free(parent_dir);
	parent_dir = NULL;
    free(basename);
	basename = NULL;

	return ERROR; // error message handled by shell
}

int fs_move(char *src, char *dest) {
	dir_entry *src_parent_dir = NULL; // the source's parent dir
	dir_entry *dest_parent_dir = NULL; // the destination's parent dir
	char *src_basename = NULL; // the basename of the source path, i.e. the filename
	char *dest_basename = NULL; // the basename of the destination path
	
	int src_is_dir = FALSE; // flag for if we are moving a directory
	int dest_is_dir = FALSE; // flag for if the destination is a directory
	int dest_exists = FALSE; // flag for if the destination already exists

	// get the start block of the parent dirs
	long long src_parent_dir_start_block = getParentBasenameStartBlock(src);
	long long dest_parent_dir_start_block = getParentBasenameStartBlock(dest);
	if (src_parent_dir_start_block == ERROR || dest_parent_dir_start_block == ERROR) {
		printf ("Error getting the parent directorys' start block. ");
		goto free_and_return_error;
	}

	// read from disk into the parent dirs
	src_parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	dest_parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(src_parent_dir, vcb->dir_blocks, src_parent_dir_start_block,
	    "fs_move init src_parent_dir") == ERROR) {
		goto free_and_return_error;
	}
	if (customLBAread(dest_parent_dir, vcb->dir_blocks, dest_parent_dir_start_block,
	    "fs_move init dest_parent_dir") == ERROR) {
		goto free_and_return_error;
	}

	// get the basenames in the src/dest path. both basenames should be freed
	src_basename = getBasename(src);
	dest_basename = getBasename(dest);

	/* src_basename and dest_basename will be null if src or dest was "/".
	 * They will be non-null later in the function, but until then,
	 * be careful with src_basename and dest_basename. */

	// check length of the new name. if dest is "/", then the length of dest_basename
	// will be src_basename's length, which is checked later.
	if (dest_basename && strnlen(dest_basename, MAX_DE_NAME_LENGTH) >= MAX_DE_NAME_LENGTH) {
		printf("The destination name can be at most %d characters long. ",
		       MAX_DE_NAME_LENGTH - 1);
		goto free_and_return_error;
	}

	// if src_basename or dest_basename is NULL, getDirEntryIndexByName returns 0.
	int src_entry_index = getDirEntryIndexByName(src_parent_dir, src_basename);
	int dest_entry_index = getDirEntryIndexByName(dest_parent_dir, dest_basename);
	if (src_entry_index == ERROR || src_entry_index == NOT_FOUND) {
		printf("Could not find source file '%s'. ", src_basename); // wont be null
		goto free_and_return_error;
	}

	if (dest_entry_index == ERROR) {
		printf("Error getting the destination file's directory entry index. ");
		goto free_and_return_error;
	} else if (dest_entry_index == NOT_FOUND) { // not going to overwrite file
		dest_exists = FALSE;
	} else dest_exists = TRUE; // found matching file/dir

	// Linux prevents you from moving or renaming the root dir, saying that the
	// process is in use. Also, renaming the root dir leads to strange things happening.
	if (src_parent_dir[src_entry_index].start_block == vcb->root_dir_start_block) {
	 	printf("You cannot move or rename the root directory. ");
	 	goto free_and_return_error;
	}

	/* src_basename is no longer null if it was. */

	if (src_parent_dir[src_entry_index].type == DIRECTORY) src_is_dir = TRUE;
	if (dest_exists && dest_parent_dir[dest_entry_index].type == DIRECTORY) dest_is_dir = TRUE;

	// cannot move src to a subdirectory of itself, otherwise src
	// and all its children will be lost, taking up space in disk yet are undeletable
	if (src_is_dir && dest_exists && dest_is_dir) {
		dir_entry *dest_child_dir = malloc(vcb->dir_blocks * vcb->block_size);

		if (customLBAread(dest_child_dir, vcb->dir_blocks,
		    dest_parent_dir[dest_entry_index].start_block, "fs_move check subdir") == ERROR) {
			free(dest_child_dir);
			dest_child_dir = NULL;
			goto free_and_return_error;
		}

		int is_sub_dir;

		// src will be moved into a subdirectory of itself
		if (dest_parent_dir[dest_entry_index].start_block
		    == src_parent_dir[src_entry_index].start_block) {
			is_sub_dir = TRUE;
		} else { // check if dest is currently a subdirectory of src
			is_sub_dir = isSubDirOf(dest_child_dir, src_parent_dir[src_entry_index].start_block);
		}

		if (is_sub_dir == TRUE || is_sub_dir == ERROR) {
			printf("Cannot move '%s' to a subdirectory of itself, '%s/%s'. ",
			       src_basename, dest, src_basename);
			
			free(dest_child_dir);
			dest_child_dir = NULL;
			goto free_and_return_error;
		}

		// passed the check
		free(dest_child_dir);
		dest_child_dir = NULL;
	}
	
	// if dest is an existing directory, we will move our file into dest.
	// therefore, we update dest_parent_dir to be dest.
	if (dest_exists && dest_is_dir) {
		if (customLBAread(dest_parent_dir, vcb->dir_blocks,
		    dest_parent_dir[dest_entry_index].start_block,
			"fs_move dest_exists dest_parent_dir") == ERROR) {
			goto free_and_return_error;
		}

		dest_parent_dir_start_block = dest_parent_dir[0].start_block;

		// no renaming will being done. dest's name will be src's name
		free(dest_basename); // dest_basename might not be large enough
		dest_basename = malloc(MAX_DE_NAME_LENGTH);
		strcpy(dest_basename, src_basename);

		// since we are in a new dir, we need to recheck if there are
		// any existing files/dirs with the same name
		dest_entry_index = getDirEntryIndexByName(dest_parent_dir, dest_basename);
		if (dest_entry_index == ERROR) {
			printf("Error getting the destination file's directory entry index. ");
			goto free_and_return_error;			
		} else if (dest_entry_index == NOT_FOUND) dest_exists = FALSE; // no overwrite
		else dest_exists = TRUE; // overwrite

		// check whether the file being overwritten is a dir
		if (dest_exists && dest_parent_dir[dest_entry_index].type == DIRECTORY) {
 			dest_is_dir = TRUE;
		} else dest_is_dir = FALSE;
	}

	/* dest_basename is no longer null if it was. */

	// if src and dest are in fact the same file/dir
	if ((src_parent_dir[0].start_block == dest_parent_dir[0].start_block)
	    && (src_entry_index == dest_entry_index)) {
		printf("The source file and the destination file are the same file. ");
		goto free_and_return_error;
	} else if (dest_exists && !dest_is_dir && src_is_dir) { // overwriting with wrong file type
		printf("You cannot overwrite a non-directory with a directory. ");
		goto free_and_return_error;
	} else if (dest_exists && dest_is_dir && !src_is_dir) { // overwriting with wrong file type
		printf("You cannot overwrite a directory with a non-directory. ");
		goto free_and_return_error;
	}

	// same directory, non-overwrite case: just rename the file/dir
	if ((src_parent_dir[0].start_block == dest_parent_dir[0].start_block) && !dest_exists) {
		time_t curr_time = time(NULL);

		strcpy(src_parent_dir[src_entry_index].name, dest_basename);
		src_parent_dir[0].last_modified = curr_time;

		// if src_parent_dir is root_dir, then update root_dir[1] since root is its own parent
		if (src_parent_dir[0].start_block == vcb->root_dir_start_block) {
			src_parent_dir[1].last_modified = curr_time;
		}

		// after modifying src_parent_dir, update it in disk
		if (customLBAwrite(src_parent_dir, vcb->dir_blocks, src_parent_dir[0].start_block,
            "fs_move same dir non-overwrite src_parent_dir") == ERROR) {
			goto free_and_return_error;
		}

		printf("Renamed '%s' to '%s'.\n", src_basename, dest_basename);
	} // same directory, overwrite case:
	// src_parent_dir and dest_parent_dir point to the same block on disk.
	// only use one of them because they point to different addresses in memory.
	// using both will write outdated/conflicting data to disk.
	// 
	// free overwritten file's blocks then update the metadata
	// not possible to overwrite a dir because two dir entries cannot have the
	// same name in the same dir, and the src dir would just get moved into the dest dir,
	// which is handled in the different directory handler.
	else if ((src_parent_dir[0].start_block == dest_parent_dir[0].start_block) && dest_exists) {
		// if either src or dest is a directory in this case, then there was a bug
		if (dest_is_dir || src_is_dir) {
			printf("Error: the source '%s' or the destination '%s' was a "
			       "directory. This should not have happened. ",
				   src_basename, dest_basename);
			goto free_and_return_error;
		}

		uint64_t file_num_blocks = ceilingDivide(src_parent_dir[dest_entry_index].size,
			                                     vcb->block_size);

		// free the overwritten file's blocks on disk because we are in effect deleting it.
		if (file_num_blocks > 0) {
			for (int i = 0; i < file_num_blocks; i++) { // mark blocks free
				markBlockFree(bitmap, src_parent_dir[dest_entry_index].start_block + i);
			}
			vcb->num_free_blocks += file_num_blocks;

			if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK,
			    "fs_move same dir update VCB") == ERROR) {
				goto free_and_return_error;
			}

			if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
				"fs_move same dir update bitmap") == ERROR) {
				goto free_and_return_error;
			}
		}

		time_t curr_time = time(NULL);

		src_parent_dir[dest_entry_index] = src_parent_dir[src_entry_index];
		strcpy(src_parent_dir[dest_entry_index].name, dest_basename);
		src_parent_dir[dest_entry_index].last_modified = curr_time;

		// delete the source entry because it overwrote the dest entry
		memset(src_parent_dir[src_entry_index].name, '\0', MAX_DE_NAME_LENGTH);
		src_parent_dir[src_entry_index].start_block = 0;
		src_parent_dir[src_entry_index].size = 0;
		src_parent_dir[src_entry_index].type = FREE_ENTRY;
		src_parent_dir[src_entry_index].creation_date = 0;
		src_parent_dir[src_entry_index].last_modified = 0;
		src_parent_dir[src_entry_index].last_opened = 0;

		src_parent_dir[0].last_modified = curr_time;

		// if src_parent_dir is root_dir, then update root_dir[1] since root is its own parent
		if (src_parent_dir[0].start_block == vcb->root_dir_start_block) {
			src_parent_dir[1].last_modified = curr_time;
		}

		// the src parent dir is finished with its overwrite, so we write it to disk
		if (customLBAwrite(src_parent_dir, vcb->dir_blocks, src_parent_dir[0].start_block,
	    	"fs_move same dir update src_dir") == ERROR) {
			goto free_and_return_error;
		}

		printf("Renamed '%s' to '%s', overwriting the old '%s'.\n",
			   src_basename, dest_basename, dest_basename);
	} // different directory, overwrite and non-overwrite cases:
	// move from src dir to dest dir, delete src dir entry after copying it to dest dir
	// if overwriting, free the overwritten file's blocks on disks first
	else {
		// Linux prevents you from moving the cwd or the cwd's ancestors, saying that the
		// process is in use. Trying to move the cwd or its ancestor results in segfaults.
		// Thus we block the user from moving the cwd or its ancestors.
		if (src_parent_dir[src_entry_index].start_block == getCWDstartBlock()) {
			printf("You cannot move the current working directory. ");
			goto free_and_return_error;
		} else { // check if we are moving an ancestor directory of the cwd
			dir_entry *cwd = malloc(vcb->dir_blocks * vcb->block_size);

			if (customLBAread(cwd, vcb->dir_blocks, getCWDstartBlock(),
			    "fs_move cwd") == ERROR) {
				free(cwd);
				cwd = NULL;
				goto free_and_return_error;
			}

			int cwd_being_moved = isSubDirOf(cwd, src_parent_dir[src_entry_index].start_block);
			if (cwd_being_moved == TRUE || cwd_being_moved == ERROR) {
				printf("You cannot move an ancestor directory of the "
				       "current working directory. ");
				free(cwd);
				cwd = NULL;
				goto free_and_return_error;
			}

			// cwd checks have passed
			free(cwd);
			cwd = NULL;
		}

		if (dest_exists) { // free the overwritten file/dir's blocks on disk
			if (dest_is_dir) { // dirs need special handling
				dir_entry *overwritten_dir = malloc(vcb->dir_blocks * vcb->block_size);

				if (customLBAread(overwritten_dir, vcb->dir_blocks,
				    dest_parent_dir[dest_entry_index].start_block,
					"fs_move diff dir overwriting dir") == ERROR) {
					free(overwritten_dir);
					overwritten_dir = NULL;
					goto free_and_return_error;
				}

				// check if the dir to be overwritten has any dir entries
				int used_entry_index = getDirNextUsedEntryIndex(overwritten_dir, 2);
				if (used_entry_index == ERROR || used_entry_index != NOT_FOUND) { // not empty
					printf("You can only overwrite empty directories. "
					       "The destination directory '%s' was not empty. ", dest_basename);
					free(overwritten_dir);
					overwritten_dir = NULL;
					goto free_and_return_error;
				}

				// dont overwrite root dir or cwd. overwriting the cwd causes a segfault.
				// in theory, root dir should not be empty but you never know
				if (overwritten_dir[0].start_block == vcb->root_dir_start_block) {
					printf("You cannot overwrite the root directory. ");
					free(overwritten_dir);
					overwritten_dir = NULL;
					goto free_and_return_error;
				} else if (overwritten_dir[0].start_block == getCWDstartBlock()) {
					printf("You cannot overwrite the current working directory. ");
					free(overwritten_dir);
					overwritten_dir = NULL;
					goto free_and_return_error;
				}

				// overwritten dir was empty and is safe to delete
				free(overwritten_dir);
				overwritten_dir = NULL;
			}

			uint64_t file_num_blocks = ceilingDivide(dest_parent_dir[dest_entry_index].size,
			                                         vcb->block_size);

			// free the overwritten file's blocks on disk because we are in effect deleting it.
			if (file_num_blocks > 0) {
				for (int i = 0; i < file_num_blocks; i++) { // mark blocks free
					markBlockFree(bitmap, dest_parent_dir[dest_entry_index].start_block + i);
				}
				vcb->num_free_blocks += file_num_blocks;

				if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK,
				    "fs_move diff dir update VCB") == ERROR) {
					goto free_and_return_error;
				}

				if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
				    "fs_move diff dir update bitmap") == ERROR) {
					goto free_and_return_error;
				}
			}
		} else { // if not overwriting, get free entry in the dir we are moving to
			dest_entry_index = getDirFreeEntryIndex(dest_parent_dir);
			if (dest_entry_index == ERROR) {
				printf("Error getting a free entry index in the destination directory. ");
				goto free_and_return_error;
			} else if (dest_entry_index == NOT_FOUND) {
				printf("The directory you are trying to move the file to is full. ");
				goto free_and_return_error;
			}
		}

		time_t curr_time = time(NULL);

		dest_parent_dir[dest_entry_index] = src_parent_dir[src_entry_index];
		strcpy(dest_parent_dir[dest_entry_index].name, dest_basename);
		dest_parent_dir[dest_entry_index].last_modified = curr_time;
		dest_parent_dir[0].last_modified = curr_time;

		// if dest_parent_dir is root_dir, then update root_dir[1] since root is its own parent
		if (dest_parent_dir[0].start_block == vcb->root_dir_start_block) {
			dest_parent_dir[1].last_modified = curr_time;
		}

		// dest dir now contains the moved file's metadata, so we update dest dir in disk
		if (customLBAwrite(dest_parent_dir, vcb->dir_blocks, dest_parent_dir[0].start_block,
	    	"fs_move diff dir update dest_dir") == ERROR) {
			goto free_and_return_error;
		}

		// if we moved a directory, we need to update the moved directory's metadata
		if (src_is_dir) {
			dir_entry *dest_child_dir = malloc(vcb->dir_blocks * vcb->block_size);
			
			if (customLBAread(dest_child_dir, vcb->dir_blocks,
			    dest_parent_dir[dest_entry_index].start_block,
				"fs_move diff dir update dest_child_dir") == ERROR) {
				free(dest_child_dir);
				dest_child_dir = NULL;
				goto free_and_return_error;
			}

			dest_child_dir[0].last_modified = curr_time;
			dest_child_dir[1] = dest_parent_dir[0];
			strcpy(dest_child_dir[1].name, "..");

			// update moved dir on disk
			if (customLBAwrite(dest_child_dir, vcb->dir_blocks,
			    dest_child_dir[0].start_block,
				"fs_move diff dir update dest_child_dir") == ERROR) {
				free(dest_child_dir);
				dest_child_dir = NULL;
				goto free_and_return_error;
			}

			// successfully updated dest_child_dir
			free(dest_child_dir);
			dest_child_dir = NULL;
		}
		
		// delete the source dir entry because it does not contain our file any more
		memset(src_parent_dir[src_entry_index].name, '\0', MAX_DE_NAME_LENGTH);
		src_parent_dir[src_entry_index].start_block = 0;
		src_parent_dir[src_entry_index].size = 0;
		src_parent_dir[src_entry_index].type = FREE_ENTRY;
		src_parent_dir[src_entry_index].creation_date = 0;
		src_parent_dir[src_entry_index].last_modified = 0;
		src_parent_dir[src_entry_index].last_opened = 0;

		src_parent_dir[0].last_modified = curr_time;

		// if src_parent_dir is root_dir, then update root_dir[1] since root is its own parent
		if (src_parent_dir[0].start_block == vcb->root_dir_start_block) {
			src_parent_dir[1].last_modified = curr_time;
		}

		// src dir no longer contains the moved file's metadata, so we update src dir in disk
		if (customLBAwrite(src_parent_dir, vcb->dir_blocks, src_parent_dir[0].start_block,
	    	"fs_move diff dir update src_dir") == ERROR) {
			goto free_and_return_error;
		}

		if (dest_exists) {
			printf("Successfully moved '%s', overwriting the old '%s'.\n",
			       src_basename, dest_basename);
		}
	}

	free(src_parent_dir);
	src_parent_dir = NULL;
	free(dest_parent_dir);
	dest_parent_dir = NULL;
	free(src_basename);
	src_basename = NULL;
	free(dest_basename);
	dest_basename = NULL;

	return SUCCESS;

	free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
	free(src_parent_dir);
	src_parent_dir = NULL;
	free(dest_parent_dir);
	dest_parent_dir = NULL;
	free(src_basename);
	src_basename = NULL;
	free(dest_basename);
	dest_basename = NULL;

	printf("Move failed.\n");
	return ERROR;
}

int fs_rmdir(const char *pathname) {
	dir_entry *parent_dir = NULL; // the parent/temp directory
	char *basename = NULL; // the name of the directory to be removed
	dir_entry *remove_dir = NULL; // the directory to be removed

	// get parent_dir start block
	long long parent_dir_start_block = getParentBasenameStartBlock(pathname);
	if (parent_dir_start_block == ERROR) {
		printf("Error getting the parent directory's start block. ");
		goto free_and_return_error;
	}

    // read from disk into parent_dir
	parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_rmdir init parent dir") == ERROR) {
		goto free_and_return_error;
	}

	// Gets the basename. basename must be freed.
	basename = getBasename(pathname);
	if (!basename) { // path was "/"
		printf("You cannot remove the root directory. ");
		goto free_and_return_error;
	}

	// searches in parent_dir for the dir to remove
	int entry_index = getDirEntryIndexByName(parent_dir, basename);
	if (entry_index == ERROR || entry_index == NOT_FOUND) { // dir not found
		printf("'%s' not found. ", basename);
		goto free_and_return_error;
	}

	// cannot remove the root directory
	if (parent_dir[entry_index].start_block == vcb->root_dir_start_block) {
		printf("You cannot remove the root directory. ");
		goto free_and_return_error;
	} // do not remove the cwd
	else if (parent_dir[entry_index].start_block == getCWDstartBlock()) {
		printf("You cannot remove the current working directory. ");
		goto free_and_return_error;
	} else if (parent_dir[entry_index].type != DIRECTORY) { // if remove_dir is not a dir
		printf("You can only remove directories with this command. ");
		goto free_and_return_error;
	}

	// load remove_dir into memory
	remove_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(remove_dir, vcb->dir_blocks, parent_dir[entry_index].start_block,
	    "fs_rmdir remove_dir") == ERROR) {
		goto free_and_return_error;
	}

	// check if remove_dir has any dir entries.
	// we start at 2 because dir[0] and dir[1] always exists.
	int used_entry_index = getDirNextUsedEntryIndex(remove_dir, 2);
	if (used_entry_index == ERROR || used_entry_index != NOT_FOUND) {
		printf("You can only remove empty directories. '%s' is not empty. ", basename);
		goto free_and_return_error;
	}

	uint64_t remove_dir_start_block = parent_dir[entry_index].start_block;

	// at this point, remove_dir is empty. now we remove all references to
	// remove_dir in parent_dir. we need to wipe all the dir_entry data members because
	// they will interfere with our search functions if we leave them.
	memset(parent_dir[entry_index].name, '\0', MAX_DE_NAME_LENGTH);
	parent_dir[entry_index].start_block = 0;
	parent_dir[entry_index].size = 0;
	parent_dir[entry_index].type = FREE_ENTRY;
	parent_dir[entry_index].creation_date = 0;
	parent_dir[entry_index].last_modified = 0;
	parent_dir[entry_index].last_opened = 0;

	time_t curr_time = time(NULL);
	parent_dir[0].last_modified = curr_time;

	// if parent is root_dir, then also update root_dir[1] since root is its own parent
	if (parent_dir[0].start_block == vcb->root_dir_start_block) {
		parent_dir[1].last_modified = curr_time;
	}

	// after modifying parent_dir, update it in the disk
	if (customLBAwrite(parent_dir, vcb->dir_blocks, parent_dir[0].start_block,
	    "update parent_dir in fs_rmdir") == ERROR) {
		goto free_and_return_error;
	}

	// mark the blocks that were once occupied by remove_dir as free
	for (int i = 0; i < vcb->dir_blocks; i++) {
		markBlockFree(bitmap, remove_dir_start_block + i);
	}
	vcb->num_free_blocks += vcb->dir_blocks;

	// Write vcb to disk after updating vcb->num_free_blocks.
	if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK, "fs_rmdir update VCB") == ERROR) {
		goto free_and_return_error;
	}

	// write to disk the updated bitmap
	if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
		"fs_rmdir update bitmap") == ERROR) {
		goto free_and_return_error;
	}

	free(parent_dir);
	parent_dir = NULL;
	free(basename);
	basename = NULL;
	free(remove_dir);
	remove_dir = NULL;

	return SUCCESS;

	free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
	free(parent_dir);
	parent_dir = NULL;
	free(basename);
	basename = NULL;
	free(remove_dir);
	remove_dir = NULL;

	printf("Remove directory failed.\n");
	return ERROR;
}

int fs_delete(char *filename) {
	dir_entry *parent_dir = NULL; // the parent/temp directory
	char *basename = NULL; // the name of the file to be removed

	// get parent_dir start block
	long long parent_dir_start_block = getParentBasenameStartBlock(filename);
	if (parent_dir_start_block == ERROR) {
		printf("Error getting the parent directory's start block. ");
		goto free_and_return_error;
	}

	// read from disk into parent_dir
	parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_delete init parent dir") == ERROR) {
		goto free_and_return_error;
	}

	// Gets the basename (filename). basename must be freed.
	basename = getBasename(filename);
	if (!basename) { // path was "/"
		printf("You can only delete files with this command. ");
		goto free_and_return_error;
	}

	// searches in parent_dir for the file to remove
	int entry_index = getDirEntryIndexByName(parent_dir, basename);
	if (entry_index == ERROR || entry_index == NOT_FOUND) { // file not found
		printf("'%s' not found. ", basename);
		goto free_and_return_error;
	}

	if (parent_dir[entry_index].type != FILE) { // trying to delete a non-file
		printf("You can only delete files with this command. ");
		goto free_and_return_error;
	}

	uint64_t file_start_block = parent_dir[entry_index].start_block;
	uint64_t file_num_blocks = ceilingDivide(parent_dir[entry_index].size, vcb->block_size);

	// we now remove all references to the soon-to-be-deleted file in parent_dir.
	// we need to wipe all the dir_entry data members because
	// they will interfere with our search functions if we leave them.
	memset(parent_dir[entry_index].name, '\0', MAX_DE_NAME_LENGTH);
	parent_dir[entry_index].start_block = 0;
	parent_dir[entry_index].size = 0;
	parent_dir[entry_index].type = FREE_ENTRY;
	parent_dir[entry_index].creation_date = 0;
	parent_dir[entry_index].last_modified = 0;
	parent_dir[entry_index].last_opened = 0;

	time_t curr_time = time(NULL);
	parent_dir[0].last_modified = curr_time;

	// if parent is root_dir, then also update root_dir[1] since root is its own parent
	if (parent_dir[0].start_block == vcb->root_dir_start_block) {
		parent_dir[1].last_modified = curr_time;
	}

	// after modifying parent_dir, update it in the disk
	if (customLBAwrite(parent_dir, vcb->dir_blocks, parent_dir[0].start_block,
	    "update parent_dir in fs_delete") == ERROR) {
		goto free_and_return_error;
	}

	// only modify the bitmap if the file took up space on disk
	if (file_num_blocks > 0) {
		for (int i = 0; i < file_num_blocks; i++) {
			markBlockFree(bitmap, file_start_block + i);
		}
		vcb->num_free_blocks += file_num_blocks;

		// Write vcb to disk after updating vcb->num_free_blocks.
		if (customLBAwrite(vcb, VCB_BLOCKS, VCB_START_BLOCK, "fs_delete update VCB") == ERROR) {
			goto free_and_return_error;
		}

		// write to disk the updated bitmap
		if (customLBAwrite(bitmap, vcb->bitmap_blocks, vcb->bitmap_start_block,
			"fs_delete update bitmap") == ERROR) {
			goto free_and_return_error;
		}
	}

	free(parent_dir);
	parent_dir = NULL;
	free(basename);
	basename = NULL;

	return SUCCESS;

	free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
	free(parent_dir);
	parent_dir = NULL;
	free(basename);
	basename = NULL;

	printf("Delete file failed.\n");
	return ERROR;
}