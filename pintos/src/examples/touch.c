#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv) {
  if (argc < 2) {
    return 1;
  }
  
  int status = 0;

  for (int i = 1; i < argc; i++) {
    char *filename = argv[i];

    int fd = open(filename);
    if (fd >= 0) {
      close(fd); 
      status = 1; 
      continue;   
    }

    if (!create(filename, 0)) {
      printf("touch: failed to create %s\n", filename);
    } else {
      printf("%s: created\n", filename);
    }
  }
  
  return status;
}



