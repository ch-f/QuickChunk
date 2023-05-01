// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2023, Christoph Fritz <chf.fritz@googlemail.com>
 */

#include "quickchunk.h"
#include "client.h"
#include "server.h"

static XXH128_hash_t get_hash128(const void *buf, gsize size)
{
	XXH128_hash_t hash = {0, 0};

	if (size) {
#if defined(__x86_64__)
		hash = XXH3_128bits_dispatch(buf, (size_t)size);
#else
		hash = XXH3_128bits(buf, (size_t)size);
#endif

	} else {
		g_error("hash size 0");
	}

	return hash;
}

gint is_file_existant(gchar *filename)
{
	struct stat status;
	gint ret;

	ret = (g_stat(filename, &status) == 0);

	return ret;
}

static gint64 total_elapsed_microseconds = 0;
static gint64 total_bytes_read = 0;

static void print_read_time_and_throughput(gint64 start_time,
                guint64 bytes_read)
{
	gint64 end_time = g_get_monotonic_time();
	gint64 elapsed_microseconds = end_time - start_time;
	total_elapsed_microseconds += elapsed_microseconds;
	total_bytes_read += bytes_read;

	gdouble elapsed_seconds = elapsed_microseconds / 1e6;
	gdouble throughput = (gdouble)bytes_read / elapsed_seconds /
	                     (1024 * 1024); // Throughput in MB/s

	g_info("Read completed in %.2lf seconds. Throughput: %.2lf MB/s",
	       elapsed_seconds, throughput);
}

static void print_overall_read_throughput()
{
	gdouble overall_elapsed_seconds = total_elapsed_microseconds / 1e6;
	gdouble overall_throughput = (gdouble)total_bytes_read /
	                             overall_elapsed_seconds / (1024 * 1024); // Overall throughput in MB/s

	g_message("Overall read completed in %.2lf seconds. Overall throughput: %.2lf MB/s",
	          overall_elapsed_seconds, overall_throughput);
}

static void *reader_thr(void *data)
{
	struct cs_data *cs = (struct cs_data *) data;
	struct chunk *chnk;
	FILE *fp;
	gint ret;
	guint64 chnk_num = 0;
	gsize n;
	gint64 start_time;

	ret = is_file_existant(cs->filename);

	if (!ret) {
		g_error("File not found: \"%s\"", cs->filename);
	}

	fp = g_fopen(cs->filename, "r");

	if (!fp) {
		g_error("Unable to open file <%s>: %s", __func__, cs->filename);
	}

	fseek(fp, 0L, SEEK_END);
	cs->filesize = ftell(fp);
	rewind(fp);
	g_debug("File: %s has size: %lu", cs->filename, cs->filesize);

	while (ftell(fp) < cs->filesize) {
		while (g_async_queue_length(cs->async_queue) >= QC_MAX_READER_QUEUE) {
			g_usleep(QC_WAIT_TIME);
		}

		chnk = g_new0(struct chunk, 1);
		chnk_num++;
		chnk->num = chnk_num;

		if ((ftell(fp) + QC_CHUNK_SIZE) < cs->filesize) {
			chnk->size = QC_CHUNK_SIZE;
		} else {
			chnk->size = cs->filesize - ftell(fp);
		}


		chnk->data = (gchar *) g_malloc(chnk->size * sizeof(gchar));

		start_time = g_get_monotonic_time();
		n = fread(chnk->data, 1, chnk->size, fp);

		if (n != chnk->size) {
			g_error("Failed to read %lu bytes, got %lu", chnk->size, n);
		}

		cs->current_file_position += n;

		print_read_time_and_throughput(start_time, n);

		chnk->hash = get_hash128(chnk->data, chnk->size);
		g_debug("%s item:%lu size:%lu hash:0x%lx%lx", __func__, chnk->num, chnk->size,
		        chnk->hash.low64, chnk->hash.high64);

		if (cs->is_server) {
			/* No need to keep the actual data in server mode */
			g_free(chnk->data);
		}

		g_async_queue_push(cs->async_queue, chnk);
		g_debug("%s item:%lu size:%lu", __func__, chnk->num, chnk->size);
	}

	fclose(fp);
	print_overall_read_throughput();

	cs->is_readthread_finished = TRUE;

	return NULL;
}

static void *worker_thr(void *data)
{
	struct cs_data *cs = (struct cs_data *) data;
	struct chunk *chnk;

	while (g_async_queue_length(cs->async_queue) || !cs->is_readthread_finished) {

		chnk = g_async_queue_timeout_pop(cs->async_queue, QC_WAIT_TIME);

		if (chnk) {
			if (cs->is_server) {
				g_mutex_lock(&cs->server->mutex);
				cs->server->current_num = chnk->num;
				cs->server->current_hash = chnk->hash;
				cs->server->update_current_finished = TRUE;
				g_cond_signal(&cs->server->cond);
				g_mutex_unlock(&cs->server->mutex);

				g_mutex_lock(&cs->mutex);
				init_server(cs);
				g_debug("waiting for client");

				while (!cs->server_one_chunk_finished) {
					//Mutex is released while waiting, and locked again before returning
					g_cond_wait(&cs->cond, &cs->mutex);
				}

				g_debug("client handled");
				cs->server_one_chunk_finished = FALSE;
				g_mutex_unlock(&cs->mutex);
			} else {
				// is client
				init_client(cs);

				if (client_check_and_upload(cs, chnk)) {
					g_error("Upload Error");
				}

				g_free(chnk->data);
			}

			g_free(chnk);
		}
	}

	if (!cs->is_server) { // is client
		client_send_exit(cs);
	}

	g_main_loop_quit(cs->main_loop);
	return NULL;
}

static void *status_thr(void *data)
{
	struct cs_data *cs = (struct cs_data *) data;
	gint64 start_time = g_get_monotonic_time();
	gint64 elapsed_time;
	gint64 estimated_time;

	if (cs->is_server) {
		return NULL;
	}

	g_print("\n\n");

	while (!cs->is_readthread_finished) {
		sleep(1); // Update every second

		gint64 current_pos = cs->current_file_position;
		gdouble percentage = (double)current_pos / (double)cs->filesize * 100;

		elapsed_time = g_get_monotonic_time() - start_time;
		gdouble speed = (double)current_pos / elapsed_time; // bytes per microsecond

		estimated_time = (cs->filesize - current_pos) / speed;

		gint64 elapsed_seconds_total = elapsed_time / 1e6;
		gint64 estimated_seconds_total = estimated_time / 1e6;

		gint64 elapsed_minutes = elapsed_seconds_total / 60;
		gint64 elapsed_seconds = elapsed_seconds_total % 60;
		gint64 estimated_minutes = estimated_seconds_total / 60;
		gint64 estimated_seconds = estimated_seconds_total % 60;

		g_print("\rprogress: %.2lf%%, elapsed time: %" G_GINT64_FORMAT ":%02"
		        G_GINT64_FORMAT ", remaining: %" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
		        percentage, elapsed_minutes, elapsed_seconds, estimated_minutes,
		        estimated_seconds);
		fflush(stdout);
	}

	g_print("\n\n");

	return NULL;
}

static gint cs_verbosity = 0;

GLogWriterOutput cs_g_log_writer_standard_streams(GLogLevelFlags log_level,
                const GLogField *fields,
                gsize n_fields, gpointer user_data)
{
	if (log_level == G_LOG_LEVEL_INFO && !cs_verbosity) {
		return G_LOG_WRITER_UNHANDLED;
	}

	if (log_level == G_LOG_LEVEL_DEBUG && cs_verbosity <= 1) {
		return G_LOG_WRITER_UNHANDLED;
	}

	return g_log_writer_standard_streams(log_level, fields, n_fields, user_data);
}

static gboolean cs_verbosity_arg_func(const gchar *option_name,
                                      const gchar *value, gpointer data, GError **error)
{
	cs_verbosity++;

	return TRUE;
}

int main(int argc, char *argv[])
{
	GThread *reader_thread;
	GThread *worker_thread;
	GThread *status_thread;
	struct cs_data *cs;
	GError *error = NULL;
	GOptionContext *context;
	gint64 start_time = g_get_monotonic_time();

	cs = g_new0(struct cs_data, 1);
	cs->client = g_new0(struct cs_client, 1);
	cs->server = g_new0(struct cs_server, 1);

	g_mutex_init(&cs->mutex);
	g_mutex_init(&cs->server->mutex);
	g_cond_init(&cs->cond);
	g_cond_init(&cs->server->cond);

	GOptionEntry entries[] = {
		{ "server", 's', 0, G_OPTION_ARG_NONE, &cs->is_server, "Run in server mode", NULL },
		{ "ip", 'i', 0, G_OPTION_ARG_STRING, &cs->server_ip, "IP address to use", "IP" },
		{ "port", 'p', 0, G_OPTION_ARG_INT, &cs->server_port, "Port to use", "PORT" },
		{ "file", 'f', 0, G_OPTION_ARG_FILENAME, &cs->filename, "File to use", "FILE" },
		{ "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, cs_verbosity_arg_func, "Increase verbosity", NULL },
		{ NULL }
	};

	g_log_set_writer_func(cs_g_log_writer_standard_streams, cs, NULL);

	context = g_option_context_new("");
	g_option_context_set_summary(context, "Version:\n  " PROJECT_VERSION);
	g_option_context_add_main_entries(context, entries, NULL);

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_error("option parsing failed: %s", error->message);
	}

	if (!cs->server_ip) {
		cs->server_ip = QC_DEFAULT_SERVER_IP;
	}

	if (!cs->server_port) {
		cs->server_port = QC_DEFAULT_SERVER_PORT;
	}

	if (!cs->filename) {
		g_error("missing filename");
	}

	if (cs->is_server) {
		g_message("NOTE: Selected file (%s) gets altered by client.", cs->filename);
	}

	g_debug("IP Address: %s", cs->server_ip);
	g_debug("Port: %d", cs->server_port);
	g_debug("Filename: %s", cs->filename);
	g_debug("is server: %d", cs->is_server);

	g_option_context_free(context);

	cs->async_queue = g_async_queue_new();
	g_return_val_if_fail(cs->async_queue != NULL, EXIT_FAILURE);

	cs->main_loop = g_main_loop_new(NULL, FALSE);

	reader_thread = g_thread_new("reader thread", &reader_thr, cs);
	worker_thread = g_thread_new("worker thread", &worker_thr, cs);
	status_thread = g_thread_new("status thread", &status_thr, cs);

	g_main_loop_run(cs->main_loop);

	g_thread_join(reader_thread);
	g_thread_join(worker_thread);
	g_thread_join(status_thread);

	g_async_queue_unref(cs->async_queue);

	g_main_loop_unref(cs->main_loop);

	deinit_client(cs);
	deinit_server(cs);

	g_mutex_clear(&cs->mutex);
	g_mutex_clear(&cs->server->mutex);
	g_cond_clear(&cs->cond);
	g_cond_clear(&cs->server->cond);
	g_free(cs->client);
	g_free(cs->server);
	g_free(cs);

	return EXIT_SUCCESS;
}
