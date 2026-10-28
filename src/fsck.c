#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t magic;                
    uint32_t block_size;           
    uint32_t total_blocks;         
    uint32_t inode_bitmap_block;   
    uint32_t data_bitmap_block;
    uint32_t inode_table_start;
    uint32_t first_data_block;
    uint32_t inode_size;
    uint32_t inode_count;
    uint8_t reserved[4058];
} Superblock;

typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint32_t links_count;
    uint32_t blocks;
    uint32_t direct[12];
    uint32_t indirect;
    uint32_t double_indirect;
    uint32_t triple_indirect;
    uint8_t reserved[156];
} Inode;


#define BLOCK_SIZE 4096
#define TOTAL_BLOCKS 64
#define INODE_COUNT 80
#define FIRST_DATA_BLOCK 8
#define INODE_SIZE 256
#define SUPERBLOCK_INDEX 0
#define INODE_BITMAP_INDEX 1
#define DATA_BITMAP_INDEX 2
#define INODE_TABLE_START 3


int get_bit(uint8_t *bitmap, int index) {
    int byte_index = index / 8;
    int bit_index = index % 8;
    return (bitmap[byte_index] >> bit_index) & 1;
}

void set_bit(uint8_t *bitmap, int index, int value) {
    int byte_index = index / 8;
    int bit_index = index % 8;
    if (value) {
        bitmap[byte_index] |= (1 << bit_index);
    } else {
        bitmap[byte_index] &= ~(1 << bit_index);
    }
}

void validate_superblock(uint8_t *buffer, FILE *fp) {
    Superblock *sb = (Superblock *)buffer;
    int fixed = 0;

    if (sb->magic != 0xD34D) {
        printf("Invalid magic number: 0x%04X. Fixing to 0xD34D\n", sb->magic);
        sb->magic = 0xD34D;
        fixed = 1;
    }
    if (sb->block_size != BLOCK_SIZE) {
        printf("Invalid block size: %u. Fixing to %d\n", sb->block_size, BLOCK_SIZE);
        sb->block_size = BLOCK_SIZE;
        fixed = 1;
    }
    if (sb->total_blocks != TOTAL_BLOCKS) {
        printf("Invalid total blocks: %u. Fixing to %d\n", sb->total_blocks, TOTAL_BLOCKS);
        sb->total_blocks = TOTAL_BLOCKS;
        fixed = 1;
    }
    if (sb->inode_bitmap_block != INODE_BITMAP_INDEX) {
        printf("Invalid inode bitmap block: %u. Fixing to %d\n", sb->inode_bitmap_block, INODE_BITMAP_INDEX);
        sb->inode_bitmap_block = INODE_BITMAP_INDEX;
        fixed = 1;
    }
    if (sb->data_bitmap_block != DATA_BITMAP_INDEX) {
        printf("Invalid data bitmap block: %u. Fixing to %d\n", sb->data_bitmap_block, DATA_BITMAP_INDEX);
        sb->data_bitmap_block = DATA_BITMAP_INDEX;
        fixed = 1;
    }
    if (sb->inode_table_start != INODE_TABLE_START) {
        printf("Invalid inode table start: %u. Fixing to %d\n", sb->inode_table_start, INODE_TABLE_START);
        sb->inode_table_start = INODE_TABLE_START;
        fixed = 1;
    }
    if (sb->first_data_block != FIRST_DATA_BLOCK) {
        printf("Invalid first data block: %u. Fixing to %d\n", sb->first_data_block, FIRST_DATA_BLOCK);
        sb->first_data_block = FIRST_DATA_BLOCK;
        fixed = 1;
    }
    if (sb->inode_size != INODE_SIZE) {
        printf("Invalid inode size: %u. Fixing to %d\n", sb->inode_size, INODE_SIZE);
        sb->inode_size = INODE_SIZE;
        fixed = 1;
    }
    if (sb->inode_count != INODE_COUNT) {
        printf("Invalid inode count: %u. Fixing to %d\n", sb->inode_count, INODE_COUNT);
        sb->inode_count = INODE_COUNT;
        fixed = 1;
    }

    if (fixed) {
        fseek(fp, BLOCK_SIZE * SUPERBLOCK_INDEX, SEEK_SET);
        fwrite(sb, sizeof(Superblock), 1, fp);
        printf("Superblock fixed.\n");
    } else {
        printf("Superblock is valid.\n");
    }
}


void collect_data_blocks(int inode_index, Inode *inode, uint8_t *buffer, int total_blocks, int first_data_block, int *ref_count) {
    for (int i = 0; i < 12; i++) {
        if (inode->direct[i] != 0) {
            if (inode->direct[i] < first_data_block || inode->direct[i] >= total_blocks) {
                printf("Bad block index in direct pointer of inode %d: %u\n", 
                       inode_index, inode->direct[i]);
                inode->direct[i] = 0;
            } else {
                ref_count[inode->direct[i]]++;
            }
        }
    }
    if (inode->indirect != 0) {
        if (inode->indirect < first_data_block || inode->indirect >= total_blocks) {
            printf("Bad block index in indirect pointer of inode %d: %u\n", 
                   inode_index, inode->indirect);
            inode->indirect = 0;
        } else {
            uint32_t *indirect_block = (uint32_t *)(buffer + inode->indirect * BLOCK_SIZE);
            for (int i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++) {
                if (indirect_block[i] != 0) {
                    if (indirect_block[i] < first_data_block || indirect_block[i] >= total_blocks) {
                        printf("Bad block index in indirect block of inode %d: %u\n", 
                               inode_index, indirect_block[i]);
                        indirect_block[i] = 0;
                    } else {
                        ref_count[indirect_block[i]]++;
                    }
                }
            }
        }
    }
}

void check_duplicate_blocks(Inode *inodes, uint8_t *buffer, int inode_count, int first_data_block, int total_blocks) {
    uint32_t *block_usage = calloc(total_blocks, sizeof(uint32_t));
    int *first_inode = calloc(total_blocks, sizeof(int));
    if (!block_usage || !first_inode) {
        printf("Memory allocation failed for duplicate block check\n");
        free(block_usage);
        free(first_inode);
        return;
    }

    for (int i = 0; i < inode_count; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            for (int j = 0; j < 12; j++) {
                if (inodes[i].direct[j] != 0) {
                    if (inodes[i].direct[j] >= first_data_block && inodes[i].direct[j] < total_blocks) {
                        block_usage[inodes[i].direct[j]]++;
                        if (block_usage[inodes[i].direct[j]] == 1) {
                            first_inode[inodes[i].direct[j]] = i;
                        } else if (block_usage[inodes[i].direct[j]] > 1) {
                            printf("Block %u used by multiple inodes: %d and %d\n", 
                                   inodes[i].direct[j], first_inode[inodes[i].direct[j]], i);
                            inodes[i].direct[j] = 0;
                        }
                    }
                }
            }
            if (inodes[i].indirect != 0 && inodes[i].indirect >= first_data_block && inodes[i].indirect < total_blocks) {
                uint32_t *indirect_block = (uint32_t *)(buffer + inodes[i].indirect * BLOCK_SIZE);
                for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                    if (indirect_block[j] != 0) {
                        if (indirect_block[j] >= first_data_block && indirect_block[j] < total_blocks) {
                            block_usage[indirect_block[j]]++;
                            if (block_usage[indirect_block[j]] == 1) {
                                first_inode[indirect_block[j]] = i;
                            } else if (block_usage[indirect_block[j]] > 1) {
                                printf("Block %u used by multiple inodes: %d and %d\n", 
                                       indirect_block[j], first_inode[indirect_block[j]], i);
                                indirect_block[j] = 0;
                            }
                        }
                    }
                }
            }
        }
    }
    free(block_usage);
    free(first_inode);
}

void check_bad_blocks(Inode *inodes, uint8_t *buffer, int inode_count, int first_data_block, int total_blocks) {
    for (int i = 0; i < inode_count; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            for (int j = 0; j < 12; j++) {
                if (inodes[i].direct[j] != 0) {
                    if (inodes[i].direct[j] < first_data_block || inodes[i].direct[j] >= total_blocks) {
                        printf("Inode %d references bad block %u\n", i, inodes[i].direct[j]);
                        inodes[i].direct[j] = 0;
                    }
                }
            }
            if (inodes[i].indirect != 0) {
                if (inodes[i].indirect < first_data_block || inodes[i].indirect >= total_blocks) {
                    printf("Inode %d references bad indirect block %u\n", i, inodes[i].indirect);
                    inodes[i].indirect = 0;
                } else {
                    uint32_t *indirect_block = (uint32_t *)(buffer + inodes[i].indirect * BLOCK_SIZE);
                    for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                        if (indirect_block[j] != 0) {
                            if (indirect_block[j] < first_data_block || indirect_block[j] >= total_blocks) {
                                printf("Inode %d references bad block %u in indirect block\n", i, indirect_block[j]);
                                indirect_block[j] = 0;
                            }
                        }
                    }
                }
            }
        }
    }
}


void check_data_bitmap(uint8_t *data_bitmap, Inode *inodes, uint8_t *buffer) {
    int fixed = 0;
    uint8_t should_be_set[TOTAL_BLOCKS] = {0};

    for (int i = 0; i < FIRST_DATA_BLOCK; i++) {
        should_be_set[i] = 1;
    }
    for (int i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count == 0 || inodes[i].dtime != 0) continue;

        for (int j = 0; j < 12; j++) {
            uint32_t blk = inodes[i].direct[j];
            if (blk >= FIRST_DATA_BLOCK && blk < TOTAL_BLOCKS) {
                should_be_set[blk] = 1;
            }
        }

        if (inodes[i].indirect != 0 &&
            inodes[i].indirect >= FIRST_DATA_BLOCK &&
            inodes[i].indirect < TOTAL_BLOCKS) {
            should_be_set[inodes[i].indirect] = 1;
            uint32_t *indirect_block = (uint32_t *)(buffer + inodes[i].indirect * BLOCK_SIZE);
            for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                uint32_t blk = indirect_block[j];
                if (blk >= FIRST_DATA_BLOCK && blk < TOTAL_BLOCKS) {
                    should_be_set[blk] = 1;
                }
            }
        }
    }

    for (int i = 0; i < TOTAL_BLOCKS - FIRST_DATA_BLOCK;i++) {
        int bit_set = get_bit(data_bitmap, i);
        int expected = should_be_set[FIRST_DATA_BLOCK+i];

        if (bit_set != expected) {
            printf("Data block %d is %s in bitmap but should be %s. Fixing...\n",
                   FIRST_DATA_BLOCK + i,
                   bit_set ? "set" : "unset",
                   expected ? "set" : "unset");
            set_bit(data_bitmap, i,expected);
            fixed = 1;
        }
    }
    if (fixed) {
        printf("Data bitmap inconsistencies found and fixed.\n");
    } else {
        printf("Data bitmap is consistent.\n");
    }
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <filesystem_image>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb+");
    if (!fp) {
        printf("Cannot open file %s\n", argv[1]);
        return 1;
    }
    uint8_t *buffer = malloc(TOTAL_BLOCKS * BLOCK_SIZE);
    if (!buffer) {
        printf("Memory allocation failed\n");
        fclose(fp);
        return 1;
    }
    size_t read_size = fread(buffer, 1, TOTAL_BLOCKS * BLOCK_SIZE, fp);
    if (read_size != TOTAL_BLOCKS * BLOCK_SIZE) {
        printf("Failed to read the entire file system image\n");
        free(buffer);
        fclose(fp);
        return 1;
    }

    Inode *inodes = (Inode *)(buffer + INODE_TABLE_START * BLOCK_SIZE);
    validate_superblock(buffer, fp);

    uint8_t *inode_bitmap = buffer + INODE_BITMAP_INDEX * BLOCK_SIZE;

    //identify valid inodes
    int valid_inodes[INODE_COUNT];
    memset(valid_inodes, 0, sizeof(valid_inodes));
    for (int i = 0; i < INODE_COUNT; i++) {
        valid_inodes[i] = (inodes[i].links_count > 0 && inodes[i].dtime == 0);
    }

    // Check and fix inode bitmap
    for (int i = 0; i < INODE_COUNT; i++) {
        int bit = get_bit(inode_bitmap, i);
        if (bit && !valid_inodes[i]) {
            printf("Inode %d marked used but invalid, clearing bit\n", i);
            set_bit(inode_bitmap, i, 0);
        } else if (!bit && valid_inodes[i]) {
            printf("Inode %d valid but not marked, setting bit\n", i);
            set_bit(inode_bitmap, i, 1);
        }
    }

    check_bad_blocks(inodes, buffer, INODE_COUNT, FIRST_DATA_BLOCK, TOTAL_BLOCKS);

    check_duplicate_blocks(inodes, buffer, INODE_COUNT, FIRST_DATA_BLOCK, TOTAL_BLOCKS);

    uint8_t *data_bitmap = buffer + DATA_BITMAP_INDEX * BLOCK_SIZE;
    check_data_bitmap(data_bitmap, inodes, buffer);

    fseek(fp, 0, SEEK_SET);
    fwrite(buffer, 1, TOTAL_BLOCKS * BLOCK_SIZE, fp);
    fclose(fp);
    free(buffer);
    printf("File system checked and repaired. Re-run fsck to verify.\n");
    return 0;
}