#include <unistd.h>             /* ftruncate, getpid */
#include <fcntl.h>              /* O_ flags */
#include <stdio.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/mman.h>


#define SHM_FILE "/mercury.shm"

int main(void)
{
    int size = 1024 * 1024 * 256; // 256 mb
    int descriptor = -1;
    int mmap_flags = MAP_SHARED;
    struct iovec wlocal[2];    
    struct iovec local[2];
    struct iovec remote[1];
    char buf1[10];
    char buf2[10];
    char wbuf1[] = "abcdefghij";
    char wbuf2[] = "0123456789";
    ssize_t nread=0, nwrite=0;
    
    pid_t pid = 28653; /* process id - get from /proc/ directory on Linux */
    local[0].iov_base = buf1;
    local[0].iov_len = 10;
    local[1].iov_base = buf2;
    local[1].iov_len = 10;
    wlocal[0].iov_base = wbuf1;
    wlocal[0].iov_len = 10;
    wlocal[1].iov_base = wbuf2;
    wlocal[1].iov_len = 10;    
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
    pid = getpid();
    remote[0].iov_base = result;    
    int i=0;
    nwrite = process_vm_writev(pid, wlocal, 2, remote, 1, 0);
    printf("nwrite=%d\n", nwrite);
#if 0    
    for(i; i < 50; ++i){
        strncpy(result, "01234567899876543210", 20);
    }
#endif    
    nread = process_vm_readv(pid, local, 2, remote, 1, 0);
    if (nread != 20){
        printf("nread != 20\n");
        return 1;
    }
    else {
        printf("nread == 20\n");
        printf("%s\n", buf1);
        printf("%s\n", buf2);
        return 0;
    }

    munmap(result, size);
    shm_unlink(SHM_FILE);

}
