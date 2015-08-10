#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <argp.h>
#include <jansson.h>
#include <curl/curl.h>

#include "config.h"
#include "main.h"
#include "command.h"
#include "result.h"
#include "response_buf.h"

struct httprcclient {
	char *ca_cert;
	char *client_cert;
	char *client_key;
	char *backend_url;

	bool graceful_exit;

	CURL *curl;	
};

const char *argp_program_version = PACKAGE_STRING;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;
static char args_doc[] = "BACKEND_URL";
static char doc[] =
"Remote command execution periodically check backend URL for commands to \
execute.\vFIXME ?.";

static struct httprcclient *_app;

static struct argp_option options[] = {
	{"ca-cert",     'a', "FILE", 0, "CA certificate to check backend certificate against." },
	{"client-cert", 'c', "FILE", 0, "Client certificate (used to authenticate to the backend server)." },
	{"client-key",  'k', "FILE", 0, "Client private key (used to authenticate to the backend server)." },
	{"backend-url", 'u', "URL",  0, "Backend URL." },
	{ NULL },
};


CURLcode curlc_filter(CURLcode rc, const char *opt) {
	if (rc != CURLE_OK) {
		fprintf(stderr, "error for %s (%d): %s\n", opt, rc, curl_easy_strerror(rc));
	}
	return rc;
}

int err_filter(int rc, const char *func_name) {
	if (rc == -1) {
		fatal_error("%s(...) returned -1\n", func_name);
	}
	return rc;
}

void fatal_error(const char *format, ...) {
	va_list pvar;

	va_start (pvar, format);
	vfprintf(stderr, format, pvar);
	va_end (pvar);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct httprcclient *app;

	app = (struct httprcclient *)state->input;

	switch(key) {
	case 'a':
		app->ca_cert = arg;
		break;
	case 'c':
		app->client_cert = arg;
		break;
	case 'k':
		app->client_key = arg;
		break;
	case ARGP_KEY_ARG:
		app->backend_url = arg;
		break;
	case ARGP_KEY_END:
		if (!app->backend_url) {
			argp_failure(state, 1, 0, "Backend URL is missing");
		}
		if (!app->ca_cert) {
			argp_failure(state, 1, 0, "Option --ca-cert is mandatory.");
		}
		if (!app->client_cert) {
			argp_failure(state, 1, 0, "Option --client-cert is mandatory.");
		}
		if (!app->client_key) {
			argp_failure(state, 1, 0, "Option --client-cert is mandatory.");
		}
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}


static void graceful_exit(int sig) {
	_app->graceful_exit = 1;
}

static void attach_sighandlers(struct httprcclient *app) {
	_app = app;
	signal(SIGINT, graceful_exit);
	signal(SIGQUIT, graceful_exit);
}

int process_response(struct httprcclient *app, struct response_buf *buf, useconds_t *sleep_interval) {
	json_t *task;
	const char *key;
	json_t *value;
	json_error_t error;

	struct command c;
	struct result *res;
	json_int_t sleep_ms = 0;

	memset(&c, 0, sizeof(c));

	//printf("%.*s", (int)buf->size, buf->start);

	task = json_loadb(buf->start, buf->size, JSON_ALLOW_NUL, &error);
	if (!task) {
		printf("jansson error: %s\n", error.text);
		goto end;
	}

	if (!json_is_object(task)) {
		goto end;
	}
	json_object_foreach(task, key, value) {
		if (strcmp(key, "sleepMs") == 0 && json_is_integer(value)) {
			sleep_ms = json_integer_value(value);
			*sleep_interval = sleep_ms * 1000;
		} else if (strcmp(key, "exec") == 0 && json_is_object(value)) {
			command_parse(&c, value);
			command_print(&c);
			res = command_execute(&c);
			command_free(&c);
			result_free(res);
		}
	}

end:
	json_decref(task);
	return 0;
}

void main_loop(struct httprcclient *app) {
	CURLcode res;
	useconds_t sleep_interval;
	struct response_buf *buf;

	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_CAINFO, app->ca_cert), "CURLOPT_CAINFO");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSLCERT, app->client_cert), "CURLOPT_SSLCERT");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSLKEY, app->client_key), "CURLOPT_SSLKEY");

	buf = response_buf_new();
	while (!app->graceful_exit) {
		sleep_interval = 5*1000*1000;
		response_buf_reset(buf);

		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_URL, app->backend_url), "CURLOPT_URL");

		curl_easy_setopt(app->curl, CURLOPT_WRITEDATA, buf);
		curl_easy_setopt(app->curl, CURLOPT_WRITEFUNCTION, response_buf_write_cb);


		res = curl_easy_perform(app->curl);
		printf("CURL result: %d\n", res);

		process_response(app, buf, &sleep_interval);
skip:
		usleep(sleep_interval);
	}
	response_buf_free(buf);
}

int main(int argc, char *argv[]) {
	struct httprcclient app;
	struct argp argp = { options, parse_opt, args_doc, doc };
	int optind;

	memset(&app, 0, sizeof(app));
	argp_parse(&argp, argc, argv, 0, &optind, &app);

	app.curl = curl_easy_init();
	if (!app.curl) {
		fprintf(stderr, "Error allocating libcurl context.\n");
		return -1;
	}

	attach_sighandlers(&app);
	main_loop(&app);
	
	curl_easy_cleanup(app.curl);
	curl_global_cleanup();

	return 0;
}
