/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_PFRO_WRITER_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_PFRO_WRITER_H_
#include "ob_imicro_block_writer.h"
#include "ob_row_writer.h"
#include "ob_data_buffer.h"
#include "ob_block_sstable_struct.h"

namespace oceanbase {
namespace common {
class ObNewRow;
}
namespace storage {
class ObStoreRow;
}
namespace blocksstable {

#define HIST_LEN 32

extern const uint8_t MAX_INT_BIT_WIDTH = 64;
extern const uint8_t CLOSET_BIT_WIDTH_MAP[65];
extern const uint8_t BIT_WIDTH_TO_INDEX[65];
extern const uint8_t INDEX_TO_BIT_WIDTH[HIST_LEN];

struct FixedBitSizes {
    enum FBS {
        ONE = 0, TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, ELEVEN, TWELVE,
        THIRTEEN, FOURTEEN, FIFTEEN, SIXTEEN, SEVENTEEN, EIGHTEEN, NINETEEN,
        TWENTY, TWENTYONE, TWENTYTWO, TWENTYTHREE, TWENTYFOUR, TWENTYSIX,
        TWENTYEIGHT, THIRTY, THIRTYTWO, FORTY, FORTYEIGHT, FIFTYSIX, SIXTYFOUR, SIZE
    };
};

struct PforEncodingMeta {
  int64_t min_int;
  int64_t max_int;
  uint32_t bits_width_95p;
  uint32_t bits_width_100p;
  uint32_t patch_width;
  uint32_t patch_gap_width;
  uint32_t patch_length;
  uint32_t patch_gap_length; // length of merged result
  //uint64_t *base_delta_literals;
  PforEncodingMeta():
      base_delta_literals(NULL)
  {}
};

struct ObMicroBlockPforEncoderHeader {
  struct {
    uint8_t value_width : 7;
    uint8_t patch_length_tail : 1;
  };
  uint8_t patch_length_head;
  struct {
    // the bit width of the base value of the whole list
    uint8_t base_width : 3;
    uint8_t patch_width : 5;
  };
  struct {
    uint8_t patch_gap_width : 3;
    uint8_t patch_list_length: 5;
  };
};

class ObMicroBlockPforEncoder {
public:
  ObMicroBlockPforEncoder(){}
  virtual ~ObMicroBlockPforEncoder();
  void init();
  int percentileBits(int64_t * data, size_t data_size, double p, 
    bool reuse_hist, uint32_t & bit_width);
  int encodoe(int64_t *data, size_t data_size, uint8_t *out, size_t &out_size);
  void reset();
private:
  int prepare(int64_t *data, size_t data_size, PforEncodingMeta &meta);
  int write(PforEncodingMeta &meta, uint8_t *out, size_t &out_size);
  inline uint32_t get_closet_bit_width(int64_t width) {
    if(width > MAX_INT_BIT_WIDTH) return MAX_INT_BIT_WIDTH;
    return CLOSET_BIT_WIDTH_MAP[width];
  }

  inline bool is_safe_subtract(int64_t left, int64_t right) {
    return ((left ^ right) >= 0) || ((left ^ (left - right)) >= 0);
  }

  uint32_t bit_width_to_index(uint32_t width);

  uint32_t index_to_bit_width(uint32_t index);

  uint32_t cal_bit_width(int64_t value);

  int bit_packing(int64_t* input, size_t len, uint32_t bit_width, ObSelfBufferWriter & writer);

  ObSelfBufferWriter data_buffer_;
  ObSelfBufferWriter base_delta_buffer_;
  ObSelfBufferWriter patch_buffer_;
  ObSelfBufferWriter patch_gap_buffer_;
  ObSelfBufferWriter patch_with_gap_buffer_; // the final patch list combining patchs and gaps
  int32_t histgram[HIST_LEN];
};
}
}
#endif