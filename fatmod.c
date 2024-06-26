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
#define __0FFFFFFF 268435455 //anther magic number
#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes
#define N_ROOTDIR_CLUSTERS 2 //how many clusters root dir has

int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
int findFreeCluster(unsigned char* FAT, int FAT_start, int FAT_end);
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
void markClusterAsUsed(unsigned char* FAT, int cluster);
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
    } else if (strcmp(argv[2], "-r") == 0 && argc > 4) {
        if (strcmp(argv[3], "-a") == 0) {
            display_contents(disk_fd, argv[4]);
        } else if (strcmp(argv[3], "-b") == 0) {
            display_raw_contents(disk_fd, argv[4]);
        }
    } else if (strcmp(argv[2], "-c") == 0 && argc > 3) {
        create_file(disk_fd, argv[3]);
    } else if (strcmp(argv[2], "-d") == 0 && argc > 3) {
        delete_file(disk_fd, argv[3]);
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
    // Creating an array that can fit the entire FAT
    unsigned char* FAT = (unsigned char*)(malloc(sizeof(char) * FAT_length * SECTORSIZE));
    if (FAT == NULL) {
        printf("Memory allocation failed\n");
        return NULL;
    }
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
    int index = 1;
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

        // increment += 4;
        currentClusterNumber = entry_value;

        if (entry_value > 0 && entry_value < FFFFFFF8 && entry_value < __0FFFFFFF) {
            temp[index] = currentClusterNumber;
            index++;
        } else if (entry_value < 0 && entry_value > FFFFFFF8 && entry_value > __0FFFFFFF) {
            break;
        }
    } while (entry_value < FFFFFFF8 && entry_value < __0FFFFFFF && entry_value > 0 && currentClusterNumber < cluster_count);

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

void markClusterAsUsed(unsigned char* FAT, int cluster) {
    // Set the FAT entry for the cluster to 0x0FFFFFFF
    FAT[4*cluster + 0] = 0xFF;
    FAT[4*cluster + 1] = 0xFF;
    FAT[4*cluster + 2] = 0xFF;
    FAT[4*cluster + 3] = 0x0F;
}

// Find the first free cluster in the FAT
int findFreeCluster(unsigned char* FAT, int FAT_start, int FAT_end) {
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
    return freeCluster;
}

void writeCluster(int fd, unsigned char* cluster, int cluster_num) {
    cluster_num -= 2; //to compensate for the fact that clusters start at 2

    unsigned char first_sector[SECTOR_SIZE];
    unsigned char second_sector[SECTOR_SIZE];

    memcpy(first_sector, cluster, SECTOR_SIZE);
    memcpy(second_sector, cluster + SECTOR_SIZE, SECTOR_SIZE);

    writesector(fd, first_sector, (data_start_sector + 2 * cluster_num));
    writesector(fd, second_sector, (data_start_sector + 2 * cluster_num + 1));
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
        //print_cluster(cluster);
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
    int dentry_length = 32;
    for (int i = 0; i < size; i++) {
        getCluster(fd, cluster, clusterChain[i]);
        // print_cluster(cluster);
        for (int k = 1; k < CLUSTERSIZE / dentry_length; k++) {
            unsigned char fileName[13] = {0};
            unsigned char fileExte[3] = {0};
            unsigned char clusterBits[4] = {0};
            int nameSize = 0, extSize = 0, j = 0;
            int offset = k*32;
            if (cluster[offset+i] == 0)
                break;
            
            for (int i = 0; i < 32; i++) {
                if (i < 8) {
                    if (!isspace(cluster[offset+i])) {
                        fileName[j] = cluster[offset+i];
                        nameSize++;
                        j++;
                    }
                }              
                if (i > 7 && i < 11) {
                    fileExte[i-8] = cluster[offset+i];
                    extSize++;
                }
                if (i > 19 && i < 22) {
                    clusterBits[i-19] = cluster[offset+i];
                }
                if (i > 25 && i < 28) {
                    clusterBits[i-26] = cluster[offset+i];
                }
            }
            printf("\n");
            strcat(fileName,".");
            strcat(fileName, fileExte);
            fileName[strlen(fileName) - 1] = '\0';

            // Compare strings
            if (strcasecmp(fileName, fName) == 0) {
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
    printf("File not found.\n");
}

void display_raw_contents(int fd, char* fName) {
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);
    int dentry_length = 32;
    for (int i = 0; i < size; i++) {
        getCluster(fd, cluster, clusterChain[i]);
        // print_cluster(cluster);
        for (int k = 1; k < CLUSTERSIZE / dentry_length; k++) {
            unsigned char fileName[13] = {0};
            unsigned char fileExte[3] = {0};
            unsigned char clusterBits[4] = {0};
            int nameSize = 0, extSize = 0, j = 0;
            int offset = k*32;
            if (cluster[offset+i] == 0)
                break;
            
            for (int i = 0; i < 32; i++) {
                if (i < 8) {
                    if (!isspace(cluster[offset+i])) {
                        fileName[j] = cluster[offset+i];
                        nameSize++;
                        j++;
                    }
                }              
                if (i > 7 && i < 11) {
                    fileExte[i-8] = cluster[offset+i];
                    extSize++;
                }
                if (i > 19 && i < 22) {
                    clusterBits[i-19] = cluster[offset+i];
                }
                if (i > 25 && i < 28) {
                    clusterBits[i-26] = cluster[offset+i];
                }
            }
            printf("\n");
            strcat(fileName,".");
            strcat(fileName, fileExte);
            fileName[strlen(fileName) - 1] = '\0';

            // Compare strings
            if (strcasecmp(fileName, fName) == 0) {
                int number = (  (int)((uint8_t)clusterBits[0]) 
                        | (int)((uint8_t)clusterBits[1]) << 8 
                        | (int)((uint8_t)clusterBits[2]) << 16 
                        | (int)((uint8_t)clusterBits[3]) << 24  );

                unsigned int* clusterChain = traceCluster(FAT, number, &size);
                
                for (int i = 0; i < size; i++) {
                    struct msdos_dir_entry* dentry;
                    getCluster(fd, cluster, clusterChain[i]);
                    dentry = (struct msdos_dir_entry*) cluster;
                    // printf("%s", dentry->name);
                    print_cluster(cluster);
                }

                printf("\n");
                return;
            } 
        }
        printf("\n");
    }
    printf("File not found.\n"); 
}

void writeFAT(int fd, unsigned char* FAT) {
    int FAT_length = FAT_end - FAT_start;
    for (int i = 0; i < FAT_length; i++) {
        writesector(fd, FAT + (i * SECTORSIZE), FAT_start + i);
        int progress = (i + 1) * 100 / FAT_length;
        printf("\rWriting back to FAT Table. Completed: %d%%", progress);
        fflush(stdout);
    }
    printf("\n");
}

void create_file(int fd, char* filename) {
    //Get the FAT and trace the root directory clusters
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);
    //Check if the file already exists
    char filename_copy[strlen(filename) + 1];
    strcpy(filename_copy, filename);
    for (int i = 0; i < strlen(filename_copy); i++) {
        filename_copy[i] = toupper(filename_copy[i]);
    }
    int i1, j1;
    struct msdos_dir_entry* dep1;
    int dep_size1 = sizeof(struct msdos_dir_entry);
    for (j1 = 0; j1 < size; j1++) {
        getCluster(fd, cluster, clusterChain[j1]);
        dep1 = (struct msdos_dir_entry*) cluster;
        for (i1 = 0; i1 < CLUSTERSIZE; i1 += dep_size1, dep1++) {
            char name[9], ext[4];
            strncpy(name, (char*)dep1->name, 8);
            strncpy(ext, (char*)dep1->name + 8, 3);
            name[8] = '\0';
            ext[3] = '\0';
            trimTrailingSpaces(name);
            char full_filename[13];
            sprintf(full_filename, "%s.%s", name, ext); //Combine the name and extension

            if (strcmp(full_filename, filename_copy) == 0) {
                // This is the directory entry for the file
                break;
            }
        }
        if (i1 < CLUSTERSIZE) {
            // Found the directory entry
            break;
        }
    }

    if (j1 != size) {
        printf("ERROR: This file already exists.\n");
        return;
    }
    // Find an empty directory entry
    int i, j;
    struct msdos_dir_entry* dep;
    int dep_size = sizeof(struct msdos_dir_entry);
    for (j = 0; j < size; j++) {
        getCluster(fd, cluster, clusterChain[j]);
        dep = (struct msdos_dir_entry*) cluster;
        for (i = 0; i < CLUSTERSIZE; i += dep_size, dep++) {
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

    char name[9], ext[4];
    sscanf(filename, "%8[^.].%3s", name, ext);
    // Convert filename and extension to uppercase
    for (int i = 0; i < 8; i++) {
        name[i] = toupper(name[i]);
    }
    for (int i = 0; i < 3; i++) {
        ext[i] = toupper(ext[i]);
    }

    strncpy((char*)dep->name, name, 8);
    strncpy((char*)dep->name + 8, ext, 3);

    //Set the attribute byte(0x20 for normal file)
    dep->attr = 0x20;

    //Set the initial size(0)
    dep->size = 0;

    int freeCluster = findFreeCluster(FAT, FAT_start, FAT_end);
    if (freeCluster == -1) {
        printf("Disk is full\n");
        return;
    }

    //Mark the free cluster as used in the FAT
    FAT[4*freeCluster + 0] = 0xFF;
    FAT[4*freeCluster + 1] = 0xFF;
    FAT[4*freeCluster + 2] = 0xFF;
    FAT[4*freeCluster + 3] = 0x0F;

    //Set the first cluster number of the file to the newly allocated cluster
    dep->starthi = (freeCluster >> 16) & 0xFFFF;
    dep->start = freeCluster & 0xFFFF;

    //Write the updated FAT back to the disk
    writeFAT(fd, FAT);

    //Write the cluster back to disk
    writeCluster(fd, cluster, clusterChain[j]);
}

void delete_file(int fd, char* filename) {
    // Get the FAT and trace the root directory clusters
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);

    char filename_copy[strlen(filename) + 1];
    strcpy(filename_copy, filename);
    for (int i = 0; i < strlen(filename_copy); i++) {
        filename_copy[i] = toupper(filename_copy[i]);
    }
    // Find the directory entry for the file
    int i, j;
    struct msdos_dir_entry* dep;
    int dep_size = sizeof(struct msdos_dir_entry);
    for (j = 0; j < size; j++) {
        getCluster(fd, cluster, clusterChain[j]);
        dep = (struct msdos_dir_entry*) cluster;
        for (i = 0; i < CLUSTERSIZE; i += dep_size, dep++) {
            char name[9], ext[4];
            strncpy(name, (char*)dep->name, 8);
            strncpy(ext, (char*)dep->name + 8, 3);
            name[8] = '\0';
            ext[3] = '\0';
            trimTrailingSpaces(name);
            char full_filename[13];
            sprintf(full_filename, "%s.%s", name, ext); 

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

    size = 0;
    unsigned int* fileClusterChain = traceCluster(FAT, dep->start | (dep->starthi << 16), &size);
    for(int i = 0; i < size; i++){
        getCluster(fd, cluster, fileClusterChain[i]);
        memset(cluster, 0, sizeof(cluster));
        writeCluster(fd, cluster, fileClusterChain[i]);
        int progress = (i + 1) * 100 / size;
        printf("Clearing data from the file. Completed: %d%%", progress);
        fflush(stdout);
    }
    printf("\n");
    getCluster(fd, cluster, clusterChain[j]);
    //Get the first cluster number of the file
    unsigned int first_cluster = dep->start | (dep->starthi << 16);

    //Deallocate all its data blocks by updating the FAT
    clearClusterChain(FAT, first_cluster);

    writeFAT(fd, FAT);

    dep->name[0] = 0xE5; //Mark the directory entry as deleted

    //Clear the name section
    for (int j = 1; j < 11; j++) {
        dep->name[j] = 0x00;
    }

    //Write the cluster back to disk
    writeCluster(fd, cluster, clusterChain[j]);
}

void write_file(int fd, char* filename, int offset, int n, unsigned char data) {
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 2, &size);
    char filename_copy[strlen(filename) + 1];
    strcpy(filename_copy, filename);
    for (int i = 0; i < strlen(filename_copy); i++) {
        filename_copy[i] = toupper(filename_copy[i]);
    }
    // Find the directory entry for the file
    int i, j;
    struct msdos_dir_entry* dep;
    int dep_size = sizeof(struct msdos_dir_entry);
    for (j = 0; j < size; j++) {
        getCluster(fd, cluster, clusterChain[j]);
        dep = (struct msdos_dir_entry*) cluster;
        for (i = 0; i < CLUSTERSIZE; i += dep_size, dep++) {
            char name[9], ext[4];
            strncpy(name, (char*)dep->name, 8);
            strncpy(ext, (char*)dep->name + 8, 3);
            name[8] = '\0'; 
            ext[3] = '\0';
            trimTrailingSpaces(name);
            char full_filename[13];
            sprintf(full_filename, "%s.%s", name, ext); 
            if (strcmp(full_filename, filename_copy) == 0) {
                //This is the directory entry for the file
                break;
            }
        }
        if (i < CLUSTERSIZE) {
            //Found the directory entry
            break;
        }
    }

    if (j == size) {
        printf("File not found\n");
        return;
    }

    //Calculate the number of clusters needed
    int clusters_needed = (offset + n + CLUSTERSIZE - 1) / CLUSTERSIZE;
    // printf("CLUSTERS NEEDED: %d\n", clusters_needed);
    //Trace the files clusters
    size = 0;
    unsigned int* fileClusterChain = traceCluster(FAT, dep->start | (dep->starthi << 16), &size);
    // printf("NUMBER OF CURRENT CLUSTERS = %d\n", size);
    // printf("BEFORE FINDING EXTRA CLUSTERS:\n");
    for(int i = 0; i < size; i++){
        getCluster(fd, cluster, fileClusterChain[i]);
        //print_cluster(cluster);
    }
    //Allocate new clusters if necessary
    while (size < clusters_needed) {
        int freeCluster = findFreeCluster(FAT, FAT_start, FAT_end);
        if (freeCluster == -1) {
            printf("Disk is full\n");
            return;
        }
        //Mark the free cluster as used in the FAT
        FAT[4*freeCluster + 0] = 0xFF;
        FAT[4*freeCluster + 1] = 0xFF;
        FAT[4*freeCluster + 2] = 0xFF;
        FAT[4*freeCluster + 3] = 0x0F;
        fileClusterChain = realloc(fileClusterChain, (size + 1) * sizeof(unsigned int));
        fileClusterChain[size] = freeCluster;
        if (size > 0) {
            //Update the FAT entry for the previously last cluster to point to the new cluster
            FAT[4*fileClusterChain[size - 1] + 0] = freeCluster & 0xFF;
            FAT[4*fileClusterChain[size - 1] + 1] = (freeCluster >> 8) & 0xFF;
            FAT[4*fileClusterChain[size - 1] + 2] = (freeCluster >> 16) & 0xFF;
            FAT[4*fileClusterChain[size - 1] + 3] = (freeCluster >> 24) & 0x0F;
        }
        size++;
    }
    // printf("AFTER FINDING EXTRA CLUSTERS:\n");
    // for(int i = 0; i < size; i++){
    //     getCluster(fd, cluster, fileClusterChain[i]);
    //     print_cluster(cluster);
    // }
    //Write the data to the file
    for (int k = 0; k < n; k++) {
        int clusterIndex = (offset + k) / CLUSTERSIZE;
        int clusterOffset = (offset + k) % CLUSTERSIZE;
        int number = fileClusterChain[clusterIndex];
        getCluster(fd, cluster, number);
        cluster[clusterOffset] = data;
        //print_cluster(cluster);
        writeCluster(fd, cluster, number);
        int progress = (k + 1) * 100 / n;
        printf("\rWriting data to file. Completed: %d%%", progress);
        fflush(stdout);
    }
    printf("\n");

    getCluster(fd, cluster, clusterChain[j]);
    //print_cluster(cluster);
    //Update the file size
    dep->size = (dep->size > offset + n) ? dep->size : offset + n;

    //Write the updated FAT back to the disk
    writeFAT(fd, FAT);

    //Write the updated directory entry back to the correct cluster
    writeCluster(fd, cluster, clusterChain[j]);
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
    printf("Usage: ./fatmod [DISKIMAGE] OPTION [PARAMETERS]\n");
    printf("Operations:\n");
    printf("\tDISKIMAGE -l                          List the names of the files in the root directory\n");
    printf("\tDISKIMAGE -r -a FILENAME              Display the content of the file named FILENAME in ASCII form\n");
    printf("\tDISKIMAGE -r -b FILENAME              Display the content of the file named FILENAME in binary form\n");
    printf("\tDISKIMAGE -c FILENAME                 Create a file named FILENAME in the root directory\n");
    printf("\tDISKIMAGE -d FILENAME                 Delete the file named FILENAME and all its associated data\n");
    printf("\tDISKIMAGE -w FILENAME OFFSET N DATA   Write data into the file starting at offset OFFSET\n");
    printf("\t-h                                    Display this help page\n");
}