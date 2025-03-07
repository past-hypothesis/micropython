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
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/objint.h"
#include "py/objlist.h"
#include "py/smallint.h"
#include "py/mpz.h"
#include "py/mphal.h"

#include "near_api.h"

#include <emscripten/em_macros.h>

 // register id to use for temp data storage for apis that output to a register
static const uint64_t default_temp_register_id = 0;

// helper functions
typedef struct
{
  uint64_t len;
  uint64_t ptr;
}
near_api_ptr_t;

static near_api_ptr_t get_mp_str_data(mp_obj_t str)
{
  near_api_ptr_t ptr = { 0, 0 };
  if (mp_obj_is_str(str)) {
    size_t len = 0;
    ptr.ptr = (uint64_t)mp_obj_str_get_data(str, &len);
    ptr.len = len;
  }
  else {
    mp_raise_TypeError(MP_ERROR_TEXT("A str value is required"));
  }
  return ptr;
}

static near_api_ptr_t get_mp_bytes_data(mp_obj_t bytes)
{
  near_api_ptr_t ptr = { 0, 0 };
  if (mp_obj_is_type(bytes, &mp_type_bytes)) {
    size_t len = 0;
    ptr.ptr = (uint64_t)mp_obj_str_get_data(bytes, &len);
    ptr.len = len;
  }
  else {
    mp_raise_TypeError(MP_ERROR_TEXT("A bytes value is required"));
  }
  return ptr;
}

static near_api_ptr_t get_mp_str_or_bytes_data(mp_obj_t str_or_bytes)
{
  near_api_ptr_t ptr = { 0, 0 };
  if (mp_obj_is_str(str_or_bytes) || mp_obj_is_type(str_or_bytes, &mp_type_bytes)) {
    size_t len = 0;
    ptr.ptr = (uint64_t)mp_obj_str_get_data(str_or_bytes, &len);
    ptr.len = len;
  }
  else {
    mp_raise_TypeError(MP_ERROR_TEXT("A str or bytes value is required"));
  }
  return ptr;
}

typedef struct
{
  uint64_t lo;
  uint64_t hi;
} u128_t;

static u128_t mp_int_to_u128(mp_obj_t value)
{
  u128_t result = { 0, 0 };
  if (!mp_obj_is_int(value)) {
    mp_raise_TypeError("Input must be an integer");
    return result;
  }
  mpz_t z = { 0 };
  if (mp_obj_is_small_int(value)) {
    mpz_init_from_int(&z, MP_OBJ_SMALL_INT_VALUE(value));
  }
  else {
    mp_obj_int_t* value_obj = MP_OBJ_TO_PTR(value);
    mpz_set(&z, &value_obj->mpz);
  }
  uint8_t buffer[16] = { 0 };
  mpz_as_bytes(&z, false, false, 16, buffer); // false = little-endian
  result.lo = 0; result.hi = 0;
  for (int i = 0; i < 8; i++) {
    result.lo |= (uint64_t)buffer[i] << (i * 8);
    result.hi |= (uint64_t)buffer[i + 8] << (i * 8);
  }
  mpz_deinit(&z);
  return result;
}

static mp_obj_t u128_to_mp_int(const u128_t* u128)
{
  uint8_t buffer[16] = { 0 };
  for (int i = 0; i < 8; i++) {
    buffer[i] = (u128->lo >> (i * 8)) & 0xff;
    buffer[i + 8] = (u128->hi >> (i * 8)) & 0xff;
  }
  mp_obj_int_t* result = mp_obj_int_new_mpz();
  mpz_set_from_bytes(&result->mpz, false, 16, buffer); // false = little-endian
  return MP_OBJ_FROM_PTR(result);
}

static mp_obj_t u64_to_mp_int(uint64_t value)
{
  if (value <= MP_SMALL_INT_MAX) {
    return MP_OBJ_NEW_SMALL_INT(value);
  }
  else {
    mp_obj_int_t* result = mp_obj_int_new_mpz();
    mpz_set_from_bytes(&result->mpz, false, 8, (const byte*)&value); // false = little-endian
    return MP_OBJ_FROM_PTR(result);
  }
}

static uint64_t mp_int_to_u64(mp_obj_t value)
{
  if (mp_obj_is_small_int(value)) {
    return mp_obj_get_int(value);
  }
  u128_t result = mp_int_to_u128(value);
  return result.lo;
}

mp_obj_t read_register_as_bytes(uint64_t register_id)
{
  uint64_t len = register_len(register_id);
  void* data = malloc(len);
  read_register(register_id, (uint64_t)data);
  mp_obj_t result = mp_obj_new_bytes(data, len);
  free(data);
  return result;
}

mp_obj_t read_register_as_str(uint64_t register_id)
{
  uint64_t len = register_len(register_id);
  void* data = malloc(len);
  read_register(register_id, (uint64_t)data);
  mp_obj_t result = mp_obj_new_str(data, len);
  free(data);
  return result;
}

mp_obj_t read_default_temp_register_as_bytes()
{
  return read_register_as_bytes(default_temp_register_id);
}

mp_obj_t read_default_temp_register_as_str()
{
  return read_register_as_str(default_temp_register_id);
}

// @near.export decorator, implemented via AST inspection, no-op here
static mp_obj_t near_export(mp_obj_t fn)
{
  return fn;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_export_obj, near_export);

// test helper, implemented in near.py, no-op here
static mp_obj_t near_test_method(mp_obj_t contract_path, mp_obj_t method_name, mp_obj_t input)
{
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(near_test_method_obj, near_test_method);

// test helper, implemented in near.py, no-op here
static mp_obj_t near_build_contract(mp_obj_t contract_path)
{
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_build_contract_obj, near_build_contract);

static mp_obj_t near_test_account_id()
{
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(near_test_account_id_obj, near_test_account_id);

static mp_obj_t near_test_add_extra_balance()
{
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(near_test_add_extra_balance_obj, near_test_add_extra_balance);

// Registers
static mp_obj_t near_read_register(mp_obj_t register_id)
{
  return read_register_as_bytes(mp_obj_get_int(register_id));
}
MP_DEFINE_CONST_FUN_OBJ_1(near_read_register_obj, near_read_register);

static mp_obj_t near_read_register_as_str(mp_obj_t register_id)
{
  return read_register_as_str(mp_obj_get_int(register_id));
}
MP_DEFINE_CONST_FUN_OBJ_1(near_read_register_as_str_obj, near_read_register_as_str);

static mp_obj_t near_register_len(mp_obj_t register_id)
{
  return u64_to_mp_int(register_len(mp_obj_get_int(register_id)));
}
MP_DEFINE_CONST_FUN_OBJ_1(near_register_len_obj, near_register_len);

static mp_obj_t near_write_register(mp_obj_t register_id, mp_obj_t data)
{
  near_api_ptr_t data_ptr = get_mp_str_or_bytes_data(data);
  write_register(mp_obj_get_int(register_id), data_ptr.len, data_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(near_write_register_obj, near_write_register);

// Context API
static mp_obj_t near_current_account_id()
{
  current_account_id(default_temp_register_id);
  return read_default_temp_register_as_str();
}
MP_DEFINE_CONST_FUN_OBJ_0(near_current_account_id_obj, near_current_account_id);

static mp_obj_t near_signer_account_id()
{
  signer_account_id(default_temp_register_id);
  return read_default_temp_register_as_str();
}
MP_DEFINE_CONST_FUN_OBJ_0(near_signer_account_id_obj, near_signer_account_id);

static mp_obj_t near_signer_account_pk()
{
  signer_account_pk(default_temp_register_id);
  return read_default_temp_register_as_bytes();
}
MP_DEFINE_CONST_FUN_OBJ_0(near_signer_account_pk_obj, near_signer_account_pk);

static mp_obj_t near_predecessor_account_id()
{
  predecessor_account_id(default_temp_register_id);
  return read_default_temp_register_as_str();
}
MP_DEFINE_CONST_FUN_OBJ_0(near_predecessor_account_id_obj, near_predecessor_account_id);

static mp_obj_t near_input()
{
  input(default_temp_register_id);
  return read_default_temp_register_as_bytes();
}
MP_DEFINE_CONST_FUN_OBJ_0(near_input_obj, near_input);

static mp_obj_t near_input_as_str()
{
  input(default_temp_register_id);
  return read_default_temp_register_as_bytes();
}
MP_DEFINE_CONST_FUN_OBJ_0(near_input_as_str_obj, near_input_as_str);

static mp_obj_t near_block_height()
{
  return u64_to_mp_int(block_index()); // we use deprecated block_index() here for now as the replacement, block_height() is not yet universally available
}
MP_DEFINE_CONST_FUN_OBJ_0(near_block_height_obj, near_block_height);

static mp_obj_t near_block_timestamp()
{
  return u64_to_mp_int(block_timestamp());
}
MP_DEFINE_CONST_FUN_OBJ_0(near_block_timestamp_obj, near_block_timestamp);

static mp_obj_t near_epoch_height()
{
  return u64_to_mp_int(epoch_height());
}
MP_DEFINE_CONST_FUN_OBJ_0(near_epoch_height_obj, near_epoch_height);

static mp_obj_t near_storage_usage()
{
  return u64_to_mp_int(storage_usage());
}
MP_DEFINE_CONST_FUN_OBJ_0(near_storage_usage_obj, near_storage_usage);

// Economics API
static mp_obj_t near_account_balance()
{
  u128_t u128 = { 0, 0 };
  account_balance((uint64_t)&u128);
  return u128_to_mp_int(&u128);
}
MP_DEFINE_CONST_FUN_OBJ_0(near_account_balance_obj, near_account_balance);

static mp_obj_t near_account_locked_balance()
{
  u128_t u128 = { 0, 0 };
  account_locked_balance((uint64_t)&u128);
  return u128_to_mp_int(&u128);
}
MP_DEFINE_CONST_FUN_OBJ_0(near_account_locked_balance_obj, near_account_locked_balance);

static mp_obj_t near_attached_deposit()
{
  u128_t u128 = { 0, 0 };
  attached_deposit((uint64_t)&u128);
  return u128_to_mp_int(&u128);
}
MP_DEFINE_CONST_FUN_OBJ_0(near_attached_deposit_obj, near_attached_deposit);

static mp_obj_t near_prepaid_gas()
{
  return u64_to_mp_int(prepaid_gas());
}
MP_DEFINE_CONST_FUN_OBJ_0(near_prepaid_gas_obj, near_prepaid_gas);

static mp_obj_t near_used_gas()
{
  return u64_to_mp_int(used_gas());
}
MP_DEFINE_CONST_FUN_OBJ_0(near_used_gas_obj, near_used_gas);

// Math API
static mp_obj_t near_random_seed()
{
  random_seed(default_temp_register_id);
  return read_default_temp_register_as_bytes();
}
MP_DEFINE_CONST_FUN_OBJ_0(near_random_seed_obj, near_random_seed);

static mp_obj_t near_hmac_impl(mp_obj_t value, void (*hmac_fn)(uint64_t value_len, uint64_t value_ptr, uint64_t register_id))
{
  near_api_ptr_t value_ptr = get_mp_bytes_data(value);
  hmac_fn(value_ptr.len, value_ptr.ptr, default_temp_register_id);
  return read_default_temp_register_as_bytes();
}

static mp_obj_t near_sha256(mp_obj_t value)
{
  return near_hmac_impl(value, sha256);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_sha256_obj, near_sha256);

static mp_obj_t near_keccak256(mp_obj_t value)
{
  return near_hmac_impl(value, keccak256);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_keccak256_obj, near_keccak256);

static mp_obj_t near_keccak512(mp_obj_t value)
{
  return near_hmac_impl(value, keccak512);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_keccak512_obj, near_keccak512);

static mp_obj_t near_ripemd160(mp_obj_t value)
{
  return near_hmac_impl(value, ripemd160);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_ripemd160_obj, near_ripemd160);

static mp_obj_t near_ecrecover(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t hash = args[0]; mp_obj_t sig = args[1]; mp_obj_t v = args[2]; mp_obj_t malleability_flag = args[3];
  if (!mp_obj_is_type(hash, &mp_type_bytes) ||
    !mp_obj_is_type(sig, &mp_type_bytes) ||
    !mp_obj_is_bool(malleability_flag)) {
    mp_raise_TypeError(MP_ERROR_TEXT("hash and sig must be bytes and malleability_flag must be bool"));
    return mp_const_none;
  }
  near_api_ptr_t hash_ptr = get_mp_bytes_data(hash);
  near_api_ptr_t sig_ptr = get_mp_bytes_data(sig);
  uint64_t result = ecrecover(hash_ptr.len, hash_ptr.ptr, sig_ptr.len, sig_ptr.ptr,
    mp_obj_get_int(v), malleability_flag == mp_const_true, default_temp_register_id);
  return result ? read_default_temp_register_as_bytes() : mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_ecrecover_obj, 4, 4, near_ecrecover);

static mp_obj_t near_ed25519_verify(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t sig = args[0]; mp_obj_t msg = args[1]; mp_obj_t pub_key = args[2];
  if (!mp_obj_is_type(sig, &mp_type_bytes) ||
    !mp_obj_is_type(msg, &mp_type_bytes) ||
    !mp_obj_is_type(pub_key, &mp_type_bytes)) {
    mp_raise_TypeError(MP_ERROR_TEXT("sig, msg and pub_key must be bytes"));
    return mp_const_none;
  }
  near_api_ptr_t sig_ptr = get_mp_bytes_data(sig);
  near_api_ptr_t msg_ptr = get_mp_bytes_data(msg);
  near_api_ptr_t pub_key_ptr = get_mp_bytes_data(pub_key);
  return mp_obj_new_bool(ed25519_verify(sig_ptr.len, sig_ptr.ptr, msg_ptr.len, msg_ptr.ptr, pub_key_ptr.len, pub_key_ptr.ptr));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_ed25519_verify_obj, 4, 4, near_ed25519_verify);

// Miscellaneous API
static mp_obj_t near_value_return(mp_obj_t value)
{
  near_api_ptr_t value_ptr = get_mp_str_or_bytes_data(value);
  value_return(value_ptr.len, value_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_value_return_obj, near_value_return);

static mp_obj_t near_panic()
{
  panic();
  return mp_const_none; // Unreachable
}
MP_DEFINE_CONST_FUN_OBJ_0(near_panic_obj, near_panic);

static mp_obj_t near_panic_utf8(mp_obj_t msg)
{
  if (mp_obj_is_str(msg)) {
    near_api_ptr_t msg_ptr = get_mp_str_data(msg);
    panic_utf8(msg_ptr.len, msg_ptr.ptr);
  }
  else {
    panic();
  }
  return mp_const_none; // Unreachable
}
MP_DEFINE_CONST_FUN_OBJ_1(near_panic_utf8_obj, near_panic_utf8);

static mp_obj_t near_log_utf8(mp_obj_t msg)
{
  near_api_ptr_t msg_ptr = get_mp_str_or_bytes_data(msg);
  log_utf8(msg_ptr.len, msg_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_log_utf8_obj, near_log_utf8);

static mp_obj_t near_log(mp_obj_t msg)
{
  near_api_ptr_t msg_ptr = get_mp_str_or_bytes_data(msg);
  log_utf8(msg_ptr.len, msg_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_log_obj, near_log);

static mp_obj_t near_log_utf16(mp_obj_t msg)
{
  near_api_ptr_t msg_ptr = get_mp_bytes_data(msg);
  log_utf16(msg_ptr.len, msg_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_log_utf16_obj, near_log_utf16);

static mp_obj_t near_abort_(size_t n_args, const mp_obj_t* args)
{
  // near_abort doesn't seem to work as we expect (should msg and filename be in utf-16?), so use panic_utf8() instead for now
  mp_obj_t msg = args[0];
  near_api_ptr_t msg_ptr = get_mp_str_data(msg);
  panic_utf8(msg_ptr.len, msg_ptr.ptr);
  return mp_const_none; // Unreachable
  // mp_obj_t msg = args[0]; mp_obj_t filename = args[1]; mp_obj_t line = args[2]; mp_obj_t col = args[3];
  // near_api_ptr_t msg_ptr = get_mp_str_data(msg);
  // char *msg_c = malloc(msg_ptr.len + 1);
  // memcpy(msg_c, (const void*)msg_ptr.ptr, msg_ptr.len); msg_c[msg_ptr.len] = 0;
  // near_api_ptr_t filename_ptr = get_mp_str_data(filename);
  // char *filename_c = malloc(filename_ptr.len + 1);
  // memcpy(filename_c, (const void*)filename_ptr.ptr, filename_ptr.len); filename_c[filename_ptr.len] = 0;
  // near_abort((uint32_t)msg_c, (uint32_t)filename_c, mp_obj_get_int(line), mp_obj_get_int(col));
  // return mp_const_none; // Unreachable
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_abort_obj, 4, 4, near_abort_);

// Promises API
static mp_obj_t near_promise_create(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t account_id = args[0]; mp_obj_t function_name = args[1]; mp_obj_t arguments = args[2]; mp_obj_t amount = args[3]; mp_obj_t gas = args[4];
  near_api_ptr_t acc_id_ptr = get_mp_str_data(account_id);
  near_api_ptr_t fn_ptr = get_mp_str_data(function_name);
  near_api_ptr_t args_ptr = get_mp_str_data(arguments);
  u128_t u128_amount = mp_int_to_u128(amount);
  return u64_to_mp_int(promise_create(acc_id_ptr.len, acc_id_ptr.ptr, fn_ptr.len, fn_ptr.ptr, args_ptr.len, args_ptr.ptr,
    (uint64_t)&u128_amount, mp_int_to_u64(gas)));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_promise_create_obj, 5, 5, near_promise_create);

static mp_obj_t near_promise_then(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t promise_index = args[0]; mp_obj_t account_id = args[1]; mp_obj_t function_name = args[2]; mp_obj_t arguments = args[3];
  mp_obj_t amount = args[4]; mp_obj_t gas = args[5];
  near_api_ptr_t acc_id_ptr = get_mp_str_data(account_id);
  near_api_ptr_t fn_ptr = get_mp_str_data(function_name);
  near_api_ptr_t args_ptr = get_mp_str_data(arguments);
  u128_t u128_amount = mp_int_to_u128(amount);
  return u64_to_mp_int(promise_then(mp_obj_get_int(promise_index), acc_id_ptr.len, acc_id_ptr.ptr, fn_ptr.len, fn_ptr.ptr, args_ptr.len, args_ptr.ptr,
    (uint64_t)&u128_amount, mp_int_to_u64(gas)));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_promise_then_obj, 6, 6, near_promise_then);

static mp_obj_t near_promise_and(mp_obj_t promise_indices)
{
  if (!mp_obj_is_type(promise_indices, &mp_type_list)) {
    mp_raise_TypeError("promise_indices should be a list of promise indices");
  }
  mp_obj_list_t* list = MP_OBJ_TO_PTR(promise_indices);
  uint64_t* promise_indices_buf = malloc(list->len * sizeof(uint64_t));
  for (size_t i = 0; i < list->len; i++) {
    mp_obj_t item = list->items[i];
    if (!mp_obj_is_int(item)) {
      mp_raise_TypeError("Each promise index should be an integer");
      free(promise_indices_buf);
      return mp_const_none;
    }
    promise_indices_buf[i] = mp_obj_get_int(item);
  }
  uint64_t result = promise_and((uint64_t)promise_indices_buf, list->len);
  free(promise_indices_buf);
  return u64_to_mp_int(result);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_promise_and_obj, near_promise_and);

static mp_obj_t near_promise_batch_create(mp_obj_t account_id)
{
  near_api_ptr_t acc_id_ptr = get_mp_str_data(account_id);
  return u64_to_mp_int(promise_batch_create(acc_id_ptr.len, acc_id_ptr.ptr));
}
MP_DEFINE_CONST_FUN_OBJ_1(near_promise_batch_create_obj, near_promise_batch_create);

static mp_obj_t near_promise_batch_then(mp_obj_t promise_index, mp_obj_t account_id)
{
  near_api_ptr_t acc_id_ptr = get_mp_str_data(account_id);
  return u64_to_mp_int(promise_batch_then(mp_int_to_u64(promise_index), acc_id_ptr.len, acc_id_ptr.ptr));
}
MP_DEFINE_CONST_FUN_OBJ_2(near_promise_batch_then_obj, near_promise_batch_then);

static mp_obj_t near_promise_batch_action_create_account(mp_obj_t promise_index)
{
  promise_batch_action_create_account(mp_int_to_u64(promise_index));
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_promise_batch_action_create_account_obj, near_promise_batch_action_create_account);

static mp_obj_t near_promise_batch_action_deploy_contract(mp_obj_t promise_index, mp_obj_t code)
{
  near_api_ptr_t code_ptr = get_mp_bytes_data(code);
  promise_batch_action_deploy_contract(mp_int_to_u64(promise_index), code_ptr.len, code_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(near_promise_batch_action_deploy_contract_obj, near_promise_batch_action_deploy_contract);

static mp_obj_t near_promise_batch_action_function_call(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t promise_index = args[0]; mp_obj_t function_name = args[1]; mp_obj_t arguments = args[2]; mp_obj_t amount = args[3]; mp_obj_t gas = args[4];
  near_api_ptr_t fn_ptr = get_mp_str_data(function_name);
  near_api_ptr_t args_ptr = get_mp_str_data(arguments);
  u128_t u128_amount = mp_int_to_u128(amount);
  promise_batch_action_function_call(mp_int_to_u64(promise_index), fn_ptr.len, fn_ptr.ptr, args_ptr.len, args_ptr.ptr,
    (uint64_t)&u128_amount, mp_int_to_u64(gas));
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_promise_batch_action_function_call_obj, 5, 5, near_promise_batch_action_function_call);

static mp_obj_t near_promise_batch_action_function_call_weight(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t promise_index = args[0]; mp_obj_t function_name = args[1]; mp_obj_t arguments = args[2]; mp_obj_t amount = args[3]; mp_obj_t gas = args[4]; mp_obj_t weight = args[5];
  near_api_ptr_t fn_ptr = get_mp_str_data(function_name);
  near_api_ptr_t args_ptr = get_mp_str_data(arguments);
  u128_t u128_amount = mp_int_to_u128(amount);
  promise_batch_action_function_call_weight(mp_int_to_u64(promise_index), fn_ptr.len, fn_ptr.ptr, args_ptr.len, args_ptr.ptr,
    (uint64_t)&u128_amount, mp_int_to_u64(gas), mp_int_to_u64(weight));
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_promise_batch_action_function_call_weight_obj, 6, 6, near_promise_batch_action_function_call_weight);

static mp_obj_t near_promise_batch_action_transfer(mp_obj_t promise_index, mp_obj_t amount)
{
  u128_t u128_amount = mp_int_to_u128(amount);
  promise_batch_action_transfer(mp_int_to_u64(promise_index), (uint64_t)&u128_amount);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(near_promise_batch_action_transfer_obj, near_promise_batch_action_transfer);

static mp_obj_t near_promise_batch_action_stake(mp_obj_t promise_index, mp_obj_t amount, mp_obj_t pub_key)
{
  u128_t u128_amount = mp_int_to_u128(amount);
  near_api_ptr_t public_key_ptr = get_mp_str_or_bytes_data(pub_key);
  promise_batch_action_stake(mp_int_to_u64(promise_index), (uint64_t)&u128_amount, public_key_ptr.len, public_key_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(near_promise_batch_action_stake_obj, near_promise_batch_action_stake);

static mp_obj_t near_promise_batch_action_add_key_with_full_access(mp_obj_t promise_index, mp_obj_t public_key, mp_obj_t nonce)
{
  near_api_ptr_t public_key_ptr = get_mp_str_or_bytes_data(public_key);
  promise_batch_action_add_key_with_full_access(mp_int_to_u64(promise_index), public_key_ptr.len, public_key_ptr.ptr, mp_int_to_u64(nonce));
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(near_promise_batch_action_add_key_with_full_access_obj, near_promise_batch_action_add_key_with_full_access);

static mp_obj_t near_promise_batch_action_add_key_with_function_call(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t promise_index = args[0]; mp_obj_t public_key = args[1]; mp_obj_t nonce = args[2];
  mp_obj_t allowance = args[3]; mp_obj_t receiver_id = args[4]; mp_obj_t function_names = args[5];
  near_api_ptr_t public_key_ptr = get_mp_str_or_bytes_data(public_key);
  u128_t u128_allowance = mp_int_to_u128(allowance);
  near_api_ptr_t receiver_id_ptr = get_mp_str_data(receiver_id);
  // todo: support function_names passed as list?
  near_api_ptr_t function_names_ptr = get_mp_str_data(function_names);
  promise_batch_action_add_key_with_function_call(mp_int_to_u64(promise_index),
    public_key_ptr.len, public_key_ptr.ptr, mp_int_to_u64(nonce), (uint64_t)&u128_allowance,
    receiver_id_ptr.len, receiver_id_ptr.ptr, function_names_ptr.len, function_names_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_promise_batch_action_add_key_with_function_call_obj, 6, 6, near_promise_batch_action_add_key_with_function_call);

static mp_obj_t near_promise_batch_action_delete_key(mp_obj_t promise_index, mp_obj_t public_key)
{
  near_api_ptr_t public_key_ptr = get_mp_str_or_bytes_data(public_key);
  promise_batch_action_delete_key(mp_int_to_u64(promise_index), public_key_ptr.len, public_key_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(near_promise_batch_action_delete_key_obj, near_promise_batch_action_delete_key);

static mp_obj_t near_promise_batch_action_delete_account(mp_obj_t promise_index, mp_obj_t beneficiary_id)
{
  near_api_ptr_t beneficiary_id_ptr = get_mp_str_or_bytes_data(beneficiary_id);
  promise_batch_action_delete_account(mp_int_to_u64(promise_index), beneficiary_id_ptr.len, beneficiary_id_ptr.ptr);
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(near_promise_batch_action_delete_account_obj, near_promise_batch_action_delete_account);

static mp_obj_t near_promise_yield_create(size_t n_args, const mp_obj_t* args)
{
  mp_obj_t function_name = args[0]; mp_obj_t arguments = args[1]; mp_obj_t gas = args[2]; mp_obj_t gas_weight = args[3];
  near_api_ptr_t fn_ptr = get_mp_str_data(function_name);
  near_api_ptr_t args_ptr = get_mp_str_data(arguments);
  uint64_t promise_id = promise_yield_create(fn_ptr.len, fn_ptr.ptr, args_ptr.len, args_ptr.ptr,
    mp_obj_get_int(gas), mp_obj_get_int(gas_weight), default_temp_register_id);
  mp_obj_t items[] = { u64_to_mp_int(promise_id), read_default_temp_register_as_str() };
  return mp_obj_new_tuple(2, items);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(near_promise_yield_create_obj, 4, 4, near_promise_yield_create);

static mp_obj_t near_promise_yield_resume(mp_obj_t data_id, mp_obj_t payload)
{
  near_api_ptr_t data_id_ptr = get_mp_str_data(data_id);
  near_api_ptr_t payload_ptr = get_mp_str_or_bytes_data(payload);
  return mp_obj_new_bool(promise_yield_resume(data_id_ptr.len, data_id_ptr.ptr, payload_ptr.len, payload_ptr.ptr));
}
MP_DEFINE_CONST_FUN_OBJ_2(near_promise_yield_resume_obj, near_promise_yield_resume);

static mp_obj_t near_promise_results_count()
{
  return u64_to_mp_int(promise_results_count());
}
MP_DEFINE_CONST_FUN_OBJ_0(near_promise_results_count_obj, near_promise_results_count);

static mp_obj_t near_promise_result(mp_obj_t result_idx)
{
  uint64_t result = promise_result(mp_obj_get_int(result_idx), default_temp_register_id);
  mp_obj_t items[] = { u64_to_mp_int(result), read_default_temp_register_as_bytes() };
  return mp_obj_new_tuple(2, items);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_promise_result_obj, near_promise_result);

static mp_obj_t near_promise_result_as_str(mp_obj_t result_idx)
{
  uint64_t result = promise_result(mp_obj_get_int(result_idx), default_temp_register_id);
  mp_obj_t items[] = { u64_to_mp_int(result), read_default_temp_register_as_str() };
  return mp_obj_new_tuple(2, items);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_promise_result_as_str_obj, near_promise_result_as_str);

static mp_obj_t near_promise_return(mp_obj_t promise_id)
{
  promise_return(mp_obj_get_int(promise_id));
  return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_promise_return_obj, near_promise_return);

// Storage API
static mp_obj_t near_storage_write(mp_obj_t key, mp_obj_t value)
{
  near_api_ptr_t key_ptr = get_mp_str_or_bytes_data(key);
  near_api_ptr_t value_ptr = get_mp_str_or_bytes_data(value);
  uint64_t result = storage_write(key_ptr.len, key_ptr.ptr, value_ptr.len, value_ptr.ptr, default_temp_register_id);
  return result == 1 ? read_default_temp_register_as_bytes() : mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(near_storage_write_obj, near_storage_write);

static mp_obj_t near_storage_read(mp_obj_t key)
{
  near_api_ptr_t key_ptr = get_mp_str_or_bytes_data(key);
  uint64_t result = storage_read(key_ptr.len, key_ptr.ptr, default_temp_register_id);
  return result == 1 ? read_default_temp_register_as_bytes() : mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_storage_read_obj, near_storage_read);

static mp_obj_t near_storage_remove(mp_obj_t key)
{
  near_api_ptr_t key_ptr = get_mp_str_or_bytes_data(key);
  uint64_t result = storage_remove(key_ptr.len, key_ptr.ptr, default_temp_register_id);
  return result == 1 ? read_default_temp_register_as_bytes() : mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_storage_remove_obj, near_storage_remove);

static mp_obj_t near_storage_has_key(mp_obj_t key)
{
  near_api_ptr_t key_ptr = get_mp_str_or_bytes_data(key);
  return storage_has_key(key_ptr.len, key_ptr.ptr) > 0 ? mp_const_true : mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_1(near_storage_has_key_obj, near_storage_has_key);

// Validator API
static mp_obj_t near_validator_stake(mp_obj_t account_id)
{
  near_api_ptr_t acc_id_ptr = get_mp_str_data(account_id);
  u128_t u128_stake = { 0, 0 };
  validator_stake(acc_id_ptr.len, acc_id_ptr.ptr, (uint64_t)&u128_stake);
  return u128_to_mp_int(&u128_stake);
}
MP_DEFINE_CONST_FUN_OBJ_1(near_validator_stake_obj, near_validator_stake);

static mp_obj_t near_validator_total_stake()
{
  u128_t u128_stake = { 0, 0 };
  validator_total_stake((uint64_t)&u128_stake);
  return u128_to_mp_int(&u128_stake);
}
MP_DEFINE_CONST_FUN_OBJ_0(near_validator_total_stake_obj, near_validator_total_stake);

// Alt BN128 API
static mp_obj_t near_alt_bn128_g1_multiexp(mp_obj_t value)
{
  near_api_ptr_t value_ptr = get_mp_bytes_data(value);
  alt_bn128_g1_multiexp(value_ptr.len, value_ptr.ptr, default_temp_register_id);
  return read_default_temp_register_as_bytes();
}
MP_DEFINE_CONST_FUN_OBJ_1(near_alt_bn128_g1_multiexp_obj, near_alt_bn128_g1_multiexp);

static mp_obj_t near_alt_bn128_g1_sum(mp_obj_t value)
{
  near_api_ptr_t value_ptr = get_mp_bytes_data(value);
  alt_bn128_g1_sum(value_ptr.len, value_ptr.ptr, default_temp_register_id);
  return read_default_temp_register_as_bytes();
}
MP_DEFINE_CONST_FUN_OBJ_1(near_alt_bn128_g1_sum_obj, near_alt_bn128_g1_sum);

static mp_obj_t near_alt_bn128_pairing_check(mp_obj_t value)
{
  near_api_ptr_t value_ptr = get_mp_bytes_data(value);
  return mp_obj_new_bool(alt_bn128_pairing_check(value_ptr.len, value_ptr.ptr));
}
MP_DEFINE_CONST_FUN_OBJ_1(near_alt_bn128_pairing_check_obj, near_alt_bn128_pairing_check);

static const mp_rom_map_elem_t near_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_near) },
    { MP_ROM_QSTR(MP_QSTR_export), MP_ROM_PTR(&near_export_obj) },
    { MP_ROM_QSTR(MP_QSTR_test_method), MP_ROM_PTR(&near_test_method_obj) },
    { MP_ROM_QSTR(MP_QSTR_build_contract), MP_ROM_PTR(&near_build_contract_obj) },
    { MP_ROM_QSTR(MP_QSTR_test_account_id), MP_ROM_PTR(&near_test_account_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_test_add_extra_balance), MP_ROM_PTR(&near_test_add_extra_balance_obj) },

    // Registers
    { MP_ROM_QSTR(MP_QSTR_read_register), MP_ROM_PTR(&near_read_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_register_as_str), MP_ROM_PTR(&near_read_register_as_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_len), MP_ROM_PTR(&near_register_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_register), MP_ROM_PTR(&near_write_register_obj) },

    // Context API
    { MP_ROM_QSTR(MP_QSTR_current_account_id), MP_ROM_PTR(&near_current_account_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_signer_account_id), MP_ROM_PTR(&near_signer_account_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_signer_account_pk), MP_ROM_PTR(&near_signer_account_pk_obj) },
    { MP_ROM_QSTR(MP_QSTR_predecessor_account_id), MP_ROM_PTR(&near_predecessor_account_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_input), MP_ROM_PTR(&near_input_obj) },
    { MP_ROM_QSTR(MP_QSTR_input_as_str), MP_ROM_PTR(&near_input_as_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_block_height), MP_ROM_PTR(&near_block_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_block_timestamp), MP_ROM_PTR(&near_block_timestamp_obj) },
    { MP_ROM_QSTR(MP_QSTR_epoch_height), MP_ROM_PTR(&near_epoch_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_usage), MP_ROM_PTR(&near_storage_usage_obj) },

    // Economics API
    { MP_ROM_QSTR(MP_QSTR_account_balance), MP_ROM_PTR(&near_account_balance_obj) },
    { MP_ROM_QSTR(MP_QSTR_account_locked_balance), MP_ROM_PTR(&near_account_locked_balance_obj) },
    { MP_ROM_QSTR(MP_QSTR_attached_deposit), MP_ROM_PTR(&near_attached_deposit_obj) },
    { MP_ROM_QSTR(MP_QSTR_prepaid_gas), MP_ROM_PTR(&near_prepaid_gas_obj) },
    { MP_ROM_QSTR(MP_QSTR_used_gas), MP_ROM_PTR(&near_used_gas_obj) },

    // Math API
    { MP_ROM_QSTR(MP_QSTR_random_seed), MP_ROM_PTR(&near_random_seed_obj) },
    { MP_ROM_QSTR(MP_QSTR_sha256), MP_ROM_PTR(&near_sha256_obj) },
    { MP_ROM_QSTR(MP_QSTR_keccak256), MP_ROM_PTR(&near_keccak256_obj) },
    { MP_ROM_QSTR(MP_QSTR_keccak512), MP_ROM_PTR(&near_keccak512_obj) },
    { MP_ROM_QSTR(MP_QSTR_ripemd160), MP_ROM_PTR(&near_ripemd160_obj) },
    { MP_ROM_QSTR(MP_QSTR_ecrecover), MP_ROM_PTR(&near_ecrecover_obj) },
    { MP_ROM_QSTR(MP_QSTR_ed25519_verify), MP_ROM_PTR(&near_ed25519_verify_obj) },

    // Miscellaneous API
    { MP_ROM_QSTR(MP_QSTR_value_return), MP_ROM_PTR(&near_value_return_obj) },
    { MP_ROM_QSTR(MP_QSTR_panic), MP_ROM_PTR(&near_panic_obj) },
    { MP_ROM_QSTR(MP_QSTR_panic_utf8), MP_ROM_PTR(&near_panic_utf8_obj) },
    { MP_ROM_QSTR(MP_QSTR_log_utf8), MP_ROM_PTR(&near_log_utf8_obj) },
    { MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&near_log_obj) },
    { MP_ROM_QSTR(MP_QSTR_log_utf16), MP_ROM_PTR(&near_log_utf16_obj) },
    { MP_ROM_QSTR(MP_QSTR_abort), MP_ROM_PTR(&near_abort_obj) },

    // Promises API
    { MP_ROM_QSTR(MP_QSTR_promise_create), MP_ROM_PTR(&near_promise_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_then), MP_ROM_PTR(&near_promise_then_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_and), MP_ROM_PTR(&near_promise_and_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_create), MP_ROM_PTR(&near_promise_batch_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_then), MP_ROM_PTR(&near_promise_batch_then_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_create_account), MP_ROM_PTR(&near_promise_batch_action_create_account_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_deploy_contract), MP_ROM_PTR(&near_promise_batch_action_deploy_contract_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_function_call), MP_ROM_PTR(&near_promise_batch_action_function_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_function_call_weight), MP_ROM_PTR(&near_promise_batch_action_function_call_weight_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_transfer), MP_ROM_PTR(&near_promise_batch_action_transfer_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_stake), MP_ROM_PTR(&near_promise_batch_action_stake_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_add_key_with_full_access_obj), MP_ROM_PTR(&near_promise_batch_action_add_key_with_function_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_delete_key), MP_ROM_PTR(&near_promise_batch_action_delete_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_batch_action_delete_account), MP_ROM_PTR(&near_promise_batch_action_delete_account_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_yield_create), MP_ROM_PTR(&near_promise_yield_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_yield_resume), MP_ROM_PTR(&near_promise_yield_resume_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_results_count), MP_ROM_PTR(&near_promise_results_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_result), MP_ROM_PTR(&near_promise_result_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_result_as_str), MP_ROM_PTR(&near_promise_result_as_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_promise_return), MP_ROM_PTR(&near_promise_return_obj) },

    // Storage API
    { MP_ROM_QSTR(MP_QSTR_storage_write), MP_ROM_PTR(&near_storage_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_read), MP_ROM_PTR(&near_storage_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_remove), MP_ROM_PTR(&near_storage_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage_has_key), MP_ROM_PTR(&near_storage_has_key_obj) },

    // Validator API
    { MP_ROM_QSTR(MP_QSTR_validator_stake), MP_ROM_PTR(&near_validator_stake_obj) },
    { MP_ROM_QSTR(MP_QSTR_validator_total_stake), MP_ROM_PTR(&near_validator_total_stake_obj) },

    // Alt BN128 API
    { MP_ROM_QSTR(MP_QSTR_alt_bn128_g1_multiexp), MP_ROM_PTR(&near_alt_bn128_g1_multiexp_obj) },
    { MP_ROM_QSTR(MP_QSTR_alt_bn128_g1_sum), MP_ROM_PTR(&near_alt_bn128_g1_sum_obj) },
    { MP_ROM_QSTR(MP_QSTR_alt_bn128_pairing_check), MP_ROM_PTR(&near_alt_bn128_pairing_check_obj) },
};
static MP_DEFINE_CONST_DICT(near_module_globals, near_module_globals_table);

const mp_obj_module_t mp_module_near = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&near_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_near, mp_module_near);
