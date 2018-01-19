/** @file simple_client.c
 *
 * @brief This simple client demonstrates the most basic features of JACK
 * as they would be used by many applications.
 */

// "standard" libraries
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// libraries/code that require building/linking
// #include <pthread.h>
#include <jack/jack.h>
#include <julia.h>
//#include <pa_ringbuffer.h>

#define JACK_JULIA_MAX_PORTS (64)
#define JACK_JULIA_MAX_FRAMES (16384)
jack_port_t *jackin_ports[JACK_JULIA_MAX_PORTS];
jack_port_t *jackout_ports[JACK_JULIA_MAX_PORTS];
jack_client_t *client;

int nframes = 0;
int inchans = 0;
int outchans = 0;
const char *JACK_CLIENT_DEFAULT_NAME = "jack_julia";
#define JACK_CLIENT_NAME_SIZE (2048)
#define JACK_PORT_NAME_SIZE (2048)
char jackname[JACK_CLIENT_NAME_SIZE]   = {0};
char inconnect[JACK_CLIENT_NAME_SIZE]  = {0};
char outconnect[JACK_CLIENT_NAME_SIZE] = {0};
char include[JACK_CLIENT_NAME_SIZE]    = {0};
char funcname[JACK_CLIENT_NAME_SIZE]   = {0};

/* Create 2D array of float64 type for input/output */
jl_array_t *input_array;
jl_array_t *output_array;




// Interleaved buffers for dumping in/out of the the PaUtilRingBuffer
// There is one for each thread, the fileio thread, and the jack thread
// jack_default_audio_sample_t linbufIN[JACK_JULIA_MAX_PORTS * JACK_JULIA_MAX_FRAMES];
// jack_default_audio_sample_t linbufOUT[JACK_JULIA_MAX_PORTS * JACK_JULIA_MAX_FRAMES];
// PaUtilRingBuffer pa_ringbuf_; // ringbuffer for communicating between threads
// PaUtilRingBuffer *pa_ringbuf = &(pa_ringbuf_);
// void * ringbuf_memory; // ringbuffer pointer for use with malloc/free
// int ringbuf_nframes = JACK_JULIA_MAX_FRAMES;

// #define ISPOW2(x) ((x) > 0 && !((x) & (x-1)))
// int nextpow2(int x) {
//     if(ISPOW2(x)) {
//         return x;
//     }
//     int power = 2;
//     while (x >>= 1) power <<= 1;
//     return (int)(1 << power);
// }


/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 * This client does nothing more than copy data from its input
 * port to its output port. It will exit when stopped by 
 * the user (e.g. using Ctrl-C on a unix-ish operating system)
 */
int jack_process(jack_nframes_t nframes, void *arg) {
    int cidx, sidx;
    jack_nframes_t fidx, nframes_read_available, nframes_write_available;
    jack_nframes_t nframes_read, nframes_written;

    // silence compiler
    arg = arg;

    jack_default_audio_sample_t *in = \
        (jack_default_audio_sample_t*)jl_array_data(input_array);
    jack_default_audio_sample_t *out = \
        (jack_default_audio_sample_t*)jl_array_data(output_array);

    // get input jack buffers as needed, and write from those buffers to in/out
    for(cidx=0; cidx<sndchans; cidx++) {
        jack_default_audio_sample_t *jackbuf = jack_port_get_buffer(jackin_ports[cidx], nframes);
        for(sidx=0; sidx<nframes; sidx++) {
            *(in++) = *(jackbuf++)
        }
    }

    // jl_call2()
    // end PLAY_MODE

    else if(sndmode == REC_MODE) {
        // get pointers for all jack port buffers
        jack_default_audio_sample_t *jackbufs[JACK_JULIA_MAX_PORTS];
        for(cidx=0; cidx<sndchans; cidx++) {
            jackbufs[cidx] = jack_port_get_buffer(jackin_ports[cidx], nframes);
        }
        
        // write to linbufJACK one sample at a time
        // set outer loop over frames/samples
        sidx = 0; // use sample index to book-keep current index in to linbufJACK
        for(fidx=0; fidx<nframes; fidx++) {
            // set inner loop over channels/jackbufs
            for(cidx=0; cidx<sndchans; cidx++) {
                // this is naive, but might be fast enough
                linbufJACK[sidx++] = jackbufs[cidx][fidx];
            }
        }

        nframes_write_available = PaUtil_GetRingBufferWriteAvailable(pa_ringbuf);
        if( nframes_write_available < nframes) {
            /* FIXME, report overflow problem */
        }

        nframes_written = PaUtil_WriteRingBuffer(
            pa_ringbuf, &(linbufJACK[0]), nframes);
        if( nframes_written != nframes) {
            /* FIXME, report overflow */
        }
    } // end REC_MODE

    else {
        /* FIXME, catch this error */
    }

    return 0;
}




/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown (void *arg)
{
    // free(ringbuf_memory);
    arg=arg; /* silence compiler */
    exit (1);
}

void usage(void) {
    printf("\n\n");
    printf("Usage: jack_play_record [OPTION...] [-p play.wav | -c chans -r rec.wav]\n");
    printf("  -h,    print this help text\n");
    printf("  -c,    specify the number of channels (required for recording)\n");
    printf("  -n,    specify the name of the jack client\n");
    printf("  -f,    specify the intended nframes for use with jack server\n");
    printf("         note, that this will save on memory, but is unsafe if the\n");
    printf("         jack server nframes value is ever increased\n");
    printf("  -w,    wait until W ports have been connected before playing or recording\n");
    printf("\n\n");
}

void fyi(void) {
    printf("\nINFO: Attempting to call \n    %s from %s, where\n    nframes=%d, inchans=%d, outchans=%d, and \n    client-name='%s'\n\n",
            funcname, include, nframes, inchans, outchans, jackname);
}

int main (int argc, char **argv)
{


    // const char **ports;
    // pthread_t fileio_thread;
    // int thr = 1;
    const char *server_name = NULL;
    jack_options_t options = JackNullOption;
    jack_status_t status;

    int cidx, sidx, c, err;

    char portname[JACK_PORT_NAME_SIZE] = {0};

    static struct option long_options[] = {
        /* These options set a flag. */
        {"nframes",    required_argument, 0, 'r'},
        {"inchans",    required_argument, 0, 'i'},
        {"outchans",   required_argument, 0, 'o'},
        {"inconnect",  required_argument, 0, 'a'},
        {"outconnect", required_argument, 0, 'b'},
        {"name",       required_argument, 0, 'n'},
        {"include",    required_argument, 0, 'c'},
        {"function",   required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };
    /* getopt_long stores the option index here. */
    int option_index = 0;

    while ((c = getopt_long(argc, argv, 
            "hr:i:o:a:b:n:c:f:h", long_options, NULL)) != -1)
    switch (c) {
        case 'h':
            usage();
            return 0;
        case 'r':
            nframes = atoi(optarg);
            break;
        case 'i':
            inchans = atoi(optarg);
            break;
        case 'o':
            outchans = atoi(optarg);
            break;
        case 'a':
            snprintf(inconnect, JACK_CLIENT_NAME_SIZE, "%s", optarg);
            break;
        case 'b':
            snprintf(outconnect, JACK_CLIENT_NAME_SIZE, "%s", optarg);
            break;
        case 'n':
            snprintf(jackname, JACK_CLIENT_NAME_SIZE, "%s", optarg);
            break;
        case 'c':
            snprintf(include, JACK_CLIENT_NAME_SIZE, "%s", optarg);
            break;
        case 'f':
            snprintf(funcname, JACK_CLIENT_NAME_SIZE, "%s", optarg);
            break;
        default:
            abort ();
    }

    /* FIXME, warn user if nframes != jack_server_nframes, or try to set the jack server? */

    /* ensure there's a reasonable jack client name if not already set */
    if( jackname[0] == 0 ) {
        snprintf(jackname, JACK_CLIENT_NAME_SIZE, "%s", \
            JACK_CLIENT_DEFAULT_NAME);
    }

    /* let user know what settings have been parsed */
    fyi();

    /* open a client connection to the JACK server */
    client = jack_client_open(jackname, options, &status, server_name);
    if (client == NULL) {
        fprintf (stderr, "jack_client_open() failed, "
                "status = 0x%2.0x\n", status);
        if (status & JackServerFailed) {
            fprintf (stderr, "Unable to connect to JACK server\n");
        }
        exit (1);
    }
    if (status & JackServerStarted) {
        fprintf (stderr, "JACK server started\n");
    }
    if (status & JackNameNotUnique) {
        snprintf(jackname, JACK_CLIENT_NAME_SIZE, "%s", jack_get_client_name(client));
        fprintf(stderr, "unique name `%s' assigned\n", &(jackname[0]));
    }

    /* initialize julia environment */
    jl_init();

    /* tell the JACK server to call `process()' whenever
        there is work to be done.
    */
    jack_set_process_callback (client, jack_process, 0);

    /* tell the JACK server to call `jack_shutdown()' if
        it ever shuts down, either entirely, or if it
        just decides to stop calling us.
    */
    jack_on_shutdown (client, jack_shutdown, 0);

    /* FIXME, throw error if file sample rate and jack sample rate are different */

    /* create jack ports */
    for(cidx=0; cidx<inchans; cidx++) {
        snprintf(portname, JACK_PORT_NAME_SIZE, "in_%02d", cidx+1);
        jackin_ports[cidx] = jack_port_register (client, portname,
                JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsInput, 0);
    }
    for(cidx=0; cidx<inchans; cidx++) {
        snprintf(portname, JACK_PORT_NAME_SIZE, "out_%02d", cidx+1);
        jackout_ports[cidx] = jack_port_register(client, portname,                    
                JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsOutput, 0);
    }

    /* create julia arrays/buffers */
    jl_value_t *array_type = jl_apply_array_type(
        (jl_value_t*)jl_float32_type, 2);
    input_array = jl_alloc_array_2d(array_type, nframes, inchans);
    output_array = jl_alloc_array_2d(array_type, nframes, outchans);
    JL_GC_PUSH3(&array_type, &input_array, &output_array);

    /* zero out arrays */
    jack_default_audio_sample_t *in = \
        (jack_default_audio_sample_t*)jl_array_data(input_array);
    jack_default_audio_sample_t *out = \
        (jack_default_audio_sample_t*)jl_array_data(output_array);
    for(sidx=0; sidx<nframes*inchans; sidx++) in[sidx] = 0.0;
    for(sidx=0; sidx<nframes*outchans; sidx++) out[sidx] = 0.0;

    /* Let's set up a pa_ringbuffer, for single producer, single consumer */
    /* ensure ringbuf_nframes is a power of 2 */
    // ringbuf_nframes = nextpow2(ringbuf_nframes);

    /* malloc space for pa_ringbuffer */
    // ringbuf_memory = malloc( 
        // sizeof(jack_default_audio_sample_t) * sndchans * 4 * ringbuf_nframes);

    // err = PaUtil_InitializeRingBuffer(pa_ringbuf, 
    //     sizeof(jack_default_audio_sample_t) * sndchans,
    //     4 * ringbuf_nframes,
    //     ringbuf_memory);
    // if(err) {
    //     printf("encountered error code (%d) trying to call PaUtil_InitializeRingBuffer\n",err);
    // }

    // start the fileio_thread
    // pthread_create(&fileio_thread, NULL, *fileio_function, (void *) &(thr));

    /* Tell the JACK server that we are ready to roll.  Our
    * process() callback will start running now. */

    if (jack_activate(client)) {
        fprintf (stderr, "cannot activate client");
        exit (1);
    }

    /* FIXME add command line arg to auto-connect to a block... */
    // ports = jack_get_ports (client, NULL, NULL,
    // 			JackPortIsPhysical|JackPortIsOutput);
    // if (ports == NULL) {
    // 	fprintf(stderr, "no physical capture ports\n");
    // 	exit (1);
    // }

    // if (jack_connect (client, ports[0], jack_port_name (input_port))) {
    // 	fprintf (stderr, "cannot connect input ports\n");
    // }

    // free (ports);

    // ports = jack_get_ports (client, NULL, NULL,
    // 			JackPortIsPhysical|JackPortIsInput);
    // if (ports == NULL) {
    // 	fprintf(stderr, "no physical playback ports\n");
    // 	exit (1);
    // }

    // if (jack_connect (client, jack_port_name (output_port), ports[0])) {
    // 	fprintf (stderr, "cannot connect output ports\n");
    // }

    // free (ports);

    /* keep running until stopped by the user */

    sleep (-1);

    /* this is never reached but if the program
        had some other way to exit besides being killed,
        they would be important to call.
    */

    jack_client_close (client);
    exit (0);
}
