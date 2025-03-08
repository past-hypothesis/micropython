#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "near_api.h"
#include <string.h>

// Buffer to store random bytes from NEAR
static unsigned char near_random_buffer[32];
static size_t buffer_position = 32;  // Start at end to force refill on first use

// Function to refill the buffer with random bytes from NEAR
static void refill_random_buffer(void) {
    random_seed(0); // Use register 0 for random data
    read_register(0, (uint64_t)near_random_buffer);
    buffer_position = 0;
}

// Get next byte from the buffer, refilling if needed
static uint8_t next_random_byte(void) {
    if (buffer_position >= sizeof(near_random_buffer)) {
        refill_random_buffer();
    }
    return near_random_buffer[buffer_position++];
}

// Get a 32-bit random value
static uint32_t next_random_uint32(void) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        value = (value << 8) | next_random_byte();
    }
    return value;
}

// Random number in range [0, n)
static uint32_t random_below(uint32_t n) {
    uint32_t mask = 1;
    while ((n & mask) < n) {
        mask = (mask << 1) | 1;
    }
    uint32_t r;
    do {
        r = next_random_uint32() & mask;
    } while (r >= n);
    return r;
}

// For floating point random
static mp_float_t random_float(void) {
    mp_float_union_t u;
    u.p.sgn = 0;
    u.p.exp = (1 << (MP_FLOAT_EXP_BITS - 1)) - 1;
    if (MP_FLOAT_FRAC_BITS <= 32) {
        u.p.frc = next_random_uint32();
    } else {
        u.p.frc = ((uint64_t)next_random_uint32() << 32) | (uint64_t)next_random_uint32();
    }
    return u.f - 1;
}

// Implementation of getrandbits
static mp_obj_t mod_random_getrandbits(mp_obj_t num_in) {
    mp_int_t n = mp_obj_get_int(num_in);
    if (n > 32 || n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 32 or less"));
        return NULL;
    }
    if (n == 0) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    uint32_t mask = ~0;
    // Beware of C undefined behavior when shifting by >= than bit size
    mask >>= (32 - n);
    return mp_obj_new_int_from_uint(next_random_uint32() & mask);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_random_getrandbits_obj, mod_random_getrandbits);

// Implementation of seed - just resets the buffer
static mp_obj_t mod_random_seed(size_t n_args, const mp_obj_t *args) {
    // Force buffer refresh on next call
    buffer_position = sizeof(near_random_buffer);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_random_seed_obj, 0, 1, mod_random_seed);

#if MICROPY_PY_RANDOM_EXTRA_FUNCS

static mp_obj_t mod_random_randrange(size_t n_args, const mp_obj_t *args) {
    mp_int_t start = mp_obj_get_int(args[0]);
    if (n_args == 1) {
        // range(stop)
        if (start > 0) {
            return mp_obj_new_int(random_below((uint32_t)start));
        } else {
            goto error;
        }
    } else {
        mp_int_t stop = mp_obj_get_int(args[1]);
        if (n_args == 2) {
            // range(start, stop)
            if (start < stop) {
                return mp_obj_new_int(start + random_below((uint32_t)(stop - start)));
            } else {
                goto error;
            }
        } else {
            // range(start, stop, step)
            mp_int_t step = mp_obj_get_int(args[2]);
            mp_int_t n;
            if (step > 0) {
                n = (stop - start + step - 1) / step;
            } else if (step < 0) {
                n = (stop - start + step + 1) / step;
            } else {
                goto error;
            }
            if (n > 0) {
                return mp_obj_new_int(start + step * random_below((uint32_t)n));
            } else {
                goto error;
            }
        }
    }

error:
    mp_raise_ValueError(NULL);
    return NULL;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_random_randrange_obj, 1, 3, mod_random_randrange);

static mp_obj_t mod_random_randint(mp_obj_t a_in, mp_obj_t b_in) {
    mp_int_t a = mp_obj_get_int(a_in);
    mp_int_t b = mp_obj_get_int(b_in);
    if (a <= b) {
        return mp_obj_new_int(a + random_below((uint32_t)(b - a + 1)));
    } else {
        mp_raise_ValueError(NULL);
        return NULL;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_random_randint_obj, mod_random_randint);

static mp_obj_t mod_random_choice(mp_obj_t seq) {
    mp_int_t len = mp_obj_get_int(mp_obj_len(seq));
    if (len > 0) {
        return mp_obj_subscr(seq, mp_obj_new_int(random_below((uint32_t)len)), MP_OBJ_SENTINEL);
    } else {
        mp_raise_type(&mp_type_IndexError);
        return NULL;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_random_choice_obj, mod_random_choice);

#if MICROPY_PY_BUILTINS_FLOAT

static mp_obj_t mod_random_random(void) {
    return mp_obj_new_float(random_float());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_random_random_obj, mod_random_random);

static mp_obj_t mod_random_uniform(mp_obj_t a_in, mp_obj_t b_in) {
    mp_float_t a = mp_obj_get_float(a_in);
    mp_float_t b = mp_obj_get_float(b_in);
    return mp_obj_new_float(a + (b - a) * random_float());
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_random_uniform_obj, mod_random_uniform);

#endif // MICROPY_PY_BUILTINS_FLOAT

#endif // MICROPY_PY_RANDOM_EXTRA_FUNCS

#if SEED_ON_IMPORT
static mp_obj_t mod_random___init__(void) {
    // This module may be imported by more than one name so need to ensure
    // that it's only ever seeded once.
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        mod_random_seed(0, NULL);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_random___init___obj, mod_random___init__);
#endif

static const mp_rom_map_elem_t mp_module_random_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_random) },
    #if SEED_ON_IMPORT
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_random___init___obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_getrandbits), MP_ROM_PTR(&mod_random_getrandbits_obj) },
    { MP_ROM_QSTR(MP_QSTR_seed), MP_ROM_PTR(&mod_random_seed_obj) },
    #if MICROPY_PY_RANDOM_EXTRA_FUNCS
    { MP_ROM_QSTR(MP_QSTR_randrange), MP_ROM_PTR(&mod_random_randrange_obj) },
    { MP_ROM_QSTR(MP_QSTR_randint), MP_ROM_PTR(&mod_random_randint_obj) },
    { MP_ROM_QSTR(MP_QSTR_choice), MP_ROM_PTR(&mod_random_choice_obj) },
    #if MICROPY_PY_BUILTINS_FLOAT
    { MP_ROM_QSTR(MP_QSTR_random), MP_ROM_PTR(&mod_random_random_obj) },
    { MP_ROM_QSTR(MP_QSTR_uniform), MP_ROM_PTR(&mod_random_uniform_obj) },
    #endif
    #endif
};

static MP_DEFINE_CONST_DICT(mp_module_random_globals, mp_module_random_globals_table);

const mp_obj_module_t mp_module_random = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_random_globals,
};

MP_REGISTER_MODULE(MP_QSTR_random, mp_module_random);