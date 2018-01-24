/** @file tw.c
 *
 * @brief This simple client demonstrates the basic features of JACK
 * as they would be used by many applications.
 */

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
// getopt needs to be included manually b/c we compile -    std=c99
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

// libraries/code that require building/linking
// #include <pthread.h>
#include <jack/jack.h>
#include <julia.h>
JULIA_DEFINE_FAST_TLS() // only define this once, in an executable (not in a shared library) if you want fast code.


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

jack_port_t *input_port;
jack_port_t *output_port;
jack_client_t *client;

/* a simple state machine for this client */
volatile enum {
	Init,
	Run,
	Exit
} client_state = Init;

static void signal_handler(int sig) {
    sig=sig;
    jack_client_close(client);
    fprintf(stderr, "signal received, exiting ...\n");
    exit(0);
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 * This client follows a simple rule: when the JACK transport is
 * running, copy the input port to the output.  When it stops, exit.
 */
static int _process (jack_nframes_t nframes) {
    jack_default_audio_sample_t *in, *out;
    jack_transport_state_t ts = jack_transport_query(client, NULL);

    if (ts == JackTransportRolling) {

        if (client_state == Init)
            client_state = Run;

        in = jack_port_get_buffer (input_port, nframes);
        out = jack_port_get_buffer (output_port, nframes);
        memcpy (out, in,
            sizeof (jack_default_audio_sample_t) * nframes);

    } else if (ts == JackTransportStopped) {

        if (client_state == Run) {
            client_state = Exit;
            return -1;  // to stop the thread
        }
    }

    return 0;
}

static void* jack_thread(void *arg) {
    jack_client_t* client = (jack_client_t*) arg;

    while (1) {

        jack_nframes_t frames = jack_cycle_wait (client);
        int status = _process(frames);
        jack_cycle_signal (client, status);

        /*
            Possibly do something else after signaling next clients in the graph
        */

        /* End condition */
        if (status != 0)
            return 0;
	}

    /* not reached*/
	return 0;
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
static void jack_shutdown (void *arg)
{
    arg=arg;
    fprintf(stderr, "JACK shut down, exiting ...\n");
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


int main (int argc, char *argv[]) {
    // const char **ports;
    // const char *client_name;
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
        fprintf (stderr, "unique name `%s' assigned\n", jackname);
    }

    /* tell the JACK server to call `process()' whenever
        there is work to be done.
    */
    if (jack_set_process_thread(client, jack_thread, client) < 0)
        exit(1);

    /* tell the JACK server to call `jack_shutdown()' if
        it ever shuts down, either entirely, or if it
        just decides to stop calling us.
    */
    jack_on_shutdown (client, jack_shutdown, 0);

    /* display the current sample rate. */
    printf ("engine sample rate: %" PRIu32 "\n",
        jack_get_sample_rate (client));

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

    if ((input_port == NULL) || (output_port == NULL)) {
        fprintf(stderr, "no more JACK ports available\n");
        exit (1);
    }

    /* Tell the JACK server that we are ready to roll.  Our
        * process() callback will start running now. */

    if (jack_activate (client)) {
        fprintf (stderr, "cannot activate client");
        exit (1);
    }

    /* Connect the ports.  You can't do this before the client is
        * activated, because we can't make connections to clients
        * that aren't running.  Note the confusing (but necessary)
        * orientation of the driver backend ports: playback ports are
        * "input" to the backend, and capture ports are "output" from
        * it.
        */


    // if (jack_connect (client, ports[0], jack_port_name (input_port))) {
    //     fprintf (stderr, "cannot connect input ports\n");
    // }

    /* install a signal handler to properly quits jack client */
    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);

    /* keep running until the transport stops */

    while (client_state != Exit) {
        sleep (1);
    }

    jack_client_close (client);
    exit (0);
}
