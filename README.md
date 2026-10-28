# vsfsck: A File System Consistency Checker

A tool in C that checks and repairs the image of a small on-disk file system, the Very Simple File System (VSFS). It works like a minimal `fsck`: it reads the raw image, finds structural inconsistencies, fixes them, and writes the image back.

Group project for CSE321 Operating Systems, BRAC University, Spring 2025.

## The problem

A file system stores its structure in a few places that must agree with each other: the superblock describes the layout, bitmaps record which inodes and data blocks are in use, and inodes point to the data blocks that hold each file. A crash or a bug can leave these out of sync, for example:

- a block marked free while a file still uses it, so it gets handed out twice and data is overwritten;
- a block marked used that no file points to, which leaks space permanently;
- an inode pointing outside the disk, or two files claiming the same block.

The task was to take a deliberately corrupted VSFS image, find every inconsistency, repair it, and confirm that a second run reports a clean file system.

## The file system

| Block | Contents |
|---|---|
| 0 | Superblock: magic number `0xD34D`, block size, block count, layout pointers, inode size and count |
| 1 | Inode bitmap |
| 2 | Data bitmap |
| 3–7 | Inode table: 80 inodes of 256 bytes, each with 12 direct pointers and indirect pointers |
| 8–63 | Data blocks |

The image is 64 blocks of 4,096 bytes (256 KB).

## What it checks

1. **Superblock.** Validates the magic number, block size, total block count, the block numbers of the bitmaps and inode table, the first data block, and the inode size and count. Wrong values are corrected.
2. **Inode bitmap.** An inode is valid when it has at least one link and no deletion time. Bits set for invalid inodes are cleared, and valid inodes missing from the bitmap are marked.
3. **Data bitmap.** Builds the set of blocks that valid inodes actually reference, through direct pointers and the single indirect block. It then clears blocks marked used that nothing references, and marks referenced blocks that the bitmap shows as free.
4. **Duplicate blocks.** Keeps a reference count per data block and reports any block claimed by more than one inode.
5. **Bad blocks.** Reports direct pointers, indirect pointers and indirect-block entries that point outside the data region (blocks 8–63).

## How it works

- The whole image is read into memory once, and the superblock, bitmaps and inode table are accessed as typed structs over that buffer.
- Bitmaps are read and changed with small bit helpers.
- Checks run in order: superblock, inode bitmap, bad blocks, duplicates, then the data bitmap.
- After all checks, the repaired buffer is written back to the image in one go.

## Example

Run on the corrupted course image, then again on the repaired one:

```
$ ./vsfsck vsfs.img
Invalid block size: 1048628. Fixing to 4096
Invalid total blocks: 16384. Fixing to 64
Invalid inode bitmap block: 256. Fixing to 1
Invalid data bitmap block: 512. Fixing to 2
Invalid inode table start: 768. Fixing to 3
Invalid first data block: 2048. Fixing to 8
Invalid inode size: 65536. Fixing to 256
Invalid inode count: 20480. Fixing to 80
Superblock fixed.
Data block 9 is set in bitmap but should be unset. Fixing...
Data block 10 is set in bitmap but should be unset. Fixing...
Data block 12 is set in bitmap but should be unset. Fixing...
Data block 14 is set in bitmap but should be unset. Fixing...
Data bitmap inconsistencies found and fixed.
File system checked and repaired. Re-run fsck to verify.

$ ./vsfsck vsfs.img
Superblock is valid.
Data bitmap is consistent.
File system checked and repaired. Re-run fsck to verify.
```

## Building and running

```bash
make
./vsfsck path/to/vsfs.img
```

The tool **modifies the image in place**, so run it on a copy if you want to keep the original. The corrupted test image belongs to the course and isn't included here.

## Project structure

```
src/fsck.c   the checker
Makefile
```
