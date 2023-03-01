/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: mfs.h
*
* Description: This is the file system interface. This is the
*  interface needed by the driver to interact with our filesystem.
*
**************************************************************/

#ifndef _MFS_H
#define _MFS_H

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include "b_io.h"
#include "fsInit.h"

#include <dirent.h>
#define FT_REGFILE	DT_REG
#define FT_DIRECTORY DT_DIR
#define FT_LINK	DT_LNK

#define NOT_FOUND -2 // used as a return value when searching in a dir for a dir entry
#define ROOT_NAME "~"
#define PATH_MAX_LEN 4096 // maximum length of a path in chars
#define D_NAME_MAX_LEN 256 // d_name max length is 255 characters + null character
#define MAX_PATHNAME_DEPTH 50 // maximum depth of a pathname

#ifndef uint64_t
typedef u_int64_t uint64_t;
#endif
#ifndef uint32_t
typedef u_int32_t uint32_t;
#endif

// This structure is returned by fs_readdir to provide the caller with information
// about each file as it iterates through a directory
struct fs_diriteminfo {
    unsigned short d_reclen;    /* length of this record */
    unsigned char fileType;    
    char d_name[D_NAME_MAX_LEN]; 	/* filename max filename is 255 characters */
};

// This is a private structure used only by fs_opendir, fs_readdir, and fs_closedir
// Think of this like a file descriptor but for a directory - one can only read
// from a directory. This structure helps you (the file system) keep track of
// which directory entry you are currently processing so that everytime the caller
// calls the function readdir, you give the next entry in the directory
typedef struct {
	unsigned short  d_reclen;		    /* length of this record */
	unsigned short	dirEntryPosition;	/* which directory entry position, like file pos */
	uint64_t directoryStartLocation;	/* Starting LBA of directory */
} fdDir;

// Key directory functions
int fs_mkdir(const char *pathname, mode_t mode);

/* Deletes a directory only if the directory is empty.
 * Returns SUCCESS on success. Returns ERROR on error. */
int fs_rmdir(const char *pathname);

// Directory iteration functions
fdDir* fs_opendir(const char *name);
struct fs_diriteminfo* fs_readdir(fdDir *dirp);

/* Frees all the pointers used by open and read. Returns SUCCESS. */
int fs_closedir(fdDir *dirp);

// Misc directory functions
char* fs_getcwd(char *buf, size_t size);
int fs_setcwd(char *buf); // linux chdir

/* Returns TRUE if path leads to a file, FALSE otherwise. */
int fs_isFile(char *path);

/* Returns TRUE if path leads to a directory, FALSE otherwise. */
int fs_isDir(char *path);

/* Deletes a file. Returns SUCCESS on success. Returns ERROR on error. */
int fs_delete(char *filename);

/* Moves the src file/dir to the dest file/dir.
 * Returns SUCCESS on success, ERROR on error. */
int fs_move(char *src, char *dest);

// This is the structure that is filled in from a call to fs_stat
struct fs_stat {
	off_t     st_size;    		/* total size, in bytes */
	blksize_t st_blksize; 		/* blocksize for file system I/O */
	blkcnt_t  st_blocks;  		/* number of 512B blocks allocated */
	time_t    st_accesstime;   	/* time of last access */
	time_t    st_modtime;   	/* time of last modification */
	time_t    st_createtime;   	/* time of last status change */
};

int fs_stat(const char *path, struct fs_stat *buf);

/* Given a path x/y/z, return's y's start block. It is up to the caller
 * to check z's validity. Returns ERROR on error. */
long long getParentBasenameStartBlock(const char *path);

/* Given a path x/y/z, return z as a string. Does not check the validity of x/y/.
 * The caller must free the returned string, though no need to free if NULL was returned.
 * Returns NULL if the return string is empty, e.g. the path was "/". */
char* getBasename(const char *path);

/* Preconditions: dir must already be allocated and be LBAread into.
 *
 * Given the name of a dir_entry, search in dir for a dir_entry with a matching name.
 * Return that dir_entry's index in dir. On error, return ERROR.
 * If the dir_entry was not found in dir, return NOT_FOUND.
 * 
 * If NULL was passed in for dir_entry_name, return 0, the index referring to dir itself.
 * This feature is useful for dealing with the root directory, for instance. */
int getDirEntryIndexByName(dir_entry *dir, const char *dir_entry_name);

/* Preconditions: dir must already be allocated and be LBAread into.
 *
 * Given the start block of a dir_entry, search in dir for a dir_entry
 * with the matching start block. Return that dir_entry's index in dir.
 * If error, return ERROR.
 * If the dir_entry was not found in dir, return NOT_FOUND. */
int getDirEntryIndexByStartBlock(dir_entry *dir, uint64_t dir_entry_start_block);

/* Preconditions: dir must already be allocated and be LBAread into.
 *
 * Searches dir for the first free entry. Returns the index of that first free entry.
 * If error, return ERROR. If no free entry available, return NOT_FOUND. */
int getDirFreeEntryIndex(dir_entry *dir);

/* Preconditions: dir must already be allocated and be LBAread into.
 *
 * Given a directory, return the number of used entries. If error, return ERROR. */
int getDirNumUsedEntries(dir_entry *dir);

/* Preconditions: dir must already be allocated and be LBAread into.
 *                0 <= start_index < MAX_DIRECTORY_ENTRIES
 * 
 * Searches dir for the next used entry starting from start_index.
 * Returns the index of that used entry. Returns ERROR if error.
 * If a used entry was not found starting from start_index, return NOT_FOUND. */
int getDirNextUsedEntryIndex(dir_entry *dir, int start_index);

/* Preconditions: dir must be malloced and LBAread into.
 *  buf must have space allocated for it via an array or malloc,
 *  and it might need to be initialized to all null characters.
 * 
 * The parameter size is the size of buf.
 * Returns the absolute path of the directory and writes it to buf.
 * If the absolute path generated is longer than size chars, return NULL.
 * Returns NULL for other errors too. */
char* getDirAbsPath(dir_entry *dir, char *buf, size_t size);

/* Preconditions: dir must be malloced and LBAread into.
 *  buf must have space allocated for it via an array or malloc,
 *  and it might need to be initialized to all null characters.
 *  buf_size should be at least MAX_DE_NAME_LENGTH.
 * 
 * Returns the name of dir and writes it to buf.
 * Returns ROOT_NAME if the root dir was passed in. Returns NULL on error. */
char* getDirName(dir_entry *dir, char *buf, size_t buf_size);

/* Preconditions: dir must be malloced and LBAread into.
 *
 * Returns TRUE if dir is a subdirectory of the directory located
 * at ancestor_start_block. Returns FALSE if not. Returns ERROR on error. */
int isSubDirOf(dir_entry *dir, uint64_t ancestor_start_block);

#endif