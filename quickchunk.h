// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2023, Christoph Fritz <chf.fritz@googlemail.com>
 */

#ifndef QUICKCHUNK_QUICKCHUNK_H
#define QUICKCHUNK_QUICKCHUNK_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#if defined(__x86_64__)
	#include <xxh_x86dispatch.h>
#else
	#include <xxhash.h>
#endif

#define QC_WAIT_TIME            (32 * 1000) /* mS */
#define QC_CHUNK_SIZE           (200 * 1000000UL) /* 200 MB */
#define QC_MAX_READER_QUEUE     20
#define QC_DEFAULT_SERVER_IP    "127.0.0.1"
#define QC_DEFAULT_SERVER_PORT  12345

#define VERSION_LENGTH 32

enum QCResponse {
	QC_RESPONSE_ACK,
	QC_RESPONSE_NOK,
	QC_RESPONSE_EQL
};

#define QC_ACK_MESSAGE  "ACK"
#define QC_NOK_MESSAGE  "NOK"
#define QC_EQL_MESSAGE  "EQL"

struct chunk {
	gint64 num;
	XXH128_hash_t hash;
	gsize size;
	gchar *data;
};

struct cs_server {
	GSocketService *service;
	gint64 current_num;
	XXH128_hash_t current_hash;
	GMutex mutex;
	gboolean update_current_finished;
	GCond cond;
};

struct cs_client {
	GSocketClient *client;
	GSocketConnection *connection;
	GInputStream *input_stream;
	GOutputStream *output_stream;
};

struct cs_data {
	GMainLoop *main_loop;
	GAsyncQueue *async_queue;
	gboolean is_readthread_finished;
	gchar *filename;
	gsize filesize;
	gsize current_file_position;
	gchar *server_ip;
	guint16 server_port;
	gboolean is_server;
	struct cs_client *client;
	struct cs_server *server;
	GMutex mutex;
	GCond cond;
	gboolean server_one_chunk_finished;
	gboolean misc_sent;
	gboolean misc_received;
};

#endif //QUICKCHUNK_QUICKCHUNK_H
