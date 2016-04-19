#include <unistd.h>             /* ftruncate */
#include <fcntl.h>              /* O_ flags */
#include <stdio.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/mman.h>


#define SHM_FILE "/tmp/mercury.shm"

int main(void)
{
    int size = 1024 * 1024 * 256; // 256 mb
    int descriptor = -1;
    int mmap_flags = MAP_SHARED;
    
    struct iovec local[2];
    struct iovec remote[1];
    char buf1[10];
    char buf2[10];
    ssize_t nread;
    pid_t pid = 28653; /* process id - get from /proc/ directory on Linux */
    local[0].iov_base = buf1;
    local[0].iov_len = 10;
    local[1].iov_base = buf2;
    local[1].iov_len = 10;
    /* e.g., get address from /proc/pid/maps */
    remote[0].iov_base = (void *) 0x00400000; 
    remote[0].iov_len = 20;

    descriptor = shm_open(SHM_FILE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (descriptor != -1) {
        ftruncate(descriptor, size);
    }
    else {
        perror("shm_open");
    }
    char *result = mmap(NULL, (size_t) size, PROT_WRITE | PROT_READ, mmap_flags,
                        descriptor, 0);
    perror("mmap");
    munmap(result, size);
    shm_unlink(SHM_FILE);
    
#ifdef CROSS_MEMORY_ATTACH
    nread = process_vm_readv(pid, local, 2, remote, 1, 0);
    if (nread != 20){
        printf("nread != 20\n");
        return 1;
    }
    else {
        printf("nread == 20\n");
        return 0;
    }
#endif 
}
