#include "config.h"
#include "command.h"
#include "result.h"
#include "main.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/signalfd.h>

extern char **environ;

int command_parse(struct command *c, json_t *obj) {
	const char *key;
	json_t *value;
	json_t *str_value;
	size_t index;

	// Set default values
	memset(c, 0, sizeof(*c));

	json_object_foreach(obj, key, value) {
		if (strcmp(key, "id") == 0 && json_is_string(value)) {
			c->id = json_string_value(value);
		} else if (strcmp(key, "command") == 0 && json_is_string(value)) {
			c->command = json_string_value(value);
		} else if (strcmp(key, "args") == 0 && json_is_array(value)) {
			// Include 2 extra args for argv[0] and NULL
			c->args = calloc(json_array_size(value) + 2, sizeof(*c->args));
			c->arg_num += 1;
			json_array_foreach(value, index, str_value) {
				if (json_is_string(str_value)) {
					c->args[c->arg_num++] = json_string_value(str_value);
				}
			}
		} else if (strcmp(key, "stdin") == 0 && json_is_string(value)) {
			c->stdin_size = json_string_length(value);
			c->stdin_buffer = json_string_value(value);
		} else if (strcmp(key, "outputBufferSize") == 0 && json_is_integer(value)) {
			c->output_size = json_integer_value(value);
		} else if (strcmp(key, "waitTimeMs") == 0 && json_is_integer(value)) {
			c->wait_time_ms = json_integer_value(value);
		}
	}

	if (!c->command) {
		return -1;
	}
	if (!c->args) {
		c->args = calloc(2, sizeof(*c->args));
	}
	if (!c->output_size) {
		c->output_size = 1*1024*1024; // Default output buffer size is 1 MB.
	}
	if (!c->wait_time_ms) {
		c->wait_time_ms = 10*1000; // Default wait time is 10 sec.
	}
	c->args[0] = c->command;
	return 0;
}

static int tv_diff_ms(struct timeval *start) {
        struct timeval now;

	gettimeofday(&now, NULL);

	return (now.tv_sec - start->tv_sec)*1000 + (now.tv_usec - start->tv_usec)/1000;
}

static struct result *manage_process(struct command *c, pid_t pid, int stdinfd, int stdoutfd, int stderrfd, int sfd) {
	struct result *res;
	int status = 0;
	ssize_t len;
	int max_len = c->output_size;
	int timeout;
	int ready;
	int epfd;
	struct epoll_event ev;
	struct timeval start_time;
	struct signalfd_siginfo siginfo;
	size_t stdin_written = 0;

	gettimeofday(&start_time, NULL);
	res = result_new(c->output_size);
	if (!res) {
		goto error;
	}

	printf("managing pid %d\n", pid);

	err_fatal(epfd = epoll_create1(0));

	ev.events = EPOLLOUT;
	ev.data.fd = stdinfd;
	err_fatal(epoll_ctl(epfd, EPOLL_CTL_ADD, stdinfd, &ev));

	ev.events = EPOLLIN;
	ev.data.fd = stdoutfd;
	err_fatal(epoll_ctl(epfd, EPOLL_CTL_ADD, stdoutfd, &ev));

	ev.events = EPOLLIN;
	ev.data.fd = stderrfd;
	err_fatal(epoll_ctl(epfd, EPOLL_CTL_ADD, stderrfd, &ev));

	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	err_fatal(epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev));

	while (true) { // Only two ways out of it: timeout or SIGCHILD
		timeout = c->wait_time_ms - tv_diff_ms(&start_time);
		if (timeout <= 0) {
			kill(pid, SIGKILL);
			break;
		}
		ready = epoll_wait(epfd, &ev, 1, timeout);
		if (ready == -1 && errno == EINTR) {
			continue;
		}
		// Exit on error
		err_fatal(ready);

		if (ready == 0) { // timeout occured
			continue;
		}

		if (ev.data.fd == stdinfd) {
			len = write(stdinfd, c->stdin_buffer + stdin_written, c->stdin_size - stdin_written);

			if (len > 0) {
				stdin_written += len;
			}
			if (len <= 0 || stdin_written == c->stdin_size) {
				err_fatal(epoll_ctl(epfd, EPOLL_CTL_DEL, stdinfd, NULL));
				err_fatal(close(stdinfd));
			}
		}

		if (ev.data.fd == stdoutfd) {
			len = read(stdoutfd, res->stdout_buffer + res->stdout_size, max_len - res->stdout_size);
			if (len <= 0) {
				err_fatal(epoll_ctl(epfd, EPOLL_CTL_DEL, stdoutfd, NULL));
				continue;
			}
			res->stdout_size += len;
		}

		if (ev.data.fd == stderrfd) {
			len = read(stderrfd, res->stderr_buffer + res->stderr_size, max_len - res->stderr_size);
			if (len <= 0) {
				err_fatal(epoll_ctl(epfd, EPOLL_CTL_DEL, stderrfd, NULL));
				continue;
			}
			res->stderr_size += len;
		}

		if (ev.data.fd == sfd) {
			len = read(sfd, &siginfo, sizeof(siginfo));
			if (len <= 0) {
				err_fatal(epoll_ctl(epfd, EPOLL_CTL_DEL, stderrfd, NULL));
				continue;
			}
			if (siginfo.ssi_pid == pid) {
				break;
			}
			fprintf(stderr, PACKAGE_NAME ": stray SIGCHILD for PID %d, reaping possible zombie\n", siginfo.ssi_pid);
			waitpid(siginfo.ssi_pid, NULL, WNOHANG);
		}

	}

	err_fatal(close(epfd));
	err_fatal(waitpid(pid, &res->return_code, 0));
	res->duration_ms = tv_diff_ms(&start_time);
	res->id = c->id;

	result_print(res);
error:
	if (stdin_written != c->stdin_size) {
		err_fatal(close(stdinfd));
	}
	err_fatal(close(stdoutfd));
	err_fatal(close(stderrfd));

	return res;
}

void command_print(struct command *c) {
	int i;

	printf("--- Command ---\n");
	printf("\tId:          %s\n", c->id);
	printf("\tDaemon mode: %d\n", c->daemon_mode);
	printf("\tCmd:         %s\n", c->command);
	printf("\tStdin[%zu]:  ", c->stdin_size);
	for (i = 0; i < c->stdin_size; i += 1) {
		printf("%02x", c->stdin_buffer[i]);
	}
	printf("\n");
	for (i = 0; i < c->arg_num; i++) {
		printf("\tArg[%d]: %s\n", i, c->args[i]);
	}
	printf("\tWait time: %d\n", c->wait_time_ms);
	printf("\tOutput size: %zu\n", c->output_size);
}

//static inline void close_pipe_parent(int fdpair[2]) {
//	err_fatal(close(fdpair[1]));
//}

static inline void fd_pair_close(int fd[2]) {
	err_fatal(close(fd[0])); // close parent end
	err_fatal(close(fd[1])); // close child end
}

static inline void fd_pair_child(int fd[2], int dst_fd) {
	int local = 1;
	int remote = 0;

	if (dst_fd == STDIN_FILENO) {
		local = 0;
		remote = 1;
	}

	err_fatal(close(fd[remote])); // close parent end
	if (fd[local] != dst_fd) {
		err_fatal(dup2(fd[local], dst_fd)); // remap child end
		err_fatal(close(fd[local]));        // close child end duplicate
	}
}

#define READ_FD 0
#define WRITE_FD 1
struct result *command_execute(struct command *c) {
	int status;
	pid_t pid;
	int stdinfd[2];
	int stdoutfd[2];
	int stderrfd[2];
	int sfd;
	sigset_t mask;
	struct result *res;

	err_fatal(pipe(stdinfd));
	err_fatal(pipe(stdoutfd));
	err_fatal(pipe(stderrfd));

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	err_fatal(sigprocmask(SIG_BLOCK, &mask, NULL));
	err_fatal(sfd = signalfd(-1, &mask, 0));

	pid = fork();
	if (pid == -1) {
		fd_pair_close(stdinfd);
		fd_pair_close(stdoutfd);
		fd_pair_close(stderrfd);
		
		printf("Error in fork()\n");
		return NULL;
	}
	if (pid == 0) {	// Child case
		fd_pair_child(stdinfd, STDIN_FILENO);
		fd_pair_child(stdoutfd, STDOUT_FILENO);
		fd_pair_child(stderrfd, STDERR_FILENO);

		err_fatal(execve(c->command, (char * const *)c->args, environ));
		// This point is never reached, err_fatal exits in case of
		// execve failure.
	}
	// Parent case
	err_fatal(close(stdinfd[READ_FD]));
	err_fatal(close(stdoutfd[WRITE_FD]));
	err_fatal(close(stderrfd[WRITE_FD]));
	// manage_process is responsible for closing stdin/out/err descriptors
	res = manage_process(c, pid, stdinfd[WRITE_FD], stdoutfd[READ_FD], stderrfd[READ_FD], sfd);
	err_fatal(close(sfd));
	err_fatal(sigprocmask(SIG_UNBLOCK, &mask, NULL));
	
	return res;
}

void command_free(struct command *c) {
	free(c->args);
}
