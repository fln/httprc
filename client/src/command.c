#include "config.h"
#include "command.h"
#include "result.h"
#include "log.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/signalfd.h>

extern char **environ;

struct command *command_parse(json_t *obj) {
	struct command *c;
	json_t *args_blob = NULL;
	json_t *environ_blob = NULL;
	json_t *value;
	size_t index;
	const char *key;
	json_error_t error;

	// Set default values
	c = calloc(1, sizeof(*c));
	if (!c) {
		return NULL;
	}

	if (json_unpack_ex(obj, &error, 0,
		"{s:s, s:s, s?:o, s?:s, s?:s%, s?:o, s?:b, s?:i, s?:i}",
		"id", &c->id,
		"command", &c->command,
		"args", &args_blob,
		"directory", &c->directory,
		"stdin", &c->stdin_buffer, &c->stdin_size,
		"environment", &environ_blob,
		"cleanEnvironment", &c->clean_environ,
		"outputBufferSize", &c->output_size,
		"waitTimeMs", &c->wait_time_ms
	)) {
		fprintf(stderr, PACKAGE_NAME ": jansson: %s\n", error.text);
		goto error;
	}

	if (args_blob && json_is_array(args_blob)) {
		// Include 2 extra args for argv[0] and NULL
		c->args = calloc(json_array_size(args_blob) + 2, sizeof(*c->args));
		c->arg_num += 1;
		json_array_foreach(args_blob, index, value) {
			if (json_is_string(value)) {
				c->args[c->arg_num++] = json_string_value(value);
			}
		}
	}

	if (environ_blob && json_is_object(environ_blob) && json_object_size(environ_blob) > 0) {
		c->environ_keys = calloc(json_object_size(environ_blob), sizeof(*c->environ_keys));
		c->environ_vals = calloc(json_object_size(environ_blob), sizeof(*c->environ_vals));
		json_object_foreach(environ_blob, key, value) {
			if (json_is_string(value)) {
				c->environ_keys[c->environ_num] = key;
				c->environ_vals[c->environ_num] = json_string_value(value);
				c->environ_num += 1;
			}
		}
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
	return c;
error:
	command_free(c);
	return NULL;
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

	die_std_error(epfd = epoll_create1(0));

	memset(&ev, 0, sizeof(ev));

	ev.events = EPOLLOUT;
	ev.data.fd = stdinfd;
	die_std_error(epoll_ctl(epfd, EPOLL_CTL_ADD, stdinfd, &ev));

	ev.events = EPOLLIN;
	ev.data.fd = stdoutfd;
	die_std_error(epoll_ctl(epfd, EPOLL_CTL_ADD, stdoutfd, &ev));

	ev.events = EPOLLIN;
	ev.data.fd = stderrfd;
	die_std_error(epoll_ctl(epfd, EPOLL_CTL_ADD, stderrfd, &ev));

	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	die_std_error(epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev));

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
		die_std_error(ready);

		if (ready == 0) { // timeout occured
			continue;
		}

		if (ev.data.fd == stdinfd) {
			len = write(stdinfd, c->stdin_buffer + stdin_written, c->stdin_size - stdin_written);

			if (len > 0) {
				stdin_written += len;
			}
			if (len <= 0 || stdin_written == c->stdin_size) {
				die_std_error(epoll_ctl(epfd, EPOLL_CTL_DEL, stdinfd, NULL));
				die_std_error(close(stdinfd));
			}
		}

		if (ev.data.fd == stdoutfd) {
			len = read(stdoutfd, res->stdout_buffer + res->stdout_size, max_len - res->stdout_size);
			if (len <= 0) {
				die_std_error(epoll_ctl(epfd, EPOLL_CTL_DEL, stdoutfd, NULL));
				continue;
			}
			res->stdout_size += len;
		}

		if (ev.data.fd == stderrfd) {
			len = read(stderrfd, res->stderr_buffer + res->stderr_size, max_len - res->stderr_size);
			if (len <= 0) {
				die_std_error(epoll_ctl(epfd, EPOLL_CTL_DEL, stderrfd, NULL));
				continue;
			}
			res->stderr_size += len;
		}

		if (ev.data.fd == sfd) {
			len = read(sfd, &siginfo, sizeof(siginfo));
			if (len <= 0) {
				die_std_error(epoll_ctl(epfd, EPOLL_CTL_DEL, stderrfd, NULL));
				continue;
			}
			if (siginfo.ssi_pid == pid) {
				break;
			}
			fprintf(stderr, PACKAGE_NAME ": stray SIGCHILD for PID %d, reaping possible zombie\n", siginfo.ssi_pid);
			waitpid(siginfo.ssi_pid, NULL, WNOHANG);
		}

	}

	die_std_error(close(epfd));
	die_std_error(waitpid(pid, &res->return_code, 0));
	res->duration_ms = tv_diff_ms(&start_time);
	res->id = c->id;
error:
	if (stdin_written != c->stdin_size) {
		die_std_error(close(stdinfd));
	}
	die_std_error(close(stdoutfd));
	die_std_error(close(stderrfd));

	return res;
}

void command_print(struct command *c) {
	int i;

	printf("--- Command ---\n");
	printf("\tId:          %s\n", c->id);
	printf("\tCmd:         %s\n", c->command);
	printf("\tDirectory:   %s\n", c->directory);
	for (i = 0; i < c->arg_num; i++) {
		printf("\tArg[%d]:      %s\n", i, c->args[i]);
	}
	for (i = 0; i < c->environ_num; i++) {
		printf("\tEnvironment: %s=%s\n", c->environ_keys[i], c->environ_vals[i]);
	}
	printf("\tClean env.:  %s\n", c->clean_environ ? "true" : "false");
	printf("\tStdin[%zu]:   0x", c->stdin_size);
	for (i = 0; i < c->stdin_size; i += 1) {
		printf("%02x", c->stdin_buffer[i]);
	}
	printf("\n");
	printf("\tWait time:   %d ms\n", c->wait_time_ms);
	printf("\tOutput size: %zu bytes\n", c->output_size);
}

static inline void fd_pair_close(int fd[2]) {
	die_std_error(close(fd[0])); // close parent end
	die_std_error(close(fd[1])); // close child end
}

static inline void fd_pair_child(int fd[2], int dst_fd) {
	int local = 1;
	int remote = 0;

	if (dst_fd == STDIN_FILENO) {
		local = 0;
		remote = 1;
	}

	die_std_error(close(fd[remote])); // close parent end
	if (fd[local] != dst_fd) {
		die_std_error(dup2(fd[local], dst_fd)); // remap child end
		die_std_error(close(fd[local]));        // close child end duplicate
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
	struct result *res = NULL;
	int i;

	die_std_error(pipe(stdinfd));
	die_std_error(pipe(stdoutfd));
	die_std_error(pipe(stderrfd));

	die_std_error(sigemptyset(&mask));
	die_std_error(sigaddset(&mask, SIGCHLD));
	die_std_error(sigprocmask(SIG_BLOCK, &mask, NULL));
	die_std_error(sfd = signalfd(-1, &mask, 0));

	log_std_error(pid = fork());
	switch (pid) {
	case -1: //Error case
		fd_pair_close(stdinfd);
		fd_pair_close(stdoutfd);
		fd_pair_close(stderrfd);
		return NULL;
	case 0:	// Child case
		fd_pair_child(stdinfd, STDIN_FILENO);
		fd_pair_child(stdoutfd, STDOUT_FILENO);
		fd_pair_child(stderrfd, STDERR_FILENO);

		if (c->directory) {
			die_std_error(chdir(c->directory));
		}

		if (c->clean_environ) {
			clearenv();
		}

		for (i = 0; i < c->environ_num; i += 1) {
			die_std_error(setenv(c->environ_keys[i], c->environ_vals[i], 1));
		}

		die_std_error(execve(c->command, (char * const *)c->args, environ));
		// This point is never reached, die_std_error exits in case of
		// execve failure.
		exit(EXIT_FAILURE);
	default: // Parent case
		die_std_error(close(stdinfd[READ_FD]));
		die_std_error(close(stdoutfd[WRITE_FD]));
		die_std_error(close(stderrfd[WRITE_FD]));
		// manage_process is responsible for closing stdin/out/err descriptors
		res = manage_process(c, pid, stdinfd[WRITE_FD], stdoutfd[READ_FD], stderrfd[READ_FD], sfd);
		die_std_error(close(sfd));
		die_std_error(sigprocmask(SIG_UNBLOCK, &mask, NULL));
	}
	
	return res;
}

void command_free(struct command *c) {
	if (c) {
		free(c->args);
		free(c->environ_keys);
		free(c->environ_vals);
		free(c);
	}
}
