#ifndef WEBSOCKETCLIENT_HTTP2HEADERDECODER_H_
#define WEBSOCKETCLIENT_HTTP2HEADERDECODER_H_

#include <cstddef>
#include <cstdint>

struct nghttp2_hd_inflater;

/**
 * @brief HTTP/2 の受けたヘッダー(HPACK)を読む。接続ごとに 1 つ持つ(動的表は接続の状態なので)。
 * @details 2026-10-17 まで自前の読み方で、Huffman を読めず、「名前は表、値は文字」の形でも値に表の既定値を使っていた
 *          (`:status: 401` が `:status: 200` に見え、続くヘッダーも読み違えた)。ESP-IDF の nghttp2 の inflater を使う。
 */
class Http2HeaderDecoder {
 public:
  /// 読んだヘッダー 1 つの通知(name・value は終端なし。呼ばれている間だけ有効)
  using FieldFn = void (*)(const char* name, std::size_t nameLength, const char* value, std::size_t valueLength,
                           void* arg);

  Http2HeaderDecoder() = default;
  ~Http2HeaderDecoder();
  Http2HeaderDecoder(const Http2HeaderDecoder&) = delete;
  Http2HeaderDecoder& operator=(const Http2HeaderDecoder&) = delete;

  /**
   * @brief ヘッダーのかたまり(HEADERS と CONTINUATION の中身)を読む。
   * @param block かたまり(HEADERS の padding と priority は除いたもの)
   * @param length その長さ
   * @param endHeaders END_HEADERS が立っているか(最後のかたまりか)
   * @param fn 読んだヘッダーごとに呼ぶ
   * @param arg fn に渡す
   * @return 読めたら true(false なら接続の HPACK の状態が壊れたので、接続を作り直す)
   */
  bool decode(const std::uint8_t* block, std::size_t length, bool endHeaders, FieldFn fn, void* arg);

  /// 接続を作り直すときに呼ぶ(動的表を捨てる)
  void reset();

 private:
  nghttp2_hd_inflater* inflater_ = nullptr;
};

#endif  // WEBSOCKETCLIENT_HTTP2HEADERDECODER_H_
