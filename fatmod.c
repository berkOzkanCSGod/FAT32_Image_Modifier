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


#define FALSE 0
#define TRUE 1

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes


int readsector (int fd, unsigned char *buf, unsigned int snum);
int writesector (int fd, unsigned char *buf, unsigned int snum);



int
main(int argc, char *argv[])
{

    char diskname[128];
    int fd;
    unsigned char sector[SECTORSIZE];    
    
    strcpy (diskname, argv[1]);
    
    fd = open (diskname,  O_SYNC | O_RDWR);
    if (fd < 0) {
        printf("could not open disk image\n");
        exit (1);
    }
 
    // read the boot sector
    readsector (fd, sector, 0);
    printf ("read sector 0\n");

    // ...
    close(fd);
}
  
int 
readsector (int fd, unsigned char *buf,unsigned int snum)
{
	off_t offset;
	int n;
	offset = snum * SECTORSIZE;
	lseek (fd, offset, SEEK_SET);
	n  = read (fd, buf, SECTORSIZE);
	if (n == SECTORSIZE)
	     return (0);
	else
             return (-1);
}


int 
writesector (int fd, unsigned char *buf, unsigned int snum)
{
	off_t offset;
	int n;
	offset = snum * SECTORSIZE;
	lseek (fd, offset, SEEK_SET);
	n  = write (fd, buf, SECTORSIZE);
        fsync (fd);
        if (n == SECTORSIZE)
	     return (0);
	else
             return (-1);
}