#include <stdio.h>
#include <syscall.h>
#include <stdbool.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("touch: missing file operand\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        const char *filename = argv[i];
        
        int fd = open(filename);
        if (fd == -1) {
            bool created = create(filename, 0);
            if (!created) {
                printf("touch: failed to create '%s'\n", filename);
                return 1;
            }
            printf("%s: created\n", filename);
            fd = open(filename);
            if (fd == -1) {
                printf("touch: failed to open '%s' after creation\n", filename);
                return 1;
            }
        }
        close(fd);
    }

    printf("touch: exit(0)\n");  // single print here
    return 0;
}


