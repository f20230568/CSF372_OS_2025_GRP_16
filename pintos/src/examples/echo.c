#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

int main(int argc, char **argv)
{
  if(argc==1){
    return EXIT_SUCCESS;
  }

  for(int i=1; i<argc; i++){
    for(int j=0; j<strlen(argv[i]); j++){
      if(argv[i][j]!='\''){
        printf("%c", argv[i][j]);
      }
    }
    printf(" ");
  }
  printf("\n");
  return EXIT_SUCCESS;
}
