/* Tests mkdir(). */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  // msg ("test");
  // mkdir ("a");
  CHECK (mkdir ("a"), "mkdir \"a\"");
  // create ("a/b", 512);
  CHECK (create ("a/b", 512), "create \"a/b\"");
  CHECK (chdir ("a"), "chdir \"a\"");
  CHECK (open ("b") > 1, "open \"b\"");
}
