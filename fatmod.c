#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <linux/msdos_fs.h>
#include <stdint.h>

#define FALSE 0
#define TRUE 1
#define FFFFFFF8 4294967288 //magic number
#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes

#define N_ROOTDIR_CLUSTERS 2 //how many clusters root dir has. (not sure about this yet)

int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
unsigned int allocate_block(int fd, unsigned int last_block);
void getCluster(int fd, unsigned char* cluster, int cluster_num);
void print_cluster(unsigned char* cluster);
void print_FAT(unsigned char* sector);
void print_FAT_mini(unsigned char* sector);
void system_specs(struct fat_boot_sector* boot);
void create_file(int fd, char* filename);
void delete_file(int fd, char* filename);
void write_file(int fd, char* filename, int offset, int n, unsigned char data);
void display_root(int fd);
void display_contents(int fd, char* fName);
void display_raw_contents(int fd, char* fName);
void display_help();
void writeFAT(int fd, unsigned char* FAT);
unsigned char* getFAT(int fd);
unsigned int* traceCluster(unsigned char* FAT, int startingCluster, int* size);
void clearClusterChain(unsigned char* FAT, int startingCluster);


//general purpose buffers
unsigned char sector[SECTORSIZE];    
unsigned char cluster[CLUSTERSIZE];    
int data_start_sector;
struct fat_boot_sector* boot;
int cluster_count;
int sector_count; 
unsigned int FAT_start; //interms of sectors number
unsigned int FAT_end; //interms of sectors number

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Insufficient arguments provided. Use -h for help.\n");
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0) {
        display_help();
        return 0;
    }
    char diskname[128];
    int disk_fd;
    strcpy(diskname, argv[1]);
    disk_fd = open(diskname, O_SYNC | O_RDWR);
    readsector(disk_fd, sector, 0);
    boot = (struct fat_boot_sector*) sector;
    data_start_sector = boot->reserved + (boot->fats * boot->fat32.length); 
    cluster_count = boot->fat32.length * 512 / 4;
    sector_count = boot->total_sect;
    FAT_start = 32;
    FAT_end = 32 + boot->fat32.length;

    if (strcmp(argv[2], "-l") == 0) {
        display_root(disk_fd);
        print_FAT_mini(getFAT(disk_fd));
    } else if (strcmp(argv[2], "-r") == 0 && argc > 4) {
        if (strcmp(argv[3], "-a") == 0) {
            display_contents(disk_fd, argv[4]);
        } else if (strcmp(argv[3], "-b") == 0) {
            display_raw_contents(disk_fd, argv[4]);
        }
    } else if (strcmp(argv[2], "-c") == 0 && argc > 3) {
        display_root(disk_fd);
        print_FAT_mini(getFAT(disk_fd));
        create_file(disk_fd, argv[3]);
        display_root(disk_fd);
        print_FAT_mini(getFAT(disk_fd));
    } else if (strcmp(argv[2], "-d") == 0 && argc > 3) {
        display_root(disk_fd);
        print_FAT_mini(getFAT(disk_fd));
        delete_file(disk_fd, argv[3]);
        display_root(disk_fd);
        print_FAT_mini(getFAT(disk_fd));
    } else if (strcmp(argv[2], "-w") == 0 && argc > 6) {
        write_file(disk_fd, argv[3], atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
    } else {
        printf("Invalid arguments provided. Use -h for help.\n");
    }

    close(disk_fd);
    return 0;
}

void system_specs(struct fat_boot_sector* boot) {
    printf("Sector per cluster: %u\n", boot->sec_per_clus);
    printf("Total Sector count: %u\n", boot->total_sect);
    printf("Number of FATs: %u\n", boot->fats);
    printf("sectors per fat: %u\n", boot->fat32.length);
    printf("Root start cluster #: %u\n", boot->fat32.root_cluster);
    printf("Reserved: %u\n", boot->reserved);
}

int readsector(int fd, unsigned char *buf, unsigned int snum) {
	off_t offset;
	int n;
	offset = snum * SECTORSIZE;
	lseek (fd, offset, SEEK_SET);
	n  = read(fd, buf, SECTORSIZE);

    return n == SECTORSIZE ? 0 : -1;
}

int writesector(int fd, unsigned char *buf, unsigned int snum) {
	off_t offset;
	int n;
	offset = snum * SECTORSIZE;
	lseek (fd, offset, SEEK_SET);
	n  = write (fd, buf, SECTORSIZE);
        fsync (fd);

    return n == SECTORSIZE ? 0 : -1;
}

void getCluster(int fd, unsigned char* cluster, int cluster_num) {
    cluster_num -= 2; //to compansate for that fact that clusters start at 2
    unsigned char first_sector[SECTORSIZE];
    unsigned char second_sector[SECTORSIZE];

    readsector(fd, first_sector, (data_start_sector + 2 * cluster_num));
    readsector(fd, second_sector, (data_start_sector + 2 * cluster_num + 1));

    memcpy(cluster, first_sector, SECTORSIZE);
    memcpy(cluster + SECTORSIZE, second_sector, SECTORSIZE);
}

unsigned char* getFAT(int fd) {
    // Calculate the length of the FAT in sectors
    int FAT_length = FAT_end - FAT_start;

    // Creating an array that can fit the entire FAT.
    unsigned char* FAT = (unsigned char*)(malloc(sizeof(char) * FAT_length * SECTORSIZE));
    // Check if memory allocation was successful
    if (FAT == NULL) {
        printf("Memory allocation failed\n");
        return NULL;
    }

    // Filling FAT array with FAT binary data by reading sector by sector
    int iteration = 0;    
    for (int i = FAT_start; i < FAT_end; i++, iteration++) {
        readsector(fd, FAT + (iteration * SECTORSIZE), i);
    }

    return FAT;
}

//return an array of cluster numbers that are chained together
unsigned int* traceCluster(unsigned char* FAT, int startingCluster, int* size) {

    uint32_t currentClusterNumber = startingCluster;
    uint32_t entry_value = 0;
    unsigned int* clusterChain;
    unsigned int temp[cluster_count];
    temp[0] = currentClusterNumber;
    int increment = 0;
    int index = 0;
    do {
        // printf("LE: 0x%02X%02X%02X%02X\n", 
        //         FAT[4*currentClusterNumber + 3], 
        //         FAT[4*currentClusterNumber + 2], 
        //         FAT[4*currentClusterNumber + 1], 
        //         FAT[4*currentClusterNumber + 0]
        //         );

        entry_value =((uint32_t)((uint8_t)FAT[4*currentClusterNumber + increment + 0]) 
                    | (uint32_t)((uint8_t)FAT[4*currentClusterNumber + increment + 1]) << 8 
                    | (uint32_t)((uint8_t)FAT[4*currentClusterNumber + increment + 2]) << 16 
                    | (uint32_t)((uint8_t)FAT[4*currentClusterNumber + increment + 3]) << 24);

        increment += 4;
        currentClusterNumber = entry_value;

        if (entry_value > 0 && entry_value < FFFFFFF8) {
            index++;
            temp[index] = currentClusterNumber;
        }
    } while (entry_value < FFFFFFF8 && entry_value > 0 && currentClusterNumber < cluster_count);

    clusterChain = (unsigned int*)(malloc(sizeof(int)*increment/4));
    for (int i = 0; i < index; i++) {
        clusterChain[i] = temp[i];
    }
    *size = index;
    return clusterChain;
}

void clearClusterChain(unsigned char* FAT, int startingCluster) {
    uint32_t currentClusterNumber = startingCluster;
    uint32_t entry_value = 0;
    do {
        entry_value = ((uint32_t)((uint8_t)FAT[4*currentClusterNumber + 0]) 
                    | (uint32_t)((uint8_t)FAT[4*currentClusterNumber + 1]) << 8 
                    | (uint32_t)((uint8_t)FAT[4*currentClusterNumber + 2]) << 16 
                    | (uint32_t)((uint8_t)FAT[4*currentClusterNumber + 3]) << 24);

        // Set the FAT entry for the current cluster to 0x00000000
        FAT[4*currentClusterNumber + 0] = 0;
        FAT[4*currentClusterNumber + 1] = 0;
        FAT[4*currentClusterNumber + 2] = 0;
        FAT[4*currentClusterNumber + 3] = 0;

        currentClusterNumber = entry_value;
    } while (entry_value < 0x0FFFFFF8 && entry_value > 0);
}

void writeCluster(int fd, unsigned char* buf, unsigned int clusterNumber) {
    // Get the number of sectors per cluster and the first data sector from the boot sector
    unsigned int sectorsPerCluster = boot->sec_per_clus;
    unsigned int firstDataSector = data_start_sector;

    // Convert the cluster number to a sector number
    unsigned int sectorNumber = firstDataSector + (clusterNumber - 2) * sectorsPerCluster;

    // Write the data to each sector in the cluster
    for (unsigned int i = 0; i < sectorsPerCluster; i++) {
        writesector(fd, buf + i * SECTOR_SIZE, sectorNumber + i);
    }
}

void display_root(int fd) {
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);

    unsigned char fileName[8];
    unsigned char fileExte[3];
    unsigned char fileSize[4];
    int dentry_length = 32;

    for (int i = 0; i < size; i++) {
        getCluster(fd, cluster, clusterChain[i]);
        print_cluster(cluster);
        for (int k = 1; k < CLUSTERSIZE / dentry_length; k++) {
            int offset = k*32;
            if (cluster[offset+i] == 0)
                break;
            
            for (int i = 0; i < 32; i++) {
                if (i < 8) {
                    fileName[i] = (cluster[offset+i] != 0x20) ? cluster[offset+i] : '\0';
                }
                if (i >= 8 && i < 11) {
                    fileExte[i-8] = (cluster[offset+i] != 0x20) ? cluster[offset+i] : '\0';
                }
                if (i > 27 && i < 32) {
                    fileSize[i-28] = cluster[offset+i];
                }
            }

            // Check if the file has been deleted
            if (fileName[0] == 0xE5) {
                continue;
            }

            for (int j = 0; j < 8; j++) {
                printf("%c",fileName[j]);
            }
            printf(".");
            for (int j = 0; j < 3; j++) {
                printf("%c",fileExte[j]);
            }
            printf(" ");
            int size = (  (int)((uint8_t)fileSize[0]) 
                        | (int)((uint8_t)fileSize[1]) << 8 
                        | (int)((uint8_t)fileSize[2]) << 16 
                        | (int)((uint8_t)fileSize[3]) << 24  );
            printf("%d", size);
            
            printf("\n");
        }
        printf("\n");
    }
}

void trimTrailingSpaces(char *str) {
    int len = strlen(str);
    while (len > 0 && str[len - 1] == ' ') {
        str[--len] = '\0';
    }
}

void display_contents(int fd, char* fName) {
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);

    unsigned char fileName[8];
    unsigned char clusterBits[4];
    int dentry_length = 32;

    for (int i = 0; i < size; i++) {
        getCluster(fd, cluster, clusterChain[i]);
        // print_cluster(cluster);
        for (int k = 1; k < CLUSTERSIZE / dentry_length; k++) {

            //     for (int i = k*32; i < k*32+32; i++){
            //         if (i == 0 || i % 16 == 0) {
            //             printf("\n");
            //         }

            //         if (cluster[i] != 0) {
            //             printf("\x1B[31m"); // Red color
            //             printf("%02X ", cluster[i]);
            //             printf("\x1B[0m"); // Reset color
            //         } else {
            //             printf("%02X ", cluster[i]);
            //         }
            //     }
            // printf("\n");

            int offset = k*32;
            if (cluster[offset+i] == 0)
                break;
            
            for (int i = 0; i < 32; i++) {
                if (i < 8) {
                    fileName[i] = cluster[offset+i];
                }
                if (i > 19 && i < 22) {
                    clusterBits[i-19] = cluster[offset+i];
                }
                if (i > 25 && i < 28) {
                    clusterBits[i-26] = cluster[offset+i];
                }
            }
            printf("\n");

            char fileNameStr[9]; 
            strncpy(fileNameStr, (char*)fileName, 8); 
            fileNameStr[8] = '\0';

            trimTrailingSpaces(fileNameStr);

            // Compare strings
            if (strcasecmp(fileNameStr, fName) == 0) {
                int number = (  (int)((uint8_t)clusterBits[0]) 
                        | (int)((uint8_t)clusterBits[1]) << 8 
                        | (int)((uint8_t)clusterBits[2]) << 16 
                        | (int)((uint8_t)clusterBits[3]) << 24  );

                unsigned int* clusterChain = traceCluster(FAT, number, &size);
                
                for (int i = 0; i < size; i++) {
                    struct msdos_dir_entry* dentry;
                    getCluster(fd, cluster, clusterChain[i]);
                    dentry = (struct msdos_dir_entry*) cluster;
                    printf("%s", dentry->name);
                    // print_cluster(cluster);
                }

                printf("\n");
                return;
            } 
        }
        printf("\n");
    }


}

void display_raw_contents(int fd, char* fName) {
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);

    unsigned char fileName[8];
    unsigned char clusterBits[4];
    int dentry_length = 32;

    for (int i = 0; i < size; i++) {
        getCluster(fd, cluster, clusterChain[i]);
        // print_cluster(cluster);
        for (int k = 1; k < CLUSTERSIZE / dentry_length; k++) {

            //     for (int i = k*32; i < k*32+32; i++){
            //         if (i == 0 || i % 16 == 0) {
            //             printf("\n");
            //         }

            //         if (cluster[i] != 0) {
            //             printf("\x1B[31m"); // Red color
            //             printf("%02X ", cluster[i]);
            //             printf("\x1B[0m"); // Reset color
            //         } else {
            //             printf("%02X ", cluster[i]);
            //         }
            //     }
            // printf("\n");

            int offset = k*32;
            if (cluster[offset+i] == 0)
                break;
            
            for (int i = 0; i < 32; i++) {
                if (i < 8) {
                    fileName[i] = cluster[offset+i];
                }
                if (i > 19 && i < 22) {
                    clusterBits[i-19] = cluster[offset+i];
                }
                if (i > 25 && i < 28) {
                    clusterBits[i-26] = cluster[offset+i];
                }
            }
            printf("\n");

            char fileNameStr[9]; 
            strncpy(fileNameStr, (char*)fileName, 8); 
            fileNameStr[8] = '\0';

            trimTrailingSpaces(fileNameStr);

            // Compare strings
            if (strcasecmp(fileNameStr, fName) == 0) {
                int number = (  (int)((uint8_t)clusterBits[0]) 
                        | (int)((uint8_t)clusterBits[1]) << 8 
                        | (int)((uint8_t)clusterBits[2]) << 16 
                        | (int)((uint8_t)clusterBits[3]) << 24  );

                unsigned int* clusterChain = traceCluster(FAT, number, &size);
                
                for (int i = 0; i < size; i++) {
                    struct msdos_dir_entry* dentry;
                    getCluster(fd, cluster, clusterChain[i]);
                    print_cluster(cluster);
                }

                printf("\n");
                return;
            } 
        }
        printf("\n");
    }

    
}

void writeFAT(int fd, unsigned char* FAT) {
    // Calculate the length of the FAT in sectors
    int FAT_length = FAT_end - FAT_start;

    // Write the FAT back to the disk image sector by sector
    int iteration = 0;
    for (int i = FAT_start; i < FAT_end; i++, iteration++) {
        writesector(fd, FAT + (iteration * SECTORSIZE), i);
    }
}

void create_file(int fd, char* filename) {
    // Get the FAT and trace the root directory clusters
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);

    // Find an empty directory entry
    int i, j;
    struct msdos_dir_entry* dep;
    for (j = 0; j < size; j++) {
        getCluster(fd, cluster, clusterChain[j]);
        dep = (struct msdos_dir_entry*) cluster;
        for (i = 0; i < CLUSTERSIZE; i += sizeof(struct msdos_dir_entry), dep++) {
            if (dep->name[0] == 0x00 || dep->name[0] == 0xE5) {
                // This directory entry is empty
                break;
            }
        }
        if (i < CLUSTERSIZE) {
            // Found an empty directory entry
            break;
        }
    }

    if (j == size) {
        printf("Root directory is full\n");
        return;
    }

    // Split the filename and extension
    char name[9], ext[4];
    sscanf(filename, "%8[^.].%3s", name, ext);
    // Convert filename and extension to uppercase
    for (int i = 0; i < 8; i++) {
        name[i] = toupper(name[i]);
    }
    for (int i = 0; i < 3; i++) {
        ext[i] = toupper(ext[i]);
    }

    // Set the filename and extension
    strncpy((char*)dep->name, name, 8);
    strncpy((char*)dep->name + 8, ext, 3);

    // Set the attribute byte (0x20 for normal file)
    dep->attr = 0x20;

    // Set the initial size (0)
    dep->size = 0;

    // Find a free cluster in the FAT
    int freeCluster = -1;
    int FATSIZE = (FAT_end - FAT_start) * (SECTORSIZE / 4);
    for (int k = 2; k < FATSIZE; k++) {
        uint32_t entry_value = ((uint32_t)((uint8_t)FAT[4*k + 0]) 
                            | (uint32_t)((uint8_t)FAT[4*k + 1]) << 8 
                            | (uint32_t)((uint8_t)FAT[4*k + 2]) << 16 
                            | (uint32_t)((uint8_t)FAT[4*k + 3]) << 24);
        if (entry_value == 0x00000000) {
            freeCluster = k;
            break;
        }
    }
    if (freeCluster == -1) {
        printf("Disk is full\n");
        return;
    }

    // Mark the free cluster as used in the FAT
    FAT[4*freeCluster + 0] = 0xFF;
    FAT[4*freeCluster + 1] = 0xFF;
    FAT[4*freeCluster + 2] = 0xFF;
    FAT[4*freeCluster + 3] = 0x0F;

    // Set the first cluster number of the file to the newly allocated cluster
    dep->starthi = (freeCluster >> 16) & 0xFFFF;
    dep->start = freeCluster & 0xFFFF;

    // Write the updated FAT back to the disk
    writeFAT(fd, FAT);

    // Write the cluster back to disk
    writeCluster(fd, cluster, clusterChain[j]);
}

void delete_file(int fd, char* filename) {
    // Get the FAT and trace the root directory clusters
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);

    // Convert filename to uppercase
    char filename_copy[strlen(filename) + 1];
    strcpy(filename_copy, filename);
    for (int i = 0; i < strlen(filename_copy); i++) {
        filename_copy[i] = toupper(filename_copy[i]);
    }

    // Find the directory entry for the file
    int i, j;
    struct msdos_dir_entry* dep;
    for (j = 0; j < size; j++) {
        getCluster(fd, cluster, clusterChain[j]);
        dep = (struct msdos_dir_entry*) cluster;
        for (i = 0; i < CLUSTERSIZE; i += sizeof(struct msdos_dir_entry), dep++) {
            char name[9], ext[4];
            strncpy(name, (char*)dep->name, 8);
            strncpy(ext, (char*)dep->name + 8, 3);
            name[8] = '\0';  // Null-terminate the strings
            ext[3] = '\0';
            trimTrailingSpaces(name);
            char full_filename[13];
            sprintf(full_filename, "%s.%s", name, ext);  // Combine the name and extension

            if (strcmp(full_filename, filename_copy) == 0) {
                // This is the directory entry for the file
                break;
            }
        }
        if (i < CLUSTERSIZE) {
            // Found the directory entry
            break;
        }
    }

    if (j == size) {
        printf("File not found\n");
        return;
    }

    // Get the first cluster number of the file
    unsigned int first_cluster = dep->start | (dep->starthi << 16);

    // Deallocate all its data blocks by updating the FAT
    clearClusterChain(FAT, first_cluster);

    // Write the updated FAT back to the disk
    writeFAT(fd, FAT);

    // Remove the directory entry
    dep->name[0] = 0xE5;  // Mark the directory entry as deleted

    // Clear the name section
    for (int j = 1; j < 11; j++) {
        dep->name[j] = 0x00;
    }

    // Write the cluster back to disk
    writeCluster(fd, cluster, clusterChain[j]);
}

void write_file(int fd, char* filename, int offset, int n, unsigned char data) { //NOT WORKING, NEED UPDATES TO WORK FOR FAT32
    // Get the root directory
    unsigned char root[CLUSTERSIZE * N_ROOTDIR_CLUSTERS];
    getCluster(fd, root, 2);  // Assuming root directory is at cluster 2

    // Find the directory entry for the file
    int i;
    for (i = 0; i < CLUSTERSIZE * N_ROOTDIR_CLUSTERS; i += 32) {
        char name[9], ext[4];
        strncpy(name, (char*)&root[i], 8);
        strncpy(ext, (char*)&root[i + 8], 3);
        name[8] = '\0';  // Null-terminate the strings
        ext[3] = '\0';

        char full_filename[13];
        sprintf(full_filename, "%s.%s", name, ext);  // Combine the name and extension

        if (strcmp(full_filename, filename) == 0) {
            // This is the directory entry for the file
            break;
        }
    }

    if (i == CLUSTERSIZE * N_ROOTDIR_CLUSTERS) {
        printf("File not found\n");
        return;
    }

    // Get the first cluster number of the file
    unsigned int first_cluster = root[i + 26] | (root[i + 27] << 8);

    // Calculate which block the offset falls within
    int block_num = offset / CLUSTERSIZE;
    int block_offset = offset % CLUSTERSIZE;

    // Get the FAT
    unsigned char* FAT = getFAT(fd);

    // Traverse the FAT to find the block
    unsigned int cluster = first_cluster;
    for (int j = 0; j < block_num; j++) {
        unsigned int next_cluster = FAT[cluster * 2] | (FAT[cluster * 2 + 1] << 8);
        if (next_cluster == 0xFFFF) {
            // Need to allocate a new block
            next_cluster = allocate_block(fd, cluster);
            if (next_cluster == 0xFFFF) {
                printf("No free blocks\n");
                return;
            }
            // Update the FAT
            FAT[cluster * 2] = next_cluster & 0xFF;
            FAT[cluster * 2 + 1] = (next_cluster >> 8) & 0xFF;
        }
        cluster = next_cluster;
    }

    // Read the block
    unsigned char block[CLUSTERSIZE];
    getCluster(fd, block, cluster);

    // Write the data to the block
    for (int j = 0; j < n; j++) {
        if (block_offset + j >= CLUSTERSIZE) {
            // Need to allocate a new block
            unsigned int new_cluster = allocate_block(fd, cluster);
            if (new_cluster == 0xFFFF) {
                printf("No free blocks\n");
                return;
            }
            // Update the FAT
            FAT[cluster * 2] = new_cluster & 0xFF;
            FAT[cluster * 2 + 1] = (new_cluster >> 8) & 0xFF;
            cluster = new_cluster;
            // Read the new block
            getCluster(fd, block, cluster);
            block_offset = 0;
        }
        block[block_offset + j] = data;
    }

    // Write the block back to disk
    writesector(fd, block, cluster);
}

unsigned int allocate_block(int fd, unsigned int last_block) {
    // Get the FAT
    unsigned char* FAT = getFAT(fd);

    // Get the number of clusters in the file system
    unsigned int cluster_count = boot->fat32.length * 512 / 4;

    // Find a free block
    unsigned int new_block;
    for (new_block = 2; new_block < cluster_count; new_block++) {
        unsigned int entry = FAT[new_block * 4] | (FAT[new_block * 4 + 1] << 8) | (FAT[new_block * 4 + 2] << 16) | (FAT[new_block * 4 + 3] << 24);
        if (entry == 0x00000000) {
            // This is a free block
            break;
        }
    }

    if (new_block >= cluster_count) {
        // No free blocks
        return 0xFFFF;
    }

    // Mark the block as used and end of file
    FAT[new_block * 4] = 0xFF;
    FAT[new_block * 4 + 1] = 0xFF;
    FAT[new_block * 4 + 2] = 0xFF;
    FAT[new_block * 4 + 3] = 0x0F;

    // Link the block to the file
    unsigned int last_entry = FAT[last_block * 4] | (FAT[last_block * 4 + 1] << 8) | (FAT[last_block * 4 + 2] << 16) | (FAT[last_block * 4 + 3] << 24);
    last_entry = new_block;
    FAT[last_block * 4] = last_entry & 0xFF;
    FAT[last_block * 4 + 1] = (last_entry >> 8) & 0xFF;
    FAT[last_block * 4 + 2] = (last_entry >> 16) & 0xFF;
    FAT[last_block * 4 + 3] = (last_entry >> 24) & 0xFF;

    // Write the FAT back to disk
    for (int i = 0; i < cluster_count / SECTORSIZE; i++) {
        writesector(fd, &FAT[i * SECTORSIZE], i + 1);
    }

    return new_block;
}

void print_cluster(unsigned char* cluster) {
    int hex = 0;
    for (int i = 0; i < CLUSTERSIZE; i++){
        if (i == 0 || i % 16 == 0) {
            printf("\n");
            printf("0x%08X: ", hex);
            hex+=16;
        } else if (i % 4 == 0){
            printf(" ");
        }
        
        if (cluster[i] != 0) {
            printf("\x1B[31m"); // Red color
            printf("%02X ", cluster[i]);
            printf("\x1B[0m"); // Reset color
        } else {
            printf("%02X ", cluster[i]);
        }
    }
    printf("\n");
}

void print_FAT(unsigned char* sector) {
    for (int i = 0; i < SECTORSIZE * boot->fat32.length; i++){
        if (i == 0 || i % 4 == 0) {
            printf("\n");
            printf("C%03d: ", i / 4);
        }
        if (sector[i] != 0) {
            printf("\x1B[31m"); // Red color
            printf("%02X ", sector[i]);
            printf("\x1B[0m"); // Reset color
        } else {
            printf("%02X ", sector[i]);
        }
    }
    printf("\n");
}

void print_FAT_mini(unsigned char* sector) {
    for (int i = 0; i < SECTORSIZE / 4; i++){
        if (i == 0 || i % 4 == 0) {
            printf("\n");
            printf("C%03d: ", i / 4);
        }
        if (sector[i] != 0) {
            printf("\x1B[31m"); // Red color
            printf("%02X ", sector[i]);
            printf("\x1B[0m"); // Reset color
        } else {
            printf("%02X ", sector[i]);
        }
    }
    printf("\n");
}

void display_help() {
    printf("Usage: fatmod DISKIMAGE OPTION [PARAMETERS]\n");
    printf("Options:\n");
    printf("\t-l                          List the names of the files in the root directory\n");
    printf("\t-r -a FILENAME              Display the content of the file named FILENAME in ASCII form\n");
    printf("\t-r -b FILENAME              Display the content of the file named FILENAME in binary form\n");
    printf("\t-c FILENAME                 Create a file named FILENAME in the root directory\n");
    printf("\t-d FILENAME                 Delete the file named FILENAME and all its associated data\n");
    printf("\t-w FILENAME OFFSET N DATA   Write data into the file starting at offset OFFSET\n");
    printf("\t-h                          Display this help page\n");
}