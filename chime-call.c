/*
 * Pidgin/libpurple Chime client plugin
 *
 * Copyright © 2017 Amazon.com, Inc. or its affiliates.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */


#include "chime-connection-private.h"
#include "chime-call.h"

#include <glib/gi18n.h>

#define BOOL_PROPS(x)							\
	x(ongoing, ONGOING, "ongoing?", "ongoing", "ongoing", TRUE)	\
	x(is_recording, IS_RECORDING, "is_recording", "is-recording", "is recording", TRUE)

#define STRING_PROPS(x)							\
	x(channel, CHANNEL, "channel", "channel", "channel", TRUE)	\
	x(roster_channel, ROSTER_CHANNEL, "roster_channel", "roster-channel", "roster channel", TRUE) \
	x(host, HOST, "host", "host", "host", TRUE)			\
	x(media_host, MEDIA_HOST, "media_host", "media-host", "media host", TRUE) \
	x(mobile_bithub_url, MOBILE_BITHUB_URL, "mobile_bithub_url", "mobile-bithub-url", "mobile bithub url", TRUE) \
	x(desktop_bithub_url, DESKTOP_BITHUB_URL, "desktop_bithub_url", "desktop-bithub-url", "desktop bithub url", TRUE) \
	x(control_url, CONTROL_URL, "control_url", "control-url", "control url", TRUE) \
	x(stun_server_url, STUN_SERVER_URL, "stun_server_url", "stun-server-url", "stun server url", TRUE) \
	x(audio_ws_url, AUDIO_WS_URL, "audio_ws_url", "audio-ws-url", "audio ws url", TRUE)

#define CHIME_PROP_OBJ_VAR call

#include "chime-props.h"

enum
{
	PROP_0,

	CHIME_PROPS_ENUM

	LAST_PROP,
};

static GParamSpec *props[LAST_PROP];

enum {
	ENDED,
	CALL_CONNECTED,
	CALL_DISCONNECTED,
	LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

struct _ChimeCall {
	ChimeObject parent_instance;

	CHIME_PROPS_VARS

	ChimeConnection *cxn;

	guint opens;
};

G_DEFINE_TYPE(ChimeCall, chime_call, CHIME_TYPE_OBJECT)

CHIME_DEFINE_ENUM_TYPE(ChimeCallParticipationStatus, chime_call_participation_status, \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_PRESENT,		"present") \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_CHECKED_IN,	"checked_in") \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_INVITED,		"invited") \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_HUNG_UP,		"hung_up") \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_DROPPED,		"dropped") \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_RUNNING_LATE,	"running_late") \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_DECLINED,		"declined") \
	CHIME_ENUM_VALUE(CHIME_PARTICIPATION_INACTIVE,		"inactive"))

static void unsub_call(gpointer key, gpointer val, gpointer data);

static void
chime_call_dispose(GObject *object)
{
	ChimeCall *self = CHIME_CALL(object);

	chime_debug("Call disposed: %p\n", self);

	unsub_call(NULL, self, NULL);
	g_signal_emit(self, signals[ENDED], 0, NULL);

	G_OBJECT_CLASS(chime_call_parent_class)->dispose(object);
}

static void
chime_call_finalize(GObject *object)
{
	ChimeCall *self = CHIME_CALL(object);

	CHIME_PROPS_FREE

	G_OBJECT_CLASS(chime_call_parent_class)->finalize(object);
}

static void chime_call_get_property(GObject *object, guint prop_id,
				    GValue *value, GParamSpec *pspec)
{
	ChimeCall *self = CHIME_CALL(object);

	switch (prop_id) {

	CHIME_PROPS_GET

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void chime_call_set_property(GObject *object, guint prop_id,
				    const GValue *value, GParamSpec *pspec)
{
	ChimeCall *self = CHIME_CALL(object);

	switch (prop_id) {

	CHIME_PROPS_SET

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void chime_call_class_init(ChimeCallClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = chime_call_finalize;
	object_class->dispose = chime_call_dispose;
	object_class->get_property = chime_call_get_property;
	object_class->set_property = chime_call_set_property;

	CHIME_PROPS_REG

	g_object_class_install_properties(object_class, LAST_PROP, props);

	signals[ENDED] =
		g_signal_new ("ended",
			      G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST,
			      0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	signals[CALL_CONNECTED] =
		g_signal_new ("call-connected",
			      G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST,
			      0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	signals[CALL_DISCONNECTED] =
		g_signal_new ("call-disconnected",
			      G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_FIRST,
			      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void chime_call_init(ChimeCall *self)
{
}


/* Internal only */
ChimeConnection *chime_call_get_connection(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->cxn;
}

gboolean chime_call_get_ongoing(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), FALSE);

	return self->ongoing;
}

const gchar *chime_call_get_uuid(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return chime_object_get_id(CHIME_OBJECT(self));
}

const gchar *chime_call_get_channel(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->channel;
}

const gchar *chime_call_get_roster_channel(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->roster_channel;
}

const gchar *chime_call_get_alert_body(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return chime_object_get_name(CHIME_OBJECT(self));
}

const gchar *chime_call_get_host(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->host;
}

const gchar *chime_call_get_media_host(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->media_host;
}

const gchar *chime_call_get_mobile_bithub_url(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->mobile_bithub_url;
}

const gchar *chime_call_get_desktop_bithub_url(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->desktop_bithub_url;
}

const gchar *chime_call_get_control_url(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->control_url;
}

const gchar *chime_call_get_stun_server_url(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->stun_server_url;
}

const gchar *chime_call_get_audio_ws_url(ChimeCall *self)
{
	g_return_val_if_fail(CHIME_IS_CALL(self), NULL);

	return self->audio_ws_url;
}

#if 0
static gboolean parse_call_participation_status(JsonNode *node, const gchar *member, ChimeCallParticipationStatus *type)
{
	const gchar *str;

	if (!parse_string(node, member, &str))
		return FALSE;

	gpointer klass = g_type_class_ref(CHIME_TYPE_CALL_PARTICIPATION_STATUS);
	GEnumValue *val = g_enum_get_value_by_nick(klass, str);
	g_type_class_unref(klass);

	if (!val)
		return FALSE;
	*type = val->value;
	return TRUE;
}
#endif
static gboolean call_jugg_cb(ChimeConnection *cxn, gpointer _unused, JsonNode *data_node)
{
	JsonObject *obj = json_node_get_object(data_node);
	JsonNode *record = json_object_get_member(obj, "record");
	if (!record)
		return FALSE;

	ChimeCall *call = chime_connection_parse_call(cxn, record, NULL);
	if (call)
		g_object_unref(call);

	return !!call;
}


ChimeCall *chime_connection_parse_call(ChimeConnection *cxn, JsonNode *node,
				       GError **error)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE(cxn);
	const gchar *uuid, *alert_body;
	CHIME_PROPS_PARSE_VARS

	if (!parse_string(node, "uuid", &uuid) ||
	    !parse_string(node, "alert_body", &alert_body) ||
	    CHIME_PROPS_PARSE) {
		g_set_error(error, CHIME_ERROR,
			    CHIME_ERROR_BAD_RESPONSE,
			    _("Failed to parse Call node"));
		return NULL;
	}

	ChimeCall *call = g_hash_table_lookup(priv->calls.by_id, uuid);
	if (!call) {
		call = g_object_new(CHIME_TYPE_CALL,
				       "id", uuid,
				       "name", alert_body,
				       CHIME_PROPS_NEWOBJ
				       NULL);

		call->cxn = cxn;
		chime_jugg_subscribe(cxn, call->channel, "Call", call_jugg_cb, NULL);
		chime_jugg_subscribe(cxn, call->roster_channel, NULL, NULL, NULL);

		g_object_ref(call);
		chime_object_collection_hash_object(&priv->calls, CHIME_OBJECT(call), FALSE);

		return call;
	}

	if (alert_body && g_strcmp0(alert_body, chime_call_get_alert_body(call))) {
		chime_object_rename(CHIME_OBJECT(call), alert_body);
		g_object_notify(G_OBJECT(call), "name");
	}

	CHIME_PROPS_UPDATE

	return g_object_ref(call);
}

void chime_init_calls(ChimeConnection *cxn)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	chime_object_collection_init(&priv->calls);
}

void chime_destroy_calls(ChimeConnection *cxn)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	if (priv->calls.by_id)
		g_hash_table_foreach(priv->calls.by_id, unsub_call, NULL);

	chime_object_collection_destroy(&priv->calls);
}

static void unsub_call(gpointer key, gpointer val, gpointer data)
{
	ChimeCall *call = CHIME_CALL (val);

	if (call->cxn) {
		chime_jugg_unsubscribe(call->cxn, call->channel, "Call", call_jugg_cb, NULL);
		chime_jugg_unsubscribe(call->cxn, call->roster_channel, NULL, NULL, NULL);
		call->cxn = NULL;
	}
}
#if 0
void chime_connection_close_call(ChimeConnection *cxn, ChimeCall *call)
{
	g_return_if_fail(CHIME_IS_CONNECTION(cxn));
	g_return_if_fail(CHIME_IS_CALL(call));
	g_return_if_fail(call->opens);

	if (!--call->opens)
		close_call(NULL, call, NULL);
}


static void chime_connection_open_call(ChimeConnection *cxn, ChimeCall *call, GTask *task)
{
	if (!call->opens++) {
	}

	g_task_return_pointer(task, g_object_ref(call), g_object_unref);
	g_object_unref(task);
}


static void join_got_room(GObject *source, GAsyncResult *result, gpointer user_data)
{
	ChimeConnection *cxn = CHIME_CONNECTION(source);
	ChimeRoom *room = chime_connection_fetch_room_finish(cxn, result, NULL);
	GTask *task = G_TASK(user_data);
	ChimeCall *call = CHIME_CALL(g_task_get_task_data(task));

	call->chat_room = room;

	chime_connection_open_call(cxn, call, task);
}

void chime_connection_join_call_async(ChimeConnection *cxn,
					 ChimeCall *call,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data)
{
	g_return_if_fail(CHIME_IS_CONNECTION(cxn));

	GTask *task = g_task_new(cxn, cancellable, callback, user_data);
	g_task_set_task_data(task, g_object_ref(call), g_object_unref);

	if (call->chat_room_id) {
		ChimeRoom *room = chime_connection_room_by_id(cxn, call->chat_room_id);
		if (room) {
			call->chat_room = g_object_ref(room);
		} else {
			/* Not yet known; need to go fetch it explicitly */
			chime_connection_fetch_room_async(cxn, call->chat_room_id,
							  NULL, join_got_room, task);
			return;
		}
	}

	chime_connection_open_call(cxn, call, task);
}

ChimeCall *chime_connection_join_call_finish(ChimeConnection *self,
						   GAsyncResult *result,
						   GError **error)
{
	g_return_val_if_fail(CHIME_IS_CONNECTION(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(result, self), FALSE);

	return g_task_propagate_pointer(G_TASK(result), error);
}

#endif