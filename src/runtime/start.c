//==============================================================================================
//
//   runtime/start - Process entry for answer() programs
//
//   DESCRIPTION:
//       Calls `answer()` and prints the result; a program with `main` gets its shim from the
//       emitter instead.
//
//==============================================================================================

#include "lucb_rt.h"

#include <inttypes.h>
#include <stdio.h>

int64_t lb_answer(void);

int main(void) {
    lb_set_alloc(lb_heap_alloc());
    int64_t value = lb_answer();
    printf("%" PRId64 "\n", value);
    return 0;
}
