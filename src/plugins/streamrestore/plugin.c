    /*
    * ngfd - Non-graphic feedback daemon
    *
    * Copyright (C) 2010 Nokia Corporation.
    * Contact: Xun Chen <xun.chen@nokia.com>
    *
    * This work is free software; you can redistribute it and/or
    * modify it under the terms of the GNU Lesser General Public
    * License as published by the Free Software Foundation; either
    * version 2.1 of the License, or (at your option) any later version.
    *
    * This work is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    * Lesser General Public License for more details.
    *
    * You should have received a copy of the GNU Lesser General Public
    * License along with this work; if not, write to the Free Software
    * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
    */

#include <string.h>
#include <stdlib.h>
#include <ngf/plugin.h>
#include "volume-controller.h"

#define LOG_CAT         "stream-restore: "
#define ROLE_KEY_PREFIX "role."
#define SET_KEY_PREFIX  "set."

N_PLUGIN_NAME        ("stream-restore")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_AUTHOR      ("Harri Mahonen <ext-harri.mahonen@nokia.com>")
N_PLUGIN_DESCRIPTION ("Volumes using Pulseaudio DBus stream restore")

static GHashTable *stream_restore_role_map = NULL;

static void
init_done_cb (NHook *hook, void *data, void *userdata)
{
    (void) hook;
    (void) data;

    NPlugin        *plugin  = (NPlugin*) userdata;
    NCore          *core    = n_plugin_get_core (plugin);
    NContext       *context = n_core_get_context (core);
    const char     *key     = NULL;
    const char     *role    = NULL;
    NValue         *value   = NULL;
    int             volume  = 0;
    GHashTableIter  iter;

    /* query the initial volume values from the keys that we care
       about. */

    g_hash_table_iter_init (&iter, stream_restore_role_map);
    while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &role)) {
        value = (NValue*) n_context_get_value (context, key);
        if (!value) {
            N_WARNING (LOG_CAT "no value for found role '%s', key '%s' from context",
                role, key);
            continue;
        }

        if (n_value_type (value) != N_VALUE_TYPE_INT) {
            N_WARNING (LOG_CAT "invalid value type for role '%s', key '%s'",
                role, key);
            continue;
        }

        volume = n_value_get_int (value);
        (void) volume_controller_update (role, volume);
    }
}

static void
volume_add_role_key_cb (const char *key, const NValue *value, gpointer userdata)
{
    (void) key;
    (void) value;
    (void) userdata;

    const char *new_key = NULL;
    int         volume  = 0;

    if (g_str_has_prefix (key, ROLE_KEY_PREFIX)) {
        new_key = (const char*) key + strlen (ROLE_KEY_PREFIX);

        if (new_key) {
            g_hash_table_replace (stream_restore_role_map,
                n_value_dup_string ((NValue*) value), g_strdup (new_key));
        }
    }
    else if (g_str_has_prefix (key, SET_KEY_PREFIX)) {
        new_key = (const char*) key + strlen (SET_KEY_PREFIX);

        if (new_key) {
            volume = atoi (n_value_get_string ((NValue*) value));
            (void) volume_controller_update (new_key, volume);
        }
    }
}

void context_value_changed_cb (NContext *context, const char *key,
                               const NValue *old_value,
                               const NValue *new_value,
                               void *userdata)
{
    (void) context;
    (void) old_value;
    (void) userdata;

    const char *role   = NULL;
    int         volume = 0;

    role = g_hash_table_lookup (stream_restore_role_map, key);
    if (!role)
        return;

    if (n_value_type ((NValue*) new_value) != N_VALUE_TYPE_INT) {
        N_WARNING (LOG_CAT "invalid value type for role '%s', key '%s'",
            role, key);
    }

    volume = n_value_get_int ((NValue*) new_value);
    (void) volume_controller_update (role, volume);
}

N_PLUGIN_LOAD (plugin)
{
    NCore     *core    = NULL;
    NContext  *context = NULL;
    NProplist *params  = NULL;

    stream_restore_role_map = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, g_free);

    volume_controller_initialize ();

    /* load the stream restore roles we are interested in. */

    params = (NProplist*) n_plugin_get_params (plugin);
    n_proplist_foreach (params, volume_add_role_key_cb, NULL);

    /* connect to the init done hook to query the initial values for
       roles. */

    core = n_plugin_get_core (plugin);

    (void) n_core_connect (core, N_CORE_HOOK_INIT_DONE, 0,
        init_done_cb, plugin);

    /* listen to the context value changes */

    context = n_core_get_context (core);
    n_context_subscribe_value_change (context, NULL, context_value_changed_cb,
        NULL);

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;

    if (stream_restore_role_map) {
        g_hash_table_destroy (stream_restore_role_map);
        stream_restore_role_map = NULL;
    }

    volume_controller_shutdown ();
}
