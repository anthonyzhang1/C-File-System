/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: b_io.h
*
* Description: Interface for the key file I/O functions.
*
**************************************************************/

#ifndef _B_IO_H
#define _B_IO_H
#include <fcntl.h>
#include "fsInit.h"
#include "mfs.h"

typedef int b_io_fd;

/* Opens a buffered file. Returns the new file descriptor on success.
 * On error, returns ERROR. */
b_io_fd b_open(char *filename, int flags);

/* Read count bytes from disk to the caller's buffer. Returns the number of bytes
 * transferred to the caller's buffer. Returns ERROR on error. */
int b_read(b_io_fd fd, char *buffer, int count);

/* Write count bytes from the caller's buffer to disk. Returns the number of bytes
 * transferred to the fcb buffer. Returns ERROR on error */
int b_write(b_io_fd fd, char *buffer, int count);

/* Modifies the file_offset. Returns the resulting file offset. On error, return ERROR. */
int b_seek(b_io_fd fd, off_t offset, int whence);

/* Closes the file. Does not return anything, so on error the close is aborted
 * before any further damage is done to the volume. */
void b_close(b_io_fd fd);

#endif