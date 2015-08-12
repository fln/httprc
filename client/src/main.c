#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <argp.h>
#include <jansson.h>
#include <curl/curl.h>

#include "config.h"
#include "command.h"
#include "result.h"
#include "response_buf.h"
#include "log.h"

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
	{ NULL },
};


CURLcode curlc_filter(CURLcode rc, const char *opt) {
	if (rc != CURLE_OK) {
		fprintf(stderr, PACKAGE_NAME ": libcurl: %s (%d): %s\n", opt, rc, curl_easy_strerror(rc));
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
	struct sigaction sa;

	_app = app;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = graceful_exit;
	sa.sa_flags = SA_RESTART;

	die_std_error(sigaction(SIGINT, &sa, NULL));
	die_std_error(sigaction(SIGQUIT, &sa, NULL));
}

size_t null_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
	return size * nmemb;
}

void report_result(struct httprcclient *app, struct result *res) {
	CURLcode rc;
	json_t *result_json;
	char *json_bytes;

	result_json = result_to_json(res);
	if (!result_json) {
		return;
	}

	json_bytes = json_dumps(result_json, JSON_INDENT(2) | JSON_ENSURE_ASCII);
	if (!json_bytes) {
		return;
	}

	printf("--------- TX ---------\n");
	printf("%s\n", json_bytes);


	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_HTTPGET, 0), "CURLOPT_HTTPGET");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POST, 1), "CURLOPT_POST");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POSTFIELDS, json_bytes), "CURLOPT_POSTFIELDS");

	curl_easy_setopt(app->curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(app->curl, CURLOPT_WRITEFUNCTION, null_write_cb);

	rc = curl_easy_perform(app->curl);
	printf("curl rc = %d\n", rc);

	free(json_bytes);
	json_decref(result_json);
}

int process_response(struct httprcclient *app, struct response_buf *buf, useconds_t *sleep_interval) {
	json_t *task;
	const char *key;
	json_t *value;
	json_error_t error;

	struct command c;
	struct result *res;
	json_int_t sleep_ms = 0;

	json_t *result_j;

	memset(&c, 0, sizeof(c));

	task = json_loadb(buf->start, buf->size, JSON_ALLOW_NUL, &error);
	if (!task) {
		fprintf(stderr, PACKAGE_NAME ": jansson: %s\n", error.text);
		goto error_load;
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
			res = command_execute(&c);
			if (res) {
				report_result(app, res);
			}
			command_free(&c);
			result_free(res);
		}
	}

end:
	json_decref(task);
error_load:
	return 0;
}

void main_loop(struct httprcclient *app) {
	CURLcode res;
	useconds_t sleep_interval;
	struct response_buf *buf;

	buf = response_buf_new();

	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_CAINFO, app->ca_cert), "CURLOPT_CAINFO");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSLCERT, app->client_cert), "CURLOPT_SSLCERT");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSLKEY, app->client_key), "CURLOPT_SSLKEY");


	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_URL, app->backend_url), "CURLOPT_URL");
	while (!app->graceful_exit) {
		sleep_interval = 5*1000*1000;
		response_buf_reset(buf);

		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POST, 0), "CURLOPT_POST");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POSTFIELDS, NULL), "CURLOPT_POSTFIELDS");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_HTTPGET, 1), "CURLOPT_HTTPGET");

		curl_easy_setopt(app->curl, CURLOPT_WRITEDATA, buf);
		curl_easy_setopt(app->curl, CURLOPT_WRITEFUNCTION, response_buf_write_cb);

		res = curl_easy_perform(app->curl);
		if (res != CURLE_OK) {
			log_error("libcurl: %s\n", curl_easy_strerror(res));
			goto skip;
		}

		printf("--------- RX ---------\n");
		printf("%.*s\n", (int)buf->size, buf->start);
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
		fprintf(stderr, PACKAGE_NAME ": error allocating libcurl context\n");
		return -1;
	}

	attach_sighandlers(&app);
	main_loop(&app);
	
	curl_easy_cleanup(app.curl);
	curl_global_cleanup();

	return 0;
}
