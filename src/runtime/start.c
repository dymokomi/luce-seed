/* Process entry for the scalar core: call answer() and print the i64. */

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
