/**
 * @file
 * FakeBus, a scripted stand in for a real bus, shared by the driver tests.
 *
 * Header only, so each test suite that includes it compiles its own copy.
 */

#pragma once

#include <Arduino.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "ir_head.h"

/**
 * Replays scripted replies and records everything the reader sends.
 *
 * Each send() starts the next scripted reply, if there is one. A reply is a
 * list of chunks, and each chunk only becomes readable once its delay has
 * passed since the previous chunk ran out, so a test can split a reply across
 * several reads with a gap of its choosing. Derives from IrHead so it serves
 * both ModbusRtuReader and Iec6205621Reader.
 */
class FakeBus : public IrHead {
 public:
  /** Part of a reply, made readable after a gap. */
  struct Chunk {
    std::vector<uint8_t> bytes;  ///< Bytes handed out by readByte().
    uint32_t delayUs;            ///< Silence before the first byte, in us.
  };

  std::vector<std::vector<uint8_t>> sent;  ///< Every frame passed to send(), in order.
  std::vector<uint32_t> baudRates;         ///< Every rate passed to setBaudRate(), in order.

  /**
   * Queues a reply that arrives all at once, straight after its send().
   *
   * @param[in] bytes  The reply.
   */
  void queueReply(const std::vector<uint8_t> &bytes) { replies.push_back({{bytes, 0}}); }

  /**
   * Queues a reply made of delayed chunks.
   *
   * @param[in] chunks  The reply, in order.
   */
  void queueChunks(const std::vector<Chunk> &chunks) { replies.push_back(chunks); }

  /**
   * Queues a reply given as text.
   *
   * @param[in] text  The reply, without its null terminator.
   */
  void queueText(const char *text) { queueReply(std::vector<uint8_t>(text, text + strlen(text))); }

  /**
   * Queues an empty reply, for a send the meter never answers.
   */
  void queueSilence() { replies.push_back({}); }

  /**
   * Does nothing, there is no peripheral.
   *
   * @retval EXIT_SUCCESS  Always.
   */
  int init() override { return EXIT_SUCCESS; }

  /**
   * Records the frame and starts the next scripted reply.
   *
   * @param[in] data  Bytes the reader sent.
   * @param[in] len   Number of bytes in @p data.
   * @retval EXIT_SUCCESS  Always.
   */
  int send(const uint8_t *data, size_t len) override {
    sent.emplace_back(data, data + len);
    current.clear();
    if (nextReply < replies.size()) {
      current = replies[nextReply++];
    }
    chunkIndex = 0;
    bytePos = 0;
    chunkStartUs = micros();
    return EXIT_SUCCESS;
  }

  /**
   * Hands out the next byte of the current reply once its chunk is due.
   *
   * @return The byte, or -1 if the reply is used up or the chunk is not due.
   */
  int readByte() override {
    if (!available()) {
      return -1;
    }
    const Chunk &chunk = current[chunkIndex];
    uint8_t byte = chunk.bytes[bytePos++];
    if (bytePos == chunk.bytes.size()) {
      chunkIndex++;
      bytePos = 0;
      chunkStartUs = micros();
    }
    return byte;
  }

  /**
   * Reports whether a byte is due.
   *
   * @retval true   readByte() will return a byte.
   * @retval false  The reply is used up, or the next chunk is still delayed.
   */
  bool available() override {
    // Empty chunks carry no bytes, so skip them rather than stall
    while (chunkIndex < current.size() && current[chunkIndex].bytes.empty()) {
      chunkIndex++;
    }
    if (chunkIndex >= current.size()) {
      return false;
    }
    return bytePos > 0 || micros() - chunkStartUs >= current[chunkIndex].delayUs;
  }

  /**
   * Records the rate, there is no link to retune.
   *
   * @param[in] baud  Rate the reader switched to, in baud.
   */
  void setBaudRate(uint32_t baud) override { baudRates.push_back(baud); }

 private:
  std::vector<std::vector<Chunk>> replies;  ///< Scripted replies, one per send().
  size_t nextReply = 0;                     ///< Index of the reply the next send() starts.
  std::vector<Chunk> current;               ///< Reply being handed out.
  size_t chunkIndex = 0;                    ///< Chunk of #current being handed out.
  size_t bytePos = 0;                       ///< Next byte within that chunk.
  uint32_t chunkStartUs = 0;                ///< micros() when the current chunk's delay began.
};
