/*#include <stdio.h>
#include <syscall.h>

int main(int argc, char **argv)
{
  int i;

  for (i = 0; i < argc; i++)
    printf("%s ", argv[i]);
  printf("\n");

  return EXIT_SUCCESS;
}*/

#include "stdio.h"
#include "stdlib.h"

int
main(int argc, char **argv)
{
  int i;
  /* Print each argument separated by a space. */
  for (i = 1; i < argc; i++) {
    printf("%s", argv[i]);
    /* Add a space after all except the last argument. */
    if (i < argc - 1)
      printf(" ");
  }
  printf("\n");
  /* Always exit success for echo. */
  return 0;
}
