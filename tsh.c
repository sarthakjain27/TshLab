
/*
 * Name: Sarthak Jain
 * Andrew ID: sarthak3
 * tsh - A tiny shell program with job control and I/O redirection.
 * <The line above is not a sufficient documentation.
 *  You will need to write your program documentation.
 *  Follow the 15-213/18-213/15-513 style guide at
 *  http://www.cs.cmu.edu/~213/codeStyle.html.>
 */

#include "tsh_helper.h"
#if 0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include "csapp.h"
#endif


/*
 * If DEBUG is defined, enable contracts and printing on dbg_printf.
 */
#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Function prototypes */
void eval(const char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);
void runJob_bckgrnd(struct cmdline_tokens token);
void runJob_foregrnd(struct cmdline_tokens token);

/*
 * <Write main's function header documentation. What does main do?>
 * "Each function should be prefaced with a comment describing the purpose
 *  of the function (in a sentence or two), the function's arguments and
 *  return value, any error cases that are relevant to the caller,
 *  any pertinent side effects, and any assumptions that the function makes."
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE_TSH];  // Cmdline for fgets
    bool emit_prompt = true;    // Emit prompt (default)
    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
    Dup2(STDOUT_FILENO, STDERR_FILENO);

    // Parse the command line
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':                   // Prints help message
            usage();
            break;
        case 'v':                   // Emits additional diagnostic info
            verbose = true;
            break;
        case 'p':                   // Disables prompt printing
            emit_prompt = false;
            break;
        default:
            usage();
        }
    }

    // Create environment variable
    if (putenv("MY_ENV=42") < 0) {
        perror("putenv");
        exit(1);
    }


    // Install the signal handlers
    Signal(SIGINT,  sigint_handler);   // Handles ctrl-c
    Signal(SIGTSTP, sigtstp_handler);  // Handles ctrl-z
    Signal(SIGCHLD, sigchld_handler);  // Handles terminated or stopped child

    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    Signal(SIGQUIT, sigquit_handler);

    // Initialize the job list
    init_job_list();

    // Execute the shell's read/eval loop
    while (true) {
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE_TSH, stdin) == NULL) && ferror(stdin)) {
            app_error("fgets error");
        }

        if (feof(stdin)) {
            // End of file (ctrl-d)
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            return 0;
        }

        // Remove the trailing newline
        cmdline[strlen(cmdline)-1] = '\0';

        // Evaluate the command line
        eval(cmdline);

        fflush(stdout);
    }

    return -1; // control never reaches here
}


/* Handy guide for eval:
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg),
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.
 * Note: each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */

/*
 * <What does eval do?>
 */
void eval(const char *cmdline) {
    sigset_t newMask,oldMask;
	parseline_return parse_result;
    struct cmdline_tokens token;
	int output_file_flag,input_file_flag;
    if(sigemptyset(&newMask)!=0)
		unix_error("Error in sigemmptyset\n");
	if(sigemptyset(&oldMask)!=0)
		unix_error("Error in sigemptyset of oldMask\n");
	if(sigaddset(&newMask,SIGCHLD)!=0)
		unix_error("Error in sigaddset of SIGCHLD\n");
	if(sigaddset(&newMask,SIGINT)!=0)
		unix_error("Error in sigaddset of SIGTSTP\n");
	if(sigaddset(&newMask,SIGTSTP)!=0)
		unix_error("Error in sigaddset of SIGINT\n");
	if(sigprocmask(SIG_BLOCK,&newMask,&oldMask)!=0)
		unix_error("Error in sigprocmask \n");
    
	// Parse command line
    parse_result = parseline(cmdline, &token);
    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY) {
        return;
    }
	
	//checking for builtin quit command as input command
	if(token.builtin==BUILTIN_QUIT)
		exit(0);
	
	output_file_flag=open(token.outfile,O_WRONLY|O_CREAT,0x777);
	input_file_flag=open(token.infile,O_RDONLY|O_CREAT,0x777);
	
	if(token.infile!=NULL && input_file_flag<0)
		unix_error("Error in opening input file\n");
	
	//checking for builtin jobs command as input command along with if we have redirection file give or not
	//by default output to stdout
	if(token.builtin==BUILTIN_JOBS)
	{
		if(token.outfile!=NULL)
		{
			if(output_file_flag<0)
			{
				unix_error("Error in opening file");
				return;
			}
			else
				list_jobs(output_file_flag);
			close(output_file_flag);
		}
		else
			list_jobs(1);		
		return;
	}
	
	//checking for builtin bg job command
	if(token.builtin==BUILTIN_BG)
	{
		runJob_bckgrnd(token);	
		return;
	}
	
	//checking for builtin fg job command
	if(token.builtin==BUILTIN_FG)
	{
		runJob_foregrnd(token);
		return;
	}

	//from here consider every command to be an executable path.
	//Fork a child. And if & then run the command in background.
	//Otherwise suspend parent to wait for child to complete and to be notified by SIGCHLD signal.
	pid_t newP=fork();
	if(newP<0)
		unix_error("Unable to fork");
	if(newP==0)
	{
		if(sigprocmask(SIG_UNBLOCK,&newMask,NULL)!=0)
			unix_error("Error in sig_setmask of child\n");	
		
		//change process group according to hint given. To avoid SIGINT to be sent to every proess created.
		if(setpgid(0,0)!=0)
			unix_error("Error in changing process group of child\n");
		
		//if output file given, make stdout a copy of output file. So that every write to stdout goes to the file descriptor
		if(output_file_flag>0)
		{
			if((dup2(output_file_flag,STDOUT_FILENO))!=-1)
				close(output_file_flag);
			else
				unix_error("Error in dup2 of outfile\n");
		}

		//if input file given, make stding a copy of input file. So that every input from stdin gets content from provided input file
		if(input_file_flag>0)
		{
			if((dup2(input_file_flag,STDIN_FILENO))!=-1)
				close(input_file_flag);
			else
				unix_error("Error in dup2 of infile\n");
		}
		if(execve(token.argv[0],token.argv,environ)<0){
			unix_error("Command not found");
		}
		exit(0);
	}
	
	//adding the newly created job with appropriate state
	if(parse_result==PARSELINE_FG)
	{
		if(!(add_job(newP,FG,cmdline)))
			unix_error("Error in adding new job\n");
	}
	
	else 
	{
		if(!(add_job(newP,BG,cmdline)))
			unix_error("Error in adding new job\n");
	}
	
	struct job_t *newJob=find_job_with_pid(newP);
	if(parse_result==PARSELINE_FG)
	{
		sigemptyset(&oldMask);
		while(fg_pid()!=0 && get_state_of_job(newJob)==FG)
			sigsuspend(&oldMask);		
	}
	else	printf("[%d] (%d) %s\n",get_jid_of_job(newJob),get_pid_of_job(newJob),cmdline);
	sigprocmask(SIG_SETMASK,&oldMask,NULL);
	return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * A SIGCHLD Signal is sent to the kernel/parent whenever a child process terminates.
 * The termination can be normally or via receiving some signals such as SIGSTOP,
 * SIGTSTP, SIGTTIN OR SIGTTOU signal.
 * On receiving the SIGCHLD signal, the parent/kernel should reap the newly created
 * zombie process.
 * The parent/kernel would only reap the terminated children and would not wait for the 
 * currently running children to terminate.
 * */
void sigchld_handler(int sig) {
	int olderrno=errno;
	pid_t term_child_pid;
	int term_child_jid=0,status=0;
	sigset_t mask,prev_mask;
	sigfillset(&mask);
	while((term_child_pid=waitpid(-1,&status,WNOHANG|WUNTRACED))>0)
	{
		sigprocmask(SIG_BLOCK,&mask,&prev_mask);	
		term_child_jid=find_jid_by_pid(term_child_pid);
		//Case 1: If child terminated normally by itself
		if(WIFEXITED(status))
			delete_job(term_child_pid);
		//Case 2: if child terminated because fo receiving a signal. 
		//We need to print pid,jid and the signal number which stopped the job
		else if(WIFSIGNALED(status))
		{
			printf("Job [%d] (%d) terminated by signal %d \n",term_child_jid,term_child_pid,WTERMSIG(status));
			delete_job(term_child_pid);
		}
		//CASE 3: If child is stopped by the sginal.
		//We need to update the status of the job to ST and not delete from job list
		else if(WIFSTOPPED(status))
		{
			printf("Job [%d] (%d) stopped by signal %d \n",term_child_jid,term_child_pid,WSTOPSIG(status));
			set_state_of_job(find_job_with_pid(term_child_pid),ST);
		}
		sigprocmask(SIG_SETMASK,&prev_mask,NULL);
	}
	errno=olderrno;
    return;
}

/*
 * Whenever a user has pressed Ctrl+C at the keyboard, it sends a SIGINT Signal.
 * We need to catch that signal and handle it so that it only terminates the foreground running job.
 */
void sigint_handler(int sig) {
	int olderrno=errno;
	sigset_t oldMask,prevMask;
	sigfillset(&oldMask);
	sigprocmask(SIG_BLOCK,&oldMask,&prevMask);
	pid_t pid_job_fg=fg_pid();
	if(pid_job_fg<=0)
		return;
	if((kill(-pid_job_fg,sig))<0)
		return;
		//unix_error("No job running in foregroung \n");
	sigprocmask(SIG_SETMASK,&prevMask,NULL);
	errno=olderrno;
	return;
}

/*
 * Whenever a user has pressed Ctrl+Z at the keyboard, it sends a SIGTSTP signal.
 * We need to catch theat signal and handle it so that it only suspends the foreground running job.
 */
void sigtstp_handler(int sig) {
	int olderr=errno;
	sigset_t oldMask,prevMask;
	sigfillset(&oldMask);
	sigprocmask(SIG_BLOCK,&oldMask,&prevMask);
	pid_t pid_job_fg=fg_pid();
	if(pid_job_fg<=0)
		return;
	if((kill(-pid_job_fg,sig))<0)
		return;
		//unix_error("No job runnign in foreground \n");
	sigprocmask(SIG_SETMASK,&prevMask,NULL);
	errno=olderr;
	return;
}



/*This function is for built in bg commang. It runs the appropriate process in bg
 * by sending a SIGCONT signal. 
 *
 * Additionally it also updates the necessary flag of the job.
 *
 */
void runJob_bckgrnd(struct cmdline_tokens token)
{
	int jobid;
	pid_t pid;
	struct job_t *related_job;
	int i=0;
	if(token.argv[1]==NULL){
		printf("No PID or JobId given with command");
		return;
	}
	if(token.argv[1][0]=='%')//given argv[1] is JOB ID as it has leading % sign
	{
		for(i=1;i<strlen(token.argv[1]);i++)//start from index 1 to ignore % sign
		{
			if(!isdigit(token.argv[1][i]))
			{
				printf("All digits of Job id and Pid must be numeric");
				return;
			}
		}
		jobid=atoi(token.argv[1]+sizeof(char));//+sizeof(char) because given JobId has % at start. So moving pointer to index 1
		related_job=find_job_with_jid(jobid);
		if(related_job==NULL)
		{
			printf("No job with this JOB id \n");
			return;
		}
		if(kill(-(get_pid_of_job(related_job)),SIGCONT)<0){
			printf("Unable to start the given job in bg \n");
			return;
		}
		printf("[%d] (%d) %s\n",get_jid_of_job(related_job),get_pid_of_job(related_job),get_cmdline_of_job(related_job));
		set_state_of_job(related_job,BG);
	}
	else//given is the PID so start checking argv[1] from 0
	{
		for(i=0;i<strlen(token.argv[1]);i++)
		{
			if(!isdigit(token.argv[1][i]))
			{
				printf("All digits of Job id and Pid must be numeric");
				return;
			}
		}
		pid=atoi(token.argv[1]);
		related_job=find_job_with_pid(pid);
		if(related_job==NULL)
		{
			printf("No job with this  Pid \n");
			return;
		}
		if(kill(-(get_pid_of_job(related_job)),SIGCONT)<0){
			printf("Unable to start the given job in bg \n");
			return;
		}
		printf("[%d] (%d) %s\n",get_jid_of_job(related_job),get_pid_of_job(related_job),get_cmdline_of_job(related_job));
		set_state_of_job(related_job,BG);	
	}
	return;
}

/*
 *This function is for the builtin command fg. It takes Job or PID as input
 *and start the stop by sending SIGCONT signal.
 *
 * It also updates the necessary members of the given job such as state
 */
void runJob_foregrnd(struct cmdline_tokens token)
{
	int jobid;
	pid_t pid;
	struct job_t *related_job;
	int i=0;
	sigset_t mask;
	if(token.argv[1]==NULL){
		printf("No PID or JobId given with command");
		return;
	}
	if(token.argv[1][0]=='%')//given argv[1] is JOB ID as it has leading % sign
	{
		for(i=1;i<strlen(token.argv[1]);i++)//start from index 1 to ignore % sign
		{
			if(!isdigit(token.argv[1][i]))
			{
				printf("All digits of Job id and Pid must be numeric");
				return;
			}
		}
		jobid=atoi(token.argv[1]+sizeof(char));//as argv[1] has leading % in case of given JobId. So pointign to index 1 using +sizeof(char)
		related_job=find_job_with_jid(jobid);
		if(related_job==NULL)
		{
			printf("No job with this JOB id \n");
			return;
		}
		if(kill(-(get_pid_of_job(related_job)),SIGCONT)<0){
			printf("Unable to start the given job in fg \n");
			return;
		}
		set_state_of_job(related_job,FG);
		sigemptyset(&mask);
		while(fg_pid()!=0 && get_state_of_job(related_job)==FG)
			sigsuspend(&mask);
	}
	else//given is the PID so start checking argv[1] from 0
	{
		for(i=0;i<strlen(token.argv[1]);i++)
		{
			if(!isdigit(token.argv[1][i]))
			{
				printf("All digits of Job id and Pid must be numeric");
				return;
			}
		}
		pid=atoi(token.argv[1]);
		related_job=find_job_with_pid(pid);
		if(related_job==NULL)
		{
			printf("No job with this PID id \n");
			return;
		}
		if(kill(-(get_pid_of_job(related_job)),SIGCONT)<0){
			printf("Unable to start the given job in fg \n");
			return;
		}
		set_state_of_job(related_job,FG);
		sigemptyset(&mask);
		while(fg_pid()!=0 && get_state_of_job(related_job)==FG)
			sigsuspend(&mask);
	}
	return;
}
