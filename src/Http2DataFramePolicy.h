#pragma once

#include <cstddef>

/**
 * @brief HTTP/2 の DATA フレームを受けたときの扱いの判断。
 * @details 長さ 0 の DATA(相手が stream を閉じるときの END_STREAM だけのフレームなど)で「この stream の中身を受け取り中」
 *          の印を立てると、次にソケットに届いた別の stream の h2 フレームの頭を、この stream の中身として読んでしまう。
 *          h2 のフレームの区切りが 1 バイトずれ、以後の受け取りが黙って全部止まった(2026-10-20 屋外 ece334c0efa4。
 *          mesh の子機の stream を CSMS が閉じた直後から、親機の OCPP の返事も pong も読めず、約 90 秒後につなぎ直すまで
 *          TransactionEvent が届かなかった)。長さ 0 なら印を立てず、END_STREAM ならその場で stream を閉じる。
 */
struct Http2DataFrameDecision {
  bool trackPayload;  ///< 中身を受け取り中の印を立てる(中身が 1 バイト以上あるとき)
  bool closeStream;   ///< その場で stream を閉じる(中身が無く END_STREAM が付いているとき)

  /**
   * @brief DATA フレームの扱いを決める。
   * @param payloadLength フレームの中身の長さ
   * @param endStream END_STREAM が付いているか
   * @return 扱い
   */
  static Http2DataFrameDecision decide(std::size_t payloadLength, bool endStream) {
    Http2DataFrameDecision d;
    d.trackPayload = payloadLength > 0;
    d.closeStream = payloadLength == 0 && endStream;
    return d;
  }
};
