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

#include <stdio.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mphal.h"

#include "emscripten/em_macros.h"

EM_IMPORT(input) void input(uint64_t register_id);
EM_IMPORT(read_register) void read_register(uint64_t register_id, uint64_t ptr);
EM_IMPORT(register_len) uint64_t register_len(uint64_t register_id);
EM_IMPORT(value_return) void value_return(uint64_t value_len, uint64_t value_ptr);
EM_IMPORT(log_utf8) void log_utf8(uint64_t len, uint64_t ptr);

static mp_obj_t near_export(mp_obj_t fn)
{
  return fn;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_near_export_obj, near_export);

static mp_obj_t near_input(mp_obj_t register_id)
{
  mp_int_t id = mp_obj_get_int(register_id);
  input(id);
  uint64_t len = register_len(id);
  void *data = malloc(len);
  read_register(id, (uint64_t)data);
  return mp_obj_new_bytes(data, len);
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_near_input_obj, near_input);

static mp_obj_t near_value_return(mp_obj_t value)
{
  if (mp_obj_is_str(value)) {
    size_t len = 0;
    const char *data = mp_obj_str_get_data(value, &len);
    // printf("near_value_return: str, %d, %s\n", (int)len, data);
    value_return(len, (uint64_t)data);
  }
  else {
    mp_buffer_info_t buffer_info = { 0 };
    mp_get_buffer_raise(value, &buffer_info, 0);
    // printf("near_value_return: bytes, %d, %s\n", (int)buffer_info.len, (const char*)buffer_info.buf);
    value_return(buffer_info.len, (uint64_t)buffer_info.buf);
  }
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_near_value_return_obj, near_value_return);

static mp_obj_t near_log_utf8(mp_obj_t msg)
{
  if (mp_obj_is_str(msg)) {
    size_t len = 0;
    const char *data = mp_obj_str_get_data(msg, &len);
    // printf("near_log_utf8: %d, %s\n", (int)len, data);
    log_utf8(len, (uint64_t)data);
  }
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_near_log_utf8_obj, near_log_utf8);

static const mp_rom_map_elem_t near_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_near) },
    { MP_ROM_QSTR(MP_QSTR_export), MP_ROM_PTR(&mod_near_export_obj) },
    { MP_ROM_QSTR(MP_QSTR_input), MP_ROM_PTR(&mod_near_input_obj) },
    { MP_ROM_QSTR(MP_QSTR_value_return), MP_ROM_PTR(&mod_near_value_return_obj) },
    { MP_ROM_QSTR(MP_QSTR_log_utf8), MP_ROM_PTR(&mod_near_log_utf8_obj) },
};
static MP_DEFINE_CONST_DICT(near_module_globals, near_module_globals_table);

const mp_obj_module_t mp_module_near = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&near_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_near, mp_module_near);
