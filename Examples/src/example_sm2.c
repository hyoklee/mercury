#include <unistd.h>             /* ftruncate, getpid */
#include <fcntl.h>              /* O_ flags */
#include <stdio.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/mman.h>


#define SHM_FILE "/mercury_bulk.shm"
// #define BUF_SIZE 4194304
#define BUF_SIZE 100
int main(void)
{
  // int size = 1024 * 1024 * 256; // 256 mb
    int size = BUF_SIZE; // 256 mb
    int descriptor = -1;
    int mmap_flags = MAP_SHARED;
    struct iovec local[2];
    struct iovec remote[1];

    // char buf1[10];
    // char buf2[10];
    char buf[BUF_SIZE];
    ssize_t nread=0;
    
// pid_t pid = getpid();
    pid_t pid = 18842;
    // local[0].iov_base = buf1;
    local[0].iov_base = buf;
    local[0].iov_len = BUF_SIZE;
    // local[0].iov_len = 10;
    // local[1].iov_base = buf2;
    // local[1].iov_len = 10;
    /* e.g., get address from /proc/pid/maps */
// descriptor = shm_open(SHM_FILE, O_RDWR, S_IRUSR | S_IWUSR);
    descriptor = shm_open(SHM_FILE, O_RDONLY, S_IRUSR);
    if (descriptor != -1) {
       //  ftruncate(descriptor, size);
    }
    else {
        perror("shm_open");
    }
// char *result = mmap(NULL, (size_t) size, PROT_WRITE | PROT_READ, mmap_flags,
    char *result = mmap(NULL, (size_t) BUF_SIZE,  PROT_READ, mmap_flags,
			  descriptor, 0);
//    remote[0].iov_base = result;
      remote[0].iov_base = (void *) 0x7f2dc9e47000;
      //    remote[0].iov_base = (void *)0x00400000; 
// remote[0].iov_base = (void *) 0x00000000; 
    // remote[0].iov_len = 20;
    remote[0].iov_len =  BUF_SIZE;

    perror("mmap");
    // nread = process_vm_readv(pid, local, 2, remote, 1, 0);
    nread = process_vm_readv(pid, local, 1, remote, 1, 0);
    if (nread != BUF_SIZE){
    // if (nread != 20){
      perror("process_vm_readv()");
      printf("nread != 20 got %d\n", nread);
      return 1;
    }
    else {
        printf("nread == %d\n", nread);
        int i;
        for(i = 0; i < nread; i++)
            printf("%x\n", buf[i]);
        printf("%s\n", buf);
        // printf("%s\n", buf2);
        return 0;
    }

    //    munmap(result, size);
    // shm_unlink(SHM_FILE);

}
