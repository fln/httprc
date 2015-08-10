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

static struct result *manage_process(struct command *c, pid_t pid, int stdoutfd, int stderrfd, int sfd) {
	struct result *res;
	int status = 0;
	ssize_t len;
	int max_len = c->output_size;
	int timeout;
	int ready;
	int epfd;
	struct epoll_event ev;
	int pending_fd = 0;
	struct timeval start_time;
	struct signalfd_siginfo siginfo;

	gettimeofday(&start_time, NULL);
	res = result_new(c->output_size);
	if (!res) {
		goto error;
	}

	epfd = err_filter(epoll_create1(0), "epoll_create1");

	ev.events = EPOLLIN;
	ev.data.fd = stdoutfd;
	err_filter(epoll_ctl(epfd, EPOLL_CTL_ADD, stdoutfd, &ev), "epoll_ctl");
	pending_fd += 1;

	ev.events = EPOLLIN;
	ev.data.fd = stderrfd;
	err_filter(epoll_ctl(epfd, EPOLL_CTL_ADD, stderrfd, &ev), "epoll_ctl");
	pending_fd += 1;

	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	err_filter(epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev), "epoll_ctl");
	pending_fd += 1;

	while (pending_fd > 0) {
		timeout = c->wait_time_ms - tv_diff_ms(&start_time);
		if (timeout <= 0) {
			kill(pid, SIGKILL);
			timeout = -1;
		}
		printf("Timeout ms: %d\n", timeout);
		ready = epoll_wait(epfd, &ev, 1, timeout);
		printf("ready = %d, fd = %d\n", ready, ev.data.fd);
		if (ready == -1 && errno == EINTR) {
			continue;
		}
		// Exit on error
		err_filter(ready, "epoll_wait");

		if (ready == 0) { // timeout occured
			continue;
		}

		if (ev.data.fd == stdoutfd) {
			len = read(stdoutfd, res->stdout_buffer + res->stdout_size, max_len - res->stdout_size);
			if (len <= 0) {
				err_filter(epoll_ctl(epfd, EPOLL_CTL_DEL, stdoutfd, NULL), "epoll_ctl");
				pending_fd -= 1;
				continue;
			}
			res->stdout_size += len;
		}

		if (ev.data.fd == stderrfd) {
			len = read(stderrfd, res->stderr_buffer + res->stderr_size, max_len - res->stderr_size);
			if (len <= 0) {
				err_filter(epoll_ctl(epfd, EPOLL_CTL_DEL, stderrfd, NULL), "epoll_ctl");
				pending_fd -= 1;
				continue;
			}
			res->stderr_size += len;
		}

		if (ev.data.fd == sfd) {
			len = read(sfd, &siginfo, sizeof(siginfo));
			if (len <= 0) {
				continue;
				//err_filter(epoll_ctl(epfd, EPOLL_CTL_DEL, stderrfd, NULL), "epoll_ctl");
				//pending_fd -= 1;
				//continue;
			}
			if (siginfo.ssi_pid == pid) {
				break;
			}
			// lost SIGCHILD signal?
			waitpid(siginfo.ssi_pid, NULL, WNOHANG);
		}

	}

	err_filter(close(epfd), "close");

	//err_filter(close(stdoutfd), "close");
	//err_filter(close(stderrfd), "close");
	waitpid(pid, &res->return_code, 0);
	if (timeout <= 0) {
		printf("Exited loop after timeout\n");
	} else {
		printf("Child terminated on its own, timeout = %d\n", timeout);
	}
	//waitpid(pid, &res->return_code, 0);
	printf("Child RC: %d\n", res->return_code);
	printf("Std out[%zu]: %.*s\n", res->stdout_size, (int)res->stdout_size, res->stdout_buffer);
	printf("Std err[%zu]: %.*s\n", res->stderr_size, (int)res->stderr_size, res->stderr_buffer);
error:
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

struct result *command_execute(struct command *c) {
	int status;
	pid_t pid;
	int stdoutfd[2];
	int stderrfd[2];
	int sfd;
	sigset_t mask;
	struct result *res;

	err_filter(pipe(stdoutfd), "pipe");
	err_filter(pipe(stderrfd), "pipe");

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	err_filter(sigprocmask(SIG_BLOCK, &mask, NULL), "sigprocmask");
	sfd = err_filter(signalfd(-1, &mask, 0), "signalfd");

	pid = fork();
	if (pid == -1) {
		err_filter(close(stdoutfd[0]), "close");
		err_filter(close(stdoutfd[1]), "close");
		err_filter(close(stderrfd[0]), "close");
		err_filter(close(stderrfd[1]), "close");
		
		printf("Error in fork()\n");
		return NULL;
	}
	if (pid == 0) {	// Child case
		err_filter(close(stdoutfd[0]), "close");
		err_filter(dup2(stdoutfd[1], STDOUT_FILENO), "dup2");
		err_filter(close(stdoutfd[1]), "close");

		err_filter(close(stderrfd[0]), "close");
		err_filter(dup2(stderrfd[1], STDERR_FILENO), "dup2");
		err_filter(close(stderrfd[1]), "close");

		err_filter(execve(c->command, (char * const *)c->args, environ), "execve");
		// This point is never reached, err_filter exits in execve failure.
	}
	// Parent case
	err_filter(close(stdoutfd[1]), "close");
	err_filter(close(stderrfd[1]), "close");
	res = manage_process(c, pid, stdoutfd[0], stderrfd[0], sfd);	
	err_filter(close(stdoutfd[0]), "close");
	err_filter(close(stderrfd[0]), "close");
	err_filter(close(sfd), "close");
	return res;
}

void command_free(struct command *c) {
	free(c->args);
}
