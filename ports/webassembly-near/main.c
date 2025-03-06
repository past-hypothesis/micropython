/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2021 Damien P. George and 2017, 2018 Rami Ali
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mpstate.h"
#include "py/runtime.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "shared/runtime/pyexec.h"
#include "near_api.h"

#include <emscripten.h>
#include <wasi/api.h>

#define PYTHON_HEAP_SIZE 32768
#define PYTHON_STACK_SIZE 16384

static void log_utf8_c(const char* str)
{
  log_utf8(strlen(str), (uint64_t)str);
}

static char log_buffer[120] = { 0 };
static size_t log_buffer_len = 0;

static void log_buffer_flush()
{
  static int skip_log_items = 0;
  static int log_count = 0;
  if (log_buffer_len > 0 && log_buffer_len <= sizeof(log_buffer)) {
    if (++log_count > skip_log_items) {
      log_utf8(log_buffer_len, (uint64_t)log_buffer);
    }
  }
  log_buffer_len = 0;
}

static void log_buffer_append(char c)
{
  if (log_buffer_len + 1 >= sizeof(log_buffer)
#if !MICROPY_DEBUG_VERBOSE
    || c == '\n'
#endif
    ) {
    log_buffer_flush();
  }
  if (c != '\n' 
#if MICROPY_DEBUG_VERBOSE
    || log_buffer_len != 0
#endif
  ) {
    log_buffer[log_buffer_len++] = c;
  }
}

NORETURN void near_abort_impl(const char *msg, const char *func, const char *filename, uint32_t line, uint32_t col)
{
  if (msg) {
    printf("NEAR_ABORT(): '%s' at %s() (%s:%d), terminating\n", msg, func, filename, line);
  }
  else {
    printf("NEAR_ABORT() called by %s() (%s:%d), terminating\n", func, filename, line);
  }
  log_buffer_flush();
  // near_abort doesn't seem to work as we expect (should msg and filename to in utf-16?), so use panic_utf8() instead for now
  if (msg) {
    panic_utf8(strlen(msg), (uint64_t)msg);
  }
  else {
    panic();
  }
  // near_abort((uint32_t)(msg ? msg : "(null)"), (uint32_t)filename, line, col); // this seems to result in incorrect log message length
}

void emscripten_scan_registers(em_scan_func func)
{
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags)
{
  return 0;
}

uint32_t mp_js_ticks_ms(void)
{
  return 0;
}

void mp_js_hook(void)
{
}

int setjmp(jmp_buf buf)
{
  return 0;
}

NORETURN void longjmp(jmp_buf buf, int value)
{
  log_buffer_flush();
  NEAR_ABORT();
}

static int handle_uncaught_exception(mp_obj_base_t* exc)
{
  mp_obj_print_exception(&mp_stderr_print, MP_OBJ_FROM_PTR(exc));
  log_buffer_flush();
  NEAR_ABORT();
}

NORETURN void __wasi_proc_exit(__wasi_exitcode_t code)
{
  log_buffer_flush();
  abort();
}

__wasi_errno_t __wasi_fd_close(__wasi_fd_t fd)
{
  return 0;
}

__wasi_errno_t __wasi_fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset, __wasi_whence_t whence, __wasi_filesize_t* newoffset)
{
  *newoffset = offset;
  return 0;
}

__wasi_errno_t __wasi_fd_write(__wasi_fd_t fd, const __wasi_ciovec_t* iovs, size_t iovs_len, __wasi_size_t* nwritten)
{
  *nwritten = 0;
  for (size_t i = 0; i != iovs_len; ++i) {
    for (size_t j = 0; j != iovs[i].buf_len; ++j) {
      log_buffer_append(iovs[i].buf[j]);
    }
    *nwritten += iovs[i].buf_len;
  }
  return 0;
}

void setTempRet0(int32_t v)
{
}

int DEBUG_printf(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int ret = vprintf(fmt, ap);
  va_end(ap);
  return ret;
}

void run_frozen_fn(const char* file_name, const char* fn_name)
{
#if MICROPY_ENABLE_PYSTACK
  mp_obj_t* pystack = (mp_obj_t*)malloc(PYTHON_STACK_SIZE * sizeof(mp_obj_t));
  mp_pystack_init(pystack, pystack + PYTHON_STACK_SIZE);
#endif
#if MICROPY_ENABLE_GC
  char* heap = (char*)malloc(PYTHON_HEAP_SIZE * sizeof(char));
  gc_init(heap, heap + PYTHON_HEAP_SIZE);
#endif
#if MICROPY_GC_SPLIT_HEAP_AUTO
  MP_STATE_MEM(gc_alloc_threshold) = 16 * 1024 / MICROPY_BYTES_PER_GC_BLOCK;
#endif
  mp_init();
  nlr_buf_t nlr;
  NLR_PUSH_BLOCK(nlr) {
    pyexec_frozen_module(file_name, false);
    mp_obj_t module_fun = mp_load_name(qstr_from_str(fn_name));
    mp_call_function_0(module_fun);
    nlr_pop();
  }
  NLR_PUSH_HANDLER(nlr) {
    handle_uncaught_exception(nlr.ret_val);
  }
  log_buffer_flush();
}

int main()
{
  return 0;
}

#if MICROPY_GC_SPLIT_HEAP_AUTO
static void gc_collect_top_level(void);
#endif

#if MICROPY_GC_SPLIT_HEAP_AUTO

static bool gc_collect_pending = false;

// The largest new region that is available to become Python heap.
size_t gc_get_max_new_split(void)
{
  return 128 * 1024 * 1024;
}

// Don't collect anything.  Instead require the heap to grow.
void gc_collect(void)
{
  gc_collect_pending = true;
}

// Collect at the top-level, where there are no root pointers from stack/registers.
static void gc_collect_top_level(void)
{
  if (gc_collect_pending) {
    gc_collect_pending = false;
    gc_collect_start();
    gc_collect_end();
  }
}

#else

static void gc_scan_func(void* begin, void* end)
{
  gc_collect_root((void**)begin, (void**)end - (void**)begin + 1);
}

void gc_collect(void)
{
  gc_collect_start();
  emscripten_scan_stack(gc_scan_func);
  emscripten_scan_registers(gc_scan_func);
  gc_collect_end();
}

#endif

#if !MICROPY_VFS
mp_lexer_t* mp_lexer_new_from_file(qstr filename)
{
  mp_raise_OSError(MP_ENOENT);
  return NULL;
}

mp_import_stat_t mp_import_stat(const char* path)
{
  return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t* args, mp_map_t* kwargs)
{
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
#endif

void nlr_jump_fail(void* val)
{
  printf("nlr_jump_fail(%08x)\n", (unsigned int)(uintptr_t)val);
  log_buffer_flush();
  NEAR_ABORT();
}

void NORETURN __fatal_error(const char* msg)
{
  log_buffer_flush();
  log_utf8_c(msg);
  NEAR_ABORT();
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char* file, int line, const char* func, const char* expr)
{
  printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
  log_buffer_flush();
  NEAR_ABORT();
}
#endif
