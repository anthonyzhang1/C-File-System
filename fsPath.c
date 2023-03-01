/**************************************************************
* Class: CSC-415-02 Fall 2021
* Names: Edel Jhon Cenario, Michael Wang, Michael Widergren, Anthony Zhang
* Student IDs: 921121224, 921460979, 921363622, 921544101
* GitHub Name: anthonyzhang1
* Group Name: Michael
* Project: Basic File System
*
* File: fsPath.c
*
* Description: Functions pertaining to paths, path parsing, and
*  searching in directories for a particular directory entry.
*
**************************************************************/

#include "fsInit.h"
#include "mfs.h"

long long getParentBasenameStartBlock(const char *path) {
    if (strcmp(path, "/") == 0) return vcb->root_dir_start_block;

    int path_is_absolute = FALSE;
    if (path[0] == '/') path_is_absolute = TRUE;
    
    dir_entry *parent_dir = malloc(vcb->dir_blocks * vcb->block_size);

    if (path_is_absolute) { // parent_dir = root dir
        if (customLBAread(parent_dir, vcb->dir_blocks, vcb->root_dir_start_block,
            "abs_path init parent_dir") == ERROR) {
            free(parent_dir);
            parent_dir = NULL;
            return ERROR;
        }
    } else { // parent_dir = cwd
        if (customLBAread(parent_dir, vcb->dir_blocks, getCWDstartBlock(),
            "rel_path init parent_dir") == ERROR) {
            free(parent_dir);
            parent_dir = NULL;
            return ERROR;
        }
    }

    // A copy of the path is needed since strtok_r modifies its source string.
    // This must be freed.
    char *path_copy = strndup(path, strlen(path));

    int dirs_in_path = 0; // number of directories in the path
    int path_length = strlen(path_copy); // length of path in characters
    int last_char_was_slash = FALSE; // tracks repeated slashes

    // cwd is implicitly a dir in a relative path
    if (!path_is_absolute) dirs_in_path++;

    for (int i = 0; i < path_length; i++) {
        if (last_char_was_slash && path_copy[i] == '/') { // check for repeated slashes
            printf("Invalid path due to repeated slashes.\n");
            free(parent_dir);
            parent_dir = NULL;
            free(path_copy);
            path_copy = NULL;
            return ERROR;
        }

        if (path_copy[i] == '/') {
            last_char_was_slash = TRUE;
            dirs_in_path++;
        } else last_char_was_slash = FALSE;
    }

    // if there is a slash at the end of the path, ignore it
    if (path_copy[path_length - 1] == '/') dirs_in_path--;

    char *saveptr; // used internally by strtok_r
    char *child_dir_name = strtok_r(path_copy, "/", &saveptr);

    int dir_path_index = 1; // current directory in path, starting at 1
    int entry_index;

    while (child_dir_name && (dir_path_index < dirs_in_path)) {
        // Search parent_dir for child_dir_name
        entry_index = getDirEntryIndexByName(parent_dir, child_dir_name);
        if (entry_index == ERROR || entry_index == NOT_FOUND) { // dir not found
            printf("Invalid path. Could not find directory entry '%s'.\n", child_dir_name);
            free(parent_dir);
            parent_dir = NULL;
            free(path_copy);
            path_copy = NULL;
            return ERROR;
        }

        // if the path was valid, but tried to path into a non-directory
        if (parent_dir[entry_index].type != DIRECTORY) {
            printf("Invalid path. '%s' is not a directory.\n", child_dir_name);
            free(parent_dir);
            parent_dir = NULL;
            free(path_copy);
            path_copy = NULL;
            return ERROR;
        }

        // read child directory into parent_dir
        if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir[entry_index].start_block,
            "getParentStartBlock update parent_dir") == ERROR) {
            free(parent_dir);
            parent_dir = NULL;
            free(path_copy);
            path_copy = NULL;
            return ERROR;
        }

        child_dir_name = strtok_r(NULL, "/", &saveptr);
        dir_path_index++;
    }

    long long parent_basename_start_block = parent_dir[0].start_block;

    free(parent_dir);
    parent_dir = NULL;
    free(path_copy);
    path_copy = NULL;

    return parent_basename_start_block;
}

char* getBasename(const char *path) {
    // A copy of the path is needed since strtok_r modifies its source string.
    // Needs to be freed!
    char *path_copy = strndup(path, strlen(path));

    char *saveptr; // used internally by strtok_r
    char *child_dir_name = strtok_r(path_copy, "/", &saveptr);

    char *dir_name = NULL;
    int dir_name_len = 0;

    // loop ends when there is no more children in the path
    while (child_dir_name) {
        dir_name = child_dir_name;
        dir_name_len = strlen(dir_name);

        child_dir_name = strtok_r(NULL, "/", &saveptr);
    }

    // If the basename is an empty string, e.g. path was "/"
    if (dir_name_len == 0 || !dir_name) {
        free(path_copy);
        path_copy = NULL;
        return NULL;
    }

    // Needs to be freed in the calling function.
    char *basename = strndup(dir_name, dir_name_len);

    free(path_copy);
    path_copy = NULL;

    return basename; // caller needs to free this
}

int getDirEntryIndexByName(dir_entry *dir, const char *dir_entry_name) {
    if (dir[0].type != DIRECTORY) {
        printf("Error: dir in getDirEntryIndexByName() was not a directory.\n");
        return ERROR;
    } else if (dir_entry_name == NULL) { // if root directory, return itself
        return 0;
    }

    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        if ((dir[i].type != FREE_ENTRY) && (strcmp(dir[i].name, dir_entry_name) == 0)) {
            return i;
        }
    }

    return NOT_FOUND; // entry not found
}

int getDirEntryIndexByStartBlock(dir_entry *dir, uint64_t dir_entry_start_block) {
    if (dir[0].type != DIRECTORY) {
        printf("Error: dir in getDirEntryIndexByStartBlock() was not a directory.\n");
        return ERROR;
    }

    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        if ((dir[i].type != FREE_ENTRY) && (dir[i].start_block == dir_entry_start_block)) {
            return i;
        }
    }

    return NOT_FOUND; // entry not found
}

int getDirFreeEntryIndex(dir_entry *dir) {
    if (dir[0].type != DIRECTORY) {
        printf("Error: dir in getDirFreeEntryIndex() was not a directory.\n");
        return ERROR;
    }

    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        if (dir[i].type == FREE_ENTRY) return i;
    }

    return NOT_FOUND; // no free entries in dir
}

int getDirNextUsedEntryIndex(dir_entry *dir, int start_index) {
    if (dir[0].type != DIRECTORY) {
        printf("Error: dir in getDirNextUsedEntryIndex() was not a directory.\n");
        return ERROR;
    } else if (start_index < 0 || start_index >= MAX_DIRECTORY_ENTRIES) {
        printf("Error: Invalid start index passed to getDirNextUsedEntryIndex().\n");
        return ERROR;
    }

    for (int i = start_index; i < MAX_DIRECTORY_ENTRIES; i++) {
        if (dir[i].type != FREE_ENTRY) return i;
    }

    return NOT_FOUND; // entries from start_index to MAX_DIR_ENTRIES are free
}

int getDirNumUsedEntries(dir_entry *dir) {
    if (dir[0].type != DIRECTORY) {
        printf("Error: dir in getDirNumUsedEntries() was not a directory.\n");
        return ERROR;
    }

    int num_used_entries = 0;
    for (int i = 0; i < MAX_DIRECTORY_ENTRIES; i++) {
        if (dir[i].type != FREE_ENTRY) num_used_entries++;
    }

    return num_used_entries;
}

char* getDirAbsPath(dir_entry *dir, char *buf, size_t size) {
    if (!buf || !dir) {
        printf("buf or dir is NULL in getDirAbsPath.\n");
        return NULL;
    } else if (size <= 1) { // needs space for null-terminator
        printf("Invalid size passed into getDirAbsPath.\n");
        return NULL;
    } else if (dir[0].type != DIRECTORY) {
        printf("dir is not a directory in getDirAbsPath.\n");
        return NULL;
    } else if (dir[0].start_block == vcb->root_dir_start_block) { // root dir
        strcpy(buf, "/");
        return buf;
    }

    // An array of filenames, later concatenated to create an absolute path.
    char dirs_in_path[MAX_PATHNAME_DEPTH][MAX_DE_NAME_LENGTH];
    int dir_num = 0;
    uint64_t curr_start_block = dir[0].start_block;

    dir_entry *parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
    if (customLBAread(parent_dir, vcb->dir_blocks, dir[1].start_block,
        "getDirAbsPath parent init") == ERROR) {
        free(parent_dir);
        parent_dir = NULL;
        return NULL;
    }

    do {
        // searches in the parent dir for the entry with the matching start block
        int entry_index = getDirEntryIndexByStartBlock(parent_dir, curr_start_block);
        if (entry_index == ERROR || entry_index == NOT_FOUND) { // error
            printf("Error getting the directory entry index in getDirAbsPath.\n");
            free(parent_dir);
            parent_dir = NULL;
            return NULL;
        }

        if (dir_num >= MAX_PATHNAME_DEPTH) { // check that we do not write out of bounds
            printf("Maximum subdirectory depth of %d reached.\n", MAX_PATHNAME_DEPTH);
            free(parent_dir);
            parent_dir = NULL;
            return NULL;
        }

        strcpy(dirs_in_path[dir_num], parent_dir[entry_index].name);
        dir_num++;
        curr_start_block = parent_dir[0].start_block;

        // read the parent of parent_dir into parent_dir
        if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir[1].start_block,
            "getDirAbsPath parent loop") == ERROR) {
            free(parent_dir);
            parent_dir = NULL;
            return NULL;
        }
    } while (curr_start_block != vcb->root_dir_start_block);

    // size of buf >= 2 due to the check at the beginning of function
    buf[0] = '\0';
    int buf_count = 1; // num of chars in buf
    int filename_len; // length of filename excluding null-terminator
    dir_num--; // decrement due to the do while loop

    while (dir_num >= 0) {
        filename_len = strlen(dirs_in_path[dir_num]);

        if (buf_count + filename_len >= size) {
            printf("Error: Maximum path size of %ld exceeded.\n", size);
            free(parent_dir);
            parent_dir = NULL;
            return NULL;
        }

        strcat(buf, "/");
        strcat(buf, dirs_in_path[dir_num]);
        buf_count += filename_len + 1;

        dir_num--;
    }

    free(parent_dir);
    parent_dir = NULL;

    return buf;
}

char* getDirName(dir_entry *dir, char *buf, size_t buf_size) {
    if (!dir || !buf) {
        printf("buf or dir is NULL in getDirName.\n");
        return NULL;
    } else if (buf_size < MAX_DE_NAME_LENGTH) { // buf too small for dir name
        printf("buf_size in getDirName is too small to hold a dir entry name.\n");
        return NULL;
    } else if (dir[0].type != DIRECTORY) {
        printf("dir is not a directory in getDirName.\n");
        return NULL;
    } else if (dir[0].start_block == vcb->root_dir_start_block) { // root dir special case
        strcpy(buf, ROOT_NAME);
        return buf;
    }

    dir_entry *parent_dir = NULL; // parent dir to search for the dir's name
    uint64_t dir_start_block = dir[0].start_block;

    parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
    if (customLBAread(parent_dir, vcb->dir_blocks, dir[1].start_block,
        "getDirName parent init") == ERROR) {
        goto free_and_return_null;
    }

    // searches in the parent dir for the entry with dir's start block
    int entry_index = getDirEntryIndexByStartBlock(parent_dir, dir_start_block);
    if (entry_index == ERROR || entry_index == NOT_FOUND) { // error
        printf("Error getting the directory entry index in getDirName.\n");
        goto free_and_return_null;
    }

    strcpy(buf, parent_dir[entry_index].name);

    free(parent_dir);
    parent_dir = NULL;

    return buf; // success

    free_and_return_null: // Label for error handling. Free the mallocs and return NULL.
    free(parent_dir);
    parent_dir = NULL;

    return NULL;
}

int isSubDirOf(dir_entry *dir, uint64_t ancestor_start_block) {
    // all dirs are subdirs of the root dir, except for the root dir itself
    if (ancestor_start_block == vcb->root_dir_start_block) {
        if (dir[0].start_block == vcb->root_dir_start_block) return FALSE;
        else return TRUE;
    }

    dir_entry *parent_dir = NULL; // the parent dir of dir

    int is_sub_dir_of = FALSE; // if dir is a sub dir of ancestor dir
    int dirs_checked = 0; // counter for how many dirs we have checked

    parent_dir = malloc(vcb->dir_blocks * vcb->block_size);
    if (customLBAread(parent_dir, vcb->dir_blocks, dir[1].start_block,
        "isSubDirOf parent_dir init") == ERROR) {
        goto free_and_return_error;
    }

    // keep going up a directory until we hit the root or we checked the maximum number of dirs
    do {
        if (parent_dir[0].start_block == ancestor_start_block) { // match found
            is_sub_dir_of = TRUE;
            break;
        }

        dirs_checked++;
        if (dirs_checked >= MAX_PATHNAME_DEPTH) {
            printf("Maximum directory checks limit of %d reached.\n", MAX_PATHNAME_DEPTH);
            goto free_and_return_error;
        }

        // read the parent of parent_dir into parent_dir
        if (customLBAread(parent_dir, vcb->dir_blocks, parent_dir[1].start_block,
            "isSubDirOf parent loop") == ERROR) {
            goto free_and_return_error;
        }
    } while (parent_dir[0].start_block != vcb->root_dir_start_block);

    free(parent_dir);
    parent_dir = NULL;

    return is_sub_dir_of;

    free_and_return_error: // Label for error handling. Free the mallocs and return ERROR.
    free(parent_dir);
    parent_dir = NULL;

    return ERROR;
}