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

	bool verbose;
	bool graceful_exit;

	CURL *curl;
	char  curl_error_buffer[CURL_ERROR_SIZE];
};

const char *argp_program_version = PACKAGE_STRING;
const char *argp_program_bug_address = "<" PACKAGE_BUGREPORT ">";
static char args_doc[] = "BACKEND_URL";
static char doc[] =
"Service for remote command execution. It periodically checks backend URL for \
commands to execute.";

static struct httprcclient *_app;

static struct argp_option options[] = {
	{"verbose",     'v', 0,      0, "Show received and transmitted JSON blobs."},
	{"server-ca",   'a', "FILE", 0, "CA certificate to check backend certificate against." },
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
	case 'v':
		app->verbose = true;
		break;
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
			break;
		}
		if (strncasecmp(app->backend_url, "https", 5) == 0) {
			if (!app->ca_cert) {
				argp_failure(state, 1, 0, "Option --ca-cert is mandatory for HTTPS protocol.");
			}
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
	struct curl_slist *headers = NULL;

	result_json = result_to_json(res);
	if (!result_json) {
		return;
	}

	json_bytes = json_dumps(result_json, JSON_ENSURE_ASCII);
	if (!json_bytes) {
		return;
	}

	if (app->verbose) {
		printf("TX: %s\n", json_bytes);
	}

	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_HTTPGET, 0), "CURLOPT_HTTPGET");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POST, 1), "CURLOPT_POST");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POSTFIELDS, json_bytes), "CURLOPT_POSTFIELDS");
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_HTTPHEADER, headers), "CURLOPT_HTTPHEADER");

	curl_easy_setopt(app->curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(app->curl, CURLOPT_WRITEFUNCTION, null_write_cb);

	rc = curl_easy_perform(app->curl);
	if (rc != CURLE_OK) {
		log_error("libcurl: %s: %s\n", curl_easy_strerror(rc), app->curl_error_buffer);
	}

	curl_slist_free_all(headers);
	free(json_bytes);
	json_decref(result_json);
}

int process_response(struct httprcclient *app, struct response_buf *buf, useconds_t *sleep_interval) {
	json_t *task_blob;
	json_t *cmd_blob = NULL;
	json_error_t error;

	struct command *c;
	struct result *res;
	json_int_t sleep_ms = 0;

	memset(&c, 0, sizeof(c));

	task_blob = json_loadb(buf->start, buf->size, JSON_ALLOW_NUL, &error);
	if (!task_blob) {
		fprintf(stderr, PACKAGE_NAME ": jansson: %s\n", error.text);
		goto error_load;
	}

	if (json_unpack_ex(task_blob, &error, 0, "{s:i, s?:o}", "sleepMs", &sleep_ms, "exec", &cmd_blob) != 0) {
		fprintf(stderr, PACKAGE_NAME ": jansson: %s\n", error.text);
		goto end;
	}
	*sleep_interval = sleep_ms * 1000;
	if (!cmd_blob) {
		goto end;
	}

	c = command_parse(cmd_blob);
	if (!c) {
		goto end;
	}
	//if (app->verbose) {
	//	command_print(c, stdout);
	//}

	res = command_execute(c);
	if (res) {
		//if (app->verbose) {
		//	result_print(res, stdout);
		//}
		report_result(app, res);
		result_free(res);
	}

	command_free(c);
end:
	json_decref(task_blob);
error_load:
	return 0;
}

void main_loop(struct httprcclient *app) {
	CURLcode rc;
	useconds_t sleep_interval;
	struct response_buf *buf;
	long response_code;

	buf = response_buf_new();

	if (app->ca_cert) {
		// Replace system-wide CAs
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_CAINFO, app->ca_cert), "CURLOPT_CAINFO");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_CAPATH, NULL), "CURLOPT_CAPATH");

		// Could be used in complex PKIs, when allowing system-wide CAs to be used.
		//curlc_filter(curl_easy_setopt(app->curl, CURLOPT_ISSUERCERT, app->ca_cert), "CURLOPT_ISSUERCERT");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSL_VERIFYPEER, 1), "CURLOPT_SSL_VERIFYPEER");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSL_VERIFYHOST, 2), "CURLOPT_SSL_VERIFYHOST");
	}
	if (app->client_cert) {
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSLCERT, app->client_cert), "CURLOPT_SSLCERT");
	}
	if (app->client_key) {
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_SSLKEY, app->client_key), "CURLOPT_SSLKEY");
	}
	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_ERRORBUFFER, app->curl_error_buffer), "CURLOPT_SSLKEY");


	curlc_filter(curl_easy_setopt(app->curl, CURLOPT_URL, app->backend_url), "CURLOPT_URL");
	while (!app->graceful_exit) {
		sleep_interval = 5*1000*1000;
		response_buf_reset(buf);

		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POST, 0), "CURLOPT_POST");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_POSTFIELDS, NULL), "CURLOPT_POSTFIELDS");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_HTTPHEADER, NULL), "CURLOPT_HTTPHEADER");
		curlc_filter(curl_easy_setopt(app->curl, CURLOPT_HTTPGET, 1), "CURLOPT_HTTPGET");

		curl_easy_setopt(app->curl, CURLOPT_WRITEDATA, buf);
		curl_easy_setopt(app->curl, CURLOPT_WRITEFUNCTION, response_buf_write_cb);

		rc = curl_easy_perform(app->curl);
		if (rc != CURLE_OK) {
			log_error("libcurl: %s: %s\n", curl_easy_strerror(rc), app->curl_error_buffer);
			goto skip;
		}

		rc = curl_easy_getinfo(app->curl, CURLINFO_RESPONSE_CODE, &response_code);
		if (rc != CURLE_OK) {
			log_error("libcurl: %s: %s\n", curl_easy_strerror(rc), app->curl_error_buffer);
			goto skip;
		}	

		if (app->verbose) {
			printf("RX (%d): %.*s\n", response_code, (int)buf->size, buf->start);
		}

		if (response_code >= 200 && response_code <= 299) {
			process_response(app, buf, &sleep_interval);
		}
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
