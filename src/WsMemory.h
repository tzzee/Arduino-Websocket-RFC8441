#pragma once

#include <cstddef>

/**
 * @brief このライブラリが使うmalloc/freeを外部から差し替える。
 * @details 未設定またはNULLを渡した場合はライブラリ内蔵の既定実装(PSRAM優先、
 *          失敗時は内部RAMへフォールバック)にフォールバックする。
 * @param malloc_fn 差し替え先のmalloc相当関数。
 * @param free_fn 差し替え先のfree相当関数。
 */
void ws_rfc8441_set_malloc_free(void* (*malloc_fn)(size_t), void (*free_fn)(void*));

/**
 * @brief 現在設定されているアロケータでメモリを確保する。
 * @param bytes 確保するバイト数。
 * @return 確保したメモリへのポインタ。失敗時はnullptr。
 */
void* ws_rfc8441_malloc(size_t bytes);

/**
 * @brief ws_rfc8441_mallocで確保したメモリを解放する。
 * @param p 解放するポインタ。nullptrの場合は何もしない。
 */
void ws_rfc8441_free(void* p);
