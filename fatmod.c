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
void display_root(int fd);
void getCluster(int fd, unsigned char* cluster, int cluster_num);
void print_cluster(unsigned char* cluster);
void print_FAT(unsigned char* sector);
void print_FAT_mini(unsigned char* sector);
unsigned char* getFAT(int fd);

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

    char diskname[128];
    int disk_fd;
    strcpy (diskname, argv[1]);
    disk_fd = open(diskname,  O_SYNC | O_RDWR);
    readsector(disk_fd, sector, 0);
    boot = (struct fat_boot_sector*) sector;
    data_start_sector = boot->reserved
                         + (boot->fats * boot->fat32.length); 
    cluster_count = boot->fat32.length * 512 / 4;
    sector_count = boot->total_sect;
    FAT_start = 32;
    FAT_end = 32 + boot->fat32.length;
    // CODE:

    display_root(disk_fd);

    // CODE END

    close(disk_fd);
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
    //Creating a array that can fit the entire FAT.
    unsigned char* FAT = (unsigned char*)(malloc(sizeof(char) * boot->fat32.length * SECTORSIZE));
    //Filling FAT array with FAT binary data by reading sector by sector
    int iteration = 0;    
    for (int i = FAT_start; i < FAT_end; i++, iteration++) {
        readsector(fd, FAT + (iteration * SECTORSIZE), i);
    }

    return FAT;
}

//return an array of cluster numbers
//ok now i need to record them into an array
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

        increment += 4;
        currentClusterNumber = entry_value;

        if (entry_value > 0 && entry_value < FFFFFFF8) {
            printf("V: %u\n", entry_value);
            temp[index] = currentClusterNumber;
            index++;
        }
    } while (entry_value < FFFFFFF8 && entry_value > 0 && currentClusterNumber < cluster_count);

    clusterChain = (unsigned int*)(malloc(sizeof(int)*increment/4));
    for (int i = 0; i < index; i++) {
        clusterChain[i] = temp[i];
    }
    *size = index;
    return clusterChain;
}


void display_root(int fd) {
    //in order to display the file names in the root I need to:
        //I now have a chain of clusters for any cluster
        //Now, I need to 
            // 1. get the initial cluster numbers
            // 2. put all the data into a single array (consistin of one or more clusters)
            // 3. display contents
    
    //printing FAT
    unsigned char* FAT = getFAT(fd);
    int size = 0;
    unsigned int* clusterChain = traceCluster(FAT, 4, &size);

    for (int i = 0; i < size; i++) {
        printf("%d ", clusterChain[i]);
    }

    printf("\nFAT Mini\n");
    print_FAT_mini(FAT);
    //printing some clusters
    // printf("\ncluster 2\n");
    // getCluster(fd, cluster, 2);
    // print_cluster(cluster);
    // printf("\ncluster 3\n");
    // getCluster(fd, cluster, 3);
    // print_cluster(cluster);
    // printf("\ncluster 4\n");
    // getCluster(fd, cluster, 4);
    // print_cluster(cluster);
    // printf("\ncluster 5\n");
    // getCluster(fd, cluster, 5);
    // print_cluster(cluster);
}

























void print_cluster(unsigned char* cluster) {
    for (int i = 0; i < CLUSTERSIZE; i++){
        if (i == 0 || i % 16 == 0) {
            printf("\n");
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