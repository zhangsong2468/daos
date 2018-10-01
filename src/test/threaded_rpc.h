/* Copyright (C) 2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * Common code for threaded_client/threaded_server testing multiple threads
 * using a single context
 */
#ifndef __THREADED_RPC_H__
#define __THREADED_RPC_H__

#include <cart/api.h>
#include "common.h"

struct rpc_in {
	int msg;
	int payload;
};

struct rpc_out {
	int msg;
	int value;
};

static struct crt_msg_field *rpc_msg_field_in[] = {
	&CMF_INT,
	&CMF_INT,
};

static struct crt_msg_field *rpc_msg_field_out[] = {
	&CMF_INT,
	&CMF_INT,
};

#define FOREACH_MSG_TYPE(ACTION)    \
	ACTION(MSG_START,  0xf00d)  \
	ACTION(MSG_TYPE1,  0xdead)  \
	ACTION(MSG_TYPE2,  0xfeed)  \
	ACTION(MSG_TYPE3,  0xdeaf)  \
	ACTION(MSG_STOP,   0xbaad)

#define GEN_ENUM(name, value) \
	name,

#define GEN_ENUM_VALUE(name, value) \
	value,

enum {
	FOREACH_MSG_TYPE(GEN_ENUM)
	MSG_COUNT,
};

static const int msg_values[MSG_COUNT] = {
	FOREACH_MSG_TYPE(GEN_ENUM_VALUE)
};

#define GEN_STR(name, value) \
	#name,

static const char *msg_strings[MSG_COUNT] = {
	FOREACH_MSG_TYPE(GEN_STR)
};

#define MSG_IN_VALUE 0xbeef
#define MSG_OUT_VALUE 0xbead


#define INIT_FMT() \
	DEFINE_CRT_REQ_FMT(rpc_msg_field_in, rpc_msg_field_out)

#define RPC_ID 0x73ff

#endif /* __THREADED_RPC_H__ */
