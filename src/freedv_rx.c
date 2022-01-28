/*---------------------------------------------------------------------------*\

  FILE........: freedv_rx.c
  AUTHOR......: David Rowe
  DATE CREATED: August 2014

  Demo/development receive program for FreeDV API functions:

  Example usage (all one line):

    $ cd codec2/build_linux/src
    $ ./freedv_tx 1600 ../../raw/ve9qrp_10s.raw - | ./freedv_rx 1600 - - | aplay -f S16

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2014 David Rowe

  All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License version 2.1, as
  published by the Free Software Foundation.  This program is
  distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "reliable_text.h"
#include "freedv_api.h"
#include "modem_stats.h"

#define NDISCARD 5                /* BER measure optionally discards first few frames after sync */

/* optioal call-back function for received txt characters */
void my_put_next_rx_char(void *states, char c) { fprintf((FILE*)states, "%c", c); }

static FILE* reliable_tx_fp;
reliable_text_t reliable_text_obj;

void on_reliable_text_rx(reliable_text_t rt, const char* txt_ptr, int length, void* state)
{
    fprintf(reliable_tx_fp, "%s\n", txt_ptr);
    reliable_text_reset(reliable_text_obj);
}

int main(int argc, char *argv[]) {
    FILE                      *fin, *fout, *ftxt_rx = NULL;
    int                        nin, nout, nout_total = 0, frame = 0;
    struct MODEM_STATS         stats = {0};
    int                        mode;
    int                        sync;
    float                      snr_est;
    float                      clock_offset;
    int                        use_testframes, verbose, discard, use_complex, use_dpsk, use_reliabletext;
    int                        use_squelch, highpassthroughgain;
    float                      squelch = 0;
    int                        i;
    struct freedv             *freedv;
    
    if (argc < 4) {
        char f2020[80] = {0};
        #ifdef __LPCNET__
        sprintf(f2020,"|2020|2020A");
        #endif
	printf("usage: %s 1600|700C|700D|700E|2400A|2400B|800XA%s InputModemSpeechFile OutputSpeechRawFile\n"
               " [--testframes] [-v] [--discard] [--usecomplex] [--dpsk] [--squelch leveldB] [--txtrx filename] [--reliabletext]\n"
	       " [--highpassthroughgain]\n", argv[0], f2020);
	printf("e.g    %s 1600 hts1a_fdmdv.raw hts1a_out.raw\n", argv[0]);
	exit(1);
    }

    mode = -1;
    if (!strcmp(argv[1],"1600")) mode = FREEDV_MODE_1600;
    if (!strcmp(argv[1],"700C")) mode = FREEDV_MODE_700C;
    if (!strcmp(argv[1],"700D")) mode = FREEDV_MODE_700D;
    if (!strcmp(argv[1],"700E")) mode = FREEDV_MODE_700E;
    if (!strcmp(argv[1],"2400A")) mode = FREEDV_MODE_2400A;
    if (!strcmp(argv[1],"2400B")) mode = FREEDV_MODE_2400B;
    if (!strcmp(argv[1],"800XA")) mode = FREEDV_MODE_800XA;
    #ifdef __LPCNET__
    if (!strcmp(argv[1],"2020"))  mode = FREEDV_MODE_2020;
    if (!strcmp(argv[1],"2020A"))  mode = FREEDV_MODE_2020A;
    #endif
    if (mode == -1) {
        fprintf(stderr, "Error in mode: %s\n", argv[1]);
        exit(1);
    }

    if (strcmp(argv[2], "-")  == 0) fin = stdin;
    else if ( (fin = fopen(argv[2],"rb")) == NULL ) {
	fprintf(stderr, "Error opening input raw modem sample file: %s: %s.\n",
         argv[2], strerror(errno));
	exit(1);
    }

    if (strcmp(argv[3], "-") == 0) fout = stdout;
    else if ( (fout = fopen(argv[3],"wb")) == NULL ) {
	fprintf(stderr, "Error opening output speech sample file: %s: %s.\n",
         argv[3], strerror(errno));
	exit(1);
    }

    use_testframes = verbose = discard = use_complex = use_dpsk = use_squelch = 0; use_reliabletext = 0;
    highpassthroughgain = 0;
    
    if (argc > 4) {
        for (i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--testframes") == 0) use_testframes = 1;
            else if (strcmp(argv[i], "-v") == 0) verbose = 1;
            else if (strcmp(argv[i], "-vv") == 0) verbose = 2;
            else if (strcmp(argv[i], "--discard") == 0) discard = 1;
            else if (strcmp(argv[i], "--usecomplex") == 0) use_complex = 1;
            else if (strcmp(argv[i], "--highpassthroughgain") == 0) highpassthroughgain = 1;
            else if (strcmp(argv[i], "--squelch") == 0) {
                squelch = atof(argv[i + 1]);
                i++;
                use_squelch = 1;
            } else if (strcmp(argv[i], "--dpsk") == 0) use_dpsk = 1;
            else if (strcmp(argv[i], "--txtrx") == 0) {
                ftxt_rx = fopen(argv[i+1], "wt"); i++;
                assert(ftxt_rx != NULL);
            } else if (strcmp(argv[i], "--reliabletext") == 0) {
                use_reliabletext = 1;
                // received text is saved to file specified by --txtrx.
            } else {
                fprintf(stderr, "unkown option: %s\n", argv[i]);
                exit(1);
            }
        }
    }

    freedv = freedv_open(mode);
    assert(freedv != NULL);

    /* set up a few options, calling these is optional -------------------------*/

    freedv_set_test_frames(freedv, use_testframes);
    if (verbose == 2) freedv_set_verbose(freedv, verbose);
    if (highpassthroughgain) { freedv_passthrough_gain(freedv, 1.0); }
    
    if (use_squelch) {
        freedv_set_snr_squelch_thresh(freedv, squelch);
        freedv_set_squelch_en(freedv, 1);
    }
    freedv_set_dpsk(freedv, use_dpsk);

    /* install optional handler for recevied txt characters */
    if (ftxt_rx != NULL)
    {
        if (use_reliabletext)
        {
            reliable_tx_fp = ftxt_rx;
            
            reliable_text_obj = reliable_text_create();
            assert(reliable_text_obj != NULL);
            reliable_text_set_string(reliable_text_obj, "AB1CDEF", 7); // not used
            reliable_text_use_with_freedv(reliable_text_obj, freedv, on_reliable_text_rx, NULL);
        }
        else
        {
            freedv_set_callback_txt(freedv, my_put_next_rx_char, NULL, ftxt_rx);
        }
    }

    /* note use of API functions to tell us how big our buffers need to be -----*/

    short speech_out[freedv_get_n_max_speech_samples(freedv)];
    short demod_in[freedv_get_n_max_modem_samples(freedv)];

    /* We need to work out how many samples the demod needs on each
       call (nin).  This is used to adjust for differences in the tx
       and rx sample clock frequencies.  Note also the number of
       output speech samples "nout" is time varying. */

    nin = freedv_nin(freedv);
    while(fread(demod_in, sizeof(short), nin, fin) == nin) {
        frame++;

        if (use_complex) {
            /* exercise the complex version of the API (useful
               for testing 700D which has a different code path for
               short samples) */
            COMP demod_in_complex[nin];

            for(int i=0; i<nin; i++) {
                demod_in_complex[i].real = (float)demod_in[i];
                demod_in_complex[i].imag = 0.0f;
            }
            nout = freedv_comprx(freedv, speech_out, demod_in_complex);
        } else {
            // most common interface - real shorts in, real shorts out
            nout = freedv_rx(freedv, speech_out, demod_in);
        }

       /* IMPORTANT: don't forget to do this in the while loop to
           ensure we fread the correct number of samples: ie update
           "nin" before every call to freedv_rx()/freedv_comprx() */
        nin = freedv_nin(freedv);

        /* optionally read some stats */
        freedv_get_modem_stats(freedv, &sync, &snr_est);
        freedv_get_modem_extended_stats(freedv, &stats);
        int total_bit_errors = freedv_get_total_bit_errors(freedv);
        clock_offset = stats.clock_offset;

        if (discard && (sync == 0)) {
            // discard BER results if we get out of sync, helps us get sensible BER results
            freedv_set_total_bits(freedv, 0); freedv_set_total_bit_errors(freedv, 0);
            freedv_set_total_bits_coded(freedv, 0); freedv_set_total_bit_errors_coded(freedv, 0);
        }

        fwrite(speech_out, sizeof(short), nout, fout);
        nout_total += nout;

        if (verbose == 1) {
            fprintf(stderr, "frame: %d  demod sync: %d  nin: %d demod snr: %3.2f dB  bit errors: %d clock_offset: %f\n",
                    frame, sync, nin, snr_est, total_bit_errors, clock_offset);
        }

	/* if using pipes we probably don't want the usual buffering
           to occur */
        if (fout == stdout) fflush(stdout);
    }

    if (ftxt_rx != NULL) fclose(ftxt_rx);
    fclose(fin);
    fclose(fout);
    fprintf(stderr, "frames decoded: %d  output speech samples: %d\n", frame, nout_total);

    /* finish up with some stats */

    if (freedv_get_test_frames(freedv)) {
        int Tbits = freedv_get_total_bits(freedv);
        int Terrs = freedv_get_total_bit_errors(freedv);
        float uncoded_ber = (float)Terrs/Tbits;
        fprintf(stderr, "BER......: %5.4f  Tbits: %8d  Terrs: %8d\n",
		                    (double)uncoded_ber, Tbits, Terrs);
        if ((mode == FREEDV_MODE_700D) || (mode == FREEDV_MODE_700E) ||
            (mode == FREEDV_MODE_2020) || (mode == FREEDV_MODE_2020A)) {
            int Tbits_coded = freedv_get_total_bits_coded(freedv);
            int Terrs_coded = freedv_get_total_bit_errors_coded(freedv);
            float coded_ber = (float)Terrs_coded/Tbits_coded;
            fprintf(stderr, "Coded BER: %5.4f  Tbits: %8d  Terrs: %8d\n",
                    (double)coded_ber, Tbits_coded, Terrs_coded);
            int Tpackets = freedv_get_total_packets(freedv);
            int Tpacket_errors = freedv_get_total_packet_errors(freedv);
            float per = (float)Tpacket_errors/Tpackets;
            fprintf(stderr, "Coded PER: %5.4f  Tpkts: %8d  Tpers: %8d\n",
                    per, Tpackets, Tpacket_errors);

            /* set return code for Ctest */
            if ((uncoded_ber < 0.1f) && (coded_ber < 0.01f))
                return 0;
            else
                return 1;
        }
    }

    if (use_reliabletext)
    {
        reliable_text_destroy(reliable_text_obj);
    }
    
    freedv_close(freedv);
    
    return 0;
}
