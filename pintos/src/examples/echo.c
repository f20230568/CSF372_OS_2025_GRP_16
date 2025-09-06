#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

int main(int argc, char **argv)
{
  int i;
  if(argc==1){
    return 0;
  }

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '\'' && argv[i][strlen(argv[i]) - 1] == '\'') {
      for (int j = 1; j < (int)strlen(argv[i]) - 1; j++) {
        printf("%c", argv[i][j]);
      }
      if (i != argc - 1) printf(" ");
      continue;
    }
    printf("%s", argv[i]);
    printf(" ");
  }
  printf("\n");

  return 0;
}
