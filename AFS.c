/* $Id: AFS.c 501 2008-09-26 19:27:07Z wollman $ */

/*
 * AFS.c: a Ruby extension for accessing AFS (currently restricted to just
 * the protection database)
 */

#include "ruby.h"

/* 
 * Older versions of OpenAFS, like the one in Debian etch, haven't
 * renamed the Common Error functions to afs_*() yet.  The CSAIL version
 * has.
 */
#ifndef HAVE_AFS_ERROR_MESSAGE
#define afs_error_message error_message
#endif

#include <afs/dirpath.h>
#include <afs/ptclient.h>
#include <afs/ptuser.h>
#include <afs/com_err.h>

static int afs_library_initialized;

struct protection_object {
	struct prcheckentry e;
	int deleted;
};

#define	PF_STATUS_ANY	0x80
#define	PF_STATUS_MEM	0x40
#define	PF_OWNED_ANY	0x20
#define	PF_MEMBER_ANY	0x10
#define	PF_MEMBER_MEM	0x08
#define	PF_ADD_ANY	0x04
#define	PF_ADD_MEM	0x02
#define	PF_REMOVE_MEM	0x01

/*
 * The Module object for this module will be stored here by Init_AFS()
 */
VALUE mAFS = Qnil;
VALUE mPrivacyFlags = Qnil;

/* Exceptions */
VALUE eProgrammerError = Qnil;
VALUE eAFSLibraryError = Qnil;

/*
 * The Class objects for this module will be stored here by Init_AFS()
 */
VALUE cProtectionObject = Qnil;
VALUE cUser = Qnil;
VALUE cGroup = Qnil;

/*
 * Likewise the Symbol objects.
 */
VALUE sOwner = Qnil;
VALUE sMembers = Qnil;
VALUE sWorld = Qnil;

VALUE vSecLevel = Qnil;
VALUE vCellName = Qnil;
VALUE vConfDir = Qnil;

/*
 * Singleton methods
 */
static VALUE afs_get_seclevel(VALUE self);
static VALUE afs_set_seclevel(VALUE self, VALUE newval);
static VALUE afs_get_cellname(VALUE self);
static VALUE afs_set_cellname(VALUE self, VALUE newval);
static VALUE afs_get_confdir(VALUE self);
static VALUE afs_set_confdir(VALUE self, VALUE newval);

static VALUE po_new(VALUE self, VALUE id_or_name);
static VALUE po_delete(VALUE self, VALUE id_or_name);
static VALUE user_new(VALUE self, VALUE id_or_name);
static VALUE user_create(int argc, VALUE *argv, VALUE self);
static VALUE group_new(VALUE self, VALUE id_or_name);
static VALUE group_create(int argc, VALUE *argv, VALUE self);
static VALUE po_translate(VALUE self, VALUE name_or_id);
static VALUE group_find_all(VALUE self);
static VALUE po_find_all(VALUE self);
static VALUE user_find_all(VALUE self);
static VALUE user_get_max_id(VALUE self);
static VALUE user_set_max_id(VALUE self, VALUE newval);
static VALUE group_get_max_id(VALUE self);
static VALUE group_set_max_id(VALUE self, VALUE newval);

/*
 * Methods
 */
static VALUE po_add_to_group(VALUE self, VALUE group);
static VALUE po_remove_from_group(VALUE self, VALUE group);
static VALUE po_delete_instance(VALUE self);
static VALUE po_deleted_p(VALUE self);
static VALUE group_add_member(VALUE self, VALUE user);
static VALUE group_remove_member(VALUE self, VALUE user);
static VALUE group_members(VALUE self);
static VALUE user_memberships(VALUE self);
static VALUE po_get_creator(VALUE self);
static VALUE group_get_owner(VALUE self);
static VALUE group_set_owner(VALUE self, VALUE newowner);
static VALUE group_has_member_p(VALUE self, VALUE other);
static VALUE po_is_member_p(VALUE self, VALUE group);
static VALUE po_get_id(VALUE self);
static VALUE po_set_id(VALUE self, VALUE newval);
static VALUE po_get_name(VALUE self);
static VALUE po_set_name(VALUE self, VALUE newval);
static VALUE po_get_flags(VALUE self);
static VALUE po_set_flags(VALUE self, VALUE newval);
static VALUE po_ownerships(VALUE self);
static VALUE user_get_group_quota(VALUE self);
static VALUE user_set_group_quota(VALUE self, VALUE newval);
static VALUE user_get_group_count(VALUE self);
static VALUE group_get_user_quota(VALUE self);
static VALUE group_set_user_quota(VALUE self, VALUE newval);
static VALUE group_get_user_count(VALUE self);
static VALUE po_equal(VALUE self, VALUE other);

void
Init_AFS(void)
{
	VALUE mSecLevel = Qnil;

	mAFS = rb_define_module("AFS");
	mSecLevel = rb_define_module_under(mAFS, "SecLevel");

	/* Functions and constants in the AFS module */
	rb_define_const(mSecLevel, "None", INT2FIX(0));
	rb_define_const(mSecLevel, "Token", INT2FIX(1));
	rb_define_const(mSecLevel, "KeyFile", INT2FIX(2));
	rb_global_variable(&vSecLevel);
	rb_global_variable(&vCellName);
	rb_global_variable(&vConfDir);
	vSecLevel = INT2FIX(1);
	vConfDir = rb_str_new2(AFSDIR_CLIENT_ETC_DIR);

	rb_define_singleton_method(mAFS, "security_level", afs_get_seclevel, 0);
	rb_define_singleton_method(mAFS, "security_level=", afs_set_seclevel,
	    1);
	rb_define_singleton_method(mAFS, "cell_name", afs_get_cellname, 0);
	rb_define_singleton_method(mAFS, "cell_name=", afs_set_cellname, 1);
	rb_define_singleton_method(mAFS, "config_dir", afs_get_confdir, 0);
	rb_define_singleton_method(mAFS, "config_dir=", afs_set_confdir, 1);

	eProgrammerError = rb_define_class_under(mAFS, "ProgrammerError",
	    rb_eRuntimeError);
	eAFSLibraryError = rb_define_class_under(mAFS, "LibraryError",
	    rb_eRuntimeError);

	/* ProtectionObject methods */
	cProtectionObject = rb_define_class_under(mAFS, "ProtectionObject",
	    rb_cObject);
	rb_define_singleton_method(cProtectionObject, "new", po_new, 1);
	rb_define_singleton_method(cProtectionObject, "delete", po_delete, 1);
	rb_define_singleton_method(cProtectionObject, "translate",
	    po_translate, 1);
	rb_define_singleton_method(cProtectionObject, "find_all", po_find_all,
				   0);
	rb_define_method(cProtectionObject, "delete", po_delete_instance, 0);
	rb_define_method(cProtectionObject, "deleted?", po_deleted_p, 0);
	rb_define_method(cProtectionObject, "==", po_equal, 1);
	rb_define_method(cProtectionObject, "===", po_equal, 1);
	rb_define_method(cProtectionObject, "flags", po_get_flags, 0);
	rb_define_method(cProtectionObject, "flags=", po_set_flags, 1);
	rb_define_method(cProtectionObject, "ptsid", po_get_id, 0);
	rb_define_method(cProtectionObject, "ptsid=", po_set_id, 1);
	rb_define_method(cProtectionObject, "name", po_get_name, 0);
	rb_define_method(cProtectionObject, "name=", po_set_name, 1);
	rb_define_method(cProtectionObject, "is_member?", po_is_member_p, 1);
	rb_define_method(cProtectionObject, "ownerships", po_ownerships, 0);
	rb_define_method(cProtectionObject, "add_to_group", po_add_to_group,
	    1);
	rb_define_method(cProtectionObject, "remove_from_group",
	    po_remove_from_group, 1);
	rb_define_method(cProtectionObject, "creator", po_get_creator, 0);

	/* Group methods */
	cGroup = rb_define_class_under(mAFS, "Group", cProtectionObject);
	rb_define_singleton_method(cGroup, "new", group_new, 1);
	rb_define_singleton_method(cGroup, "create", group_create, -1);
	rb_define_singleton_method(cGroup, "find_all", group_find_all, 0);
	rb_define_singleton_method(cGroup, "max_id", group_get_max_id, 0);
	rb_define_singleton_method(cGroup, "max_id=", group_set_max_id, 1);
	rb_define_method(cGroup, "add_member", group_add_member, 1);
	rb_define_alias(cGroup, "<<", "add_member");
	rb_define_method(cGroup, "remove_member", group_remove_member, 1);
	rb_define_method(cGroup, "members", group_members, 0);
	rb_define_method(cGroup, "has_member?", group_has_member_p, 1);
	rb_define_method(cGroup, "owner", group_get_owner, 0);
	rb_define_method(cGroup, "owner=", group_set_owner, 1);
	rb_define_method(cGroup, "user_quota", group_get_user_quota, 0);
	rb_define_method(cGroup, "user_quota=", group_set_user_quota, 1);
	rb_define_method(cGroup, "user_count", group_get_user_count, 0);

	/* User methods */
	cUser = rb_define_class_under(mAFS, "User", cProtectionObject);
	rb_define_singleton_method(cUser, "new", user_new, 1);
	rb_define_singleton_method(cUser, "create", user_create, -1);
	rb_define_singleton_method(cUser, "find_all", user_find_all, 0);
	rb_define_singleton_method(cUser, "max_id", user_get_max_id, 0);
	rb_define_singleton_method(cUser, "max_id=", user_set_max_id, 1);
	rb_define_method(cUser, "memberships", user_memberships, 0);
	rb_define_method(cUser, "group_quota", user_get_group_quota, 0);
	rb_define_method(cUser, "group_quota=", user_set_group_quota, 1);
	rb_define_method(cUser, "group_count", user_get_group_count, 0);

	/* PrivacyFlags constants */
	mPrivacyFlags = rb_define_module_under(mAFS, "PrivacyFlags");
#define PF(name)	\
	rb_define_const(mPrivacyFlags, #name, INT2FIX(PF_##name))
	PF(STATUS_ANY);
	PF(STATUS_MEM);
	PF(OWNED_ANY);
	PF(MEMBER_ANY);
	PF(MEMBER_MEM);
	PF(ADD_ANY);
	PF(ADD_MEM);
	PF(REMOVE_MEM);
#undef PF
}

static void
assert_not_initialized(const char *setting)
{
	if (afs_library_initialized)
		rb_raise(eProgrammerError,
		    "cannot alter %s after AFS library has been initialized",
		    setting);
}

static void
assert_not_deleted(struct protection_object *po)
{
	if (po->deleted)
		rb_raise(eProgrammerError,
			 "attempted use of deleted ProtectionObject");
}

static void
assert_success(int error, const char *function)
{
	if (error != 0)
		rb_raise(eAFSLibraryError, "%s: %s", function,
		    afs_error_message(error));
}

static void
ensure_initialized(void)
{
	if (!afs_library_initialized) {
		pr_Initialize(FIX2INT(vSecLevel),
		    StringValueCStr(vConfDir),
		    vCellName == Qnil ? NULL : StringValueCStr(vCellName));
		afs_library_initialized = 1;
	}
}

static VALUE
afs_get_seclevel(VALUE self)
{
	return vSecLevel;
}

static VALUE
afs_set_seclevel(VALUE self, VALUE newval)
{
	assert_not_initialized("security level");
	Check_Type(newval, T_FIXNUM);
	return (vSecLevel = newval);
}

static VALUE
afs_get_cellname(VALUE self)
{
	return vCellName;
}

static VALUE
afs_set_cellname(VALUE self, VALUE newval)
{
	assert_not_initialized("cell name");
	if (newval != Qnil)
		Check_Type(newval, T_STRING);
	SafeStringValue(newval);
	return (vCellName = newval);
}

static VALUE
afs_get_confdir(VALUE self)
{
	return vConfDir;
}

static VALUE
afs_set_confdir(VALUE self, VALUE newval)
{
	assert_not_initialized("configuration directory");
	if (newval == Qnil) {
		newval = rb_str_new2(AFSDIR_CLIENT_ETC_DIR);
	} else {
		Check_Type(newval, T_STRING);
		SafeStringValue(newval);
	}
	return (vConfDir = newval);
}

static VALUE
po_new_internal(VALUE klass)
{
	struct protection_object *po;
	VALUE obj;

	obj = Data_Make_Struct(klass, struct protection_object, NULL,
			       free, po);
	return (obj);
}

static void
assert_name_ok(VALUE obj)
{
	SafeStringValue(obj);
	if (RSTRING_LEN(obj) >= PR_MAXNAMELEN)
		rb_raise(eAFSLibraryError,
		    "protection object name too long (%lu >= %lu)",
		    (unsigned long)RSTRING_LEN(obj),
		    (unsigned long)PR_MAXNAMELEN);
}

static VALUE
get_name(VALUE source)
{
	if (TYPE(source) == T_STRING)
		return (source);
	else
		return (rb_funcall(source, rb_intern("name"), 0));
}

static VALUE
po_new(VALUE self, VALUE id_or_name)
{
	struct protection_object *po;
	VALUE obj;
	afs_int32 id;
	int error;

	ensure_initialized();
	if (TYPE(id_or_name) == T_STRING) {
		assert_name_ok(id_or_name);
		error = pr_SNameToId(StringValueCStr(id_or_name), &id);
		assert_success(error, "pr_SNameToId");
	} else {
		id = NUM2INT(id_or_name);
	}

	obj = po_new_internal(id < 0 ? cGroup : cUser);
	Data_Get_Struct(obj, struct protection_object, po);

	error = pr_ListEntry(id, &po->e);
	assert_success(error, "pr_ListEntry");
	po->deleted = 0;

	return (obj);
}

static VALUE
po_delete(VALUE self, VALUE id_or_name)
{
	int error;

	ensure_initialized();
	if (TYPE(id_or_name) == T_STRING) {
		assert_name_ok(id_or_name);
		error = pr_Delete(StringValueCStr(id_or_name));
		assert_success(error, "pr_Delete");
	} else {
		error = pr_DeleteByID(NUM2INT(id_or_name));
		assert_success(error, "pr_DeleteByID");
	}

	return Qnil;
}

static VALUE
user_new(VALUE self, VALUE id_or_name)
{
	VALUE rv;
	struct protection_object *po;

	rv = po_new(self, id_or_name);
	Data_Get_Struct(rv, struct protection_object, po);
	if (po->e.id < 0) {
		rb_raise(eAFSLibraryError,
			 "`%s' (id %ld) exists but is not a user",
			 po->e.name, (long)po->e.id);
	}
	return (rv);
}

static VALUE
group_new(VALUE self, VALUE id_or_name)
{
	VALUE rv;
	struct protection_object *po;

	rv = po_new(self, id_or_name);
	Data_Get_Struct(rv, struct protection_object, po);
	if (po->e.id >= 0) {

		rb_raise(eAFSLibraryError,
			 "`%s' (id %ld) exists but is not a group",
			 po->e.name, (long)po->e.id);
	}
	return (rv);
}

/*
 * Create a user.
 * One argument (the name) is mandatory.
 * The second argument (the desired ID) is optional.
 * Returns an AFS::User object.
 */
static VALUE
user_create(int argc, VALUE *argv, VALUE self)
{
	afs_int32 id;
	int error;

	if (!(argc == 1 || argc == 2))
		rb_raise(rb_eArgError, 
			 "wrong number of arguments (%d for 2)", argc);
	if (argc == 2 && argv[1] != Qnil) {
		id = NUM2INT(argv[1]);
		if (id <= 0)
			rb_raise(eAFSLibraryError,
				 "ptsid (%ld) must be positive for users",
				 (long)id);
	} else
		id = 0;		/* special flag to pr_CreateUser */
	assert_name_ok(argv[0]);
	ensure_initialized();
	error = pr_CreateUser(StringValueCStr(argv[0]), &id);
	assert_success(error, "pr_CreateUser");

	return (po_new(self, INT2NUM(id)));
}

/*
 * Create a group.
 * One argument (the name) in mandatory.
 * The second argument (the ID) and the third argument (the owner) are
 * optional.
 */
static VALUE
group_create(int argc, VALUE *argv, VALUE self)
{
	afs_int32 id;
	int error;
	char *owner;
	VALUE oobj;

	if (argc < 1 || argc > 3)
		rb_raise(rb_eArgError, 
			 "wrong number of arguments (%d for 3)", argc);
	if (argc == 3) {
		oobj = get_name(argv[2]);
		assert_name_ok(oobj);
		owner = StringValueCStr(oobj);
	} else
		owner = NULL;

	if (argc >= 2 && argv[1] != Qnil) {
		id = NUM2INT(argv[1]);
		if (id >= 0)
			rb_raise(eAFSLibraryError,
				 "ptsid (%ld) must be negative for groups",
				 (long)id);
	} else
		id = 0;		/* special flag to pr_CreateGroup */
	assert_name_ok(argv[0]);
	ensure_initialized();
	error = pr_CreateGroup(StringValueCStr(argv[0]), owner, &id);
	assert_success(error, "pr_CreateGroup");

	return (po_new(self, INT2NUM(id)));
}

static VALUE
po_translate(VALUE self, VALUE id_or_name)
{
	VALUE obj;
	afs_int32 id;
	int error;
	char name[PR_MAXNAMELEN + 1];

	ensure_initialized();
	if (TYPE(id_or_name) == T_STRING) {
		assert_name_ok(id_or_name);
		error = pr_SNameToId(StringValueCStr(id_or_name), &id);
		assert_success(error, "pr_SNameToId");
		obj = INT2NUM(id);
	} else {
		error = pr_SIdToName(NUM2INT(id_or_name), name);
		assert_success(error, "pr_SIdToName");
		name[PR_MAXNAMELEN] = '\0'; /* make sure it's terminated */
		obj = rb_str_new2(name);
	}

	return (obj);
}

/*
 * Get a list of protection objects, yielding them one at a time (if a block 
 * is given) or returning them in an array (otherwise).
 */
static VALUE
find_all_internal(VALUE self, int flags)
{
	afs_int32 index, nentries, nextindex, error;
	struct prlistentries *e;
	VALUE ary, obj;
	int block_given, i;
	struct protection_object *po;

	ensure_initialized();
	block_given = rb_block_given_p();
	if (!block_given)
		ary = rb_ary_new();
	else
		ary = Qnil;

	nextindex = 0;
	do {
		e = NULL;
		index = nextindex;
		error = pr_ListEntries(flags, index, &nentries, &e, 
				       &nextindex);
		assert_success(error, "pr_ListEntries");

		for (i = 0; i < nentries; i++) {
			/*
			 * Avoid making a pr_ListEntry call for each
			 * object returned by copying the data from
			 * our "struct prlistentries" into the object's
			 * "struct prcheckentry" manually.  (They are
			 * actually identical structures, but we can't
			 * depend on this.)
			 */
			obj = po_new_internal(e[i].id < 0 ? cGroup : cUser);
			Data_Get_Struct(obj, struct protection_object, po);
			po->e.flags = e[i].flags;
			po->e.id = e[i].id;
			po->e.owner = e[i].owner;
			po->e.creator = e[i].creator;
			po->e.ngroups = e[i].ngroups;
			po->e.nusers = e[i].nusers;
			po->e.count = e[i].count;
			strncpy(po->e.name, e[i].name, PR_MAXNAMELEN);

			if (block_given)
				rb_yield(obj);
			else
				rb_ary_push(ary, obj);
		}

		if (e != NULL)
			free(e);
	} while (nextindex > index);
	return (ary);
}

static VALUE
group_find_all(VALUE self)
{
	return (find_all_internal(self, PRGROUPS));
}

static VALUE
user_find_all(VALUE self)
{
	return (find_all_internal(self, PRUSERS));
}

static VALUE
po_find_all(VALUE self)
{
	return (find_all_internal(self, PRGROUPS | PRUSERS));
}

static VALUE
group_get_max_id(VALUE self)
{
	afs_int32 max_id;
	int error;

	ensure_initialized();
	error = pr_ListMaxGroupId(&max_id);
	assert_success(error, "pr_ListMaxGroupId");
	return (INT2NUM(max_id));
}

static VALUE
user_get_max_id(VALUE self)
{
	afs_int32 max_id;
	int error;

	ensure_initialized();
	error = pr_ListMaxUserId(&max_id);
	assert_success(error, "pr_ListMaxUserId");
	return (INT2NUM(max_id));
}

static VALUE
group_set_max_id(VALUE self, VALUE newval)
{
	afs_int32 max_id;
	int error;

	max_id = NUM2INT(newval);
	ensure_initialized();
	error = pr_SetMaxGroupId(max_id);
	assert_success(error, "pr_SetMaxGroupId");
	return (INT2NUM(max_id));
}

static VALUE
user_set_max_id(VALUE self, VALUE newval)
{
	afs_int32 max_id;
	int error;

	max_id = NUM2INT(newval);
	ensure_initialized();
	error = pr_SetMaxUserId(max_id);
	assert_success(error, "pr_SetMaxUserId");
	return (INT2NUM(max_id));
}

static VALUE
po_add_to_group(VALUE self, VALUE group)
{
	struct protection_object *po;
	int error;
	VALUE gname;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);

	gname = get_name(group);
	assert_name_ok(gname);
	ensure_initialized();
	error = pr_AddToGroup(po->e.name, StringValueCStr(gname));
	assert_success(error, "pr_AddToGroup");
	return (group_new(cGroup, gname));
}

static VALUE
po_remove_from_group(VALUE self, VALUE group)
{
	struct protection_object *po;
	int error;
	VALUE gname;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);

	gname = get_name(group);
	assert_name_ok(gname);
	ensure_initialized();
	error = pr_RemoveUserFromGroup(po->e.name, StringValueCStr(gname));
	assert_success(error, "pr_RemoveUserFromGroup");
	return (group_new(cGroup, gname));
}

static VALUE
group_add_member(VALUE self, VALUE member)
{
	struct protection_object *po;
	int error;
	VALUE poname;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	poname = get_name(member);
	ensure_initialized();
	assert_name_ok(poname);
	error = pr_AddToGroup(po->e.name, StringValueCStr(poname));
	assert_success(error, "pr_AddToGroup");
	return (self);
}

static VALUE
group_remove_member(VALUE self, VALUE member)
{
	struct protection_object *po;
	int error;
	VALUE poname;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	poname = get_name(member);
	ensure_initialized();
	assert_name_ok(poname);
	error = pr_RemoveUserFromGroup(po->e.name, StringValueCStr(poname));
	assert_success(error, "pr_RemoveUserFromGroup");
	return (self);
}

static VALUE
group_members(VALUE self)
{
	struct protection_object *po;
	int block_given, error, i;
	VALUE ary, obj;
	namelist members;
	idlist member_ids;

	block_given = rb_block_given_p();
	if (block_given)
		ary = Qnil;
	else
		ary = rb_ary_new();
	members.namelist_len = 0;
	members.namelist_val = NULL;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	ensure_initialized();

	error = pr_IDListMembers(po->e.id, &members);
	assert_success(error, "pr_ListMembers");
	if (members.namelist_len > 0) {
		member_ids.idlist_len = 0;
		member_ids.idlist_val = NULL;
		error = pr_NameToId(&members, &member_ids);
		assert_success(error, "pr_NameToId");
		for (i = 0; i < member_ids.idlist_len; i++) {
			obj = po_new(cProtectionObject, 
				     INT2NUM(member_ids.idlist_val[i]));
			if (block_given)
				rb_yield(obj);
			else
				rb_ary_push(ary, obj);
		}
		if (member_ids.idlist_val != NULL)
			free(member_ids.idlist_val);
	}
	if (members.namelist_val != NULL)
		free(members.namelist_val);
	return (ary);
}

/*
 * For the moment at least, group_members() and user_memberships() have
 * identical implementations.
 */
static VALUE
user_memberships(VALUE self)
{
	return (group_members(self));
}

static VALUE
group_get_owner(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	return (po_new(cProtectionObject, INT2NUM(po->e.owner)));
}

static VALUE
group_set_owner(VALUE self, VALUE newowner)
{
	struct protection_object *po;
	struct prcheckentry e;
	VALUE name;
	int error;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	name = get_name(newowner);

	assert_name_ok(name);
	ensure_initialized();
	/* bogus interface: newname must be passed as "" rather than NULL */
	error = pr_ChangeEntry(po->e.name, "", NULL, StringValueCStr(name));
	assert_success(error, "pr_ChangeEntry");

	error = pr_ListEntry(po->e.id, &e);
	assert_success(error, "pr_ListEntry");
	po->e.owner = e.owner;

	return (name);
}

static VALUE
group_has_member_p(VALUE self, VALUE other)
{
	struct protection_object *po;
	int error;
	VALUE name;
	afs_int32 flag;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	name = get_name(other);
	assert_name_ok(name);
	ensure_initialized();
	error = pr_IsAMemberOf(StringValueCStr(name), po->e.name, &flag);
	assert_success(error, "pr_IsAMemberOf");
	return (flag ? Qtrue : Qfalse);
}

static VALUE
po_is_member_p(VALUE self, VALUE group)
{
	struct protection_object *po;
	int error;
	VALUE name;
	afs_int32 flag;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	name = get_name(group);
	assert_name_ok(name);
	ensure_initialized();
	error = pr_IsAMemberOf(po->e.name, StringValueCStr(name), &flag);
	assert_success(error, "pr_IsAMemberOf");
	return (flag ? Qtrue : Qfalse);
}

static VALUE
po_delete_instance(VALUE self)
{
	struct protection_object *po;
	int error;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	ensure_initialized();
	error = pr_DeleteByID(po->e.id);
	assert_success(error, "pr_DeleteByID");
	po->deleted = 1;
	rb_obj_freeze(self);
	return (self);
}

static VALUE
po_deleted_p(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	return (po->deleted ? Qtrue : Qfalse);
}

static VALUE
po_get_id(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	return (INT2NUM(po->e.id));
}

static VALUE
po_set_id(VALUE self, VALUE newval)
{
	struct protection_object *po;
	int error;
	afs_int32 newval_i;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	ensure_initialized();
	newval_i = NUM2INT(newval);
	/* bogus interface: newname must be passed as "" rather than NULL */
	error = pr_ChangeEntry(po->e.name, "", &newval_i, NULL);
	assert_success(error, "pr_ChangeEntry");
	po->e.id = newval_i;
	return (INT2NUM(newval_i));
}

static VALUE
po_get_name(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	return (rb_str_new2(po->e.name));
}

static VALUE
po_set_name(VALUE self, VALUE newval)
{
	struct protection_object *po;
	int error;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	ensure_initialized();
	assert_name_ok(newval);
	error = pr_ChangeEntry(po->e.name, StringValueCStr(newval),
			       NULL, NULL);
	assert_success(error, "pr_ChangeEntry");
	error = pr_ListEntry(po->e.id, &po->e);
	assert_success(error, "pr_ListEntry");
	return (newval);
}

static VALUE
po_get_flags(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	return (INT2NUM(po->e.flags));
}

static VALUE
po_set_flags(VALUE self, VALUE newval)
{
	struct protection_object *po;
	afs_int32 error, flags;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	flags = NUM2INT(newval);

	ensure_initialized();
	error = pr_SetFieldsEntry(po->e.id, PR_SF_ALLBITS, flags, 0, 0);
	assert_success(error, "pr_SetFieldsEntry");
	po->e.flags = flags;
	return (newval);
}

static VALUE
po_get_creator(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	return (po_new(cProtectionObject, INT2NUM(po->e.creator)));
}

static VALUE
po_ownerships(VALUE self)
{
	struct protection_object *po;
	int block_given, error, i;
	VALUE ary, obj;
	namelist owned;
	idlist owned_ids;
	afs_int32 id, more;

	block_given = rb_block_given_p();
	if (block_given)
		ary = Qnil;
	else
		ary = rb_ary_new();
	more = 0;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	ensure_initialized();

	do {
		owned.namelist_len = 0;
		owned.namelist_val = NULL;
		error = pr_ListOwned(po->e.id, &owned, &more);
		assert_success(error, "pr_ListOwned");
		if (owned.namelist_len > 0) {
			owned_ids.idlist_len = 0;
			owned_ids.idlist_val = NULL;
			error = pr_NameToId(&owned, &owned_ids);
			assert_success(error, "pr_NameToId");
			for (i = 0; i < owned_ids.idlist_len; i++) {
				id = owned_ids.idlist_val[i];
				obj = po_new(cProtectionObject, INT2NUM(id));
				if (block_given)
					rb_yield(obj);
				else
					rb_ary_push(ary, obj);
			}
			if (owned_ids.idlist_val != NULL)
				free(owned_ids.idlist_val);
		}
		if (owned.namelist_val != NULL)
			free(owned.namelist_val);
	} while (more);
	return (ary);
}

static VALUE
user_get_group_quota(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	return (INT2NUM(po->e.ngroups));
}

static VALUE
user_set_group_quota(VALUE self, VALUE newval)
{
	struct protection_object *po;
	afs_int32 error, ngroups;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	ngroups = NUM2INT(newval);

	ensure_initialized();
	error = pr_SetFieldsEntry(po->e.id, PR_SF_NGROUPS, 0, ngroups, 0);
	assert_success(error, "pr_SetFieldsEntry");
	po->e.ngroups = ngroups;
	return (newval);
}

static VALUE
user_get_group_count(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	return (INT2NUM(po->e.count));
}

static VALUE
group_get_user_quota(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	return (INT2NUM(po->e.nusers));
}

static VALUE
group_set_user_quota(VALUE self, VALUE newval)
{
	struct protection_object *po;
	afs_int32 error, nusers;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	nusers = NUM2INT(newval);

	ensure_initialized();
	error = pr_SetFieldsEntry(po->e.id, PR_SF_NUSERS, 0, 0, nusers);
	assert_success(error, "pr_SetFieldsEntry");
	po->e.nusers = nusers;
	return (newval);
}

static VALUE
group_get_user_count(VALUE self)
{
	struct protection_object *po;

	Data_Get_Struct(self, struct protection_object, po);
	assert_not_deleted(po);
	return (INT2NUM(po->e.count));
}

static VALUE
po_equal(VALUE self, VALUE other)
{
	struct protection_object *po1, *po2;

	if (CLASS_OF(other) != CLASS_OF(self))
		return (Qfalse);
	Data_Get_Struct(self, struct protection_object, po1);
	Data_Get_Struct(self, struct protection_object, po2);
	return (po1->e.id == po2->e.id ? Qtrue : Qfalse);
}


/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
