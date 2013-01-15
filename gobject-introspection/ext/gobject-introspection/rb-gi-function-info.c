/* -*- c-file-style: "ruby"; indent-tabs-mode: nil -*- */
/*
 *  Copyright (C) 2012-2013  Ruby-GNOME2 Project Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include "rb-gobject-introspection.h"

#define RG_TARGET_NAMESPACE rb_cGIFunctionInfo
#define SELF(self) RVAL2GI_FUNCTION_INFO(self)

static VALUE RG_TARGET_NAMESPACE;
static const char *callbacks_key = "gi_callbacks";

GType
gi_function_info_get_type(void)
{
    static GType type = 0;
    if (type == 0) {
	type = g_boxed_type_register_static("GIFunctionInfo",
                                            (GBoxedCopyFunc)g_base_info_ref,
                                            (GBoxedFreeFunc)g_base_info_unref);
    }
    return type;
}

static VALUE
rg_symbol(VALUE self)
{
    GIFunctionInfo *info;

    info = SELF(self);
    return CSTR2RVAL(g_function_info_get_symbol(info));
}

static VALUE
rg_flags(VALUE self)
{
    GIFunctionInfo *info;

    info = SELF(self);
    return GI_FUNCTION_INFO_FLAGS2RVAL(g_function_info_get_flags(info));
}

static VALUE
rg_property(VALUE self)
{
    GIFunctionInfo *info;

    info = SELF(self);
    return GI_BASE_INFO2RVAL(g_function_info_get_property(info));
}

static VALUE
rg_vfunc(VALUE self)
{
    GIFunctionInfo *info;

    info = SELF(self);
    return GI_BASE_INFO2RVAL(g_function_info_get_vfunc(info));
}

typedef struct
{
    GIArgInfo arg_info;
    GIScopeType scope_type;
    GIDirection direction;
    gboolean callback_p;
    gboolean closure_p;
    gboolean destroy_p;
    gboolean inout_argv_p;
    gint in_arg_index;
    gint closure_in_arg_index;
    gint destroy_in_arg_index;
    gint rb_arg_index;
    gint out_arg_index;
    gint inout_argc_arg_index;
} ArgMetadata;

static void
allocate_arguments(GICallableInfo *info,
                   GArray *in_args, GArray *out_args,
                   GPtrArray *args_metadata)
{
    gint i, n_args;
    gint rb_arg_index = 0;

    n_args = g_callable_info_get_n_args(info);
    for (i = 0; i < n_args; i++) {
        GIArgument argument;
        ArgMetadata *metadata;
        GIArgInfo *arg_info;
        GIDirection direction;

        memset(&argument, 0, sizeof(GIArgument));

        metadata = ALLOC(ArgMetadata);
        arg_info = &(metadata->arg_info);
        g_callable_info_load_arg(info, i, arg_info);
        metadata->scope_type = g_arg_info_get_scope(arg_info);
        metadata->direction = g_arg_info_get_direction(arg_info);
        metadata->callback_p = (metadata->scope_type != GI_SCOPE_TYPE_INVALID);
        metadata->closure_p = FALSE;
        metadata->destroy_p = FALSE;
        metadata->inout_argv_p = FALSE;
        metadata->in_arg_index = -1;
        metadata->closure_in_arg_index = -1;
        metadata->destroy_in_arg_index = -1;
        metadata->rb_arg_index = -1;
        metadata->out_arg_index = -1;
        metadata->inout_argc_arg_index = -1;

        direction = metadata->direction;
        if (direction == GI_DIRECTION_IN || direction == GI_DIRECTION_INOUT) {
            metadata->in_arg_index = in_args->len;
            g_array_append_val(in_args, argument);
            metadata->rb_arg_index = rb_arg_index++;
        }
        if (direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT) {
            metadata->out_arg_index = out_args->len;
            g_array_append_val(out_args, argument);
        }

        g_ptr_array_add(args_metadata, metadata);
    }
}

static void
fill_metadata_inout_argv(GPtrArray *args_metadata)
{
    guint i;
    gint inout_argc_arg_index = -1;

    for (i = 0; i < args_metadata->len; i++) {
        ArgMetadata *metadata;
        GIArgInfo *arg_info;
        const gchar *name;

        metadata = g_ptr_array_index(args_metadata, i);
        if (metadata->direction != GI_DIRECTION_INOUT) {
            continue;
        }
        arg_info = &(metadata->arg_info);
        name = g_base_info_get_name(arg_info);
        if (strcmp(name, "argc") == 0) {
            inout_argc_arg_index = i;
        } else if (strcmp(name, "argv") == 0) {
            metadata->inout_argv_p = TRUE;
            metadata->inout_argc_arg_index = inout_argc_arg_index;
        }
    }
}

static void
fill_metadata_callback(GPtrArray *args_metadata)
{
    guint i;

    for (i = 0; i < args_metadata->len; i++) {
        ArgMetadata *metadata;
        GIArgInfo *arg_info;
        gint closure_index;
        gint destroy_index;

        metadata = g_ptr_array_index(args_metadata, i);
        if (!metadata->callback_p) {
            continue;
        }

        arg_info = &(metadata->arg_info);
        closure_index = g_arg_info_get_closure(arg_info);
        if (closure_index != -1) {
            ArgMetadata *closure_metadata;
            closure_metadata = g_ptr_array_index(args_metadata, closure_index);
            closure_metadata->closure_p = TRUE;
            metadata->closure_in_arg_index = closure_metadata->in_arg_index;
            closure_metadata->rb_arg_index = -1;
        }

        destroy_index = g_arg_info_get_destroy(arg_info);
        if (destroy_index != -1) {
            ArgMetadata *destroy_metadata;
            destroy_metadata = g_ptr_array_index(args_metadata, destroy_index);
            destroy_metadata->destroy_p = TRUE;
            metadata->destroy_in_arg_index = destroy_metadata->in_arg_index;
            destroy_metadata->rb_arg_index = -1;
        }
    }
}

static void
fill_metadata(GPtrArray *args_metadata)
{
    fill_metadata_inout_argv(args_metadata);
    fill_metadata_callback(args_metadata);
}

static gboolean
source_callback_p(GIArgInfo *info)
{
    GITypeInfo type_info;
    GIBaseInfo *interface_info;
    GICallableInfo *callback_info;
    GITypeInfo return_type_info;
    GIArgInfo first_arg_info;
    GITypeInfo first_arg_type_info;

    g_arg_info_load_type(info, &type_info);
    if (g_type_info_get_tag(&type_info) != GI_TYPE_TAG_INTERFACE) {
        return FALSE;
    }

    interface_info = g_type_info_get_interface(&type_info);
    if (g_base_info_get_type(interface_info) != GI_INFO_TYPE_CALLBACK) {
        g_base_info_unref(interface_info);
        return FALSE;
    }

    callback_info = (GICallableInfo *)interface_info;
    g_callable_info_load_return_type(callback_info, &return_type_info);
    if (g_type_info_get_tag(&return_type_info) != GI_TYPE_TAG_BOOLEAN) {
        g_base_info_unref(interface_info);
        return FALSE;
    }

    if (g_callable_info_get_n_args(interface_info) != 1) {
        g_base_info_unref(interface_info);
        return FALSE;
    }

    g_callable_info_load_arg(interface_info, 0, &first_arg_info);
    g_arg_info_load_type(&first_arg_info, &first_arg_type_info);
    if (g_type_info_get_tag(&first_arg_type_info) != GI_TYPE_TAG_VOID) {
        g_base_info_unref(interface_info);
        return FALSE;
    }

    g_base_info_unref(interface_info);
    return TRUE;
}

typedef struct {
    ArgMetadata *metadata;
    VALUE rb_gc_guard_key;
    VALUE rb_callback;
} CallbackData;

static void
callback_data_guard_from_gc(CallbackData *callback_data)
{
    VALUE rb_callbacks;

    rb_callbacks = rb_iv_get(RG_TARGET_NAMESPACE, callbacks_key);
    callback_data->rb_gc_guard_key = rb_class_new_instance(0, NULL, rb_cObject);
    rb_hash_aset(rb_callbacks,
                 callback_data->rb_gc_guard_key,
                 callback_data->rb_callback);
}

static void
callback_data_unguard_from_gc(CallbackData *callback_data)
{
    VALUE rb_callbacks;

    rb_callbacks = rb_iv_get(RG_TARGET_NAMESPACE, callbacks_key);
    rb_hash_delete(rb_callbacks, callback_data->rb_gc_guard_key);
}

static void
callback_data_free(CallbackData *callback_data)
{
    callback_data_unguard_from_gc(callback_data);
    xfree(callback_data->metadata);
    xfree(callback_data);
}

static gboolean
source_callback(gpointer user_data)
{
    CallbackData *callback_data = user_data;
    VALUE rb_keep;
    ID id_call;

    CONST_ID(id_call, "call");
    rb_keep = rb_funcall(callback_data->rb_callback, id_call, 0);
    if (callback_data->metadata->scope_type == GI_SCOPE_TYPE_ASYNC) {
        callback_data_free(callback_data);
    }
    return RVAL2CBOOL(rb_keep);
}

static void
destroy_notify(gpointer data)
{
    CallbackData *callback_data = data;
    callback_data_free(callback_data);
}

static void
in_callback_argument_from_ruby(ArgMetadata *metadata, VALUE *argv,
                               GArray *in_args)
{
    GIArgument *callback_argument;

    if (!source_callback_p(&(metadata->arg_info))) {
        rb_raise(rb_eNotImpError,
                 "TODO: GSourceFunc callback is only supported.");
    }

    callback_argument = &(g_array_index(in_args,
                                        GIArgument,
                                        metadata->in_arg_index));
    callback_argument->v_pointer = source_callback;

    if (metadata->closure_in_arg_index != -1) {
        CallbackData *callback_data;
        GIArgument *closure_argument;

        callback_data = ALLOC(CallbackData);
        callback_data->metadata = metadata;
        callback_data->rb_callback = rb_block_proc();
        callback_data_guard_from_gc(callback_data);
        closure_argument = &(g_array_index(in_args,
                                           GIArgument,
                                           metadata->closure_in_arg_index));
        closure_argument->v_pointer = callback_data;
    }

    if (metadata->destroy_in_arg_index != -1) {
        GIArgument *destroy_argument;
        destroy_argument = &(g_array_index(in_args,
                                           GIArgument,
                                           metadata->destroy_in_arg_index));
        destroy_argument->v_pointer = destroy_notify;
    }
}

static void
in_argument_from_ruby(ArgMetadata *metadata, VALUE *argv, GArray *in_args)
{
    if (metadata->rb_arg_index == -1) {
        return;
    }

    if (metadata->callback_p) {
        in_callback_argument_from_ruby(metadata, argv, in_args);
    } else {
        GIArgument *argument;

        argument = &(g_array_index(in_args, GIArgument, metadata->in_arg_index));
        RVAL2GI_CALL_ARGUMENT(argument,
                              &(metadata->arg_info),
                              argv[metadata->rb_arg_index]);
    }
}

static void
out_argument_from_ruby(ArgMetadata *metadata, GArray *out_args)
{
    GIArgument *argument;

    argument = &(g_array_index(out_args, GIArgument, metadata->out_arg_index));
    rb_gi_out_argument_init(argument, &(metadata->arg_info));
}

static void
arg_metadata_free(gpointer data)
{
    ArgMetadata *metadata = data;
    if (metadata->scope_type == GI_SCOPE_TYPE_ASYNC ||
        metadata->scope_type == GI_SCOPE_TYPE_NOTIFIED) {
        return;
    }
    xfree(metadata);
}

static void
arguments_from_ruby(GICallableInfo *info,
                    int argc, VALUE *argv,
                    GArray *in_args, GArray *out_args,
                    GPtrArray *args_metadata)
{
    gint i, n_args;

    allocate_arguments(info, in_args, out_args, args_metadata);
    fill_metadata(args_metadata);

    /* TODO: validate_rb_args(args_metadata); */

    n_args = g_callable_info_get_n_args(info);
    for (i = 0; i < n_args; i++) {
        ArgMetadata *metadata;

        metadata = g_ptr_array_index(args_metadata, i);
        if (metadata->in_arg_index != -1) {
            in_argument_from_ruby(metadata, argv, in_args);
        } else {
            out_argument_from_ruby(metadata, out_args);
        }
    }
}

static VALUE
inout_argv_argument_to_ruby(GArray *in_args, ArgMetadata *metadata)
{
    GIArgument *inout_argc_argument;
    GIArgument *inout_argv_argument;
    gint i, argc;
    gchar **argv;
    VALUE rb_argv_argument;

    inout_argc_argument = &g_array_index(in_args, GIArgument,
                                         metadata->inout_argc_arg_index);
    inout_argv_argument = &g_array_index(in_args, GIArgument,
                                         metadata->in_arg_index);
    argc = *((gint *)(inout_argc_argument->v_pointer));
    argv = *((gchar ***)(inout_argv_argument->v_pointer));
    rb_argv_argument = rb_ary_new2(argc);
    for (i = 0; i < argc; i++) {
        rb_ary_push(rb_argv_argument, CSTR2RVAL(argv[i]));
    }
    return rb_argv_argument;
}

static VALUE
out_arguments_to_ruby(GICallableInfo *callable_info,
                      GArray *in_args, GArray *out_args,
                      GPtrArray *args_metadata)
{
    gint i, n_args;
    VALUE rb_out_args;

    rb_out_args = rb_ary_new();
    n_args = g_callable_info_get_n_args(callable_info);
    for (i = 0; i < n_args; i++) {
        ArgMetadata *metadata;
        GIArgument *argument = NULL;
        VALUE rb_argument;

        metadata = g_ptr_array_index(args_metadata, i);
        switch (metadata->direction) {
          case GI_DIRECTION_IN:
            break;
          case GI_DIRECTION_OUT:
            argument = &g_array_index(out_args, GIArgument,
                                      metadata->out_arg_index);
            break;
          case GI_DIRECTION_INOUT:
            argument = &g_array_index(in_args, GIArgument,
                                      metadata->in_arg_index);
            break;
          default:
            g_assert_not_reached();
            break;
        }

        if (!argument) {
            continue;
        }

        if (metadata->inout_argv_p) {
            rb_argument = inout_argv_argument_to_ruby(in_args, metadata);
        } else {
            rb_argument = GI_OUT_ARGUMENT2RVAL(argument, &(metadata->arg_info));
        }
        rb_ary_push(rb_out_args, rb_argument);
    }

    if (RARRAY_LEN(rb_out_args) == 0) {
        return Qnil;
    } else {
        return rb_out_args;
    }
}

static void
arguments_init(GArray **in_args, GArray **out_args, GPtrArray **args_metadata)
{
    *in_args = g_array_new(FALSE, FALSE, sizeof(GIArgument));
    *out_args = g_array_new(FALSE, FALSE, sizeof(GIArgument));
    *args_metadata = g_ptr_array_new_with_free_func(arg_metadata_free);
}

static void
arguments_free(GArray *in_args, GArray *out_args, GPtrArray *args_metadata)
{
    guint i;

    for (i = 0; i < args_metadata->len; i++) {
        ArgMetadata *metadata;
        gint in_arg_index;

        metadata = g_ptr_array_index(args_metadata, i);
        if (metadata->direction == GI_DIRECTION_IN ||
            metadata->direction == GI_DIRECTION_INOUT) {
            in_arg_index = metadata->in_arg_index;
            if (in_arg_index != -1) {
                GIArgument *argument;
                argument = &(g_array_index(in_args, GIArgument, in_arg_index));
                rb_gi_call_argument_free(argument, &(metadata->arg_info));
            }
        } else {
            GIArgument *argument;
            argument = &(g_array_index(out_args, GIArgument,
                                       metadata->out_arg_index));
            rb_gi_out_argument_fin(argument, &(metadata->arg_info));
        }
    }

    g_array_unref(in_args);
    g_array_unref(out_args);
    g_ptr_array_unref(args_metadata);
}

VALUE
rb_gi_function_info_invoke_raw(GIFunctionInfo *info, GIArgument *receiver,
                               int argc, VALUE *argv,
                               GIArgument *return_value)
{
    GICallableInfo *callable_info;
    GArray *in_args, *out_args;
    GPtrArray *args_metadata;
    VALUE rb_out_args = Qnil;
    gboolean succeeded;
    GError *error = NULL;

    callable_info = (GICallableInfo *)info;
    arguments_init(&in_args, &out_args, &args_metadata);
    if (receiver) {
        g_array_append_val(in_args, *receiver);
    }
    arguments_from_ruby(callable_info,
                        argc, argv,
                        in_args, out_args, args_metadata);
    succeeded = g_function_info_invoke(info,
                                       (GIArgument *)(in_args->data),
                                       in_args->len,
                                       (GIArgument *)(out_args->data),
                                       out_args->len,
                                       return_value,
                                       &error);
    if (succeeded) {
        rb_out_args = out_arguments_to_ruby(callable_info,
                                            in_args, out_args,
                                            args_metadata);
    }
    arguments_free(in_args, out_args, args_metadata);
    if (!succeeded) {
        RG_RAISE_ERROR(error);
    }

    return rb_out_args;
}

static VALUE
rg_invoke(int argc, VALUE *argv, VALUE self)
{
    GIFunctionInfo *info;
    GICallableInfo *callable_info;
    GIArgument return_value;
    VALUE rb_out_args;
    VALUE rb_return_value;

    info = SELF(self);
    /* TODO: use rb_protect() */
    rb_out_args = rb_gi_function_info_invoke_raw(info, NULL, argc, argv,
                                                 &return_value);

    callable_info = (GICallableInfo *)info;
    rb_return_value = GI_RETURN_ARGUMENT2RVAL(&return_value, callable_info);

    if (NIL_P(rb_out_args)) {
        return rb_return_value;
    } else {
        GITypeInfo return_value_info;
        g_callable_info_load_return_type(callable_info, &return_value_info);
        if (g_type_info_get_tag(&return_value_info) != GI_TYPE_TAG_VOID) {
            rb_ary_unshift(rb_out_args, rb_return_value);
        }
        return rb_out_args;
    }
}

void
rb_gi_function_info_init(VALUE rb_mGI, VALUE rb_cGICallableInfo)
{
    RG_TARGET_NAMESPACE =
	G_DEF_CLASS_WITH_PARENT(GI_TYPE_FUNCTION_INFO, "FunctionInfo", rb_mGI,
				rb_cGICallableInfo);

    rb_iv_set(RG_TARGET_NAMESPACE, callbacks_key, rb_hash_new());

    RG_DEF_METHOD(symbol, 0);
    RG_DEF_METHOD(flags, 0);
    RG_DEF_METHOD(property, 0);
    RG_DEF_METHOD(vfunc, 0);
    RG_DEF_METHOD(invoke, -1);

    G_DEF_CLASS(G_TYPE_I_FUNCTION_INFO_FLAGS, "FunctionInfoFlags", rb_mGI);

    rb_gi_method_info_init(rb_mGI, RG_TARGET_NAMESPACE);
    rb_gi_constructor_info_init(rb_mGI, RG_TARGET_NAMESPACE);
}
