#ifndef WEBSOCKETCLIENT_HPACKINTEGER_H_
#define WEBSOCKETCLIENT_HPACKINTEGER_H_

/**
 * @file HpackInteger.h
 * @brief HPACK(RFC 7541 5.1)の整数の符号化(純粋ロジック)。
 * @details 2026-10-17 まで、HEADERS の名前と値の長さを 1 バイト(126 まで)で書いていて、それより長いヘッダー
 *          (Basic 認証の Authorization など)を送れなかった。prefix に入りきらない長さは続きのバイトに 7 ビットずつ書く。
 */

#include <cstddef>
#include <cstdint>

namespace HpackInteger {

/**
 * @brief 整数を符号化したときのバイト数を返す。
 * @param prefixBits 最初のバイトで使えるビット数(1〜8)
 * @param value 値
 * @return バイト数
 */
inline std::size_t encodedSize(std::uint8_t prefixBits, std::uint32_t value) {
  const std::uint32_t max_prefix = (1U << prefixBits) - 1U;
  if (value < max_prefix) {
    return 1;
  }
  std::size_t size = 1;
  value -= max_prefix;
  do {
    ++size;
    value >>= 7;
  } while (value > 0);
  return size;
}

/**
 * @brief 整数を符号化して書く。
 * @param prefixBits 最初のバイトで使えるビット数(1〜8)
 * @param firstByteFlags 最初のバイトの prefix より上のビット(種類のビットや Huffman のビット)
 * @param value 値
 * @param out 書き先(encodedSize() のバイト数があること)
 * @return 書いたバイト数
 */
inline std::size_t encode(std::uint8_t prefixBits, std::uint8_t firstByteFlags, std::uint32_t value, std::uint8_t* out) {
  const std::uint32_t max_prefix = (1U << prefixBits) - 1U;
  if (value < max_prefix) {
    out[0] = static_cast<std::uint8_t>(firstByteFlags | value);
    return 1;
  }
  out[0] = static_cast<std::uint8_t>(firstByteFlags | max_prefix);
  std::size_t i = 1;
  value -= max_prefix;
  while (value >= 0x80U) {
    out[i++] = static_cast<std::uint8_t>((value & 0x7FU) | 0x80U);
    value >>= 7;
  }
  out[i++] = static_cast<std::uint8_t>(value);
  return i;
}

}  // namespace HpackInteger

#endif  // WEBSOCKETCLIENT_HPACKINTEGER_H_
