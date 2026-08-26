/**
 * @file micropython_engine.h
 * @brief Embedded MicroPython / Python 3 Runtime Core for Bullet OS
 */

#ifndef MICROPYTHON_ENGINE_H
#define MICROPYTHON_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool is_initialized;
    uint32_t heap_size;
    uint32_t free_heap;
    char last_error[128];
} mpy_engine_status_t;

bool mpy_engine_init(size_t heap_bytes);
void mpy_engine_deinit(void);
bool mpy_engine_exec_file(const char* filepath, char* out_buf, size_t out_max);
bool mpy_engine_eval_str(const char* code_str, char* out_buf, size_t out_max);
void mpy_engine_get_status(mpy_engine_status_t* out_status);

#ifdef __cplusplus
}
#endif

#endif
