/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_attach.c
 * \brief   9P version
 *
 * 9p_attach.c : _9P_interpretor, request ATTACH
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "export_mgr.h"
#include "log.h"
#include "fsal.h"
#include "nfs_exports.h"
#include "9p.h"

int _9p_attach(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *afid = NULL;
	u16 *uname_len = NULL;
	char *uname_str = NULL;
	u16 *aname_len = NULL;
	char *aname_str = NULL;
	u32 *n_uname = NULL;
	u32 err = 0;

	struct _9p_fid *pfid = NULL;

	struct gsh_export *export = NULL;
	fsal_status_t fsal_status;
	char exppath[MAXPATHLEN];
	struct gsh_buffdesc fh_desc;
	struct fsal_obj_handle *pfsal_handle;
	int port;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, afid, u32);
	_9p_getstr(cursor, uname_len, uname_str);
	_9p_getstr(cursor, aname_len, aname_str);
	_9p_getptr(cursor, n_uname, u32);

	LogDebug(COMPONENT_9P,
		 "TATTACH: tag=%u fid=%u afid=%d uname='%.*s' aname='%.*s' n_uname=%d",
		 (u32) *msgtag, *fid, *afid, (int) *uname_len, uname_str,
		 (int) *aname_len, aname_str, *n_uname);

	if (*fid >= _9P_FID_PER_CONN) {
		err = ERANGE;
		goto errout;
	}

	/*
	 * Find the export for the aname (using as well Path or Tag)
	 */
	snprintf(exppath, MAXPATHLEN, "%.*s", (int)*aname_len, aname_str);

	if (exppath[0] == '/')
		export = get_gsh_export_by_path(exppath, false);
	else
		export = get_gsh_export_by_tag(exppath);

	/* Did we find something ? */
	if (export == NULL) {
		err = ENOENT;
		goto errout;
	}

	port = get_port(&req9p->pconn->addrpeer);
	if (export->export_perms.options & EXPORT_OPTION_PRIVILEGED_PORT &&
	    port >= IPPORT_RESERVED) {
		LogInfo(COMPONENT_9P,
			"Port %d is too high for this export entry, rejecting client",
			port);
		err = EACCES;
		goto errout;
	}

	/* Set export and fid id in fid */
	pfid = gsh_calloc(1, sizeof(struct _9p_fid));

	pfid->fid = *fid;
	req9p->pconn->fids[*fid] = pfid;

	/* Is user name provided as a string or as an uid ? */
	if (*n_uname != _9P_NONUNAME) {
		/* Build the fid creds */
		err = _9p_tools_get_req_context_by_uid(*n_uname, pfid);
		if (err != 0) {
			err = -err;
			goto errout;
		}
	} else if (*uname_len != 0) {
		/* Build the fid creds */
		err = _9p_tools_get_req_context_by_name(*uname_len, uname_str,
							pfid);
		if (err != 0) {
			err = -err;
			goto errout;
		}
	} else {
		/* No n_uname nor uname */
		err = EINVAL;
		goto errout;
	}

	/* Keep track of the export in the req_ctx */
	pfid->export = export;
	get_gsh_export_ref(export);
	op_ctx->export = export;
	op_ctx->fsal_export = export->fsal_export;
	op_ctx->caller_addr = &req9p->pconn->addrpeer;
	op_ctx->export_perms = &req9p->pconn->export_perms;

	export_check_access();

	if (exppath[0] != '/' ||
	    !strcmp(exppath, export->fullpath)) {
		/* Check if root object is correctly set, fetch it, and take an
		 * LRU reference.
		 */
		fsal_status = nfs_export_get_root_entry(export, &pfid->pentry);
		if (FSAL_IS_ERROR(fsal_status)) {
			err = _9p_tools_errno(fsal_status);
			goto errout;
		}
	} else {
		fsal_status = op_ctx->fsal_export->exp_ops.lookup_path(
						op_ctx->fsal_export,
						exppath,
						&pfsal_handle, NULL);
		if (FSAL_IS_ERROR(fsal_status)) {
			err = _9p_tools_errno(fsal_status);
			goto errout;
		}

		pfsal_handle->obj_ops.handle_to_key(pfsal_handle,
						 &fh_desc);
		fsal_status = export->fsal_export->exp_ops.create_handle(
				 export->fsal_export, &fh_desc, &pfid->pentry,
				 NULL);
		if (FSAL_IS_ERROR(fsal_status)) {
			err = _9p_tools_errno(fsal_status);
			goto errout;
		}
	}

	/* Initialize state_t embeded in fid. The refcount is initialized
	 * to one to represent the state_t being embeded in the fid. This
	 * prevents it from ever being reduced to zero by dec_state_t_ref.
	 */
	pfid->state =
		op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							 STATE_TYPE_9P_FID,
							 NULL);

	glist_init(&pfid->state->state_data.fid.state_locklist);
	pfid->state->state_refcount = 1;

	/* Compute the qid */
	pfid->qid.type = _9P_QTDIR;
	pfid->qid.version = 0;	/* No cache, we want the client
				 * to stay synchronous with the server */
	pfid->qid.path = pfid->pentry->fileid;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RATTACH);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, pfid->qid);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RATTACH: tag=%u fid=%u qid=(type=%u,version=%u,path=%llu)",
		 *msgtag, *fid, (u32) pfid->qid.type, pfid->qid.version,
		 (unsigned long long)pfid->qid.path);

	return 1;

errout:

	if (export != NULL)
		put_gsh_export(export);

	if (pfid != NULL) {
		if (pfid->pentry != NULL)
			pfid->pentry->obj_ops.put_ref(pfid->pentry);
		if (pfid->ucred != NULL)
			release_9p_user_cred_ref(pfid->ucred);

		gsh_free(pfid);
	}

	return _9p_rerror(req9p, msgtag, err, plenout, preply);
}
