/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 1999-2001 Carnegie Mellon University.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*
 * demo.c -- An example SphinxII program using continuous listening/silence filtering
 * 		to segment speech into utterances that are then decoded.
 * 
 * HISTORY
 *
 * 15-Jun-99    Kevin A. Lenzo (lenzo@cs.cmu.edu) at Carnegie Mellon University
 *              Added i386_linux and used ad_open_sps instead of ad_open
 * 
 * 14-Jun-96	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon University.
 * 		Created.
 */

/*
 * This is a simple, tty-based example of a SphinxII client that uses continuous listening
 * with silence filtering to automatically segment a continuous stream of audio input
 * into utterances that are then decoded.
 * 
 * Remarks:
 *   - Each utterance is ended when a silence segment of at least 1 sec is recognized.
 *   - Single-threaded implementation for portability.
 *   - Uses fbs8 audio library; can be replaced with an equivalent custom library.
 */

#include <stdio.h>
#include <string.h>

/* #include "fbs.h" */
/* #include "s2types.h" */

#include "s3_decode.h"
#include "utt.h"
#include "err.h"
#include "ad.h"
#include "cont_ad.h"

#if !defined(_WIN32_WCE)
#include <signal.h>
#include <setjmp.h>
#endif
#if defined(WIN32) && !defined(GNUWINCE)
#include <time.h>
#else
#include <sys/types.h>
#include <sys/time.h>
#endif

static ad_rec_t *ad;
static s3_decode_t decoder;
static fe_t *fe;

/* Sleep for specified msec */
static void
sleep_msec(int32 ms)
{
#if (defined(WIN32) && !defined(GNUWINCE)) || defined(_WIN32_WCE)
    Sleep(ms);
#else
    /* ------------------- Unix ------------------ */
    struct timeval tmo;

    tmo.tv_sec = 0;
    tmo.tv_usec = ms * 1000;

    select(0, NULL, NULL, NULL, &tmo);
#endif
}

#define STDIN_CMD_STRLN 256

float silence_length;

void reset_beam()
{
        s3_decode_set_lm(&decoder, "default");
            decoder.kb.beam = beam_init(
                                          cmd_ln_float64("-beam"),
                                          cmd_ln_float64("-pbeam"),
                                          cmd_ln_float64("-wbeam"),
                                          cmd_ln_float64("-wend_beam"),
                                          cmd_ln_int32("-ptranskip"), 
                                          mdef_n_ciphone(kbcore_mdef(decoder.kb.kbcore))
                                          );

            beam_report(decoder.kb.beam);
	if (((decoder).kb.srch = (srch_t *) srch_init(&(decoder.kb), (decoder).kb.op_mode)) == NULL) {
	    E_FATAL("Search initialization failed. Forced exit\n");
	}
        
}

void set_cmd(char *name, char *val)
{
    float float_arg;
    char *endptr;

    if (!strcmp(name, "LM"))
        s3_decode_set_lm(&decoder, val);
    else if (!strcmp(name, "SILENCE_LENGTH"))
        {
            if (sscanf(val, "%f", &float_arg) == 1)
                silence_length = float_arg;
            else {fprintf(stderr, "ERROR: SET SILENCE_LENGTH requires a float argument\n");}
        }  
    else if (!strcmp(name, "BEAM")) {
            cmd_ln_set_float64("-beam", strtod(val, &endptr)); 
        }               
    else if (!strcmp(name, "PBEAM")) {
            cmd_ln_set_float64("-pbeam", strtod(val, &endptr)); 
    }
    else if (!strcmp(name, "WBEAM")) {
            cmd_ln_set_float64("-wbeam", strtod(val, &endptr)); 
    }
    else if (!strcmp(name, "SUBVQBEAM")) {
            cmd_ln_set_float64("-subvqbeam", strtod(val, &endptr)); 
    }
    else if (!strcmp(name, "CI_BEAM")) {
            cmd_ln_set_float64("-ci_beam", strtod(val, &endptr)); 
    }
    else if (!strcmp(name, "WEND_BEAM")) {
            cmd_ln_set_float64("-wend_beam", strtod(val, &endptr)); 
    }
    else if (!strcmp(name, "MAXCDSENPF")) {
            cmd_ln_set_int32("-maxcdsenpf", atoi(val)); 
    }
    else if (!strcmp(name, "MAXHMMPF")) {
            cmd_ln_set_int32("-maxhmmpf", atoi(val)); 
    }
    else if (!strcmp(name, "MAXHISTPF")) {
            cmd_ln_set_int32("-maxhistpf", atoi(val)); 
    }
    else if (!strcmp(name, "MAXWPF")) {
            cmd_ln_set_int32("-maxwpf", atoi(val)); 
    }
    else                 
        {fprintf(stderr, "ERROR: command SET %s unknown\n", name);}

}

void reset_cmd(char *name)
{
    if (!strcmp(name, "BEAM"))
        reset_beam();
    else                 
        {fprintf(stderr, "ERROR: command RESET %s unknown\n", name);}
}



void process_cmd(char *cmdstr)
{
    char cmd[STDIN_CMD_STRLN], arg1[STDIN_CMD_STRLN], arg2[STDIN_CMD_STRLN];
    int num_cmd;

    num_cmd = sscanf(cmdstr, "%s %s %s", cmd, arg1, arg2);
    if (num_cmd > 0) {
        /* fprintf(stderr, "nc: %d", num_cmd); */
            if (!strcmp(cmd, "SET"))
                if (num_cmd == 3) 
                    {set_cmd(arg1, arg2);} else {fprintf(stderr, "ERROR: SET takes 2 arguments\n");}
            else if (!strcmp(cmd, "RESET"))
                if (num_cmd == 2) 
                    {reset_cmd(arg1);} else {fprintf(stderr, "ERROR: RESET takes 1 argument\n");}
            else 
                {fprintf(stderr, "ERROR: command %s unknown\n", cmd);}
        }
}

    /* thanks to http://cc.byexamples.com/20070408/non-blocking-user-input-in-loop-without-ncurses/ and http://www.developerweb.net/forum/showthread.php?p=23832 */

#include "fcntl.h"

void setnonblocking(int fd)
{
	int opts;

	opts = fcntl(fd,F_GETFL);
	if (opts < 0) {
		perror("fcntl(F_GETFL)");
		exit(EXIT_FAILURE);
	}
	opts = (opts | O_NONBLOCK);
	if (fcntl(fd,F_SETFL,opts) < 0) {
		perror("fcntl(F_SETFL)");
		exit(EXIT_FAILURE);
	}
	return;
}

void process_stdin()
{
    char instr[STDIN_CMD_STRLN];
    char cmd_str[STDIN_CMD_STRLN];
    char errstr[256];

    struct timeval tv;
    fd_set fds;
    char *fgets_success;

    instr[0] = 0;
    setnonblocking(STDIN_FILENO);

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    fgets_success = 0;
    while (FD_ISSET(STDIN_FILENO, &fds) || fgets_success) { /* bug: should handle multiple cmds at once */
            /*printf("debug 10 %d\n", STDIN_FILENO);*/

            /* we have input coming into stdin */

            /*fflush(stdin); */

            /* *(instr+instrpos) = fgetc(stdin); */
            fgets_success = fgets(instr+strlen(instr), STDIN_CMD_STRLN-strlen(instr), stdin);
            
            /*printf("debug 11:\n%s\n***\n", instr);*/

            
            if (strlen(instr) && (*(instr+strlen(instr)-1) == '\n')) {
                printf(instr);
                process_cmd(instr);
                instr[0] = 0; /* bug: should handle multiple cmds at once */
            }
            

            /*
            if (strlen(instr))
                while (sscanf(instr, "%[^\n]\n", cmd_str) > 0) {
                    printf("debug 12:\n%s\n***\n", instr);
                    strncpy(instr, instr+strlen(cmd_str)+1, STDIN_CMD_STRLN);
                    printf("debug 13:\n%s\n***\n", cmd_str);
                    printf("debug 14:\n%s\n***\n", instr);

                    printf(cmd_str);
                    process_cmd(cmd_str);                    
                }
            */


            else if (strlen(instr) >= STDIN_CMD_STRLN)
                {
                    sprintf(errstr, "incoming command longer than %d; ignoring\n", (STDIN_CMD_STRLN-1));
                    E_WARN(errstr);
                    instr[0] = 0;
                }

            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
            select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
        }

}


/*
 * Main utterance processing loop:
 *     for (;;) {
 * 	   wait for start of next utterance;
 * 	   decode utterance until silence of at least 1 sec observed;
 * 	   print utterance result;
 *     }
 */
static void
utterance_loop()
{
    int16 adbuf[4096];
    int32 k, fr, ts, rem;
    char *hyp;
    cont_ad_t *cont;
    char word[256];
    mfcc_t **cep_block;
    int32 nframes;
    hyp_t **hypsegs;
    long long int uttid;
    char uttid_str[64];
    int d;
    d=0;
    uttid = 0;

    /* Initialize continuous listening module */
    if ((cont = cont_ad_init(ad, ad_read)) == NULL)
        E_FATAL("cont_ad_init failed\n");
    if (ad_start_rec(ad) < 0)
        E_FATAL("ad_start_rec failed\n");
    if (cont_ad_calib(cont) < 0)
        E_FATAL("cont_ad_calib failed\n");

    for (;;) {
        /* Indicate listening for next utterance */
        printf("READY....\n");
        fflush(stdout);
        fflush(stderr);

        while ((k = cont_ad_read(cont, adbuf, 4096)) < 1) {
            //fprintf(stderr, "k: %d\n", k);
       
            if (k < 0) {

                E_WARN("cont_ad_read failed\n");

                while (k < 0) {
                    if (cont) {
                        E_WARN("closing cont\n");
                        cont_ad_close(cont);
                        cont = NULL;
                    }
                    if (ad) {
                        E_WARN("closing ad\n");
                        ad_close(ad);
                        ad = NULL;
                    }
                    sleep_msec(20);

                    k = 0;
                    if ((ad = ad_open_dev(cmd_ln_str("-adcdev"), (int)cmd_ln_float32("-samprate"))) == NULL) {
                        E_WARN("ad_open_dev failed\n");
                        k=-1;
                        continue;
                    }
                    if ((cont = cont_ad_init(ad, ad_read)) == NULL) {
                        E_WARN("cont_ad_init failed\n");
                        k=-1;
                        continue;
                    }
                    if (ad_start_rec(ad) < 0) {
                        E_WARN("cont_start_rec failed\n");
                        k=-1;
                        continue;
                    }
                    if (cont_ad_calib(cont) < 0) {
                        E_WARN("cont_ad_calib failed\n");
                        printf("calib cont: %p\n", cont);
                        printf("calib: %d\n", cont_ad_calib(cont));
                        k=-1;
                        continue;
                    }
                }
                
            }
            else {       
                process_stdin();
                sleep_msec(20);
            }
        }
        


        /*
         * Non-zero amount of data received; start recognition of new utterance.
         * NULL argument to uttproc_begin_utt => automatic generation of utterance-id.
         */
        
        sprintf(uttid_str, "%lld", uttid++);
        if (s3_decode_begin_utt(&decoder, uttid_str) < 0)
            E_FATAL("s3_decode_begin_utt failed\n");
        /* uttproc_rawdata(adbuf, k, 0); */

	/* nsamps = PyString_Size(str)/2;

	if (fe_process_utt(fe, data, nsamps, &cep_block, &nframes) == -1) {
		PyErr_SetString(PyExc_ValueError, "Problem in fe_process_utt()");
		return NULL;
	}
	s3_decode_process(&decoder, cep_block, nframes);
        */
        /* if (1) { */

        if (fe_process_utt(fe, adbuf, k, &cep_block, &nframes) == -1) {
            /* E_INFO("Problem in fe_process_utt()");
            return NULL; */
            E_FATAL("Problem in fe_process_utt()");
	}
	s3_decode_process(&decoder, cep_block, nframes);
        /* d++;} */
        printf("Listening...\n");
        fflush(stdout);

        /* Note timestamp for this first block of data */
        ts = cont->read_ts;

        /* Decode utterance until end (marked by a "long" silence, >1sec) */
        for (;;) {
            /* Read non-silence audio data, if any, from continuous listening module */

            k = cont_ad_read(cont, adbuf, 4096);

            if (k < 0) {
                E_WARN("cont_ad_read failed\n");
                while (k < 0) {
                    if (cont) {
                        E_WARN("closing cont\n");
                        cont_ad_close(cont);
                        cont = NULL;
                    }
                    if (ad) {
                        E_WARN("closing ad\n");
                        ad_close(ad);
                        ad = NULL;
                    }
                    sleep_msec(20);

                    k = 0;
                    if ((ad = ad_open_dev(cmd_ln_str("-adcdev"), (int)cmd_ln_float32("-samprate"))) == NULL) {
                        E_WARN("ad_open_dev failed\n");
                        k=-1;
                        continue;
                    }
                    if ((cont = cont_ad_init(ad, ad_read)) == NULL) {
                        E_WARN("cont_ad_init failed\n");
                        k=-1;
                        continue;
                    }
                    if (ad_start_rec(ad) < 0) {
                        E_WARN("cont_start_rec failed\n");
                        k=-1;
                        continue;
                    }
                    if (cont_ad_calib(cont) < 0) {
                        E_WARN("cont_ad_calib failed\n");
                        printf("calib cont: %p\n", cont);
                        printf("calib: %d\n", cont_ad_calib(cont));
                        k=-1;
                        continue;
                    }
                }
                
            }
        

            if (k == 0) {
                /*
                 * No speech data available; check current timestamp with most recent
                 * speech to see if more than 1 sec elapsed.  If so, end of utterance.
                 */ /* bayle: .05 */
                /* if ((cont->read_ts - ts) > 0.25*DEFAULT_SAMPLES_PER_SEC) */
                if ((cont->read_ts - ts) > silence_length*DEFAULT_SAMPLES_PER_SEC)
                    break;
            }
            else {
                /* New speech data received; note current timestamp */
                ts = cont->read_ts;
            }
        
            /*
             * Decode whatever data was read above.  NOTE: Non-blocking mode!!
             * rem = #frames remaining to be decoded upon return from the function.
             */
            /* rem = uttproc_rawdata(adbuf, k, 0); */

            /*if (1) {*/
            if (fe_process_utt(fe, adbuf, k, &cep_block, &nframes) == -1) {
            /* E_INFO("Problem in fe_process_utt()");
            return NULL; */
                E_FATAL("Problem in fe_process_utt()");
            }
            s3_decode_process(&decoder, cep_block, nframes);
        /* d++;} */


            /* If no work to be done, sleep a bit */
            if ((k == 0))
                sleep_msec(20);
        }

        /*
         * Utterance ended; flush any accumulated, unprocessed A/D data and stop
         * listening until current utterance completely decoded
         */
        /*
        ad_stop_rec(ad);


         printf("Stopped listening, please wait...\n"); 
         fflush(stdout); 

        while (ad_read(ad, adbuf, 4096) >= 0)
            cont_ad_reset(cont); 

// Resume A/D recording for next utterance
         if (ad_start_rec(ad) < 0) 
             E_FATAL("ad_start_rec failed\n"); 

         printf("Resumed listening.\n"); 
         fflush(stdout); 
        */

#if 0
        /* Power histogram dump (FYI) */
        cont_ad_powhist_dump(stdout, cont);
#endif
        /* Finish decoding, obtain and print result */
        /* uttproc_end_utt(); */
        s3_decode_end_utt(&decoder);

        /* if (uttproc_result(&fr, &hyp, 1) < 0)
           E_FATAL("uttproc_result failed\n"); */
        s3_decode_hypothesis(&decoder, NULL, &hyp, &hypsegs);
        fr= 0;
        printf("%d: %s\n", fr, hyp);
        fflush(stdout);

        /* Exit if the first word spoken was GOODBYE */
        sscanf(hyp, "%s", word);
        if (strcmp(word, "goodbye") == 0)
            break;

    }

    cont_ad_close(cont);
}

static jmp_buf jbuf;
static void
sighandler(int signo)
{
    longjmp(jbuf, 1);
}

#ifdef _WIN32_WCE

int WINAPI
WinMain(HINSTANCE hInstance,
        HINSTANCE hPrevInstance,
        LPTSTR lpCmdLine, int nCmdShow)
{
    static char *fake_argv[] = { "pocketsphinx_continuous.exe", NULL };

    /* FIXME: This needs serious improvement. */
    /*    fbs_init(1, fake_argv);*/
    s3_decode_init(&decoder);
    fe = fe_init_auto();

    if ((ad = ad_open_sps(/*cmd_ln_float32("-samprate")*/ 22050)) == NULL)
        E_FATAL("ad_open_sps failed\n");

	utterance_loop();

        /*fbs_end();*/
    s3_decode_close(&decoder);
    fe_close(fe);
    cmd_ln_free();
    ad_close(ad);
    return 0;
}
#else
int
main(int argc, char *argv[])
{
    /* Make sure we exit cleanly (needed for profiling among other things) */
    /* Signals seem to be broken in arm-wince-pe. */
#if !defined(GNUWINCE) && !defined(_WIN32_WCE)
    signal(SIGINT, &sighandler);
#endif

    /*
      if (cmd_ln_parse_file(S3_DECODE_ARG_DEFS, "sphinx3_mycontinuous.cfg", FALSE) == -1) {
        E_FATAL("sphinx3_mycontinuous.cfg could not be read\n");
    }
    */

    
    if (cmd_ln_parse(S3_DECODE_ARG_DEFS, argc, argv, FALSE) == -1) {
        E_FATAL("Arguments are invalid");
    }

    silence_length  = 0.25;


    printf("debug 11\n");
    /* fbs_init(argc, argv); */
    fe = fe_init_auto();
    printf("debug 2\n");

    s3_decode_init(&decoder);

    printf("debug 3\n");

    //s3_decode_read_lm(&decoder, "keyboard-vocab.lm", "k");
    //    s3_decode_read_lm(&decoder, "voice-keyboard-current.lm.DMP", "k");
    //    s3_decode_set_lm(&decoder, "keyboard");
    






    if ((ad = ad_open_dev(cmd_ln_str("-adcdev"), (int)cmd_ln_float32("-samprate"))) == NULL)
    //    if ((ad = ad_open_dev(DEFAULT_DEVICE, (int)cmd_ln_float32("-samprate"))) == NULL)
    /* if ((ad = ad_open_dev("/dev/dsp1", (int)cmd_ln_float32("-samprate"))) == NULL) */
        E_FATAL("ad_open_dev failed\n");

    E_INFO("%s COMPILED ON: %s, AT: %s\n\n", argv[0], __DATE__, __TIME__);

    if (setjmp(jbuf) == 0) {
        utterance_loop();
    }

    ad_close(ad);
    /* fbs_end();*/
    s3_decode_close(&decoder);
    fe_close(fe);
    cmd_ln_free();
  

    return 0;
}
#endif /* !_WIN32_WCE */
