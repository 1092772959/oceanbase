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

#include "ob_micro_block_pfor_encoder.h"
#include "ob_row_writer.h"
#include "storage/ob_i_store.h"

namespace oceanbase {
using namespace common;
using namespace storage;
namespace blocksstable {
  const uint8_t CLOSET_BIT_WIDTH_MAP[65] = {
    1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    26, 26, 28, 28, 30, 30, 32, 32,
    40, 40, 40, 40, 40, 40, 40, 40,
    48, 48, 48, 48, 48, 48, 48, 48,
    56, 56, 56, 56, 56, 56, 56, 56,
    64, 64, 64, 64, 64, 64, 64, 64
  };

  const uint8_t INDEX_TO_BIT_WIDTH[HIST_LEN] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    26, 28, 30, 32, 40, 48, 56, 64
  };


  const uint8_t BIT_WIDTH_TO_INDEX[65] = {
      FixedBitSizes::ONE, FixedBitSizes::ONE, FixedBitSizes::TWO, FixedBitSizes::THREE, FixedBitSizes::FOUR,
      FixedBitSizes::FIVE, FixedBitSizes::SIX, FixedBitSizes::SEVEN, FixedBitSizes::EIGHT,
      FixedBitSizes::NINE, FixedBitSizes::TEN, FixedBitSizes::ELEVEN, FixedBitSizes::TWELVE,
      FixedBitSizes::THIRTEEN, FixedBitSizes::FOURTEEN, FixedBitSizes::FIFTEEN, FixedBitSizes::SIXTEEN,
      FixedBitSizes::SEVENTEEN, FixedBitSizes::EIGHTEEN, FixedBitSizes::NINETEEN, FixedBitSizes::TWENTY,
      FixedBitSizes::TWENTYONE, FixedBitSizes::TWENTYTWO, FixedBitSizes::TWENTYTHREE, FixedBitSizes::TWENTYFOUR,
      FixedBitSizes::TWENTYSIX, FixedBitSizes::TWENTYSIX,
      FixedBitSizes::TWENTYEIGHT, FixedBitSizes::TWENTYEIGHT,
      FixedBitSizes::THIRTY, FixedBitSizes::THIRTY,
      FixedBitSizes::THIRTYTWO, FixedBitSizes::THIRTYTWO,
      FixedBitSizes::FORTY, FixedBitSizes::FORTY, FixedBitSizes::FORTY, FixedBitSizes::FORTY,
      FixedBitSizes::FORTY, FixedBitSizes::FORTY, FixedBitSizes::FORTY, FixedBitSizes::FORTY,
      FixedBitSizes::FORTYEIGHT, FixedBitSizes::FORTYEIGHT, FixedBitSizes::FORTYEIGHT, FixedBitSizes::FORTYEIGHT,
      FixedBitSizes::FORTYEIGHT, FixedBitSizes::FORTYEIGHT, FixedBitSizes::FORTYEIGHT, FixedBitSizes::FORTYEIGHT,
      FixedBitSizes::FIFTYSIX, FixedBitSizes::FIFTYSIX, FixedBitSizes::FIFTYSIX, FixedBitSizes::FIFTYSIX,
      FixedBitSizes::FIFTYSIX, FixedBitSizes::FIFTYSIX, FixedBitSizes::FIFTYSIX, FixedBitSizes::FIFTYSIX,
      FixedBitSizes::SIXTYFOUR, FixedBitSizes::SIXTYFOUR, FixedBitSizes::SIXTYFOUR, FixedBitSizes::SIXTYFOUR,
      FixedBitSizes::SIXTYFOUR, FixedBitSizes::SIXTYFOUR, FixedBitSizes::SIXTYFOUR, FixedBitSizes::SIXTYFOUR
  };

  int ObMicroBlockPforEncoder::percentileBits(int32_t * data, size_t data_size, double ratio, 
    bool reuse_hist, uint32_t & bit_width) 
  {
    int ret = OB_SUCCESS;
    if(p <= 0.0 || p > 1) {
      return OB_INVALID_ARGUMENT;
    }
    if (!reuseHist) {
        // histogram that store the encoded bit requirement for each values.
        // maximum number of bits that can encoded is 32 (refer FixedBitSizes)
        memset(histgram, 0, HIST_LEN * sizeof(int32_t));
        // compute the histogram
        for(size_t i = offset; i < data_size; i++) {
            uint32_t idx = bit_width_to_index(cal_bit_width(data[i]));
            histgram[idx] += 1;
        }
    }
    int32_t ratio_len = static_cast<int32_t>(static_cast<double>(data_size) * (1.0 - ratio));
    for(int32_t i = HIST_LEN - 1; i >= 0; i--) {
        ratio_len -= histgram[i];
        if (ratio_len < 0) {
            return index_to_bit_width(static_cast<uint32_t>(i));
        }
    }
    return ret;
  }

  int encodoe(int64_t * data, size_t data_size, uint8_t *out, size_t &out_size)
  {
    int ret = OB_SUCCESS;
    PforEncodingMeta meta;
    if(OB_LIKELY(data == NULL) || OB_LIKELY(data_size > 0)){
      ret = OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "Pfor encoding invalid argument.", K(ret));
      return ret;
    }
    // get min and max
    meta.max_int = 1LL << 63;
    meta.min_int = 0x7fffffffffffffff;
    
    for(int i = 0; i < data_size; ++i) {
      if(data[i] < meta.min_int){
        meta.min_int = data[i];
      } else if(data[i] > meta.max_int) {
        meta.max_int = data[i];
      }
    }

    // check delta overflow
    if(OB_UNLIKELY(meta.min_int, meta.max_int)){
      STORAGE_LOG(WARN, "Unable to use Pfor encoding due to delta overflow.");
      return OB_INTEGER_PRECISION_OVERFLOW;
    }

    //prepare

    // get bit width percentile 95
    if(OB_FAIL(percentileBits(data, data_size, 0.95, false, meta.bits_width_95p))){
      STORAGE_LOG(WARN,"Pfor encoding fail to get percentile 95 bits.", K(ret));
    } else if(OB_FAIL(percentileBits(data, data_size, 1.0, false, meta.bits_width_100p))){
      STORAGE_LOG(WARN, "Pfor encoding fail to get percentile 100 bits.");
    } else if(OB_FAIL(prepare(data, data_size, meta)){
      STORAGE_LOG(WARN, "Pfor prepare fails.");
    } else if(OB_FAIL(write(meta, out, out_size)) {
      STORAGE_LOG(ERROR, "Pfor write fails.");
    }
    return ret;
  }
  
  // 
  int write(PforEncodingMeta &meta, uint8_t *out, size_t out_size) {
    int ret = OB_SUCCESS;
    ObSelfBufferWriter writer;
    // encode header
    const uint32_t value_width = bit_width_to_index(meta.bits_width_95p);
    
    // length is from [1, 512]
    meta.patch_length--;
    
    const uint32_t tailBit = (meta.patch_length & 0x100) >> 8;
    const uint8_t first_byte = static_cast<uint8_t>(efb | tailBit);
    const uint8_t second_byte = static_cast<uint8_t>(meta.patch_length & 0xff);

    const bool is_negative = meta.min_int < 0;
    if (is_negative) {
      meta.min_int = -meta.min_int;
    }
    
    const uint32_t base_width = cal_bit_width(meta.min) + 1;
    const uint32_t base_bytes = base_width % 8 == 0 ? base_width / 8 : (base_width / 8) + 1;
    // first 3 bits of the third byte
    const uint32_t bw = (base_bytes - 1) << 5;
    if (is_negative) {
      meta.min_int |= (1LL << ((base_bytes * 8) - 1));
    }

    // the lowest 5 bits represent the patch width
    const uint8_t third_byte = static_cast<uint8_t>(bw | bit_width_to_index(meta.patch_width));
    
    // the fourth byte is 
    const uint8_t fourth_byte = static_cast<uint8_t>((meta.patch_gap_width - 1) << 5 | meta.patch_length);

    writer.write_pod(first_byte);
    writer.write_pod(second_byte);
    writer.write_pod(third_byte);
    writer.write_pod(fourth_byte);

    // start encode the data.
    
    // big edian for base value
    for(int32_t i = static_cast<int32_t>(bw); i >= 0; --i) {
      uint8_t b = static_cast<uint8_t>(((meta.min_int) >> (i * 8)) & 0xff);
      // encode
      writer.writer_pod(b);
    }
    
    // write data list with bit packing
    bit_packing();

    // write patch list with bit packing
    return ret;
  }
  
  int ObMicroBlockPforEncoder::prepare(int64_t * data, size_t data_size, 
        PforEncodingMeta &meta)
  {
    int ret = OB_SUCCESS;
    int64_t mask = static_cast<int64_t>(static_cast<uint64_t>(1) << meta.bits_width_95p) -  1;
    
    base_delta_buffer_.ensure_space(data_size * sizeof(int64_t));
    for(size_t i = 0; i < data_size; ++i){
      base_delta_buffer_.write_pod(data[i] - meta.min_int);
    }

    // batch meta data
    meta.patch_length = static_cast<uint32_t>(std::ceil(data_size / 20));
    meta.patch_width = meta.bits_width_100p - meta.bits_width_95p;
    if(meta.patch_width == 64) {
      meta.patch_width = 56;
      meta.bits_width_95p = 8;
      mask = static_cast<int64_t>(static_cast<uint64_t>(1) << meta.bits_width_95p) -  1;
    }
    uint64_t max_gap = 0;
    uint64_t prev_patch_idx = 0;
    uint64_t patch_count = 0;
    uint64_t gap_count = 0;
  
    for(size_t i = 0; i < data_size; ++i) {
      if(data[i] <= mask){
        continue;
      }
      patch_count++;
      uint64_t gap = i - prev_patch_idx;
      if(gap > max_gap) {
        gap = max_gap;
      }
      prev_patch_idx = i;
      patch_buffer_.write_pod(static_cast<int64_t>(data[i] >> meta.bits_width_95p));
      patch_gap_buffer_.write_pod(static_cast<int64_t>(gap));
      
      // store the low k bits instead
      base_delta_buffer_.write_pod(data[i] & mask);
    }
    meta.patch_length = patch_idx;

    // deal with gap length width encoding
    if (max_gap == 0 && meta.patch_length > 0) {
      meta.patch_gap_width = 1;
    } else {
      meta.patch_gap_width = get_closet_bit_width(static_cast<int64_t>(max_gap));
    }

    if (meta.patch_gap_width > 8) {
      meta.patch_gap_width = 8;
      if (max_gap >= 511) { // need 2 extra entries
        meta.patch_length += 2;
      } else {
        meta.patch_length += 1; // need 1 extra entry
      }
    }
    
    // merge patch gap and patch value
    uint32_t gap_idx = 0;
    uint32_t patch_idx = 0;
    ObBufferReader patch_reader(patch_buffer_.data(), patch_buffer_.length(), 0);
    ObBufferReader patch_gap_reader(patch_gap_buffer_.data(), patch_gap_buffer_.length(), 0);
    int64_t patch_gap_list_size = patch_buffer_.length() + patch_gap_buffer_.length();
    patch_gap_buffer_.ensure_space(patch_gap_list_size);

    int64_t gap;
    int64_t patch;
    for (size_t i = 0; i < meta.patch_length; ++i) {
      patch_reader.read_pod(patch);
      patch_gap_reader.read_pod(gap);
      while (gap > 255) {
        patch_gap_buffer_.write_pod(static_cast<int64_t>(255 << meta.patch_width));
        ++i;
        gap -= 255;
      }
      patch_gap_buffer_.write_pod(static_cast<int64_t>(255 << meta.patch_width | patch));
    }
    meta.patch_gap_length = patch_gap_buffer_.length() / sizeof(int64_t);
    return ret;
  }

  uint32_t ObMicroBlockPforEncoder::cal_bit_width(int64_t value)
  {
    if (value < 0) 
      return get_closet_bit_width(MAX_INT_BIT_WIDTH);
    uint32_t width = 0;
    while (value != 0) {
      ++width;
      value >>= 1;
    }
    return get_closet_bit_width(width);
  }

  uint32_t ObMicroBlockPforEncoder::index_to_bit_width(uint32_t index) {
    return INDEX_TO_BIT_WIDTH[index];
  }

  uint32_t ObMicroBlockPforEncoder::bit_width_to_index(uint32_t width) {
    if (width > MAX_INT_BIT_WIDTH) {
      return FixedBitSizes::SIXTYFOUR;
    }
    return BIT_WIDTH_TO_INDEX[width];
  }

  int ObMicroBlockPfroEncoder::bit_packing(int64_t* input, size_t len, uint32_t bit_width,
      ObSelfBufferWriter& writer) {
    int ret = OB_SUCCESS;
    if(input == NULL || len < 1 || bit_width < 1) {
      return OB_FAIL;
    }
    if (get_closet_bit_width(bit_width) == bit_width) { // one byte to store more than one item
      if (bit_width < 8) {
        uint8_t bit_mask = static_cast<uint8_t>((1 << bit_width) - 1);
        uint32_t elem_per_byte = 8 / bit_width;
        unit32_t elem_rem = static_cast<uint32_t>(len % elem_per_byte);
        uint32_t loop_count = len - elem_rem;
        for(uint32_t i = 0; i < loop_count; i += elem_per_byte) {
          uint8_t to_write = 0;
          for(uint32_t j = 0; j < elem_per_byte; ++j) {
            to_write |= static_cast<uint8_t>((input[i+j] & bit_mask) <<(8 - (j + 1) * bit_width));
          }
          writer.write_pod(to_write);
        }

        // process remaining elements
        if (elem_rem > 0) {
          uint32_t shift = 8 - bit_width;
          uint8_t to_write = 0;
          for(uint32_t i = loop_count; i < len; ++i) {
            to_write |= static_cast<uint8_t>((input[i] & bit_mask) << shift);
            shift -= bit_width;
          }
        }
      } else { // more than one byte to represent
        uint32_t byte_per_elem = bit_width / 8;
        uint32_t byte_mask = 255;
        for(uint32_t i = 0; i < len; ++i) {
          for(uint32_t j = 0; j < byte_per_elem; ++j) {
            uint8_t to_write = static_cast<uint8_t>(input[i] >> (8 * (byte_per_elem - j - 1)) & byte_mask);
            writer.write(to_write);
          }
        }
      }
      return ret;
    }

    // unaligned bit_width
    uint32_t bits_left = 8;
    uint8_t to_write = 0;
    for(uint32_t i = 0; i < len; ++i) {
      int64_t val = input[i];
      uint32_t bits = bit_width;
      while (bits > bits_left) {
        to_write |= static_cast<uint8_t>(value >> (bits - bits_left));
        bits -= bits_left;
        value &= (static_cast<uint64_t>(1) << bits) - 1; // mask the lower bits
        writer.write_pod(to_write);
        to_write = 0;
        bits_left = 8;
      }
      bits_left -= bits;
      to_write |= static_cast<uint8_t>(value << bits_left);
      if (bits_left == 0) {
        writer.write_pod(to_write);
        to_write = 0;
        bits_left = 0;
      }
    }
    
    // the last bits
    if (bits_left != 0) {
      writer.write(to_write);
    }
    return ret;
  }
}
}