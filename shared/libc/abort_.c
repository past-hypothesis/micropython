#include <py/runtime.h>

NLR_NORETURN void abort_(void);

NLR_NORETURN void abort_(void) {
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("abort() called"));
    return;
}
