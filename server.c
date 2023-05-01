// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2023, Christoph Fritz <chf.fritz@googlemail.com>
 */

#include "server.h"

static gboolean are_hashes_equal(XXH128_hash_t hash1, XXH128_hash_t hash2)
{
	return (hash1.low64 == hash2.low64) && (hash1.high64 == hash2.high64);
}

static gboolean
on_incoming_connection(GThreadedSocketService *self,
                       GSocketConnection *connection,
                       GObject *source_object,
                       gpointer user_data)
{
	GInputStream *input_stream = g_io_stream_get_input_stream(G_IO_STREAM(
	                                     connection));
	GOutputStream *output_stream = g_io_stream_get_output_stream(G_IO_STREAM(
	                                       connection));
	struct cs_data *cs = (struct cs_data *) user_data;
	gsize bytes_read, bytes_written;
	struct chunk *chnk;
	GError *error = NULL;
	size_t fw_nb;
	long offset = 0;
	gsize remote_filesize;
	gint64 current_num;
	XXH128_hash_t current_hash;

	chnk = g_new0(struct chunk, 1);

	FILE *fp = g_fopen(cs->filename, "r+");

	if (!fp) {
		g_error("Failed to open fp for writing");
	}

	while (TRUE) {
		g_mutex_lock(&cs->server->mutex);
		g_debug("server: waiting for worker_thr to update current variables");

		while (!cs->server->update_current_finished) {
			//Mutex is released while waiting, and locked again before returning
			g_cond_wait(&cs->server->cond, &cs->server->mutex);
		}

		g_debug("server: waiting finished");
		cs->server->update_current_finished = FALSE;
		current_num = cs->server->current_num;
		current_hash = cs->server->current_hash;
		g_mutex_unlock(&cs->server->mutex);

		if (!cs->misc_received) {
			// Read version
			gchar version_str[VERSION_LENGTH];

			if (g_input_stream_read_all(input_stream, version_str,
			                            VERSION_LENGTH,
			                            &bytes_read, NULL,
			                            &error)) {
				g_debug("Received version: %s", version_str);

				if (strcmp(version_str, PROJECT_VERSION) != 0) {
					g_error("Version mismatch: client version %s, server version %s",
					        version_str, PROJECT_VERSION);
				}
			} else {
				g_error("Error reading version: %s",
				        error->message);
			}

			// Read filesize
			if (g_input_stream_read_all(input_stream, &remote_filesize,
			                            sizeof(remote_filesize),
			                            &bytes_read, NULL,
			                            &error)) {
				if (bytes_read != sizeof(remote_filesize)) {
					g_error("protocol error: bytes_read(%zu) unequal to expected %zu", bytes_read,
					        sizeof(remote_filesize));
				}

				g_debug("Received remote_filesize: %" G_GSIZE_FORMAT, remote_filesize);

				if (remote_filesize != cs->filesize) {
					g_error("not yet supported: remote_filesize (%" G_GSIZE_FORMAT
					        ") differ from local filesize (% " G_GSIZE_FORMAT ")",
					        remote_filesize, cs->filesize);
				}
			} else {
				g_error("Error reading chunk num: %s", error->message);
			}

			cs->misc_received = TRUE;
		}

		// Read chunk num
		if (g_input_stream_read_all(input_stream, &chnk->num, sizeof(chnk->num),
		                            &bytes_read, NULL,
		                            &error)) {
			if (bytes_read != sizeof(chnk->num)) {
				g_error("protocol error: bytes_read(%zu) unequal to expected %zu", bytes_read,
				        sizeof(chnk->num));
			}

			g_debug("Received chunk->num: %" G_GINT64_FORMAT, chnk->num);

			if (chnk->num < 0) {
				g_debug("Client sent negative num, means end of transmission");
				break;
			}

			if (chnk->num != current_num) {
				g_error("Sync issue: chnk->num (%" G_GINT64_FORMAT
				        ") is unequal to current_num %" G_GINT64_FORMAT,
				        chnk->num, current_num);
			}
		} else {
			g_error("Error reading chunk num: %s", error->message);
		}

		// Read chunk size
		if (g_input_stream_read_all(input_stream, &chnk->size, sizeof(chnk->size),
		                            &bytes_read, NULL,
		                            &error)) {
			if (bytes_read != sizeof(chnk->size)) {
				g_error("protocol error: bytes_read(%zu) unequal to expected %zu", bytes_read,
				        sizeof(chnk->size));
			}

			g_debug("Received chunk->size: %" G_GSIZE_FORMAT, chnk->size);

			if (chnk->size <= 0 || chnk->size > QC_CHUNK_SIZE) {
				g_error("chunk->size issue");
			}
		} else {
			g_error("Error reading chunk->size: %s", error->message);
		}

		// Read chunk hash
		if (g_input_stream_read_all(input_stream, &chnk->hash, sizeof(chnk->hash),
		                            &bytes_read, NULL,
		                            &error)) {
			if (bytes_read != sizeof(chnk->hash)) {
				g_error("protocol error: bytes_read(%zu) unequal to expected %zu", bytes_read,
				        sizeof(chnk->hash));
			}

			g_debug("Received chunk->hash: 0x%lx%lx", chnk->hash.high64,
			        chnk->hash.low64);

			if (chnk->hash.low64 == 0 && chnk->hash.high64 == 0) {
				g_error("chunk->hash issue");
			}
		} else {
			g_error("Error reading chunk->hash: %s", error->message);
		}

		g_debug("current_hash: 0x%lx%lx,  received chnk->hash: 0x%lx%lx",
		        current_hash.high64, current_hash.low64,
		        chnk->hash.high64, chnk->hash.low64);

		if (are_hashes_equal(current_hash, chnk->hash)) {
			g_debug("HASH IS EQUAL - no need to transfer");

			// Send EQL
			if (!g_output_stream_write_all(output_stream, QC_EQL_MESSAGE,
			                               strlen(QC_EQL_MESSAGE), &bytes_written, NULL,
			                               &error)) {
				g_error("Error sending EQL: %s", error->message);
			}
		} else {
			// Send ACK
			if (!g_output_stream_write_all(output_stream, QC_ACK_MESSAGE,
			                               strlen(QC_ACK_MESSAGE), &bytes_written, NULL,
			                               &error)) {
				g_error("Error sending ACK: %s", error->message);
			}

			chnk->data = (gchar *) g_malloc(chnk->size * sizeof(gchar));

			// Read chunk data and write it to fp
			if (!g_input_stream_read_all(input_stream, chnk->data, chnk->size, &bytes_read,
			                             NULL,
			                             &error)) {
				g_error("Error reading chunk data: %s", error->message);
			}

			if (bytes_read != chnk->size) {
				g_error("ERROR: bytes_read %zu unequal to chunk size", bytes_read);
			}

			gint64 start_time = g_get_monotonic_time();

			fseek(fp, offset, SEEK_SET);

			fw_nb = fwrite(chnk->data, 1, chnk->size, fp);

			g_free(chnk->data);

			if (fw_nb != chnk->size) {
				g_error("Fail to write %" G_GSIZE_FORMAT " bytes, did: %&lu", chnk->size,
				        fw_nb);
			}

			gint64 end_time = g_get_monotonic_time();
			gint64 elapsed_microseconds = 1 + (end_time - start_time);

			gdouble throughput = (gdouble)chnk->size / elapsed_microseconds;
			g_info("Wrote %" G_GSIZE_FORMAT
			       " bytes at offset %li in %.2lf seconds. Throughput: %.2lf MB/s",
			       chnk->size, offset, elapsed_microseconds / 1e6, throughput);
		}

		offset += chnk->size;

		// Send ACK
		if (!g_output_stream_write_all(output_stream, QC_ACK_MESSAGE,
		                               strlen(QC_ACK_MESSAGE), &bytes_written, NULL,
		                               &error)) {
			g_error("Error sending final chunk ACK: %s", error->message);
		}

		g_mutex_lock(&cs->mutex);
		cs->server_one_chunk_finished = TRUE;
		g_cond_signal(&cs->cond);
		g_mutex_unlock(&cs->mutex);
	}

	fclose(fp);
	g_free(chnk);

	return FALSE; // Return FALSE so that the connection will be closed after the callback is done
}

gint init_server(struct cs_data *cs)
{
	GSocketAddress *address;
	GError *error = NULL;

	if (cs->server->service) {
		return 0;
	}

	cs->server->service = g_threaded_socket_service_new(1);

	// Add the service to listen on specified ip and port
	address = g_inet_socket_address_new(g_inet_address_new_from_string(cs->server_ip),
	                                    cs->server_port);

	if (!g_socket_listener_add_address(G_SOCKET_LISTENER(cs->server->service),
	                                   address, G_SOCKET_TYPE_STREAM,
					   G_SOCKET_PROTOCOL_TCP, NULL, NULL,
					   &error)) {
		g_error("Failed to add address: %s", error->message);
	}

	g_object_unref(address);

	g_signal_connect(cs->server->service, "run",
			 G_CALLBACK(on_incoming_connection), cs);

	g_socket_service_start(cs->server->service);

	return 0;
}

gint deinit_server(struct cs_data *cs)
{
	GSocketService *service = cs->server->service;

	if (!service) {
		return 0;
	}

	g_object_unref(service);

	return 0;
}
