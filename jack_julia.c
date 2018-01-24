/** @file jack_julia.c
 *
 * @brief This code will create a jack client with the following inputs 
 * inchans = number of audio input channels
 * inchans = number of audio output frames
 * include_file = file to include for custom audio processing code
 * funcname = name of processing function to call
 */

// "standard" libraries
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
// getopt needs to be included manually b/c we compile -    std=c99
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// libraries/code that require building/linking
// #include <pthread.h>
#include <jack/jack.h>
#include <julia.h>
JULIA_DEFINE_FAST_TLS() // only define this once, in an executable (not in a shared library) if you want fast code.

#include <pa_ringbuffer.h>

// START declaring variables for client
#define JACK_JULIA_MAX_PORTS (64)
#define JACK_JULIA_MAX_FRAMES (16384)
jack_port_t *jackin_ports[JACK_JULIA_MAX_PORTS];
jack_port_t *jackout_ports[JACK_JULIA_MAX_PORTS];
jack_client_t *client;

unsigned int nframes = 0;
unsigned int inchans = 0;
unsigned int outchans = 0;
const char *JACK_CLIENT_DEFAULT_NAME = "jack_julia";
#define JACK_CLIENT_NAME_SIZE (2048)
#define JACK_PORT_NAME_SIZE (2048)
char jackname[JACK_CLIENT_NAME_SIZE]       = {0};
char inconnect[JACK_CLIENT_NAME_SIZE]      = {0};
char outconnect[JACK_CLIENT_NAME_SIZE]     = {0};
char include_file[JACK_CLIENT_NAME_SIZE]   = {0};
char include_str[JACK_CLIENT_NAME_SIZE]    = {0};
char funcname[JACK_CLIENT_NAME_SIZE]       = {0};
char func_check_str[JACK_CLIENT_NAME_SIZE] = {0};
jl_function_t *funchandle;
/* Create 2D array of float64 type for input/output */
jl_array_t *input_array;
jl_array_t *output_array;
// STOP declaring variables for client

// 1-channel buffers for dumping in/out to/from JACK and Julia
PaUtilRingBuffer inrbufs[JACK_JULIA_MAX_PORTS]; // ringbuffer for communicating between threads
PaUtilRingBuffer outrbufs[JACK_JULIA_MAX_PORTS]; // ringbuffer for communicating between threads

// void * ringbuf_memory; // ringbuffer pointer for use with malloc/free
// int ringbuf_nframes = JACK_JULIA_MAX_FRAMES;

#define ISPOW2(x) ((x) > 0 && !((x) & (x-1)))
int nextpow2(int x) {
    if(ISPOW2(x)) {
        return x;
    }
    int power = 2;
    while (x >>= 1) power <<= 1;
    return (int)(1 << power);
}


/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 * This client does nothing more than copy data from its input
 * port to its output port. It will exit when stopped by 
 * the user (e.g. using Ctrl-C on a unix-ish operating system)
 */
int jack_process(jack_nframes_t nframes, void *arg) {
    unsigned int cidx;
    jack_default_audio_sample_t *jackbuf;

    for(cidx=0; cidx<inchans; cidx++) {
        if(PaUtil_GetRingBufferWriteAvailable(inrbufs[cidx]) < nframes_jack) {
            // FIXME, report overflow, and keep all channels/buffers synced 
            continue;
        }
    }

    for(cidx=0; cidx<outchans; cidx++) {
        if(PaUtil_GetRingBufferReadAvailable(outrbufs[cidx]) < nframes_jack) { 
            // FIXME, report underflow, and keep all channels/buffers synced
            // FIXME, write zeros to jack output
            continue;
        }
    }

    for(cidx=0; cidx<inchans; cidx++) {
        jackbuf = jack_port_get_buffer(inports[cidx]);
        PaUtil_WriteRingBuffer(inbufs[cidx], jackbuf, nframes);
    }

    for(cidx=0; cidx<outchans; cidx++) {
        jackbuf = jack_port_get_buffer(outports[cidx]);
        PaUtil_ReadRingBuffer(outrbufs[cidx]) < nframes_jack) { 
    }
}




/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void jack_shutdown (void *arg)
{
    arg=arg; /* silence compiler */
    // free(ringbuf_memory);
    /* let Julia GC know it can safely GC input_array and output_array */
    JL_GC_POP(); 
    /* strongly recommended: notify Julia that the
         program is about to terminate. this allows
         Julia time to cleanup pending write requests
         and run all finalizers
    */
    jl_atexit_hook(0);
    exit (1);
}

void usage() {
    printf("\n\n");
    printf("Usage:\n");
    printf("jack_julia [OPTIONS...] -i INCHANS -o OUTCHANS\n");
    printf("           --include ALGO.jl --function ALGOFUNC\n");
    printf("  -h, --help        print this help text\n");
    printf("REQUIRED ARGS:\n");
    printf("  -i, --inchans     set number of input channels\n");
    printf("  -o, --outchans    set number of output channels\n");
    printf("  -c, --include     set julia source file to include\n");
    printf("  -f, --function    set julia processing function\n");
    printf("      jack_julia will call this this function with two arguments:\n");
    printf("        1) input_array, of size (nframes x inchans)\n");
    printf("        2) output_array, of size (nframes x outchans)\n");
    printf("OPTIONAL ARGS:\n");
    printf("  -r, --nframes     set number of frames per function call\n");
    printf("                    currently this must be the same as jack_get_buffer_size()\n");
    printf("  -a, --inconnect   jack client to automatically connect to inputs\n");
    printf("  -b, --outconnect  jack client to automatically connect to inputs\n");
    printf("  -n, --name      specify the name of the jack client\n");
    // printf("  -w,    wait until W ports have been connected before playing or recording\n");
    printf("\n\n");
}

void fyi(void) {
    printf("\n\nINFO: Attempting to call"
           "\n    '%s' (funcname) from "
           "\n    '%s' (include_file)"
           "\n    nframes=%d"
           "\n    inchans=%d"
           "\n    outchans=%d"
           "\n    client-name='%s'\n\n",
            funcname, include_file, nframes, inchans, outchans, jackname);
    return;
}

int main (int argc, char **argv)
{
    // const char **ports;
    pthread_t fileio_thread;
    int thr = 1;
    const char *server_name = NULL;
    jack_options_t options = JackNullOption;
    jack_status_t status;

    int c;
    unsigned int cidx;//, sidx;
    char portname[JACK_PORT_NAME_SIZE] = {0};

    static struct option long_options[] = {
        /* These options set a flag. */
        {"help",       no_argument,       0, 'h'},
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
            "hr:i:o:a:b:n:c:f:h", long_options, &option_index)) != -1)
    switch (c) {
        case 'h':
            usage("");
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
            snprintf(include_file, JACK_CLIENT_NAME_SIZE, "%s", optarg);
            snprintf(include_str, JACK_CLIENT_NAME_SIZE, \
                "include(\"%s\")", include_file);
            break;
        case 'f':
            snprintf(funcname, JACK_CLIENT_NAME_SIZE, "%s", optarg);
            snprintf(func_check_str, JACK_CLIENT_NAME_SIZE, \
                "isdefined(:%s) && typeof(%s) <: Function",
                funcname, funcname);

            break;
        default:
            abort ();
    }

    /* if there are no inchans AND no outchans, tell user to try again */
    if( 0==inchans && 0==outchans ) {
        printf("ERR: inchans or outchans must be greater than 0");
        usage();
        return 0;
    }

    /* check if file exists and is readable */
    if( access( include_file, F_OK|R_OK ) == -1 ) {
        printf("ERR: can not see file, '%s' as readable file", include_file);
        usage();
        return 0;
    }

    /* after file check, init julia, include file, and verify function */
    /* initialize julia environment */
    jl_init();
    jl_eval_string(include_str);
    if( !jl_unbox_bool(jl_eval_string(func_check_str)) ) {
        printf("ERR: can not see function, '%s', after including '%s'\n", 
            funcname, include_file);
        usage();
        return 0;
    }
    else {
        printf("\n\nINFO: recognized '%s' as julia function after including '%s'\n",
            funcname, include_file); 
    }

    /* ensure there's a reasonable jack client name if not already set */
    if( jackname[0] == 0 ) {
        snprintf(jackname, JACK_CLIENT_NAME_SIZE, "%s", \
            JACK_CLIENT_DEFAULT_NAME);
    }

    /* create unactivated jack client for using jack to determine arg values*/
    /* open a client connection to the JACK server */
    client = jack_client_open(jackname, options, &status, server_name);
    if (client == NULL) {
        fprintf (stderr, "jack_client_open() failed, "
                "status = 0x%02x\n", status);
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

    /* set nframes from jack server if not set via args */
    if( 0==nframes ) {
        nframes = jack_get_buffer_size(client);
    }

    /* error out if nframes !=  jack_get_buffer_size() */
    if(jack_get_buffer_size(client) != nframes) {
        // FIXME, try to change jack_server
        printf("ERR: currently, jack_get_buffer_size() can not be different than nframes\n");
        printf("     in the future, jack_julia may attempt to set jack server buffer size\n");
        usage();
        return 0;
    }

    /* let user know what settings have been parsed */
    fyi();

    /* tell the JACK server to call `process()' whenever
        there is work to be done.
    */
    printf("INFO: setting client jack_process\n");
    jack_set_process_callback (client, jack_process, 0);

    /* tell the JACK server to call `jack_shutdown()' if
        it ever shuts down, either entirely, or if it
        just decides to stop calling us.
    */
    printf("INFO: setting client jack_shutdown\n");
    jack_on_shutdown (client, jack_shutdown, 0);

    /* FIXME, throw error if file sample rate and jack sample rate are different */

    /* create jack ports */
    printf("INFO: creating %d in-ports\n", inchans);
    for(cidx=0; cidx<inchans; cidx++) {
        snprintf(portname, JACK_PORT_NAME_SIZE, "in_%02d", cidx+1);
        jackin_ports[cidx] = jack_port_register (client, portname,
                JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsInput, 0);
    }
    printf("INFO: creating %d out-ports\n", outchans);
    for(cidx=0; cidx<inchans; cidx++) {
        snprintf(portname, JACK_PORT_NAME_SIZE, "out_%02d", cidx+1);
        jackout_ports[cidx] = jack_port_register(client, portname,                    
                JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsOutput, 0);
    }


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
    printf("\n\nINFO: activated jack client, '%s'\n", jackname);

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

    /* keep calling julia code until stopped by the user */
    while(1) {

        // jack_nframes_t nframes_read_available, nframes_write_available;
        // jack_nframes_t nframes_read, nframes_written;
        // jack_nframes_t fidx;

        // ensure (1)nframes_julia available to get from inrbuf, and 
        //        (2)nframes_julia available to put into outrbuf
        for(cidx=0; cidx<inchans; cidx++) {
            if(PaUtil_GetRingBufferReadAvailable(inrbufs[cidx]) < nframes_jack) { 
                sched_yield();
                continue;
            }
        }

        for(cidx=0; cidx<outchans; cidx++) {
            if(PaUtil_GetRingBufferWriteAvailable(outrbufs[cidx]) < nframes_jack) { 
                sched_yield();
                continue;
            }
        }


        jack_default_audio_sample_t *in = \
            (jack_default_audio_sample_t*)jl_array_data(input_array);
        jack_default_audio_sample_t *out = \
            (jack_default_audio_sample_t*)jl_array_data(output_array);

        // write from input jack-buffers to in input_array
        for(cidx=0; cidx<inchans; cidx++) {
            jack_default_audio_sample_t *jackbuf = 
                    jack_port_get_buffer(jackin_ports[cidx], nframes);
            for(sidx=0; sidx<nframes; sidx++) {
                *(in++) = *(jackbuf++);
            }
        }

        // printf("DBG: jl_is_initialized() = %d\n", jl_is_initialized());
        // printf("DBG: jl_eval_string('%s') evaluates to ", func_check_str);
        // printf("%d\n", jl_unbox_bool(jl_eval_string(func_check_str)));

        jl_eval_string("@show sqrt(2.0)");

        // printf("DBG: jl_is_initialized() = %d\n", jl_is_initialized());

        /* call fulia function */
        /* FIXME, should we check funchandle for NULL each time? */
        // jl_call2(funchandle, (jl_value_t*)input_array, (jl_value_t*)output_array);

        // write from output_array to output jack-buffers
        for(cidx=0; cidx<outchans; cidx++) {
            jack_default_audio_sample_t *jackbuf = 
                    jack_port_get_buffer(jackout_ports[cidx], nframes);
            for(sidx=0; sidx<nframes; sidx++) {
                *(jackbuf++) = *(out++);
            }
        }

        // nframes_write_available = PaUtil_GetRingBufferWriteAvailable(pa_ringbuf);
        // if( nframes_write_available < nframes) {
        //     /* FIXME, report overflow problem */
        // }

        // nframes_written = PaUtil_WriteRingBuffer(
        //     pa_ringbuf, &(linbufJACK[0]), nframes);
        // if( nframes_written != nframes) {
        //     /* FIXME, report overflow */
        // }

        return 0;
    } // end while(1) julia processing loop


    sleep (-1);

    /* this is never reached but if the program
        had some other way to exit besides being killed,
        they would be important to call.
    */

    jack_client_close (client);
    exit (0);
}
