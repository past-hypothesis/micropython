#include "py/runtime.h"
#include "py/obj.h"
#include "py/objint.h"
#include "py/objstr.h"
#include "py/smallint.h"
#include "py/mpz.h"
#include "py/mphal.h"
#include "cmp.h"

#include <string.h>

// MessagePack extension type to use for Python bigint serialization
#define MSGPACK_BIGINT_EXT_TYPE 81

static mp_obj_t u64_to_mp_int(uint64_t value)
{
  if (value <= MP_SMALL_INT_MAX) {
    return MP_OBJ_NEW_SMALL_INT(value);
  }
  else {
    mp_obj_int_t* result = mp_obj_int_new_mpz();
    mpz_set_from_ll(&result->mpz, value, false);
    return MP_OBJ_FROM_PTR(result);
  }
}

static mp_obj_t i64_to_mp_int(int64_t value)
{
  if (value <= MP_SMALL_INT_MAX) {
    return MP_OBJ_NEW_SMALL_INT(value);
  }
  else {
    mp_obj_int_t* result = mp_obj_int_new_mpz();
    mpz_set_from_ll(&result->mpz, value, true);
    return MP_OBJ_FROM_PTR(result);
  }
}

typedef struct
{
  uint8_t* buf;
  size_t buf_size;
  size_t buf_pos;
} buf_writer_ctx;

static size_t buf_writer(cmp_ctx_t* ctx, const void* data, size_t limit)
{
  buf_writer_ctx* w = ctx->buf;
  if (w->buf_pos + limit > w->buf_size) {
    size_t new_size = w->buf_size * 2 + limit;
    w->buf = m_realloc(w->buf, new_size);
    w->buf_size = new_size;
  }
  memcpy(w->buf + w->buf_pos, data, limit);
  w->buf_pos += limit;
  return limit;
}

static void obj_to_msgpack(cmp_ctx_t* cmp, mp_obj_t obj)
{
  if (mp_obj_is_type(obj, &mp_type_dict)) {
    mp_obj_dict_t* dict = MP_OBJ_TO_PTR(obj);
    cmp_write_map(cmp, dict->map.used);
    for (size_t i = 0; i < dict->map.alloc; i++) {
      if (dict->map.table[i].key != MP_OBJ_NULL) {
        obj_to_msgpack(cmp, dict->map.table[i].key);
        obj_to_msgpack(cmp, dict->map.table[i].value);
      }
    }
  }
  else if (mp_obj_is_type(obj, &mp_type_list)) {
    mp_obj_list_t* list = MP_OBJ_TO_PTR(obj);
    cmp_write_array(cmp, list->len);
    for (size_t i = 0; i < list->len; i++) {
      obj_to_msgpack(cmp, list->items[i]);
    }
  }
  else if (mp_obj_is_str(obj)) {
    size_t len;
    const char* str = mp_obj_str_get_data(obj, &len);
    cmp_write_str(cmp, str, len);
  }
  else if (mp_obj_is_type(obj, &mp_type_bytes)) {
    size_t len;
    const uint8_t* data = (const uint8_t*)mp_obj_str_get_data(obj, &len);
    cmp_write_bin(cmp, data, len);
  }
  else if (mp_obj_is_int(obj)) {
    if (mp_obj_is_small_int(obj)) {
      cmp_write_sint(cmp, mp_obj_get_int(obj));
    }
    else {
      mpz_t z = { 0 };
      mp_obj_int_t* int_obj = MP_OBJ_TO_PTR(obj);
      mpz_set(&z, &int_obj->mpz);
      int buf_size = (mpz_max_num_bits(&z) + 7) / 8;
      uint8_t* buf = malloc(buf_size);
      mpz_as_bytes(&z, false, false, buf_size, buf); // false = little-endian
      cmp_write_ext(cmp, MSGPACK_BIGINT_EXT_TYPE, buf_size, buf);
      free(buf);
      mpz_deinit(&z);
    }
  }
  else if (mp_obj_is_float(obj)) {
#if MICROPY_FLOAT_IMPL == MICROPY_FLOAT_IMPL_FLOAT
    float val = mp_obj_get_float(obj);
    cmp_write_float(cmp, val);
#elif MICROPY_FLOAT_IMPL == MICROPY_FLOAT_IMPL_DOUBLE
    double val = mp_obj_get_float(obj);
    cmp_write_double(cmp, val);
#else
#error "MICROPY_FLOAT_IMPL is not set"
#endif
  }
  else if (obj == mp_const_true) {
    cmp_write_bool(cmp, true);
  }
  else if (obj == mp_const_false) {
    cmp_write_bool(cmp, false);
  }
  else if (obj == mp_const_none) {
    cmp_write_nil(cmp);
  }
  else {
    mp_raise_TypeError("Unsupported type");
  }
}

static mp_obj_t msgpack_packb(mp_obj_t obj)
{
  cmp_ctx_t cmp;
  buf_writer_ctx ctx = { 0 };
  cmp_init(&cmp, &ctx, NULL, NULL, (cmp_writer)buf_writer);
  obj_to_msgpack(&cmp, obj);
  return mp_obj_new_bytes(ctx.buf, ctx.buf_pos);
}
MP_DEFINE_CONST_FUN_OBJ_1(msgpack_packb_obj, msgpack_packb);

// todo: convert 64-bit ints to mpz?
static mp_obj_t msgpack_to_obj(cmp_ctx_t* cmp)
{
  cmp_object_t obj;
  if (!cmp_read_object(cmp, &obj)) {
    mp_raise_ValueError("Invalid MessagePack data");
  }
  switch (obj.type) {
  case CMP_TYPE_NIL:
    return mp_const_none;
  case CMP_TYPE_BOOLEAN:
    return mp_obj_new_bool(obj.as.boolean);
  case CMP_TYPE_POSITIVE_FIXNUM:
    return mp_obj_new_int(obj.as.u8);
  case CMP_TYPE_UINT8:
    return mp_obj_new_int(obj.as.u8);
  case CMP_TYPE_UINT16:
    return mp_obj_new_int(obj.as.u16);
  case CMP_TYPE_UINT32:
    return u64_to_mp_int(obj.as.u32);
  case CMP_TYPE_UINT64:
    return u64_to_mp_int(obj.as.u64);
  case CMP_TYPE_NEGATIVE_FIXNUM:
    return mp_obj_new_int(obj.as.s8);
  case CMP_TYPE_SINT8:
    return mp_obj_new_int(obj.as.s8);
  case CMP_TYPE_SINT16:
    return mp_obj_new_int(obj.as.s16);
  case CMP_TYPE_SINT32:
    return i64_to_mp_int(obj.as.s32);
  case CMP_TYPE_SINT64:
    return i64_to_mp_int(obj.as.s32);
  case CMP_TYPE_FLOAT:
    return mp_obj_new_float((mp_float_t)obj.as.flt);
  case CMP_TYPE_DOUBLE:
    return mp_obj_new_float((mp_float_t)obj.as.dbl);
  case CMP_TYPE_FIXSTR:
  case CMP_TYPE_STR8:
  case CMP_TYPE_STR16:
  case CMP_TYPE_STR32: {
    char* str = m_new(char, obj.as.str_size + 1);
    uint32_t size = obj.as.str_size;
    cmp->read(cmp, str, size);
    return mp_obj_new_str(str, size);
  }
  case CMP_TYPE_BIN8:
  case CMP_TYPE_BIN16:
  case CMP_TYPE_BIN32: {
    uint8_t* data = m_new(uint8_t, obj.as.bin_size);
    uint32_t size = obj.as.bin_size;
    cmp->read(cmp, data, size);
    return mp_obj_new_bytes(data, size);
  }
  case CMP_TYPE_FIXARRAY:
  case CMP_TYPE_ARRAY16:
  case CMP_TYPE_ARRAY32: {
    uint32_t size = obj.as.array_size;
    mp_obj_list_t* list = MP_OBJ_TO_PTR(mp_obj_new_list(size, NULL));
    for (uint32_t i = 0; i < size; i++) {
      list->items[i] = msgpack_to_obj(cmp);
    }
    return MP_OBJ_FROM_PTR(list);
  }
  case CMP_TYPE_FIXMAP:
  case CMP_TYPE_MAP16:
  case CMP_TYPE_MAP32: {
    uint32_t size = obj.as.map_size;
    mp_obj_dict_t* dict = MP_OBJ_TO_PTR(mp_obj_new_dict(size));
    for (uint32_t i = 0; i < size; i++) {
      mp_obj_t key = msgpack_to_obj(cmp);
      mp_obj_t value = msgpack_to_obj(cmp);
      mp_obj_dict_store(MP_OBJ_FROM_PTR(dict), key, value);
    }
    return MP_OBJ_FROM_PTR(dict);
  }
  case CMP_TYPE_FIXEXT1:
  case CMP_TYPE_FIXEXT2:
  case CMP_TYPE_FIXEXT4:
  case CMP_TYPE_FIXEXT8:
  case CMP_TYPE_FIXEXT16:
  case CMP_TYPE_EXT8:
  case CMP_TYPE_EXT16:
  case CMP_TYPE_EXT32: {
    if (obj.as.ext.type == MSGPACK_BIGINT_EXT_TYPE) {
      uint8_t* data = malloc(obj.as.ext.size);
      uint32_t size = obj.as.ext.size;
      cmp->read(cmp, data, size);
      mp_obj_int_t* result = mp_obj_int_new_mpz();
      mpz_set_from_bytes(&result->mpz, false, obj.as.ext.size, data); // false = little-endian
      free(data);
      return MP_OBJ_FROM_PTR(result);
    }
    else {
      mp_raise_ValueError("Unsupported MessagePack extension type");
    }
  }
  default: mp_raise_ValueError("Unsupported MessagePack type");
  }
  return mp_const_none;
}

typedef struct
{
  const uint8_t* data;
  size_t pos;
  size_t length;
} buf_reader_ctx;

static bool buf_reader(cmp_ctx_t* ctx, void* data, size_t limit)
{
  buf_reader_ctx* b = ctx->buf;
  if (b->pos + limit > b->length) {
    return false;
  }
  memcpy(data, b->data + b->pos, limit);
  b->pos += limit;
  return true;
}

static mp_obj_t msgpack_unpackb(mp_obj_t data)
{
  mp_buffer_info_t bufinfo;
  mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);
  buf_reader_ctx ctx = {
      .data = bufinfo.buf,
      .pos = 0,
      .length = bufinfo.len
  };
  cmp_ctx_t cmp;
  cmp_init(&cmp, &ctx, buf_reader, NULL, NULL);
  mp_obj_t result = msgpack_to_obj(&cmp);
  if (ctx.pos != ctx.length) {
    mp_raise_ValueError("MessagePack data contained trailing bytes");
  }
  return result;
}
MP_DEFINE_CONST_FUN_OBJ_1(msgpack_unpackb_obj, msgpack_unpackb);

static const mp_rom_map_elem_t msgpack_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_msgpack) },
    { MP_ROM_QSTR(MP_QSTR_packb), MP_ROM_PTR(&msgpack_packb_obj) },
    { MP_ROM_QSTR(MP_QSTR_unpackb), MP_ROM_PTR(&msgpack_unpackb_obj) },
};

static MP_DEFINE_CONST_DICT(msgpack_module_globals, msgpack_module_globals_table);

const mp_obj_module_t msgpack_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&msgpack_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_msgpack, msgpack_user_cmodule);
