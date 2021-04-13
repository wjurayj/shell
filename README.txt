The shell_1 portion of this file is organized as follows:
The main function of this program is just an infinite loop that keeps calling
read_input(). read_input() reads input from the user, splits it by whitespace
using parse, and then calls redirect(), which handles most of the work.
redirect() cleans the input of redirect symbols and their files, and then calls
builtin_handler() and executable_handler() (if the input's not a built in).

The program's organization is very similar to that of shell_1. There is now a global
variable job_list that keeps track of the jobs in the background. The only additional
function is reap, which I call at the beginning of every repl cycle, before I print
(or don't print) the prompt. This function handles the reaping for jobs in the
job list. I also added fg and bg to the builtin handler's functionality, and added
some code in executable_handler to deal with background processes.
As far as I know, there are no bugs in this program.
To compile this program, just make sure the Makefile's in the same folder, and
run the command "make" in the terminal.
I collaborated with Peter Harvie and Toly Brevnov on this project.
