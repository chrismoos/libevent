/*
 * Copyright (c) 2003-2006 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#ifdef HAVE_CONFIG_H
#include "evconfig.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#ifndef WIN32
#include <sys/socket.h>
#include <sys/signal.h>
#include <unistd.h>
#endif
#include <netdb.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "event.h"
#include "evhttp.h"
#include "log.h"
#include "evrpc.h"

#include "regress.gen.h"

extern int test_ok;

static struct evhttp *
http_setup(short *pport)
{
	int i;
	struct evhttp *myhttp;
	short port = -1;

	/* Try a few different ports */
	for (i = 0; i < 50; ++i) {
		myhttp = evhttp_start("127.0.0.1", 8080 + i);
		if (myhttp != NULL) {
			port = 8080 + i;
			break;
		}
	}

	if (port == -1)
		event_errx(1, "Could not start web server");

	*pport = port;
	return (myhttp);
}

EVRPC_HEADER(Message, msg, kill);
EVRPC_HEADER(NeverReply, msg, kill);

EVRPC_GENERATE(Message, msg, kill);
EVRPC_GENERATE(NeverReply, msg, kill);

void
MessageCb(EVRPC_STRUCT(Message)* rpc, void *arg)
{
	struct kill* kill_reply = rpc->reply;

	/* we just want to fill in some non-sense */
	EVTAG_ASSIGN(kill_reply, weapon, "dagger");
	EVTAG_ASSIGN(kill_reply, action, "wave around like an idiot");

	/* no reply to the RPC */
	EVRPC_REQUEST_DONE(rpc);
}

void
NeverReplyCb(EVRPC_STRUCT(NeverReply)* rpc, void *arg)
{
	test_ok += 1;
}

static void
rpc_setup(struct evhttp **phttp, short *pport, struct evrpc_base **pbase)
{
	short port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;

	http = http_setup(&port);
	base = evrpc_init(http);
	
	EVRPC_REGISTER(base, Message, msg, kill, MessageCb, NULL);
	EVRPC_REGISTER(base, NeverReply, msg, kill, NeverReplyCb, NULL);

	*phttp = http;
	*pport = port;
	*pbase = base;
}

static void
rpc_postrequest_failure(struct evhttp_request *req, void *arg)
{
	if (req->response_code != HTTP_SERVUNAVAIL) {
	
		fprintf(stderr, "FAILED (response code)\n");
		exit(1);
	}

	test_ok = 1;
	event_loopexit(NULL);
}

/*
 * Test a malformed payload submitted as an RPC
 */

static void
rpc_basic_test(void)
{
	short port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evhttp_connection *evcon = NULL;
	struct evhttp_request *req = NULL;

	fprintf(stdout, "Testing Basic RPC Support: ");

	rpc_setup(&http, &port, &base);

	evcon = evhttp_connection_new("127.0.0.1", port);
	if (evcon == NULL) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	/*
	 * At this point, we want to schedule an HTTP POST request
	 * server using our make request method.
	 */

	req = evhttp_request_new(rpc_postrequest_failure, NULL);
	if (req == NULL) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	/* Add the information that we care about */
	evhttp_add_header(req->output_headers, "Host", "somehost");
	evbuffer_add_printf(req->output_buffer, "Some Nonsense");
	
	if (evhttp_make_request(evcon, req,
		EVHTTP_REQ_POST,
		"/.rpc.Message") == -1) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	test_ok = 0;

	event_dispatch();
	
	if (test_ok != 1) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	fprintf(stdout, "OK\n");

	evhttp_free(http);
}

static void
rpc_postrequest_done(struct evhttp_request *req, void *arg)
{
	struct kill* kill_reply = NULL;

	if (req->response_code != HTTP_OK) {
	
		fprintf(stderr, "FAILED (response code)\n");
		exit(1);
	}

	kill_reply = kill_new();

	if ((kill_unmarshal(kill_reply, req->input_buffer)) == -1) {
		fprintf(stderr, "FAILED (unmarshal)\n");
		exit(1);
	}
	
	kill_free(kill_reply);

	test_ok = 1;
	event_loopexit(NULL);
}

static void
rpc_basic_message(void)
{
	short port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evhttp_connection *evcon = NULL;
	struct evhttp_request *req = NULL;
	struct msg *msg;

	fprintf(stdout, "Testing Good RPC Post: ");

	rpc_setup(&http, &port, &base);

	evcon = evhttp_connection_new("127.0.0.1", port);
	if (evcon == NULL) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	/*
	 * At this point, we want to schedule an HTTP POST request
	 * server using our make request method.
	 */

	req = evhttp_request_new(rpc_postrequest_done, NULL);
	if (req == NULL) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	/* Add the information that we care about */
	evhttp_add_header(req->output_headers, "Host", "somehost");

	/* set up the basic message */
	msg = msg_new();
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");
	msg_marshal(req->output_buffer, msg);
	msg_free(msg);

	if (evhttp_make_request(evcon, req,
		EVHTTP_REQ_POST,
		"/.rpc.Message") == -1) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	test_ok = 0;

	event_dispatch();
	
	if (test_ok != 1) {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}

	fprintf(stdout, "OK\n");

	evhttp_free(http);
}

static struct evrpc_pool *
rpc_pool_with_connection(short port)
{
	struct evhttp_connection *evcon;
	struct evrpc_pool *pool;

	pool = evrpc_pool_new();
	assert(pool != NULL);

	evcon = evhttp_connection_new("127.0.0.1", port);
	assert(evcon != NULL);

	evrpc_pool_add_connection(pool, evcon);
	
	return (pool);
}

static void
GotKillCb(struct msg *msg, struct kill *kill, void *arg)
{
	char *weapon;
	char *action;

	if (EVTAG_GET(kill, weapon, &weapon) == -1) {
		fprintf(stderr, "get weapon\n");
		goto done;
	}
	if (EVTAG_GET(kill, action, &action) == -1) {
		fprintf(stderr, "get action\n");
		goto done;
	}

	if (strcmp(weapon, "dagger"))
		goto done;

	if (strcmp(action, "wave around like an idiot"))
		goto done;

	test_ok += 1;

done:
	event_loopexit(NULL);
}

static void
GotKillCbTwo(struct msg *msg, struct kill *kill, void *arg)
{
	char *weapon;
	char *action;

	if (EVTAG_GET(kill, weapon, &weapon) == -1) {
		fprintf(stderr, "get weapon\n");
		goto done;
	}
	if (EVTAG_GET(kill, action, &action) == -1) {
		fprintf(stderr, "get action\n");
		goto done;
	}

	if (strcmp(weapon, "dagger"))
		goto done;

	if (strcmp(action, "wave around like an idiot"))
		goto done;

	test_ok += 1;

done:
	if (test_ok == 2)
		event_loopexit(NULL);
}

static void
rpc_basic_client(void)
{
	short port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evrpc_pool *pool = NULL;
	struct msg *msg;
	struct kill *kill;

	fprintf(stdout, "Testing RPC Client: ");

	rpc_setup(&http, &port, &base);

	pool = rpc_pool_with_connection(port);

	/* set up the basic message */
	msg = msg_new();
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");

	kill = kill_new();

	EVRPC_MAKE_REQUEST(Message, msg, kill,  GotKillCb, NULL);

	test_ok = 0;

	event_dispatch();
	
	if (test_ok != 1) {
		fprintf(stdout, "FAILED (1)\n");
		exit(1);
	}

	/* we do it twice to make sure that reuse works correctly */
	kill_clear(kill);

	EVRPC_MAKE_REQUEST(Message, msg, kill,  GotKillCb, NULL);

	event_dispatch();
	
	if (test_ok != 2) {
		fprintf(stdout, "FAILED (2)\n");
		exit(1);
	}

	fprintf(stdout, "OK\n");

	msg_free(msg);
	kill_free(kill);

	evrpc_pool_free(pool);
	evhttp_free(http);
}

/* 
 * We are testing that the second requests gets send over the same
 * connection after the first RPCs completes.
 */
static void
rpc_basic_queued_client(void)
{
	short port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evrpc_pool *pool = NULL;
	struct msg *msg;
	struct kill *kill_one, *kill_two;

	fprintf(stdout, "Testing RPC (Queued) Client: ");

	rpc_setup(&http, &port, &base);

	pool = rpc_pool_with_connection(port);

	/* set up the basic message */
	msg = msg_new();
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");

	kill_one = kill_new();
	kill_two = kill_new();

	EVRPC_MAKE_REQUEST(Message, msg, kill_one,  GotKillCbTwo, NULL);
	EVRPC_MAKE_REQUEST(Message, msg, kill_two,  GotKillCb, NULL);

	test_ok = 0;

	event_dispatch();
	
	if (test_ok != 2) {
		fprintf(stdout, "FAILED (1)\n");
		exit(1);
	}

	fprintf(stdout, "OK\n");

	msg_free(msg);
	kill_free(kill_one);
	kill_free(kill_two);

	evrpc_pool_free(pool);
	evhttp_free(http);
}

static void
GotErrorCb(struct msg *msg, struct kill *kill, void *arg)
{
	/* should never be complete but just to check */
	if (kill_complete(kill) == 0)
		goto done;

	test_ok += 1;

done:
	event_loopexit(NULL);
}

static void
rpc_client_timeout(void)
{
	short port;
	struct evhttp *http = NULL;
	struct evrpc_base *base = NULL;
	struct evrpc_pool *pool = NULL;
	struct msg *msg;
	struct kill *kill;

	fprintf(stdout, "Testing RPC Client Timeout: ");

	rpc_setup(&http, &port, &base);

	pool = rpc_pool_with_connection(port);

	/* set the timeout to 5 seconds */
	evrpc_pool_set_timeout(pool, 5);

	/* set up the basic message */
	msg = msg_new();
	EVTAG_ASSIGN(msg, from_name, "niels");
	EVTAG_ASSIGN(msg, to_name, "tester");

	kill = kill_new();

	EVRPC_MAKE_REQUEST(NeverReply, msg, kill,  GotErrorCb, NULL);

	test_ok = 0;

	event_dispatch();
	
	if (test_ok != 2) {
		fprintf(stdout, "FAILED (1)\n");
		exit(1);
	}

	fprintf(stdout, "OK\n");

	msg_free(msg);
	kill_free(kill);

	evrpc_pool_free(pool);
	evhttp_free(http);
}

void
rpc_suite(void)
{
	rpc_basic_test();
	rpc_basic_message();
	rpc_basic_client();
	rpc_basic_queued_client();
	rpc_client_timeout();
}
