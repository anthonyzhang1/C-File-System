/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: fsDirOps.c
*
* Description: Functions for the shell commands pertaining to directories,
*  i.e. list (ls).
*
**************************************************************/

#include "mfs.h"

#define DIRMAX_LEN 4096 // maximum length of a path
#define PARENT_ENTRY_INDEX 1 // index of the parent entry in a dir
#define DIR_TYPE_CHAR 'D' // char that represents the type 'dir'
#define FILE_TYPE_CHAR '-' // char that represents the type 'file'
#define UNKNOWN_TYPE_CHAR '?' // char that represents a type that is neither a file nor dir

struct fs_diriteminfo *di = NULL;
dir_entry *dir = NULL;
fdDir *dirp = NULL;

int fs_isFile(char *path) {
	dir_entry *parent_dir = NULL; // holds the parent directory
	char *dir_name = NULL; // the directory's filename

	int parent_dir_start_block = getParentBasenameStartBlock(path);
	if (parent_dir_start_block == ERROR) {
		printf("Error getting the parent directory's start block.\n");
		goto free_and_return_false;
	}

	// read from disk into parent_dir
	parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_isFile init parent_dir") == ERROR) {
		goto free_and_return_false;
	}

	dir_name = getBasename(path); // dir_name must be freed
	if (!dir_name) return FALSE; // if path was "/". root dir is not a file
	
	// searches in the parent dir and searches for an entry with the matching name
	int entry_index = getDirEntryIndexByName(parent_dir, dir_name);
	if (entry_index == ERROR) { // error
		printf("Error getting the directory entry index.\n");
		goto free_and_return_false;
	} else if (entry_index == NOT_FOUND) { // the entry doesnt exist
		goto free_and_return_false;
	}

	int is_file = FALSE;
	if (parent_dir[entry_index].type == FILE) is_file = TRUE;

	free(parent_dir);
	parent_dir = NULL;
	free(dir_name);
	dir_name = NULL;

	return is_file;

	free_and_return_false: // Label for error handling. Free the mallocs and return FALSE.
	free(parent_dir);
	parent_dir = NULL;
	free(dir_name);
	dir_name = NULL;

	return FALSE; // error message handled by shell
}

int fs_isDir(char *path) {
	int parent_dir_start_block = getParentBasenameStartBlock(path);
	if (parent_dir_start_block == ERROR) {
		printf("Error getting the parent's start block.\n");
		return FALSE;
	}

	// read from disk into parent_dir
	dir_entry *parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_isDir init parent_dir") == ERROR) {
		free(parent_dir);
		parent_dir = NULL;
		return FALSE;
	}

	char *dir_name = getBasename(path); // dir_name needs to be freed
	if (!dir_name) return TRUE; // path was "/". the root directory is indeed a directory
	
	// searches in the parent dir and searches for an entry with the matching name
	int entry_index = getDirEntryIndexByName(parent_dir, dir_name);
	if (entry_index == ERROR) { // error
		printf("Error searching the parent directory.\n");
		free(dir_name);
		dir_name = NULL;
		free(parent_dir);
		parent_dir = NULL;
		return FALSE;
	} else if (entry_index == NOT_FOUND) { // the entry doesnt exist
		// error message handled by shell
		free(dir_name);
		dir_name = NULL;
		free(parent_dir);
		parent_dir = NULL;
		return FALSE;
	}

	int is_dir = FALSE;
	if (parent_dir[entry_index].type == DIRECTORY) is_dir = TRUE;

	free(dir_name);
	dir_name = NULL;
	free(parent_dir);
	parent_dir = NULL;

	return is_dir;
}

fdDir *fs_opendir(const char *name) {
	if (strcmp(name, "/") == 0) { // is root
		dirp = malloc(sizeof(fdDir)); // Freed in fs_closedir.

		dirp->d_reclen = sizeof(dir_entry);
		dirp->dirEntryPosition = (unsigned short) 0; // start at the first entry of root_dir
		dirp->directoryStartLocation = vcb->root_dir_start_block;
		
		dir = malloc(vcb->dir_blocks * vcb->block_size); // Freed in fs_closedir.

		// read into dir
		if (customLBAread(dir, vcb->dir_blocks, vcb->root_dir_start_block,
            "fs_opendir root_dir dir") == ERROR) {
			free(dir); // free upon error, but not upon success
			dir = NULL;
			return NULL;
		}

		di = malloc(sizeof(struct fs_diriteminfo)); // Freed in fs_closedir.
		return dirp;
	}

	int parent_dir_start_block = getParentBasenameStartBlock(name);
	if (parent_dir_start_block == ERROR) {
		printf("Error getting the parent directory's start block.\n");
		return NULL;
	}

	// read from disk into parent_dir
	dir_entry *parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_opendir init parent_dir") == ERROR) {
		free(parent_dir);
		parent_dir = NULL;
		return NULL;
	}

	char *dir_name = getBasename(name); // dir_name must be freed
	if (!dir_name) {
		printf("Directory name was empty. Failed to open directory.\n");
		free(parent_dir);
		parent_dir = NULL;
		free(dir_name);
		dir_name = NULL;
		return NULL;
	}
	
	// searches in the parent dir and searches for an entry with the matching name
	int entry_index = getDirEntryIndexByName(parent_dir, dir_name);
	if (entry_index == ERROR || entry_index == NOT_FOUND) { // the entry doesnt exist
		// error message handled by shell
		free(dir_name);
		dir_name = NULL;
		free(parent_dir);
		parent_dir = NULL;
		return NULL;
	}

	dirp = malloc(sizeof(fdDir)); // Freed in fs_closedir.

	dirp->d_reclen = sizeof(dir_entry);
	dirp->dirEntryPosition = (unsigned short) 0; // start at first entry in the directory
	dirp->directoryStartLocation = parent_dir[entry_index].start_block;

	dir = malloc(vcb->dir_blocks * vcb->block_size); // Freed in fs_closedir

	// read into dir
	if (customLBAread(dir, vcb->dir_blocks, dirp->directoryStartLocation,
	    "fs_opendir dir") == ERROR) {
		free(dir_name);
		dir_name = NULL;
		free(parent_dir);
		parent_dir = NULL;
		free(dir); // free upon error, but not upon success
		dir = NULL; 
		return NULL;
	}

	di = malloc(sizeof(struct fs_diriteminfo)); // Freed in fs_closedir

	free(dir_name);
	dir_name = NULL;
	free(parent_dir);
	parent_dir = NULL;

	return dirp;
}

struct fs_diriteminfo* fs_readdir(fdDir *dirp) {
	if (!di) return NULL; // called read before open
	if (dirp->dirEntryPosition >= MAX_DIRECTORY_ENTRIES) return NULL;

	// convert dir_entry's integer for file type into an unsigned char for fs_diriteminfo
	int dir_entry_type = dir[dirp->dirEntryPosition].type;
	if (dir_entry_type == DIRECTORY) di->fileType = DIR_TYPE_CHAR;
	else if (dir_entry_type == FILE) di->fileType = FILE_TYPE_CHAR;
	else di->fileType = UNKNOWN_TYPE_CHAR;

	di->d_reclen = sizeof(dir_entry);
	strcpy(di->d_name, dir[dirp->dirEntryPosition].name);

	// update last opened metadata, except for the entry about the parent
	if (dirp->dirEntryPosition != PARENT_ENTRY_INDEX) {
		dir[dirp->dirEntryPosition].last_opened = time(NULL);
	}

	dirp->dirEntryPosition++;
	if (dirp->dirEntryPosition >= MAX_DIRECTORY_ENTRIES) return NULL;

	dirp->dirEntryPosition = getDirNextUsedEntryIndex(dir, dirp->dirEntryPosition);

	// if nothing more to list
	if (dirp->dirEntryPosition == ERROR || dirp->dirEntryPosition == NOT_FOUND) return NULL;
	
	return di;
}

/* The argument is called path but it is actually a filename, since di->d_name is a filename.
 * Therefore we need to concatenate the absolute path of the parent directory and the d_name
 * if getParentBasenameStartBlock is to actually return the parent directory's start block. */
int fs_stat(const char *path, struct fs_stat *buf) {
	if (!dir) return ERROR; // fs_stat called before opendir

	dir_entry *parent_dir = NULL; // holds the parent directory

	char full_path[DIRMAX_LEN] = {0}; // stores the absolute path of the filename
	if (!getDirAbsPath(dir, full_path, DIRMAX_LEN - D_NAME_MAX_LEN - 1)) {
		printf("Error getting the absolute path. ");
		goto free_and_return_error;
	}

	if (full_path[strlen(full_path) - 1] != '/') strcat(full_path, "/");
	strcat(full_path, path);

	// check that we did not overwrite memory
	if (strlen(full_path) >= DIRMAX_LEN) {
		printf("The absolute path exceeded %d characters. ", DIRMAX_LEN);
		goto free_and_return_error;
	}

	int parent_dir_start_block = getParentBasenameStartBlock(full_path);
	if (parent_dir_start_block == ERROR) {
		printf("Error getting parent directory start block. ");
		goto free_and_return_error;
	}

	// read from disk into parent_dir
	parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
	if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir_start_block,
	    "fs_stat init parent_dir") == ERROR) {
		goto free_and_return_error;
	}

	// searches in parent_dir for an entry with the matching filename
	int entry_index = getDirEntryIndexByName(parent_dir, path);
	if (entry_index == ERROR || entry_index == NOT_FOUND) {
		printf("The basename directory entry was not found. ");
		goto free_and_return_error;
	}

	buf->st_size = (off_t) parent_dir[entry_index].size;
	buf->st_blksize = (blksize_t) vcb->block_size;
	buf->st_blocks = (blkcnt_t) ceilingDivide(parent_dir[entry_index].size, vcb->block_size);

	buf->st_accesstime = parent_dir[entry_index].last_opened;
	buf->st_modtime = parent_dir[entry_index].last_modified;
	buf->st_createtime = parent_dir[entry_index].creation_date;

	free(parent_dir);
	parent_dir = NULL;

	return SUCCESS;

	free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
	free(parent_dir);
	parent_dir = NULL;

	printf("fs_stat failed.\n");
	return ERROR;
}

int fs_closedir(fdDir *dirp) {
	free(di);
	di = NULL;
	free(dir);
	dir = NULL;
	free(dirp);
	dirp = NULL;

	return SUCCESS;
}