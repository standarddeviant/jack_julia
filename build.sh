# a proper makefile would be nice, but this is functional

set -u
# 0.6.2 in the home directory
#export JULIA_DIR="$HOME/julia-d386e40c17/" #bashrc
#export JULIA_HOME="$JULIA_DIR/bin" #bashrc
#export LD_LIBRARY_PATH="$JULIA_DIR/lib:$LD_LIBRARY_PATH" #bashrc
echo "JULIA_DIR = $JULIA_DIR"
echo "JULIA_HOME = $JULIA_HOME"
# echo "LD_LIBRARY_PATH = $LD_LIBRARY_PATH"

# gcc -Wall -Wextra -Wunused                           \

gcc -Wall -Wextra -Wunused                           \
    -o jack_julia -fPIC -I$JULIA_DIR/include/julia   \
    -std=gnu99                                       \
    -L$JULIA_DIR/lib -L$JULIA_DIR/lib/julia          \
    jack_set_process_thread_example.c                \
    -l:libjulia.so.0.7.0                             \
    -l:libstdc++.so.6 -l:libLLVM-3.9.so              \
    -DJULIA_ENABLE_THREADING=1                       \
    -lpthread -ljack -lm                             \



    #  jack_julia.c ./pa_ringbuffer/pa_ringbuffer.c    \