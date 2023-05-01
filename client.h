// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2023, Christoph Fritz <chf.fritz@googlemail.com>
 */

#ifndef QUICKCHUNK_CLIENT_H
#define QUICKCHUNK_CLIENT_H

#include "quickchunk.h"

gint init_client(struct cs_data *cs);
gint client_check_and_upload(struct cs_data *cs, struct chunk *chnk);
gint client_send_exit(struct cs_data *cs);
gint deinit_client(struct cs_data *cs);

#endif //QUICKCHUNK_CLIENT_H
