// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2023, Christoph Fritz <chf.fritz@googlemail.com>
 */

#ifndef QUICKCHUNK_SERVER_H
#define QUICKCHUNK_SERVER_H

#include "quickchunk.h"

gint init_server(struct cs_data *cs);
gint deinit_server(struct cs_data *cs);

#endif //QUICKCHUNK_SERVER_H
