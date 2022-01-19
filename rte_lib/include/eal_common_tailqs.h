/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <sys/queue.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>

#include "rte_memory.h"
#include "rte_launch.h"
//#include "rte_eal.h"
#include "rte_eal_memconfig.h"
#include "rte_per_lcore.h"
#include "rte_lcore.h"
#include "generic/rte_atomic.h"
#include "rte_branch_prediction.h"
#include "rte_log.h"
#include "rte_string_fns.h"
#include "rte_debug.h"



//#include "eal_private.h"
//#include "eal_memcfg.h"



struct rte_tailq_head*
	rte_eal_tailq_lookup(const char* name);


void
rte_dump_tailq(FILE* f);


static struct rte_tailq_head*
rte_eal_tailq_create(const char* name);

/* local register, used to store "early" tailqs before rte_eal_init() and to
 * ensure secondary process only registers tailqs once. */
static int
rte_eal_tailq_local_register(struct rte_tailq_elem* t);


static void
rte_eal_tailq_update(struct rte_tailq_elem* t);

int
rte_eal_tailq_register(struct rte_tailq_elem* t);


int
rte_eal_tailqs_init(void);
