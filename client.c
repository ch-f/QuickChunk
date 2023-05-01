// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2023, Christoph Fritz <chf.fritz@googlemail.com>
 */

#include "client.h"

gint init_client(struct cs_data *cs)
{
	GError *error = NULL;

	if (!cs->client->client) {	// not yet initialized
		cs->client->client = g_socket_client_new();
		cs->client->connection = g_socket_client_connect_to_host(cs->client->client,
		                         cs->server_ip, cs->server_port, NULL, &error);

		if (!cs->client->connection) {
			g_error("Failed to connect: %s", error->message);
		}

		cs->client->input_stream = g_io_stream_get_input_stream(G_IO_STREAM(
		                                   cs->client->connection));
		cs->client->output_stream = g_io_stream_get_output_stream(G_IO_STREAM(
		                                    cs->client->connection));
	}

	return 0;
}

gint deinit_client(struct cs_data *cs)
{
	if (cs->client->client) {
		g_object_unref(cs->client->client);
		g_object_unref(cs->client->connection);
	}

	return 0;
}

static enum QCResponse wait_and_get_response(GInputStream *input_stream)
{
	GError *error = NULL;
	gsize bytes_read;
	size_t len = strlen(QC_ACK_MESSAGE);
	gchar msg_received[len + 1];

	if (g_input_stream_read_all(input_stream, msg_received, len, &bytes_read, NULL,
	                            &error)) {
		if (bytes_read != len) {
			g_error("bytes_read(%zu) unequal to expected %zu", bytes_read, len);
		}
	} else {
		g_error("Error reading response: %s", error->message);
	}

	msg_received[len] = '\0';

	if (g_strcmp0(msg_received, QC_ACK_MESSAGE) == 0) {
		g_debug("GOT ACK: %s", msg_received);
		return QC_RESPONSE_ACK;
	} else if (g_strcmp0(msg_received, QC_NOK_MESSAGE) == 0) {
		g_debug("GOT NOK: %s", msg_received);
		return QC_RESPONSE_NOK;
	} else if (g_strcmp0(msg_received, QC_EQL_MESSAGE) == 0) {
		g_debug("GOT EQL: %s", msg_received);
		return QC_RESPONSE_EQL;
	}

	g_error("Unknown msg received (%s) from server, aborting.", msg_received);
}

gint send_data(GOutputStream *output_stream, gpointer data, gsize size,
               const gchar *error_msg)
{
	gsize bytes_written;
	GError *error = NULL;

	if (g_output_stream_write_all(output_stream, data, size, &bytes_written, NULL,
	                              &error)) {
		if (bytes_written != size) {
			g_critical("Sent size (%" G_GSIZE_FORMAT ") unequal to expected %"
			           G_GSIZE_FORMAT,
			           bytes_written, size);
			return -1;
		}

		return 0;
	} else {
		g_critical("%s: %s", error_msg, error->message);
		g_error_free(error);
		return -1;
	}
}

gint client_check_and_upload(struct cs_data *cs, struct chunk *chnk)
{
	GInputStream *input_stream = cs->client->input_stream;
	GOutputStream *output_stream = cs->client->output_stream;
	enum QCResponse resp;

	if (!cs->misc_sent) {
		// Send version
		gchar version_str[VERSION_LENGTH] = PROJECT_VERSION;

		if (send_data(output_stream, version_str, VERSION_LENGTH,
		              "Error writing version") != 0) {
			return -1;
		}

		g_debug("Sent version: %s", version_str);

		// Send filesize
		if (send_data(output_stream, &cs->filesize, sizeof(cs->filesize),
		              "Error writing filesize") != 0) {
			return -1;
		}

		g_debug("Sent filesize: %" G_GSIZE_FORMAT, cs->filesize);

		cs->misc_sent = TRUE;
	}

	// Send chunk num
	if (send_data(output_stream, &chnk->num, sizeof(chnk->num),
	              "Error writing chunk num") != 0) {
		return -1;
	}

	g_debug("Sent chunk num: %" G_GINT64_FORMAT, chnk->num);

	// Send chunk size
	if (send_data(output_stream, &chnk->size, sizeof(chnk->size),
	              "Error writing chunk size") != 0) {
		return -1;
	}

	g_debug("Sent chunk size: %d", chnk->size);

	// Send chunk hash
	if (send_data(output_stream, &chnk->hash, sizeof(chnk->hash),
	              "Error writing chunk hash") != 0) {
		return -1;
	}

	g_debug("Sent chunk hash: 0x%lx%lx", chnk->hash.high64, chnk->hash.low64);

	// Wait for response
	resp = wait_and_get_response(input_stream);

	if (resp == QC_RESPONSE_NOK) {
		g_critical("Protocol error: QC_RESPONSE_NOK");
		return -1;
	} else if (resp == QC_RESPONSE_EQL) {
		g_debug("Hash equal, do not send chunk data");
	} else if (resp == QC_RESPONSE_ACK) {
		// Send the chunk data
		gint64 start_time = g_get_monotonic_time();

		if (send_data(output_stream, chnk->data, chnk->size,
		              "Error writing chunk data") != 0) {
			return -1;
		}

		gint64 end_time = g_get_monotonic_time();
		gint64 elapsed_microseconds = 1 + (end_time - start_time);

		gdouble throughput = (gdouble)chnk->size / elapsed_microseconds;
		g_info("Sent %zu bytes in %.2lf seconds. Throughput: %.2lf MB/s",
		       chnk->size, elapsed_microseconds / 1e6, throughput);
	}

	// Wait for ACK
	resp = wait_and_get_response(input_stream);

	if (resp != QC_RESPONSE_ACK) {
		g_critical("Protocol error!");
		return -1;
	}

	return 0;
}

gint client_send_exit(struct cs_data *cs)
{
	gsize bytes_written, bytes_read;
	GInputStream *input_stream = cs->client->input_stream;
	GOutputStream *output_stream = cs->client->output_stream;
	GError *error = NULL;
	gint64 num = -1;

	// Send negative num of chunk as indicator to stop
	if (g_output_stream_write_all(output_stream, &num, sizeof(num),
	                              &bytes_written, NULL, &error)) {
		g_debug("Sent negative chunk num: %d to indicate exit", num);
	} else {
		g_error("Error writing negative chunk num: %s", error->message);
	}

	return 0;
}
