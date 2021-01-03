/*
 * (C) Copyright 2019-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of the DAOS server. It implements the dRPC client for
 * communicating with daos_server.
 */

#define D_LOGFAC DD_FAC(server)

#include <daos_types.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/ras.h>
#include "srv.pb-c.h"
#include "event.pb-c.h"
#include "srv_internal.h"
#include "drpc_internal.h"

/** dRPC client context */
struct drpc *dss_drpc_ctx;

/* Notify daos_server that we are ready (e.g., to receive dRPC requests). */
static int
notify_ready(void)
{
	Srv__NotifyReadyReq	req = SRV__NOTIFY_READY_REQ__INIT;
	uint8_t		       *reqb;
	size_t			reqb_size;
	Drpc__Call	       *dreq;
	Drpc__Response	       *dresp;
	int			rc;

	rc = crt_self_uri_get(0 /* tag */, &req.uri);
	if (rc != 0)
		goto out;
	req.nctxs = DSS_CTX_NR_TOTAL;
	/* Do not free, this string is managed by the dRPC listener */
	req.drpclistenersock = drpc_listener_socket_path;
	req.instanceidx = dss_instance_idx;
	req.ntgts = dss_tgt_nr;

	reqb_size = srv__notify_ready_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL)
		D_GOTO(out_uri, rc = -DER_NOMEM);

	srv__notify_ready_req__pack(&req, reqb);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_NOTIFY_READY, &dreq);
	if (rc != 0) {
		D_FREE(reqb);
		goto out_uri;
	}

	dreq->body.len = reqb_size;
	dreq->body.data = reqb;

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

	drpc_response_free(dresp);
out_dreq:
	/* This also frees reqb via dreq->body.data. */
	drpc_call_free(dreq);
out_uri:
	D_FREE(req.uri);
out:
	return rc;
}

/* Notify daos_server that there has been a I/O error. */
int
notify_bio_error(int media_err_type, int tgt_id)
{
	Srv__BioErrorReq	 bioerr_req = SRV__BIO_ERROR_REQ__INIT;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_INVAL;
	}

	/* TODO: How does this get freed on error? */
	rc = crt_self_uri_get(0 /* tag */, &bioerr_req.uri);
	if (rc != 0)
		return rc;

	/* TODO: add checksum error */
	if (media_err_type == MET_UNMAP)
		bioerr_req.unmaperr = true;
	else if (media_err_type == MET_WRITE)
		bioerr_req.writeerr = true;
	else if (media_err_type == MET_READ)
		bioerr_req.readerr = true;
	bioerr_req.tgtid = tgt_id;
	bioerr_req.instanceidx = dss_instance_idx;
	bioerr_req.drpclistenersock = drpc_listener_socket_path;

	req_size = srv__bio_error_req__get_packed_size(&bioerr_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		return -DER_NOMEM;

	srv__bio_error_req__pack(&bioerr_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_BIO_ERR, &dreq);
	if (rc != 0) {
		D_FREE(req);
		return rc;
	}

	dreq->body.len = req_size;
	dreq->body.data = req;

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

	drpc_response_free(dresp);

out_dreq:
	drpc_call_free(dreq);

	return rc;
}

static int
init_ras(const char *id, uint32_t sev, type, const char *msg,
	 Mgmt__RASEvent *evt)
{
	evt->id = id;
	evt->severity = sev;
	evt->type = type;
	evt->msg = msg;

	(void)gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);
	if (tm) {
		D_ALLOC(evt->timestamp, DAOS_RAS_EVENT_STR_MAX_LEN);
		if (evt->timestamp == NULL) {
			D_ERROR("failed to allocate timestamp\n");
			return -DER_NOMEM;
		}
		snprintf(evt->timestamp, DAOS_RAS_EVENT_STR_MAX_LEN - 1,
			 "%04d/%02d/%02d-%02d:%02d:%02d.%02ld",
			 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			 tm->tm_hour, tm->tm_min, tm->tm_sec,
			 (long int)tv.tv_usec / 10000);
	}

	/** XXX: nodename should be fetched only once and not on every call */
	D_ALLOC(evt->hostname, DAOS_RAS_EVENT_STR_MAX_LEN);
	if (evt->hostname == NULL) {
		D_ERROR("failed to allocate hostname\n");
		D_FREE(evt->timestamp);
		return -DER_NOMEM;
	}
	(void)uname(&uts);
	D_STRNDUP(evt->hostname, uts.nodename, DAOS_RAS_EVENT_STR_MAX_LEN - 1);

	/* TODO: add rank id to event */

	D_DEBUG(DB_MGMT, "&&& RAS EVENT id: [%s] ts: [%s] host: [%s] sev: [%s]"
		" type: [%s] puuid: ["DF_UUID"] msg: [%s]",
		evt->id, evt->timestamp, evt->hostname,
		ras_event_sev2str(evt->severity), ras_event_type2str(evt->type),
		DP_UUID(evt->pool_svc_info->pool_uuid), evt->msg);

	return 0;
}

static void
free_ras(Mgmt__RASEvent *evt)
{
	D_FREE(evt->hostname);
	D_FREE(evt->timestamp);
}

static int
notify_ras_event(Mgmt__RASEvent *evt)
{
	Mgmt__ClusterEventReq	 req = MGMT__CLUSTER_EVENT_REQ__INIT;
	uint8_t			*reqb;
	size_t			 reqb_size;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	int			 rc;

	if (evt == NULL) {
		D_ERROR("invalid RAS event\n");
		return -DER_INVAL;
	}

	req.event_case = MGMT__CLUSTER_EVENT_REQ__EVENT_RAS;
	req.ras = evt;

	reqb_size = mgmt__cluster_event_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	mgmt__cluster_event_req__pack(&req, reqb);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_MGMT,
			      DRPC_METHOD_MGMT_CLUSTER_EVENT, &dreq);
	if (rc != 0) {
		D_FREE(reqb);
		goto out;
	}

	dreq->body.len = reqb_size;
	dreq->body.data = reqb;

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

	drpc_response_free(dresp);
out_dreq:
	/* This also frees reqb via dreq->body.data. */
	drpc_call_free(dreq);
out:
	return rc;
}

int
ds_notify_pool_svc_update(uuid_t pool_uuid, d_rank_list_t *svc)
{
	Mgmt__RASEvent		 evt = MGMT__RASEVENT__INIT;
	Mgmt__PoolSvcEventInfo	 info = MGMT__POOL_SVC_EVENT_INFO__INIT;
	struct timeval		 tv;
	struct tm		*tm;
	struct utsname		 uts;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_UNINIT;
	}

	D_ASSERT(pool_uuid != NULL);

	D_ALLOC(info.pool_uuid, DAOS_UUID_STR_SIZE);
	if (info.pool_uuid == NULL) {
		D_ERROR("failed to allocate pool uuid\n");
		return -DER_NOMEM;
	}
	uuid_unparse_lower(pool_uuid, info.pool_uuid);

	D_ASSERT(svc != NULL && svc->rl_nr > 0);

	rc = rank_list_to_uint32_array(svc, &info.svc_reps, &info.n_svc_reps);
	if (rc != 0) {
		D_ERROR("failed to convert svc replicas to proto\n");
		goto out_uuid;
	}

	evt.extended_info_case = MGMT__RASEVENT__EXTENDED_INFO_POOL_SVC_INFO;
	evt.pool_svc_info = &info;

	rc = init_ras("pool_svc_ranks_update", (uint32_t)RAS_SEV_INFO,
		      (uint32_t)RAS_TYPE_STATE_CHANGE,
		      "List of pool service replica ranks has been updated.",
		      &evt)
	if (rc != 0) {
		D_ERROR("failed to populate generic ras event details\n");
		goto out_svcreps;
	}

	rc = notify_ras_event(&evt);

	free_ras(&evt);
out_svcreps:
	D_FREE(info.svc_reps);
out_uuid:
	D_FREE(info.pool_uuid);

	return rc;
}

int
get_pool_svc_ranks(uuid_t pool_uuid, d_rank_list_t **svc_ranks)
{
	Srv__GetPoolSvcReq	gps_req = SRV__GET_POOL_SVC_REQ__INIT;
	Srv__GetPoolSvcResp	*gps_resp = NULL;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	d_rank_list_t		*ranks;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_UNINIT;
	}

	D_ALLOC(gps_req.uuid, DAOS_UUID_STR_SIZE);
	if (gps_req.uuid == NULL) {
		D_ERROR("failed to allocate pool uuid\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	uuid_unparse_lower(pool_uuid, gps_req.uuid);

	D_DEBUG(DB_MGMT, "fetching svc_ranks for "DF_UUID"\n",
		DP_UUID(pool_uuid));

	req_size = srv__get_pool_svc_req__get_packed_size(&gps_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		D_GOTO(out_uuid, rc = -DER_NOMEM);

	srv__get_pool_svc_req__pack(&gps_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_GET_POOL_SVC, &dreq);
	if (rc != 0) {
		D_FREE(req);
		goto out_uuid;
	}

	dreq->body.len = req_size;
	dreq->body.data = req;

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	gps_resp = srv__get_pool_svc_resp__unpack(
			NULL, dresp->body.len, dresp->body.data);
	if (gps_resp == NULL) {
		D_ERROR("failed to unpack resp (get pool svc)\n");
		D_GOTO(out_dresp, rc = -DER_NOMEM);
	}

	if (gps_resp->status != 0) {
		D_ERROR("failure fetching svc_ranks for "DF_UUID": "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(gps_resp->status));
		D_GOTO(out_dresp, rc = gps_resp->status);
	}

	ranks = uint32_array_to_rank_list(gps_resp->svcreps,
					  gps_resp->n_svcreps);
	if (ranks == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	D_DEBUG(DB_MGMT, "fetched %d svc_ranks for "DF_UUID"\n",
		ranks->rl_nr, DP_UUID(pool_uuid));
	*svc_ranks = ranks;

out_dresp:
	drpc_response_free(dresp);
out_dreq:
	/* also frees req via dreq->body.data */
	drpc_call_free(dreq);
out_uuid:
	D_FREE(gps_req.uuid);
out:
	return rc;
}

int
drpc_init(void)
{
	char	*path;
	int	 rc;

	D_ASPRINTF(path, "%s/%s", dss_socket_dir, "daos_server.sock");
	if (path == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}

	D_ASSERT(dss_drpc_ctx == NULL);
	rc = drpc_connect(path, &dss_drpc_ctx);
	if (dss_drpc_ctx == NULL)
		D_GOTO(out_path, 0);

	rc = notify_ready();
	if (rc != 0) {
		drpc_close(dss_drpc_ctx);
		dss_drpc_ctx = NULL;
	}

out_path:
	D_FREE(path);
out:
	return rc;
}

void
drpc_fini(void)
{
	int rc;

	D_ASSERT(dss_drpc_ctx != NULL);
	rc = drpc_close(dss_drpc_ctx);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	dss_drpc_ctx = NULL;
}
