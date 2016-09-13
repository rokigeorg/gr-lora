/* -*- c++ -*- */
/* 
 * Copyright 2016 <+YOU OR YOUR COMPANY+>.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include <volk/volk.h>
#include "encode_impl.h"
#include "stdio.h"

#include <bitset>

#define HAMMING_T1_BITMASK 0x0D  // 0b00001101
#define HAMMING_T2_BITMASK 0x0B  // 0b00001011
#define HAMMING_T4_BITMASK 0x07  // 0b00000111
#define HAMMING_T8_BITMASK 0xFF  // 0b11111111

#define INTERLEAVER_BLOCK_SIZE 8

namespace gr {
  namespace lora {

    encode::sptr
    encode::make( short spreading_factor,
                  short code_rate,
                  bool  header)
    {
      return gnuradio::get_initial_sptr
        (new encode_impl(spreading_factor, code_rate, header));
    }

    /*
     * The private constructor
     */
    encode_impl::encode_impl( short spreading_factor,
                              short code_rate,
                              bool  header)
      : gr::block("encode",
              gr::io_signature::make(0, 0, 0),
              gr::io_signature::make(0, 0, 0))
              // gr::io_signature::make(1, 1, sizeof(unsigned char)),
              // gr::io_signature::make(1, 1, sizeof(unsigned short)))
    {
      d_in_port = pmt::mp("in");
      d_out_port = pmt::mp("out");

      message_port_register_in(d_in_port);
      message_port_register_out(d_out_port);

      set_msg_handler(d_in_port, boost::bind(&encode_impl::encode, this, _1));

      d_sf = spreading_factor;
      d_cr = code_rate;
      d_header = (header) ? true : false;

      d_interleaver_size = d_sf;

      d_fft_size = (1 << spreading_factor);
    }

    /*
     * Our virtual destructor.
     */
    encode_impl::~encode_impl()
    {
    }

    void
    encode_impl::to_gray(std::vector<unsigned short> &symbols)
    {
      for (int i = 0; i < symbols.size(); i++)
      {
        symbols[i] = (symbols[i] >> 1) ^ symbols[i];
      }
    }

    void
    encode_impl::from_gray(std::vector<unsigned short> &symbols)
    {
      for (int i = 0; i < symbols.size(); i++)
      {
        symbols[i] = symbols[i] ^ (symbols[i] >> 16);
        symbols[i] = symbols[i] ^ (symbols[i] >>  8);
        symbols[i] = symbols[i] ^ (symbols[i] >>  4);
        symbols[i] = symbols[i] ^ (symbols[i] >>  2);
        symbols[i] = symbols[i] ^ (symbols[i] >>  1);
      }
    }

    void
    encode_impl::whiten(std::vector<unsigned short> &symbols)
    {
      for (int i = 0; i < symbols.size(); i++)
      {
        symbols[i] = ((unsigned char)(symbols[i] & 0xFF) ^ d_whitening_sequence[i]) & 0xFF;
      }
    }

    void
    encode_impl::il_block_print(unsigned char *block)
    {
      for (int i = 0; i < INTERLEAVER_BLOCK_SIZE; i++)
      {
        for (int j = INTERLEAVER_BLOCK_SIZE-1; j >=0 ; j--)
        {
          printf("%d ", (block[i] & (0x01 << j)) ? 1 : 0);
        }
        printf("\n");
      }
    }

    void
    encode_impl::interleave(std::vector <unsigned short> &codewords,
                            std::vector <unsigned short> &symbols)
    {
      unsigned char ppm = 8;
      int inner = 0;
      int outer = 0;
      unsigned char reordered[INTERLEAVER_BLOCK_SIZE] = {0};
      unsigned char block[INTERLEAVER_BLOCK_SIZE] = {0};
      unsigned char word = 0;
      unsigned char temp_word = 0;

      for (outer = 0; outer < codewords.size()/ppm; outer++)
      {
        memset(block, 0, d_interleaver_size*sizeof(unsigned char));

        reordered[0] = codewords[0];
        reordered[7] = codewords[1];
        reordered[2] = codewords[2];
        reordered[1] = codewords[3];
        reordered[4] = codewords[4];
        reordered[3] = codewords[5];
        reordered[6] = codewords[6];
        reordered[5] = codewords[7];

        // for (int i = 0; i < 8; i++)
        // {
        //   std::cout << std::bitset<8>(block[i]) << std::endl;
        // }

        for (inner = 0; inner < INTERLEAVER_BLOCK_SIZE; inner++)
        {
          word = reordered[inner];

          // Rotate
          word = (word >> (inner+1)%ppm) | (word << (ppm-inner-1)%ppm);
          word &= 0xFF;
          // std::cout << std::bitset<8>(word) << std::endl;

          // Reverse endianness
          word = ((word & 128) >> 7) | ((word & 64) >> 5) | ((word & 32) >> 3) | ((word & 16) >> 1) | ((word & 8) << 1) | ((word & 4) << 3) | ((word & 2) << 5) | ((word & 1) << 7);
          word &= 0xFF;

          // std::cout << std::bitset<8>(word) << std::endl;

          // Flip most significant bits
          // block[(inner+2) % ppm] |= (word & 128) >> 1;
          // block[(inner+1) % ppm] |= (word & 64) << 1;
          block[(inner+1) % ppm] |= (word & 128) >> 1;
          block[(inner+2) % ppm] |= (word & 64) << 1;
          block[(inner+3) % ppm] |= word & 32;
          block[(inner+4) % ppm] |= word & 16;
          block[(inner+5) % ppm] |= word & 8;
          block[(inner+6) % ppm] |= word & 4;
          block[(inner+7) % ppm] |= word & 2;
          block[(inner+8) % ppm] |= word & 1;
        }

        std::cout << "ENCODE " << std::bitset<8>(block[0]) << std::endl;
        std::cout << "ENCODE " << std::bitset<8>(block[1]) << std::endl;
        std::cout << "ENCODE " << std::bitset<8>(block[2]) << std::endl;
        std::cout << "ENCODE " << std::bitset<8>(block[3]) << std::endl;
        std::cout << "ENCODE " << std::bitset<8>(block[4]) << std::endl;
        std::cout << "ENCODE " << std::bitset<8>(block[5]) << std::endl;
        std::cout << "ENCODE " << std::bitset<8>(block[6]) << std::endl;
        std::cout << "ENCODE " << std::bitset<8>(block[7]) << std::endl;

        symbols.push_back(block[0]);
        symbols.push_back(block[1]);
        symbols.push_back(block[2]);
        symbols.push_back(block[3]);
        symbols.push_back(block[4]);
        symbols.push_back(block[5]);
        symbols.push_back(block[6]);
        symbols.push_back(block[7]);

      }
    }

    void
    encode_impl::hamming_encode(std::vector<unsigned char> &nybbles,
                                std::vector<unsigned short> &codewords)
    {
      unsigned char t1, t2, t4, t8;
      unsigned char mask;

      for (int i = 0; i < nybbles.size(); i++)
      {
        t1 = parity((unsigned char)nybbles[i], mask = (unsigned char)HAMMING_T1_BITMASK);
        t2 = parity((unsigned char)nybbles[i], mask = (unsigned char)HAMMING_T2_BITMASK);
        t4 = parity((unsigned char)nybbles[i], mask = (unsigned char)HAMMING_T4_BITMASK);
        t8 = parity((unsigned char)nybbles[i] | t1 << 7 | t2 << 6 | t4 << 4, 
                      mask = (unsigned char)HAMMING_T8_BITMASK);

        codewords.push_back(( (t1 << 7) |
                              (t2 << 6) |
                              (t8 << 5) |
                              (t4 << 4) |
                              (nybbles[i] & 0x08) | 
                              (nybbles[i] & 0x04) |
                              (nybbles[i] & 0x02) |
                              (nybbles[i] & 0x01)   ));

        printf("DATA:     %d\t\t", nybbles[i]);
        printf("CODEWORD: %d\n", codewords[i]);
      }
    }

    unsigned char
    encode_impl::parity(unsigned char c, unsigned char bitmask)
    {
      unsigned char parity = 0;
      unsigned char shiftme = c & bitmask;

      for (int i = 0; i < 8; i++)
      {
        if (shiftme & 0x1) parity++;
        shiftme = shiftme >> 1;
      }

      return parity % 2;
    }

    void
    encode_impl::print_payload(std::vector<unsigned char> &payload)
    {
        std::cout << "Encoded LoRa packet (hex): ";
        for (int i = 0; i < payload.size(); i++)
        {
          std::cout << std::hex << (unsigned int)payload[i] << " ";
        }
        std::cout << std::endl;
    }

    void
    encode_impl::encode (pmt::pmt_t msg)
    {
      // pmt::pmt_t meta(pmt::car(msg));
      pmt::pmt_t bytes(pmt::cdr(msg));

      size_t pkt_len(0);
      const uint8_t* bytes_in = pmt::u8vector_elements(bytes, pkt_len);

      std::cout << "ENCODE pkt_len: " << pkt_len << std::endl;

      std::vector<unsigned char> nybbles;
      std::vector<unsigned short> symbols;
      std::vector<unsigned short> codewords;

      if (d_header)
      {
        nybbles.push_back(0x3);
        nybbles.push_back(0x7);
        nybbles.push_back(0x3);
        nybbles.push_back(0x7);
        nybbles.push_back(0x3);
        nybbles.push_back(0x7);
        nybbles.push_back(0x3);
        nybbles.push_back(0x7);
      }

      for (int i = 0; i < pkt_len; i++)
      {
        nybbles.push_back((bytes_in[i] & 0xF0) >> 4);
        nybbles.push_back((bytes_in[i] & 0x0F));
      }

      hamming_encode(nybbles, codewords);
      interleave(codewords, symbols);
      whiten(symbols);
      from_gray(symbols);

      pmt::pmt_t output = pmt::init_u16vector(symbols.size(), symbols);
      pmt::pmt_t msg_pair = pmt::cons(pmt::make_dict(), output);

      std::cout << "ENCODE symbols_size" << symbols.size() << std::endl;
      std::cout << "ENCODE msg_pair: " << output << std::endl;

      message_port_pub(d_out_port, msg_pair);
    }

    void
    encode_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
    {
      // <+forecast+> e.g. ninput_items_required[0] = noutput_items 
      ninput_items_required[0] = noutput_items;
    }

    int
    encode_impl::work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      return noutput_items;
    }    

  } /* namespace lora */
} /* namespace gr */

#if 0
    int
    encode_impl::work (int noutput_items,
    // encode_impl::encode (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      const unsigned char *in = (const unsigned char *) input_items[0];
      unsigned short *out = (unsigned short *) output_items[0];
      unsigned int data_length = ninput_items[0];

      std::vector<unsigned char> nybbles;

      std::cout << "HERE" << std::endl;

      d_codewords.clear();

      // Split 8-bit bytes into nybbles for FEC processing
      for(int i=0; i < data_length; i++)
      {
        nybbles.push_back((in[i] & 0xF0) >> 4);
        nybbles.push_back((in[i] & 0x0F));
      }

      hamming_encode(nybbles, d_codewords);

      // Do <+signal processing+>
      // Tell runtime system how many input items we consumed on
      // each input stream.
      consume_each (noutput_items);

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }
#endif