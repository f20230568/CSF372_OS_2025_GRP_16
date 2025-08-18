/** Tests echo command with mixed whitespace arguments. */

#include "tests/lib.h"

int
main (void) 
{
  pid_t pid = exec ("shell echo '  leading' 'trailing  ' '  both  '");
  if (pid == -1) {
    fail ("shell exec failed with mixed whitespace arguments");
  }
  
  int status = wait (pid);
  msg ("Shell exited with status %d for mixed whitespace arguments", status);
  
  if (status != 0) {
    fail ("shell should execute echo with mixed whitespace arguments successfully");
  }
  
  msg ("Echo mixed whitespace test completed successfully");
  return 0;
}
