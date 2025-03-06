/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2023 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/mpstate.h"
#include "py/runtime.h"

#if MICROPY_NLR_WASM

unsigned int nlr_push(nlr_buf_t *nlr) {
    return nlr_push_tail(nlr);
}

void nlr_jump(void *val) {
    // mp_obj_print_exception(&mp_stderr_print, MP_OBJ_FROM_PTR(val));
    nlr_buf_t *prev_top = MP_STATE_THREAD(nlr_top);
    MP_NLR_JUMP_HEAD(val, top);
    // we don't need to assign top to top->prev as MP_NLR_JUMP_HEAD() above does for the nlrwasm case, so restore the original value here
    MP_STATE_THREAD(nlr_top) = prev_top;
    MP_STATE_THREAD(nlr_exc) = val;
}

#endif
