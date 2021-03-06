/*
 *
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal.h
 * @brief Main FSAL externs and functions
 * @note  not called by other header files.
 */

#ifndef FSAL_H
#define FSAL_H

#include "fsal_api.h"
#include "nfs23.h"
#include "nfs4_acls.h"

/**
 * @brief If we don't know how big a buffer we want for a link, use
 * this value.
 */

#define fsal_default_linksize (4096)

/**
 * @brief Pointer to FSAL module by number.
 * This is actually defined in common_pnfs.c
 */
extern struct fsal_module *pnfs_fsal[];

/**
 * @brief Delegations types list for the Delegations parameter in FSAL.
 * This is actually defined in exports.c
 */
extern struct config_item_list deleg_types[];

/**
 * @brief Thread Local Storage (TLS).
 *
 * TLS variables look like globals but since they are global only in the
 * context of a single thread, they do not require locks.  This is true
 * of all thread either within or separate from a/the fridge.
 *
 * All thread local storage is declared extern here.  The actual
 * storage declaration is in fridgethr.c.
 */

/**
 * @brief Operation context (op_ctx).
 *
 * This carries everything relevant to a protocol operation.
 * Space for the struct itself is allocated elsewhere.
 * Test/assert opctx != NULL first (or let the SEGV kill you)
 */

extern __thread struct req_op_context *op_ctx;

/* Export permissions for root op context, defined in protocol layer */
extern uint32_t root_op_export_options;
extern uint32_t root_op_export_set;

/**
 * @brief node id used to construct recovery directory in
 * cluster implementation.
 */
extern int g_nodeid;

/**
 * @brief Ops context for asynch and not protocol tasks that need to use
 * subsystems that depend on op_ctx.
 */

struct root_op_context {
	struct req_op_context req_ctx;
	struct req_op_context *old_op_ctx;
	struct user_cred creds;
	struct export_perms export_perms;
};

extern size_t open_fd_count;

static inline void init_root_op_context(struct root_op_context *ctx,
					struct gsh_export *exp,
					struct fsal_export *fsal_exp,
					uint32_t nfs_vers,
					uint32_t nfs_minorvers,
					uint32_t req_type)
{
	/* Initialize req_ctx.
	 * Note that a zeroed creds works just fine as root creds.
	 */
	memset(ctx, 0, sizeof(*ctx));
	ctx->req_ctx.creds = &ctx->creds;
	ctx->req_ctx.nfs_vers = nfs_vers;
	ctx->req_ctx.nfs_minorvers = nfs_minorvers;
	ctx->req_ctx.req_type = req_type;

	ctx->req_ctx.export = exp;
	ctx->req_ctx.fsal_export = fsal_exp;
	if (fsal_exp)
		ctx->req_ctx.fsal_module = fsal_exp->fsal;
	else if (op_ctx)
		ctx->req_ctx.fsal_module = op_ctx->fsal_module;

	ctx->req_ctx.export_perms = &ctx->export_perms;
	ctx->export_perms.set = root_op_export_set;
	ctx->export_perms.options = root_op_export_options;

	ctx->old_op_ctx = op_ctx;
	op_ctx = &ctx->req_ctx;
}

static inline void release_root_op_context(void)
{
	struct root_op_context *ctx;

	ctx = container_of(op_ctx, struct root_op_context, req_ctx);
	op_ctx = ctx->old_op_ctx;
}

/**
 * @brief init_complete used to indicate if ganesha is during
 * startup or not
 */
extern bool init_complete;

/******************************************************
 *                Structure used to define a fsal
 ******************************************************/

#include "FSAL/access_check.h"	/* rethink where this should go */

/**
 * Global fsal manager functions
 * used by nfs_main to initialize fsal modules.
 */

/* Called only within MODULE_INIT and MODULE_FINI functions of a fsal
 * module
 */

/**
 * @brief Register a FSAL
 *
 * This function registers an FSAL with ganesha and initializes the
 * public portion of the FSAL data structure, including providing
 * default operation vectors.
 *
 * @param[in,out] fsal_hdl      The FSAL module to register.
 * @param[in]     name          The FSAL's name
 * @param[in]     major_version Major version fo the API against which
 *                              the FSAL was written
 * @param[in]     minor_version Minor version of the API against which
 *                              the FSAL was written.
 *
 * @return 0 on success.
 * @return EINVAL on version mismatch.
 */

int register_fsal(struct fsal_module *fsal_hdl, const char *name,
		  uint32_t major_version, uint32_t minor_version,
		  uint8_t fsal_id);
/**
 * @brief Unregister an FSAL
 *
 * This function unregisters an FSAL from Ganesha.  It should be
 * called from the module finalizer as part of unloading.
 *
 * @param[in] fsal_hdl The FSAL to unregister
 *
 * @return 0 on success.
 * @return EBUSY if outstanding references or exports exist.
 */

int unregister_fsal(struct fsal_module *fsal_hdl);

/**
 * @brief Find and take a reference on an FSAL
 *
 * This function finds an FSAL by name and increments its reference
 * count.  It is used as part of export setup.  The @c put method
 * should be used  to release the reference before unloading.
 */
struct fsal_module *lookup_fsal(const char *name);

int load_fsal(const char *name,
	      struct fsal_module **fsal_hdl);

int fsal_load_init(void *node, const char *name,
		   struct fsal_module **fsal_hdl_p,
		   struct config_error_type *err_type);

struct fsal_args {
	char *name;
};

void *fsal_init(void *link_mem, void *self_struct);

struct subfsal_args {
	char *name;
	void *fsal_node;
};

int subfsal_commit(void *node, void *link_mem, void *self_struct,
		   struct config_error_type *err_type);

void destroy_fsals(void);
void emergency_cleanup_fsals(void);
void start_fsals(void);

void display_fsinfo(struct fsal_staticfsinfo_t *info);

int display_attrlist(struct display_buffer *dspbuf,
		     struct attrlist *attr, bool is_obj);

void log_attrlist(log_components_t component, log_levels_t level,
		  const char *reason, struct attrlist *attr, bool is_obj,
		  char *file, int line, char *function);

#define LogAttrlist(component, level, reason, attr, is_obj) \
	log_attrlist(component, level, reason, attr, is_obj, \
		     (char *) __FILE__, __LINE__, (char *) __func__)

const char *msg_fsal_err(fsal_errors_t fsal_err);
#define fsal_err_txt(s) msg_fsal_err((s).major)

/*
 * FSAL helpers
 */

enum cb_state {
	CB_ORIGINAL,
	CB_JUNCTION,
	CB_PROBLEM,
};

typedef fsal_errors_t (*fsal_getattr_cb_t)
	(void *opaque,
	 struct fsal_obj_handle *obj,
	 const struct attrlist *attr,
	 uint64_t mounted_on_fileid,
	 uint64_t cookie,
	 enum cb_state cb_state);

/**
 * Indicate whether this is a read or write operation, for fsal_rdwr.
 */

typedef enum io_direction__ {
	FSAL_IO_READ = 1,		/*< Reading */
	FSAL_IO_WRITE = 2,		/*< Writing */
	FSAL_IO_READ_PLUS = 3,	/*< Reading plus */
	FSAL_IO_WRITE_PLUS = 4	/*< Writing plus */
} fsal_io_direction_t;

/**
 * @brief Type of callback for fsal_readdir
 *
 * This callback provides the upper level protocol handling function
 * with one directory entry at a time.  It may use the opaque to keep
 * track of the structure it is filling, space used, and so forth.
 *
 * This function should return true if the entry has been added to the
 * caller's responde, or false if the structure is fulled and the
 * structure has not been added.
 */

struct fsal_readdir_cb_parms {
	void *opaque;		/*< Protocol specific parms */
	const char *name;	/*< Dir entry name */
	bool attr_allowed;	/*< True if caller has perm to getattr */
	bool in_result;		/*< true if the entry has been added to the
				 *< caller's responde, or false if the
				 *< structure is filled and the entry has not
				 *< been added. */
};

fsal_status_t fsal_setattr(struct fsal_obj_handle *obj, bool bypass,
			   struct state_t *state, struct attrlist *attr);

fsal_status_t fsal_access(struct fsal_obj_handle *obj,
			  fsal_accessflags_t access_type,
			  fsal_accessflags_t *allowed,
			  fsal_accessflags_t *denied);
fsal_status_t fsal_link(struct fsal_obj_handle *obj,
			struct fsal_obj_handle *dest_dir,
			const char *name);
fsal_status_t fsal_readlink(struct fsal_obj_handle *obj,
			    struct gsh_buffdesc *link_content);
fsal_status_t fsal_lookup(struct fsal_obj_handle *parent,
			  const char *name,
			  struct fsal_obj_handle **obj,
			  struct attrlist *attrs_out);
fsal_status_t fsal_lookupp(struct fsal_obj_handle *obj,
			   struct fsal_obj_handle **parent,
			   struct attrlist *attrs_out);
fsal_status_t fsal_create(struct fsal_obj_handle *parent,
			  const char *name,
			  object_file_type_t type,
			  struct attrlist *attrs,
			  const char *link_content,
			  struct fsal_obj_handle **obj,
			  struct attrlist *attrs_out);
void fsal_create_set_verifier(struct attrlist *sattr, uint32_t verf_hi,
			      uint32_t verf_lo);
bool fsal_create_verify(struct fsal_obj_handle *obj, uint32_t verf_hi,
			uint32_t verf_lo);

fsal_status_t fsal_read2(struct fsal_obj_handle *obj,
			 bool bypass,
			 struct state_t *state,
			 uint64_t offset,
			 size_t io_size,
			 size_t *bytes_moved,
			 void *buffer,
			 bool *eof,
			 struct io_info *info);
fsal_status_t fsal_write2(struct fsal_obj_handle *obj,
			  bool bypass,
			  struct state_t *state,
			  uint64_t offset,
			  size_t io_size,
			  size_t *bytes_moved,
			  void *buffer,
			  bool *sync,
			  struct io_info *info);
fsal_status_t fsal_rdwr(struct fsal_obj_handle *obj,
		      fsal_io_direction_t io_direction,
		      uint64_t offset, size_t io_size,
		      size_t *bytes_moved, void *buffer,
		      bool *eof,
		      bool *sync, struct io_info *info);
fsal_status_t fsal_readdir(struct fsal_obj_handle *directory, uint64_t cookie,
			   unsigned int *nbfound, bool *eod_met,
			   attrmask_t attrmask, fsal_getattr_cb_t cb,
			   void *opaque);
fsal_status_t fsal_remove(struct fsal_obj_handle *parent, const char *name);
fsal_status_t fsal_rename(struct fsal_obj_handle *dir_src,
			  const char *oldname,
			  struct fsal_obj_handle *dir_dest,
			  const char *newname);
fsal_status_t fsal_open(struct fsal_obj_handle *obj_hdl,
			fsal_openflags_t openflags);
fsal_status_t fsal_open2(struct fsal_obj_handle *in_obj,
			 struct state_t *state,
			 fsal_openflags_t openflags,
			 enum fsal_create_mode createmode,
			 const char *name,
			 struct attrlist *attr,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **obj,
			 struct attrlist *attrs_out);
fsal_status_t fsal_reopen2(struct fsal_obj_handle *obj,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   bool check_permission);
/**
 * @brief Close a file
 *
 * This handles both support_ex case and regular case (in case of
 * support_ex, close method is expected to manage whether file is
 * actually open or not, in old API case, close method should only
 * be closed if the file is open).
 *
 * In a change to the old way, non-regular files are just ignored.
 *
 * @param[in] obj	File to close
 * @return FSAL status
 */

static inline fsal_status_t fsal_close(struct fsal_obj_handle *obj_hdl)
{
	bool support_ex;

	if (obj_hdl->type != REGULAR_FILE) {
		/* Can only close a regular file */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	support_ex = obj_hdl->fsal->m_ops.support_ex(obj_hdl);

	if (!support_ex && obj_hdl->obj_ops.status(obj_hdl) == FSAL_O_CLOSED) {
		/* If not support_ex and the file isn't open, return no error.
		 */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* Otherwise, return the result of close method. */
	fsal_status_t status = obj_hdl->obj_ops.close(obj_hdl);

	if (!FSAL_IS_ERROR(status) && !support_ex)
		atomic_dec_size_t(&open_fd_count);

	return status;
}

fsal_status_t fsal_statfs(struct fsal_obj_handle *obj,
			  fsal_dynamicfsinfo_t *dynamicinfo);
fsal_status_t fsal_commit(struct fsal_obj_handle *obj_hdl, off_t offset,
			 size_t len);
fsal_status_t fsal_verify2(struct fsal_obj_handle *obj,
			   fsal_verifier_t verifier);
bool fsal_is_open(struct fsal_obj_handle *obj);

/**
 * @brief Pepare an attrlist for fetching attributes.
 *
 * @param[in,out] attrs   The attrlist to work with
 * @param[in]             The mask to use for the fetch
 *
 */

static inline void fsal_prepare_attrs(struct attrlist *attrs,
				      attrmask_t mask)
{
	memset(attrs, 0, sizeof(*attrs));
	attrs->mask = mask;
}

/**
 * @brief Release any extra resources from an attrlist.
 *
 * @param[in] attrs   The attrlist to work with
 *
 */

static inline void fsal_release_attrs(struct attrlist *attrs)
{
	if (attrs->acl != NULL) {
		int acl_status;

		acl_status = nfs4_acl_release_entry(attrs->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);

		/* Poison the acl since we no longer hold a reference. */
		attrs->acl = NULL;
		attrs->mask &= ~ATTR_ACL;
	}
}

/**
 * @brief Copy a set of attributes
 *
 * If ACL is requested in dest->mask, then ACL reference is acquired, otherwise
 * acl pointer is set to NULL.
 *
 * @param[in,out] dest       The attrlist to receive the copy (mask must be set)
 * @param[in]     src        The attrlist to make a copy of
 * @param[in]     pass_refs  If true, pass the ACL reference to dest.
 *
 */

static inline void fsal_copy_attrs(struct attrlist *dest,
				   struct attrlist *src,
				   bool pass_refs)
{
	bool acl_asked = dest->mask & ATTR_ACL;

	*dest = *src;

	if (pass_refs) {
		/* Pass any ACL reference to the dest, so remove from src
		 * without adjusting the refcount.
		 */
		src->acl = NULL;
		src->mask &= ~ATTR_ACL;
	} else if (dest->acl != NULL && acl_asked) {
		/* Take reference on ACL if necessary */
		nfs4_acl_entry_inc_ref(dest->acl);
	} else {
		/* Make sure acl is NULL and don't pass a ref back (so
		 * caller when calling fsal_release_attrs will not have to
		 * release the ACL reference).
		 */
		dest->acl = NULL;
		dest->mask &= ~ATTR_ACL;
	}
}

/**
 * @brief Return a changeid4 for this file.
 *
 * @param[in] obj   The file to query.
 *
 * @return A changeid4 indicating the last modification of the file.
 */

static inline changeid4
fsal_get_changeid4(struct fsal_obj_handle *obj)
{
	struct attrlist attrs;
	fsal_status_t status;
	changeid4 change;

	fsal_prepare_attrs(&attrs, ATTR_CHANGE | ATTR_CHGTIME);

	status = obj->obj_ops.getattrs(obj, &attrs);

	if (FSAL_IS_ERROR(status))
		return 0;

	change = (changeid4) attrs.change;

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	return change;
}

static inline
enum fsal_create_mode nfs4_createmode_to_fsal(createmode4 createmode)
{
	return (enum fsal_create_mode) 1 + (unsigned int) createmode;
}

static inline
enum fsal_create_mode nfs3_createmode_to_fsal(createmode3 createmode)
{
	return (enum fsal_create_mode) 1 + (unsigned int) createmode;
}

/**
 * @brief Determine if the openflags associated with an fd indicate it
 * is not open in a mode usable by the caller.
 *
 * The caller may pass FSAL_O_ANY to indicate any mode of open (RDONLY,
 * WRONLY, or RDWR is useable - often just to fetch attributes or something).
 *
 * Note that FSAL_O_SYNC is considered by this function, so the caller
 * expects the fd to be considered not usable if O_SYNC doesn't match.
 *
 * @param[in] fd_openflags The openflags describing the fd
 * @param[in] to_openflags The openflags describing the desired mode
 */

static inline bool not_open_usable(fsal_openflags_t fd_openflags,
				   fsal_openflags_t to_openflags)
{
	/* 1. fd_openflags will NEVER be FSAL_O_ANY.
	 * 2. If to_openflags == FSAL_O_ANY, the first half will be true if the
	 *    file is closed, and the second half MUST be true (per statement 1)
	 * 3. If to_openflags is anything else, the first half will be true and
	 *    the second half will be true if fd_openflags does not include
	 *    the requested modes.
	 */
	return (to_openflags != FSAL_O_ANY || fd_openflags == FSAL_O_CLOSED)
	       && ((fd_openflags & to_openflags) != to_openflags);
}

/**
 * @brief Determine if the openflags associated with an fd indicate it
 * is open in a mode usable by the caller.
 *
 * The caller may pass FSAL_O_ANY to indicate any mode of open (RDONLY,
 * WRONLY, or RDWR is useable - often just to fetch attributes or something).
 *
 * Note that this function is not just an inversion of the above function
 * because O_SYNC is not considered.
 *
 * @param[in] fd_openflags The openflags describing the fd
 * @param[in] to_openflags The openflags describing the desired mode
 */

static inline bool open_correct(fsal_openflags_t fd_openflags,
				fsal_openflags_t to_openflags)
{
	return (to_openflags == FSAL_O_ANY && fd_openflags != FSAL_O_CLOSED)
	       || (to_openflags != FSAL_O_ANY
		   && (fd_openflags & to_openflags & FSAL_O_RDWR)
					== (to_openflags & FSAL_O_RDWR));
}

#endif				/* !FSAL_H */
/** @} */
