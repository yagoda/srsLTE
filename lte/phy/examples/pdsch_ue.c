/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The libLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the libLTE library.
 *
 * libLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include "liblte/phy/phy.h"

#ifndef DISABLE_UHD
#include "liblte/cuhd/cuhd.h"
void *uhd;
#endif

#ifndef DISABLE_GRAPHICS
#include "liblte/graphics/plot.h"
plot_real_t poutfft;
plot_complex_t pce;
plot_scatter_t pscatrecv, pscatequal;
#endif

#define MHZ         1000000
#define SAMP_FREQ   1920000

#define NOF_PORTS 2

float find_threshold = 10.0;
int nof_frames = -1;
int pdsch_errors = 0, pdsch_total = 0;
int frame_cnt;
char *input_file_name = NULL;
int disable_plots = 0;

/* These are the number of PRBs used during the SYNC procedure */
int sampling_nof_prb = 6;

/* Number of samples in a subframe */
int sf_n_samples;

int cell_id_initated = 0, mib_initiated = 0;

int go_exit = 0;

float uhd_freq = 2600000000.0, uhd_gain = 20.0;
char *uhd_args = "";

filesource_t fsrc;
cf_t *input_buffer, *sf_buffer, *fft_buffer, *input_decim_buffer, *ce[MAX_PORTS];
float *tmp_plot;
pbch_t pbch;
pbch_mib_t mib;
pcfich_t pcfich;
pdcch_t pdcch;
dci_t dci_set;
pdsch_t pdsch;
regs_t regs;
lte_fft_t fft;
chest_t chest;
sync_frame_t sframe;

#define CLRSTDOUT printf("\r\n"); fflush(stdout); printf("\r\n")

#define DOWNSAMPLE_FACTOR(x, y) lte_symbol_sz(x) / lte_symbol_sz(y)

void usage(char *prog) {
  printf("Usage: %s [iagfndvtp]\n", prog);
  printf("\t-i input_file [Default use USRP]\n");
#ifndef DISABLE_UHD
  printf("\t-a UHD args [Default %s]\n", uhd_args);
  printf("\t-g UHD RX gain [Default %.2f dB]\n", uhd_gain);
  printf("\t-f UHD RX frequency [Default %.1f MHz]\n", uhd_freq / 1000000);
#else
  printf("\t   UHD is disabled. CUHD library not available\n");
#endif
  printf("\t-p sampling_nof_prb [Default %d]\n", sampling_nof_prb);
  printf("\t-n nof_frames [Default %d]\n", nof_frames);
  printf("\t-t PSS threshold [Default %f]\n", find_threshold);
#ifndef DISABLE_GRAPHICS
  printf("\t-d disable plots [Default enabled]\n");
#else
  printf("\t plots are disabled. Graphics library not available\n");
#endif
  printf("\t-v [set verbose to debug, default none]\n");
}

void parse_args(int argc, char **argv) {
  int opt;
  while ((opt = getopt(argc, argv, "iagfndvtp")) != -1) {
    switch (opt) {
    case 'i':
      input_file_name = argv[optind];
      break;
    case 'a':
      uhd_args = argv[optind];
      break;
    case 'g':
      uhd_gain = atof(argv[optind]);
      break;
    case 'f':
      uhd_freq = atof(argv[optind]);
      break;
    case 't':
      find_threshold = atof(argv[optind]);
      break;
    case 'p':
      sampling_nof_prb = atof(argv[optind]);
      break;
    case 'n':
      nof_frames = atoi(argv[optind]);
      break;
    case 'd':
      disable_plots = 1;
      break;
    case 'v':
      verbose++;
      break;
    default:
      usage(argv[0]);
      exit(-1);
    }
  }
}

#ifndef DISABLE_GRAPHICS

void init_plots() {
  plot_init();
  plot_real_init(&poutfft);
  plot_real_setTitle(&poutfft, "Output FFT - Magnitude");
  plot_real_setLabels(&poutfft, "Index", "dB");
  plot_real_setYAxisScale(&poutfft, -60, 0);
  plot_real_setXAxisScale(&poutfft, 1, 504);

  plot_complex_init(&pce);
  plot_complex_setTitle(&pce, "Channel Estimates");
  plot_complex_setYAxisScale(&pce, Ip, -0.01, 0.01);
  plot_complex_setYAxisScale(&pce, Q, -0.01, 0.01);
  plot_complex_setYAxisScale(&pce, Magnitude, 0, 0.01);
  plot_complex_setYAxisScale(&pce, Phase, -M_PI, M_PI);

  plot_scatter_init(&pscatrecv);
  plot_scatter_setTitle(&pscatrecv, "Received Symbols");
  plot_scatter_setXAxisScale(&pscatrecv, -0.01, 0.01);
  plot_scatter_setYAxisScale(&pscatrecv, -0.01, 0.01);

  plot_scatter_init(&pscatequal);
  plot_scatter_setTitle(&pscatequal, "Equalized Symbols");
  plot_scatter_setXAxisScale(&pscatequal, -1, 1);
  plot_scatter_setYAxisScale(&pscatequal, -1, 1);
}

#endif


/* This function initializes the objects defined as global variables */
int base_init(int nof_prb) {
  int i;
  
  int sf_n_re = 2 * CPNORM_NSYMB * nof_prb * RE_X_RB;
  int sf_n_samples = 2 * SLOT_LEN_CPNORM(lte_symbol_sz(nof_prb));


#ifndef DISABLE_GRAPHICS
  if (!disable_plots) {
    tmp_plot = malloc(sizeof(cf_t) * sf_n_re);
    if (!tmp_plot) {
      perror("malloc");
      return -1;
    }
    init_plots();
  }
#else
  printf("-- PLOTS are disabled. Graphics library not available --\n\n");
#endif

  if (input_file_name) {
    if (filesource_init(&fsrc, input_file_name, COMPLEX_FLOAT_BIN)) {
      return -1;
    }
  } else {
    /* open UHD device */
#ifndef DISABLE_UHD
    printf("Opening UHD device...\n");
    if (cuhd_open(uhd_args, &uhd)) {
      fprintf(stderr, "Error opening uhd\n");
      return -1;
    }
#else
    printf("Error UHD not available. Select an input file\n");
    return -1;
#endif
  }

  /* For the input buffer, we allocate space for 1 ms of samples */
  input_buffer = (cf_t*) malloc(sf_n_samples * sizeof(cf_t));
  if (!input_buffer) {
    perror("malloc");
    return -1;
  }
  input_decim_buffer = (cf_t*) malloc(sf_n_samples * sizeof(cf_t));
  if (!input_decim_buffer) {
    perror("malloc");
    return -1;
  }
  

  /* This buffer is the aligned version of input_buffer */
  sf_buffer = (cf_t*) malloc(sf_n_samples * sizeof(cf_t));
  if (!sf_buffer) {
    perror("malloc");
    return -1;
  }

  /* For the rest of the buffers, we allocate for the number of RE in one subframe */
  fft_buffer = (cf_t*) malloc(sf_n_re * sizeof(cf_t));
  if (!fft_buffer) {
    perror("malloc");
    return -1;
  }

  for (i = 0; i < MAX_PORTS; i++) {
    ce[i] = (cf_t*) malloc(sf_n_re * sizeof(cf_t));
    if (!ce[i]) {
      perror("malloc");
      return -1;
    }
  }
  
  bzero(&mib, sizeof(pbch_mib_t));
  
  if (sync_frame_init(&sframe, DOWNSAMPLE_FACTOR(nof_prb,6))) {
    fprintf(stderr, "Error initiating PSS/SSS\n");
    return -1;
  }

  if (chest_init(&chest, LINEAR, CPNORM, nof_prb, NOF_PORTS)) {
    fprintf(stderr, "Error initializing equalizer\n");
    return -1;
  }

  if (lte_fft_init(&fft, CPNORM, nof_prb)) {
    fprintf(stderr, "Error initializing FFT\n");
    return -1;
  }
  
  dci_init(&dci_set, 10);

  return 0;
}

void base_free() {
  int i;

  if (input_file_name) {
    filesource_free(&fsrc);
  } else {
#ifndef DISABLE_UHD
    cuhd_close(uhd);
#endif
  }

#ifndef DISABLE_GRAPHICS
  if (!disable_plots) {
    if (tmp_plot) {
      free(tmp_plot);      
    }
    plot_exit();
  }
#endif

  pbch_free(&pbch);
  pdsch_free(&pdsch);
  pdcch_free(&pdcch);
  regs_free(&regs);
  sync_frame_free(&sframe);
  lte_fft_free(&fft);
  chest_free(&chest);

  free(input_buffer);
  free(input_decim_buffer);
  free(fft_buffer);
  for (i = 0; i < MAX_PORTS; i++) {
    free(ce[i]);
  }
}

int mib_init(int cell_id) {

  if (mib.nof_prb > sampling_nof_prb) {
    fprintf(stderr, "Error sampling frequency is %.2f Mhz but captured signal has %d PRB\n", 
      (float) lte_sampling_freq_hz(sampling_nof_prb)/MHZ, mib.nof_prb);
    return -1;
  }
  if (regs_init(&regs, cell_id, mib.nof_prb, mib.nof_ports, 
    mib.phich_resources, mib.phich_length, CPNORM)) {
    fprintf(stderr, "Error initiating regs\n");
    return -1;
  }

  if (pcfich_init(&pcfich, &regs, cell_id, mib.nof_prb, mib.nof_ports, CPNORM)) {
    fprintf(stderr, "Error creating PCFICH object\n");
    return -1;
  }

  if (pdcch_init(&pdcch, &regs, mib.nof_prb, mib.nof_ports, cell_id, CPNORM)) {
    fprintf(stderr, "Error creating PDCCH object\n");
    return -1;
  }

  if (pdsch_init(&pdsch, 1234, mib.nof_prb, mib.nof_ports, cell_id, CPNORM)) {
    fprintf(stderr, "Error creating PDSCH object\n");
    return -1;
  }
  
  chest_set_nof_ports(&chest, mib.nof_ports);
  
  mib_initiated = 1;
  
  DEBUG("Receiver initiated cell_id=%d nof_prb=%d\n", cell_id, mib.nof_prb);
  
  return 0;
}

int cell_id_init(int nof_prb, int cell_id) {

  if (chest_ref_LTEDL(&chest, cell_id)) {
    fprintf(stderr, "Error initializing reference signal\n");
    return -1;
  }

  if (pbch_init(&pbch, nof_prb, cell_id, CPNORM)) {
    fprintf(stderr, "Error initiating PBCH\n");
    return -1;
  }
  
  cell_id_initated = 1;
  DEBUG("PBCH initiated cell_id=%d\n", cell_id);
  
  return 0;
}

char data[10000];

int rx_run(cf_t *input, int sf_idx) {
  int cfi, i, cfi_distance, nof_dcis;
  cf_t *input_decim;
  ra_pdsch_t ra_dl;
  ra_prb_t prb_alloc;
  
  /* Downsample if the signal bandwith is shorter */
  if (sampling_nof_prb > mib.nof_prb) {
    decim_c(input, input_decim_buffer, sf_n_samples, DOWNSAMPLE_FACTOR(sampling_nof_prb, mib.nof_prb));
    input_decim = input_decim_buffer;
  } else {
    input_decim = input;
  }
  
  lte_fft_run_sf(&fft, input_decim, fft_buffer);

  /* Get channel estimates for each port */
  chest_ce_sf(&chest, fft_buffer, ce, sf_idx);
  
  /* First decode PCFICH and obtain CFI */
  if (pcfich_decode(&pcfich, fft_buffer, ce, sf_idx, &cfi, &cfi_distance)<0) {
    return -1;
  }
  
  INFO("Decoded CFI=%d with distance %d\n", cfi, cfi_distance);

  if (regs_set_cfi(&regs, cfi)) {
    fprintf(stderr, "Error setting CFI\n");
    return -1;
  }
  if (pdcch_set_cfi(&pdcch, cfi)) {
    fprintf(stderr, "Error setting CFI\n");
    return -1;
  }
  pdcch_init_search_ue(&pdcch, 1234);

  dci_set.nof_dcis = 0;
  nof_dcis = pdcch_decode(&pdcch, fft_buffer, ce, &dci_set, sf_idx);
  INFO("Received %d DCIs\n", nof_dcis);
  for (i=0;i<nof_dcis;i++) {
    dci_msg_type_t type;
    if (dci_msg_get_type(&dci_set.msg[i], &type, mib.nof_prb, 1234)) {
      fprintf(stderr, "Can't get DCI message type\n");      
    } else {      
      INFO("MSG %d: L: %d nCCE: %d Nbits: %d. ",i,dci_set.msg[i].location.L,
             dci_set.msg[i].location.ncce, dci_set.msg[i].location.nof_bits);
      if (VERBOSE_ISINFO()) {
        dci_msg_type_fprint(stdout, type);        
      }
      switch(type.type) {
      case PDSCH_SCHED:
        bzero(&ra_dl, sizeof(ra_pdsch_t));
        if (dci_msg_unpack_pdsch(&dci_set.msg[i], &ra_dl, mib.nof_prb, 
            false)) {
          fprintf(stderr, "Can't unpack PDSCH message\n");
          break;
        }
        if (VERBOSE_ISINFO() || !pdsch_total) {
          printf("\n");
          ra_pdsch_fprint(stdout, &ra_dl, mib.nof_prb);        
          printf("\n");
        }
        if (ra_prb_get_dl(&prb_alloc, &ra_dl, mib.nof_prb)) {
          fprintf(stderr, "Error computing resource allocation\n");
          break;
        }
        ra_prb_get_re(&prb_alloc, mib.nof_prb, mib.nof_ports, 
                      mib.nof_prb<10?(cfi+1):cfi, CPNORM);

        if (pdsch_decode(&pdsch, fft_buffer, ce, data, sf_idx, ra_dl.mcs, &prb_alloc)) {
          pdsch_errors++;
        }
        pdsch_total++;
        break;
      default:
        fprintf(stderr, "Unsupported message type\n");
        break;
      }
    }
  }

  #ifndef DISABLE_GRAPHICS
  if (!disable_plots && nof_dcis > 0) {
    int n_re = 2 * RE_X_RB * CPNORM_NSYMB * mib.nof_prb;
    for (i = 0; i < n_re; i++) {
      tmp_plot[i] = 10 * log10f(cabsf(fft_buffer[i]));
      if (isinf(tmp_plot[i])) {
        tmp_plot[i] = -80;
      }
    }
    plot_real_setNewData(&poutfft, tmp_plot, n_re);
    plot_complex_setNewData(&pce, ce[0], n_re);
    plot_scatter_setNewData(&pscatrecv, pdsch.pdsch_symbols[0], prb_alloc.re_sf[sf_idx]);
    plot_scatter_setNewData(&pscatequal, pdsch.pdsch_d, prb_alloc.re_sf[sf_idx]);    
  }
#endif

  return 0;
}

int mib_decoder_run(cf_t *input) {
  int i, n;

  lte_fft_run_slot(&fft, input, fft_buffer);
  
  /* Get channel estimates for each port */
  for (i = 0; i < NOF_PORTS; i++) {
    chest_ce_slot_port(&chest, fft_buffer, ce[i], 1, i);
  }

  DEBUG("Decoding PBCH\n", 0);
  n = pbch_decode(&pbch, fft_buffer, ce, &mib);

  return n;
}

int run_receiver(cf_t *input, int cell_id, int sf_idx) {

  if (!cell_id_initated) {
    cell_id_init(sampling_nof_prb, cell_id);
  }
  if (!mib.nof_prb) {
    
    if (!sf_idx) {
      if (mib_decoder_run(&input[sf_n_samples/2])) {
        INFO("MIB decoded!\n", 0);
        if (!mib_initiated) {
          if (mib_init(cell_id)) {
            return -1;
          }
        }
        if (VERBOSE_ISINFO() || !frame_cnt) {
          CLRSTDOUT;
          printf(" - Phy. CellId:\t    %d\n", cell_id);
          pbch_mib_fprint(stdout, &mib);                  
        }
      }
    }    
  } 
  if (mib.nof_prb) {
    if (rx_run(input, sf_idx)) {
      return -1;
    }
  }
  return 0;
}

void sigintHandler(int sig_num) {
  go_exit = 1;
}

void setup_uhd() {
  double samp_freq; 

#ifndef DISABLE_UHD
  /* Get the sampling rate from the number of PRB */
  samp_freq = lte_sampling_freq_hz(sampling_nof_prb);

  INFO("Setting sampling frequency %.2f MHz\n", (float) samp_freq/MHZ);
  cuhd_set_rx_srate(uhd, samp_freq);
  cuhd_set_rx_gain(uhd, uhd_gain);
  
  /* set uhd_freq */
  cuhd_set_rx_freq(uhd, (double) uhd_freq);
  cuhd_rx_wait_lo_locked(uhd);
  DEBUG("Set uhd_freq to %.3f MHz\n", (double ) uhd_freq);

  DEBUG("Starting receiver...\n", 0);
  cuhd_start_rx_stream(uhd);
#endif
}

void read_io(cf_t *buffer, int nsamples) {
  int n; 
  DEBUG(" -----   RECEIVING %d SAMPLES ---- \n", nsamples);
  if (input_file_name) {
    n = filesource_read(&fsrc, buffer, nsamples);
    if (n == -1) {
      fprintf(stderr, "Error reading file\n");
      exit(-1);
      /* wrap file if arrive to end */
    } else if (n < nsamples) {
      DEBUG("Read %d from file. Seeking to 0\n",n);
      filesource_seek(&fsrc, 0);
      filesource_read(&fsrc, buffer, nsamples);
    }
  } else {
#ifndef DISABLE_UHD
    cuhd_recv(uhd, buffer, nsamples, 1);
#endif
  }
}

int main(int argc, char **argv) {

#ifdef DISABLE_UHD
  if (argc < 3) {
    usage(argv[0]);
    exit(-1);
  }
#endif

  parse_args(argc, argv);

  if (base_init(sampling_nof_prb)) {
    fprintf(stderr, "Error initializing memory\n");
    exit(-1);
  }

  /* If input_file_name is NULL, we read from the USRP */
  if (!input_file_name) {
    setup_uhd();
  }

  printf("\n --- Press Ctrl+C to exit --- \n");
  signal(SIGINT, sigintHandler);

  /* Initialize variables */
  mib.sfn = -1;
  frame_cnt = 0;
  
  /* The number of samples read from the USRP or file corresponds to 1 ms (subframe) */
  sf_n_samples = 1920 * lte_symbol_sz(sampling_nof_prb)/128;
  
  sync_frame_set_threshold(&sframe, find_threshold);

  while (!go_exit && (frame_cnt < nof_frames || nof_frames == -1)) {

    read_io(input_buffer, sf_n_samples);

    switch(sync_frame_push(&sframe, input_buffer, sf_buffer)) {
      case 0:
        /* not yet synched */
        break;
      case 1:
        if (!(frame_cnt%10)) {
          mib.sfn++;
        }
        /* synch'd and tracking */
        if (run_receiver(sf_buffer, sync_frame_cell_id(&sframe), sync_frame_sfidx(&sframe))) {
          exit(-1);
        }
        if (!(frame_cnt % 10)) {
          printf(
              "SFN: %4d, CFO: %+.4f KHz, SFO: %+.4f Khz, TimeOffset: %4d, Errors: %4d/%4d, BLER: %.1e\r",
              mib.sfn, sframe.cur_cfo * 15, sframe.timeoffset / 5, sframe.peak_idx,
              pdsch_errors, pdsch_total,
              (float) pdsch_errors / pdsch_total);
          fflush(stdout);          
        }

        break;
      default:
        fprintf(stderr, "Error running automatic synchronization\n");
        exit(-1);
    }

    frame_cnt++;
    if (input_file_name) {
      usleep(5000);
    }
  }

  base_free();

  printf("\nBye\n");
  exit(0);
}
