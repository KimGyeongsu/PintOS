#ifndef FILESYS_FREE_MAP_H
#define FILESYS_FREE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"
#include <bitmap.h>

struct file *free_map_file;   /* Free map file. */
struct bitmap *free_map;      /* Free map, one bit per sector. */

void free_map_init (void);
void free_map_read (void);
void free_map_create (void);
void free_map_open (void);
void free_map_close (void);

bool free_map_allocate (size_t, block_sector_t *);
void free_map_release (block_sector_t, size_t);

/////////////////////EXTENSIBLE/////////////////////////////
bool
free_map_allocate_extensible (enum status_inode status, 
	size_t block_offset ,struct inode_disk * inode_disk, bool block_write_flag);

void 
free_map_release_extensible(enum status_inode status, size_t block_offset ,struct inode_disk * inode_disk);

#endif /* filesys/free-map.h */
