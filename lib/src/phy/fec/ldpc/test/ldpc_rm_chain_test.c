/*
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

/*!
 * \file ldpc_rm_chain_test.c
 * \brief End-to-end test for LDPC encoder, rate-matcher, rate-dematcher and decoder.
 *
 * A batch of example messages is randomly generated, encoded, rate-matched, 2-PAM modulated,
 * sent over an AWGN channel and, finally, rate-dematched and decoded by all three types of
 * decoder. Transmitted and received messages are compared to estimate the WER.
 * Multiple batches are simulated if the number of errors is not significant
 * enough.
 *
 *
 * Synopsis: **ldpc_rm_chain_test [options]**
 *
 * Options:
 *  - **-b \<number\>** Base Graph (1 or 2. Default 1).
 *  - **-l \<number\>** Lifting Size (according to 5GNR standard. Default 2).
 *  - **-e \<number\>** Codeword length after rate matching (set to 0 [default] for full rate).
 *  - **-f \<number\>** Number of filler bits (Default 17).
 *  - **-r \<number\>** Redundancy version {0-3}.
 *  - **-m \<number\>** Modulation type BPSK = 0, QPSK =1, QAM16 = 2, QAM64 = 3, QAM256 = 4.
 *  - **-M \<number\>** Limited buffer size.
 *  - **-s \<number\>** SNR in dB(Default 3 dB).
 *  - **-B \<number\>** Number of codewords in a batch.(Default 100).
 *  - **-N \<number\>** Max number of simulated batches.(Default 10000).
 *  - **-E \<number\>** Minimum number of errors for a significant simulation.(Default 100).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "srslte/phy/channel/ch_awgn.h"
#include "srslte/phy/fec/ldpc/ldpc_common.h"
#include "srslte/phy/fec/ldpc/ldpc_decoder.h"
#include "srslte/phy/fec/ldpc/ldpc_encoder.h"
#include "srslte/phy/fec/ldpc/ldpc_rm.h"
#include "srslte/phy/utils/debug.h"
#include "srslte/phy/utils/random.h"
#include "srslte/phy/utils/vector.h"

static srslte_basegraph_t base_graph = BG1;    /*!< \brief Base Graph (BG1 or BG2). */
static uint32_t           lift_size  = 2;      /*!< \brief Lifting Size. */
static uint32_t           rm_length  = 0;      /*!< \brief Codeword length after rate matching. */
static uint32_t           F          = 22 - 5; /*!< \brief Number of filler bits in each CBS. */
static uint8_t            rv         = 0;      /*!< \brief Redundancy version {0-3}. */
static mod_type_t         mod_type   = BPSK;   /*!< \brief Modulation type: BPSK, QPSK, QAM16, QAM64, QAM256 = 4 */
static uint32_t           Nref       = 0;      /*!< \brief Limited buffer size. */
static float              snr        = 0;      /*!< \brief Signal-to-Noise Ratio [dB]. */

static int finalK = 0; /*!< \brief Number of uncoded bits (message length, including punctured and filler bits). */
static int finalN = 0; /*!< \brief Number of coded bits (codeword length). */

static int batch_size  = 100;   /*!< \brief Number of codewords in a batch. */
static int max_n_batch = 10000; /*!< \brief Max number of simulated batches. */
static int req_errors  = 100;   /*!< \brief Minimum number of errors for a significant simulation. */
#define MS_SF 0.75f             /*!< \brief Scaling factor for the normalized min-sum decoding algorithm. */

/*!
 * \brief Prints test help when wrong parameter is passed as input.
 */
void usage(char* prog)
{

  printf("Usage: %s [-bX] [-lX] [-eX] [-fX] [-rX] [-mX] [-MX] [sX]\n", prog);
  printf("\t-b Base Graph [(1 or 2) Default %d]\n", base_graph + 1);
  printf("\t-l Lifting Size [Default %d]\n", lift_size);
  printf("\t-e Word length after rate matching [Default %d (no rate matching i.e. E = N - F)]\n", rm_length);
  printf("\t-f Filler bits size (F) [Default %d]\n", F);
  printf("\t-r Redundancy version (rv) [Default %d]\n", rv);
  printf("\t-m Modulation_type BPSK=0, QPSK=1, 16QAM=2, 64QAM=3, 256QAM = 4 [Default %d]\n", mod_type);
  printf("\t-M Limited buffer size (Nref) [Default = %d (normal buffer Nref = N)]\n", Nref);
  printf("\t-s SNR [dB, Default %.1f dB]\n", snr);
  printf("\t-B Number of codewords in a batch. [Default %d]\n", batch_size);
  printf("\t-N Max number of simulated batches. [Default %d]\n", max_n_batch);
  printf("\t-E Minimum number of errors for a significant simulation. [Default %d]\n", req_errors);
}

/*!
 * \brief Parses the input line.
 */
void parse_args(int argc, char** argv)
{
  int opt = 0;
  while ((opt = getopt(argc, argv, "b:l:e:f:r:m:M:s:B:N:E:")) != -1) {
    switch (opt) {
      case 'b':
        base_graph = (int)strtol(optarg, NULL, 10) - 1;
        break;
      case 'l':
        lift_size = (int)strtol(optarg, NULL, 10);
        break;
      case 'e':
        rm_length = (int)strtol(optarg, NULL, 10);
        break;
      case 'f':
        F = (uint32_t)strtol(optarg, NULL, 10);
        break;
      case 'r':
        rv = (uint8_t)strtol(optarg, NULL, 10);
        break;
      case 'm':
        mod_type = (mod_type_t)strtol(optarg, NULL, 10);
        break;
      case 'M':
        Nref = (uint32_t)strtol(optarg, NULL, 10);
        break;
      case 's':
        snr = (float)strtod(optarg, NULL);
        break;
      case 'B':
        batch_size = (int)strtol(optarg, NULL, 10);
        break;
      case 'N':
        max_n_batch = (int)strtol(optarg, NULL, 10);
        break;
      case 'E':
        req_errors = (int)strtol(optarg, NULL, 10);
        break;
      default:
        usage(argv[0]);
        exit(-1);
    }
  }
}

/*!
 * \brief Prints decoder statistics.
 */
void print_decoder(char* title, int n_batches, int n_errors, double elapsed_time);

/*!
 * \brief Main test function.
 */
int main(int argc, char** argv)
{

  uint8_t* messages_true          = NULL;
  uint8_t* messages_sim_f         = NULL;
  uint8_t* messages_sim_s         = NULL;
  uint8_t* messages_sim_c         = NULL;
  uint8_t* messages_sim_c_flood   = NULL;
  uint8_t* messages_sim_avx       = NULL;
  uint8_t* messages_sim_avx_flood = NULL;
  uint8_t* codewords              = NULL;
  uint8_t* rm_codewords           = NULL;
  float*   rm_symbols             = NULL;
  int16_t* rm_symbols_s           = NULL;
  int8_t*  rm_symbols_c           = NULL;
  float*   symbols                = NULL; // unrm_symbols
  int16_t* symbols_s              = NULL; // unrm_symbols
  int8_t*  symbols_c              = NULL; // unrm_symbols

  int i = 0;
  int j = 0;

  parse_args(argc, argv);

  // create an LDPC encoder
  srslte_ldpc_encoder_t encoder;
#ifdef LV_HAVE_AVX2
  if (srslte_ldpc_encoder_init(&encoder, SRSLTE_LDPC_ENCODER_AVX2, base_graph, lift_size) != 0) {
    perror("encoder init");
    exit(-1);
  }
#else  // no AVX2
  if (srslte_ldpc_encoder_init(&encoder, SRSLTE_LDPC_ENCODER_C, base_graph, lift_size) != 0) {
    perror("encoder init");
    exit(-1);
  }
#endif // LV_HAVE_AVX2

  // create a LDPC rate DeMatcher
  finalK = encoder.liftK;
  finalN = encoder.liftN - 2 * lift_size;
  if (rm_length == 0) {
    rm_length = finalN - F;
  }
  if (Nref == 0) {
    Nref = finalN;
  }

  // create a LDPC rate Matcher
  srslte_ldpc_rm_t rm_tx;
  if (srslte_ldpc_rm_tx_init(&rm_tx) != 0) {
    perror("rate matcher init");
    exit(-1);
  }

  // create a LDPC rate DeMatcher
  srslte_ldpc_rm_t rm_rx;
  if (srslte_ldpc_rm_rx_init_f(&rm_rx) != 0) {
    perror("rate dematcher init");
    exit(-1);
  }

  // create a LDPC rate DeMatcher (int16_t)
  srslte_ldpc_rm_t rm_rx_s;
  if (srslte_ldpc_rm_rx_init_s(&rm_rx_s) != 0) {
    perror("rate dematcher init (int16_t)");
    exit(-1);
  }

  // create a LDPC rate DeMatcher (int8_t)
  srslte_ldpc_rm_t rm_rx_c;
  if (srslte_ldpc_rm_rx_init_c(&rm_rx_c) != 0) {
    perror("rate dematcher init (int8_t)");
    exit(-1);
  }

  // create an LDPC decoder (float)
  srslte_ldpc_decoder_t decoder_f;
  if (srslte_ldpc_decoder_init(&decoder_f, SRSLTE_LDPC_DECODER_F, base_graph, lift_size, MS_SF) != 0) {
    perror("decoder init");
    exit(-1);
  }
  // create an LDPC decoder (16 bit)
  srslte_ldpc_decoder_t decoder_s;
  if (srslte_ldpc_decoder_init(&decoder_s, SRSLTE_LDPC_DECODER_S, base_graph, lift_size, MS_SF) != 0) {
    perror("decoder init (int16_t)");
    exit(-1);
  }
  // create an LDPC decoder (8 bit)
  srslte_ldpc_decoder_t decoder_c;
  if (srslte_ldpc_decoder_init(&decoder_c, SRSLTE_LDPC_DECODER_C, base_graph, lift_size, MS_SF) != 0) {
    perror("decoder init (int8_t)");
    exit(-1);
  }
  // create an LDPC decoder (8 bit, flooded)
  srslte_ldpc_decoder_t decoder_c_flood;
  if (srslte_ldpc_decoder_init(&decoder_c_flood, SRSLTE_LDPC_DECODER_C_FLOOD, base_graph, lift_size, MS_SF) != 0) {
    perror("decoder init");
    exit(-1);
  }
#ifdef LV_HAVE_AVX2
  // create an LDPC decoder (8 bit, AVX2 version)
  srslte_ldpc_decoder_t decoder_avx;
  if (srslte_ldpc_decoder_init(&decoder_avx, SRSLTE_LDPC_DECODER_C_AVX2, base_graph, lift_size, MS_SF) != 0) {
    perror("decoder init");
    exit(-1);
  }

  // create an LDPC decoder (8 bit, flooded scheduling, AVX2 version)
  srslte_ldpc_decoder_t decoder_avx_flood;
  if (srslte_ldpc_decoder_init(&decoder_avx_flood, SRSLTE_LDPC_DECODER_C_AVX2_FLOOD, base_graph, lift_size, MS_SF) !=
      0) {
    perror("decoder init");
    exit(-1);
  }
#endif // LV_HAVE_AVX2

  // create a random generator
  srslte_random_t random_gen = srslte_random_init(0);

  printf("Test LDPC chain:\n");
  printf("  Base Graph      -> BG%d\n", encoder.bg + 1);
  printf("  Lifting Size    -> %d\n", encoder.ls);
  printf("  Protograph      -> M = %d, N = %d, K = %d\n", encoder.bgM, encoder.bgN, encoder.bgK);
  printf("  Lifted graph    -> M = %d, N = %d, K = %d\n", encoder.liftM, encoder.liftN, encoder.liftK);
  printf("  Base code rate -> K/(N-2) = %d/%d = 1/%d\n",
         encoder.liftK,
         encoder.liftN - 2 * lift_size,
         encoder.bg == BG1 ? 3 : 5);
  printf("\n");
  printf("  Codeblock length             ->   K = %d\n", finalK);
  printf("  Codeword length              ->   N = %d\n", finalN);
  printf("  Rate matched codeword length ->   E = %d\n", rm_length);
  printf("  Number of filler bits        ->   F = %d\n", F);
  printf("  Redundancy version           ->  rv = %d\n", rv);
  printf("  Final code rate  -> (K-F)/E = (%d - %d)/%d = %.3f\n",
         encoder.liftK,
         F,
         rm_length,
         1.0 * (encoder.liftK - F) / rm_length);
  printf("\n  Signal-to-Noise Ratio -> %.2f dB\n", snr);

  messages_true          = malloc(finalK * batch_size * sizeof(uint8_t));
  messages_sim_f         = malloc(finalK * batch_size * sizeof(uint8_t));
  messages_sim_s         = malloc(finalK * batch_size * sizeof(uint8_t));
  messages_sim_c         = malloc(finalK * batch_size * sizeof(uint8_t));
  messages_sim_c_flood   = malloc(finalK * batch_size * sizeof(uint8_t));
  messages_sim_avx       = malloc(finalK * batch_size * sizeof(uint8_t));
  messages_sim_avx_flood = malloc(finalK * batch_size * sizeof(uint8_t));
  codewords              = malloc(finalN * batch_size * sizeof(uint8_t));
  rm_codewords           = malloc(rm_length * batch_size * sizeof(uint8_t));
  rm_symbols             = malloc(rm_length * batch_size * sizeof(float));
  rm_symbols_s           = malloc(rm_length * batch_size * sizeof(uint16_t));
  rm_symbols_c           = malloc(rm_length * batch_size * sizeof(uint8_t));

  symbols   = malloc(finalN * batch_size * sizeof(float));
  symbols_s = malloc(finalN * batch_size * sizeof(int16_t));
  symbols_c = malloc(finalN * batch_size * sizeof(int8_t));
  if (!messages_true || !messages_sim_f || !messages_sim_s || !messages_sim_c || //
      !messages_sim_avx || !messages_sim_c_flood || !messages_sim_avx_flood ||   //
      !codewords || !rm_codewords || !rm_symbols || !rm_symbols_s || !rm_symbols_c || !symbols || !symbols_s ||
      !symbols_c) {
    perror("malloc");
    exit(-1);
  }

  int            i_bit   = 0;
  int            i_batch = 0;
  struct timeval t[3];
  double         elapsed_time_enc         = 0;
  double         elapsed_time_dec_f       = 0;
  double         elapsed_time_dec_s       = 0;
  double         elapsed_time_dec_c       = 0;
  double         elapsed_time_dec_c_flood = 0;
  int            n_error_words_f          = 0;
  int            n_error_words_s          = 0;
  int            n_error_words_c          = 0;
  int            n_error_words_c_flood    = 0;

#ifdef LV_HAVE_AVX
  double elapsed_time_dec_avx       = 0;
  double elapsed_time_dec_avx_flood = 0;
  int    n_error_words_avx          = 0;
  int    n_error_words_avx_flood    = 0;
#endif // LV_HAVE_AVX2

  float noise_std_dev = srslte_convert_dB_to_amplitude(-snr);

  int16_t inf15  = (1U << 14U) - 1;
  float   gain_s = inf15 * noise_std_dev / 20 / (1 / noise_std_dev + 2);

  int8_t inf7   = (1U << 6U) - 1;
  float  gain_c = inf7 * noise_std_dev / 8 / (1 / noise_std_dev + 2);

  printf("\nBatch:\n  ");

  while (((n_error_words_f < req_errors) || (n_error_words_s < req_errors) || (n_error_words_c < req_errors)) &&
         (i_batch < max_n_batch)) {
    i_batch++;

    if (!(i_batch % 10)) {
      printf("%8d", i_batch);
      if (!(i_batch % 90)) {
        printf("\n  ");
      }
    }

    /* generate data_tx */
    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < finalK - F; j++) {
        messages_true[i * finalK + j] = srslte_random_uniform_int_dist(random_gen, 0, 1);
      }
      for (; j < finalK; j++) {
        messages_true[i * finalK + j] = FILLER_BIT;
      }
    }

    gettimeofday(&t[1], NULL);
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_encoder_encode(&encoder, messages_true + j * finalK, codewords + j * finalN, finalK);
    }
    gettimeofday(&t[2], NULL);
    get_time_interval(t);
    elapsed_time_enc += t[0].tv_sec + 1e-6 * t[0].tv_usec;

    // rate matching
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_rm_tx(&rm_tx,
                        codewords + j * finalN,
                        rm_codewords + j * rm_length,
                        rm_length,
                        base_graph,
                        lift_size,
                        rv,
                        mod_type,
                        Nref);
    }

    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < rm_length; j++) {
        rm_symbols[i * rm_length + j] = 1 - 2 * rm_codewords[i * rm_length + j];
      }
    }

    // Apply AWGN
    srslte_ch_awgn_f(rm_symbols, rm_symbols, noise_std_dev, batch_size * rm_length);

    // Convert symbols into LLRs
    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < rm_length; j++) {
        rm_symbols[i * rm_length + j] = rm_symbols[i * rm_length + j] * 2 / (noise_std_dev * noise_std_dev);
      }
    }

    for (i = 0; i < batch_size; i++) {
      if (srslte_ldpc_rm_rx_f(&rm_rx,
                              rm_symbols + i * rm_length,
                              symbols + i * finalN,
                              rm_length,
                              F,
                              base_graph,
                              lift_size,
                              rv,
                              mod_type,
                              Nref)) {
        exit(-1);
      }
    }

    //////// Floating point
    // Recover messages
    gettimeofday(&t[1], NULL);
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_decoder_decode_f(&decoder_f, symbols + j * finalN, messages_sim_f + j * finalK, finalN);
    }
    gettimeofday(&t[2], NULL);
    get_time_interval(t);
    elapsed_time_dec_f += t[0].tv_sec + 1e-6 * t[0].tv_usec;

    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < finalK; j++) {
        i_bit = i * finalK + j;
        if (messages_sim_f[i_bit] != (1U & messages_true[i_bit])) {
          n_error_words_f++;
          break;
        }
      }
    }

    //////// Fixed point - 16 bit

    // Quantize LLRs with 16 bits
    srslte_vec_quant_fs(rm_symbols, rm_symbols_s, gain_s, 0, inf15, batch_size * rm_length);

    // Rate dematcher
    for (i = 0; i < batch_size; i++) {
      if (srslte_ldpc_rm_rx_s(&rm_rx_s,
                              rm_symbols_s + i * rm_length,
                              symbols_s + i * finalN,
                              rm_length,
                              F,
                              base_graph,
                              lift_size,
                              rv,
                              mod_type,
                              Nref)) {
        exit(-1);
      }
    }

    // Recover messages
    gettimeofday(&t[1], NULL);
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_decoder_decode_s(&decoder_s, symbols_s + j * finalN, messages_sim_s + j * finalK, finalN);
    }
    gettimeofday(&t[2], NULL);
    get_time_interval(t);
    elapsed_time_dec_s += t[0].tv_sec + 1e-6 * t[0].tv_usec;

    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < finalK; j++) {
        i_bit = i * finalK + j;
        if (messages_sim_s[i_bit] != (1U & messages_true[i_bit])) {
          n_error_words_s++;
          break;
        }
      }
    }

    //////// Fixed point - 8 bit
    // Quantize LLRs with 8 bits
    srslte_vec_quant_fc(rm_symbols, rm_symbols_c, gain_c, 0, inf7, batch_size * rm_length);

    // Rate dematcher
    for (i = 0; i < batch_size; i++) {
      if (srslte_ldpc_rm_rx_c(&rm_rx_c,
                              rm_symbols_c + i * rm_length,
                              symbols_c + i * finalN,
                              rm_length,
                              F,
                              base_graph,
                              lift_size,
                              rv,
                              mod_type,
                              Nref)) {
        exit(-1);
      }
    }

    // Recover messages
    gettimeofday(&t[1], NULL);
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_decoder_decode_c(&decoder_c, symbols_c + j * finalN, messages_sim_c + j * finalK, finalN);
    }
    gettimeofday(&t[2], NULL);
    get_time_interval(t);
    elapsed_time_dec_c += t[0].tv_sec + 1e-6 * t[0].tv_usec;

    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < finalK; j++) {
        i_bit = i * finalK + j;
        if (messages_sim_c[i_bit] != (1U & messages_true[i_bit])) {
          n_error_words_c++;
          break;
        }
      }
    }

    //////// Fixed point - 8 bit, flooded scheduling

    // Recover messages
    gettimeofday(&t[1], NULL);
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_decoder_decode_c(&decoder_c_flood, symbols_c + j * finalN, messages_sim_c_flood + j * finalK, finalN);
    }
    gettimeofday(&t[2], NULL);
    get_time_interval(t);
    elapsed_time_dec_c_flood += t[0].tv_sec + 1e-6 * t[0].tv_usec;

    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < finalK; j++) {
        i_bit = i * finalK + j;
        if (messages_sim_c_flood[i_bit] != (1U & messages_true[i_bit])) {
          n_error_words_c_flood++;
          break;
        }
      }
    }

#ifdef LV_HAVE_AVX2
    //////// Fixed point - 8 bit - AVX2 version

    // Recover messages
    gettimeofday(&t[1], NULL);
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_decoder_decode_c(&decoder_avx, symbols_c + j * finalN, messages_sim_avx + j * finalK, finalN);
    }
    gettimeofday(&t[2], NULL);
    get_time_interval(t);
    elapsed_time_dec_avx += t[0].tv_sec + 1e-6 * t[0].tv_usec;

    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < finalK; j++) {
        i_bit = i * finalK + j;
        if (messages_sim_avx[i_bit] != (1U & messages_true[i_bit])) {
          n_error_words_avx++;
          break;
        }
      }
    }

    //////// Fixed point - 8 bit, flooded scheduling - AVX2 version

    // Recover messages
    gettimeofday(&t[1], NULL);
    for (j = 0; j < batch_size; j++) {
      srslte_ldpc_decoder_decode_c(
          &decoder_avx_flood, symbols_c + j * finalN, messages_sim_avx_flood + j * finalK, finalN);
    }
    gettimeofday(&t[2], NULL);
    get_time_interval(t);
    elapsed_time_dec_avx_flood += t[0].tv_sec + 1e-6 * t[0].tv_usec;

    for (i = 0; i < batch_size; i++) {
      for (j = 0; j < finalK; j++) {
        i_bit = i * finalK + j;
        if (messages_sim_avx_flood[i_bit] != (1U & messages_true[i_bit])) {
          n_error_words_avx_flood++;
          break;
        }
      }
    }
#endif // LV_HAVE_AVX2
  }

  printf("\nEstimated throughput encoder:\n  %e word/s\n  %e bit/s (information)\n  %e bit/s (encoded)\n",
         i_batch * batch_size / elapsed_time_enc,
         i_batch * batch_size * finalK / elapsed_time_enc,
         i_batch * batch_size * finalN / elapsed_time_enc);

  print_decoder("FLOATING POINT", i_batch, n_error_words_f, elapsed_time_dec_f);
  print_decoder("FIXED POINT (16 bits)", i_batch, n_error_words_s, elapsed_time_dec_s);
  print_decoder("FIXED POINT (8 bits)", i_batch, n_error_words_c, elapsed_time_dec_c);
  print_decoder("FIXED POINT (8 bits, flooded scheduling)", i_batch, n_error_words_c_flood, elapsed_time_dec_c_flood);

#ifdef LV_HAVE_AVX2
  print_decoder("FIXED POINT (8 bits - AVX2)", i_batch, n_error_words_avx, elapsed_time_dec_avx);
  print_decoder(
      "FIXED POINT (8 bits, flooded scheduling - AVX2)", i_batch, n_error_words_avx_flood, elapsed_time_dec_avx_flood);
#endif // LV_HAVE_AVX2

  if (n_error_words_s > 10 * n_error_words_f) {
    perror("16-bit performance too low!");
    exit(-1);
  }
  if (n_error_words_c > 10 * n_error_words_f) {
    perror("8-bit performance too low!");
    exit(-1);
  }
  printf("\nTest completed successfully!\n\n");

  free(symbols);
  free(symbols_s);
  free(symbols_c);
  free(rm_symbols);
  free(rm_symbols_s);
  free(rm_symbols_c);
  free(rm_codewords);
  free(codewords);
  free(messages_sim_avx);
  free(messages_sim_avx_flood);
  free(messages_sim_c_flood);
  free(messages_sim_c);
  free(messages_sim_s);
  free(messages_sim_f);
  free(messages_true);
  srslte_random_free(random_gen);
#ifdef LV_HAVE_AVX2
  srslte_ldpc_decoder_free(&decoder_avx);
  srslte_ldpc_decoder_free(&decoder_avx_flood);
#endif // LV_HAVE_AVX2
  srslte_ldpc_decoder_free(&decoder_c_flood);
  srslte_ldpc_decoder_free(&decoder_c);
  srslte_ldpc_decoder_free(&decoder_s);
  srslte_ldpc_decoder_free(&decoder_f);
  srslte_ldpc_encoder_free(&encoder);
  srslte_ldpc_rm_tx_free(&rm_tx);
  srslte_ldpc_rm_rx_free_f(&rm_rx);
  srslte_ldpc_rm_rx_free_s(&rm_rx_s);
  srslte_ldpc_rm_rx_free_c(&rm_rx_c);
}

void print_decoder(char* title, int n_batches, int n_errors, double elapsed_time)
{
  printf("\n**** %s ****", title);
  printf("\nEstimated word error rate:\n  %e (%d errors)\n", (double)n_errors / n_batches / batch_size, n_errors);

  printf("Estimated throughput decoder:\n  %e word/s\n  %e bit/s (information)\n  %e bit/s (encoded)\n",
         n_batches * batch_size / elapsed_time,
         n_batches * batch_size * finalK / elapsed_time,
         n_batches * batch_size * finalN / elapsed_time);
}