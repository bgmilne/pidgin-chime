
/*
 * Pidgin/libpurple Chime client plugin
 *
 * Copyright © 2017 Amazon.com, Inc. or its affiliates.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *          Ignacio Casal Quinteiro <qignacio@amazon.com>
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
#include "chime-conversation.h"
#include "chime.h"

#include <glib/gi18n.h>

enum
{
	PROP_0,
	PROP_CHANNEL,
	PROP_FAVOURITE,
	PROP_VISIBILITY,
	PROP_LAST_SENT,
	PROP_CREATED_ON,
	PROP_UPDATED_ON,
	PROP_MOBILE_NOTIFICATION_PREFS,
	PROP_DESKTOP_NOTIFICATION_PREFS,
	LAST_PROP,
};

static GParamSpec *props[LAST_PROP];

struct _ChimeConversation {
	ChimeObject parent_instance;

	gchar *channel;
	gboolean favourite;
	gboolean visibility;
	gchar *last_sent;
	gchar *created_on;
	gchar *updated_on;
	ChimeNotifyPref mobile_notification;
	ChimeNotifyPref desktop_notification;
};

G_DEFINE_TYPE(ChimeConversation, chime_conversation, CHIME_TYPE_OBJECT)

static void
chime_conversation_dispose(GObject *object)
{
	ChimeConversation *self = CHIME_CONVERSATION(object);

	printf("Conversation disposed: %p\n", self);

	G_OBJECT_CLASS(chime_conversation_parent_class)->dispose(object);
}

static void
chime_conversation_finalize(GObject *object)
{
	ChimeConversation *self = CHIME_CONVERSATION(object);

	g_free(self->channel);
	g_free(self->last_sent);
	g_free(self->created_on);
	g_free(self->updated_on);

	G_OBJECT_CLASS(chime_conversation_parent_class)->finalize(object);
}

static void chime_conversation_get_property(GObject *object, guint prop_id,
				    GValue *value, GParamSpec *pspec)
{
	ChimeConversation *self = CHIME_CONVERSATION(object);

	switch (prop_id) {
	case PROP_CHANNEL:
		g_value_set_string(value, self->channel);
		break;
	case PROP_FAVOURITE:
		g_value_set_boolean(value, self->favourite);
		break;
	case PROP_VISIBILITY:
		g_value_set_boolean(value, self->visibility);
		break;
	case PROP_LAST_SENT:
		g_value_set_string(value, self->last_sent);
		break;
	case PROP_CREATED_ON:
		g_value_set_string(value, self->created_on);
		break;
	case PROP_UPDATED_ON:
		g_value_set_string(value, self->updated_on);
		break;
	case PROP_MOBILE_NOTIFICATION_PREFS:
		g_value_set_enum(value, self->mobile_notification);
		break;
	case PROP_DESKTOP_NOTIFICATION_PREFS:
		g_value_set_enum(value, self->desktop_notification);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void chime_conversation_set_property(GObject *object, guint prop_id,
				    const GValue *value, GParamSpec *pspec)
{
	ChimeConversation *self = CHIME_CONVERSATION(object);

	switch (prop_id) {
	case PROP_CHANNEL:
		g_free(self->channel);
		self->channel = g_value_dup_string(value);
		break;
	case PROP_FAVOURITE:
		self->favourite = g_value_get_boolean(value);
		break;
	case PROP_VISIBILITY:
		self->visibility = g_value_get_boolean(value);
		break;
	case PROP_LAST_SENT:
		g_free(self->last_sent);
		self->last_sent = g_value_dup_string(value);
		break;
	case PROP_CREATED_ON:
		g_free(self->created_on);
		self->created_on = g_value_dup_string(value);
		break;
	case PROP_UPDATED_ON:
		g_free(self->updated_on);
		self->updated_on = g_value_dup_string(value);
		break;
	case PROP_MOBILE_NOTIFICATION_PREFS:
		self->mobile_notification = g_value_get_enum(value);
		break;
	case PROP_DESKTOP_NOTIFICATION_PREFS:
		self->desktop_notification = g_value_get_enum(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void chime_conversation_class_init(ChimeConversationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = chime_conversation_finalize;
	object_class->dispose = chime_conversation_dispose;
	object_class->get_property = chime_conversation_get_property;
	object_class->set_property = chime_conversation_set_property;

	props[PROP_CHANNEL] =
		g_param_spec_string("channel",
				    "channel",
				    "channel",
				    NULL,
				    G_PARAM_READWRITE |
				    G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS);

	props[PROP_FAVOURITE] =
		g_param_spec_boolean("favourite",
				     "favourite",
				     "favourite",
				     FALSE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_STRINGS);

	props[PROP_VISIBILITY] =
		g_param_spec_boolean("visibility",
				     "visibility",
				     "visibility",
				     TRUE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_STRINGS);

	props[PROP_LAST_SENT] =
		g_param_spec_string("last-sent",
				    "last sent",
				    "last sent",
				    NULL,
				    G_PARAM_READWRITE |
				    G_PARAM_CONSTRUCT |
				    G_PARAM_STATIC_STRINGS);

	props[PROP_CREATED_ON] =
		g_param_spec_string("created-on",
				    "created on",
				    "created on",
				    NULL,
				    G_PARAM_READWRITE |
				    G_PARAM_CONSTRUCT |
				    G_PARAM_STATIC_STRINGS);

	props[PROP_UPDATED_ON] =
		g_param_spec_string("updated-on",
				    "updated on",
				    "updated on",
				    NULL,
				    G_PARAM_READWRITE |
				    G_PARAM_CONSTRUCT |
				    G_PARAM_STATIC_STRINGS);

	props[PROP_MOBILE_NOTIFICATION_PREFS] =
		g_param_spec_enum("mobile-notification-prefs",
				  "mobile-notification-prefs",
				  "mobile-notification-prefs",
				  CHIME_TYPE_NOTIFY_PREF,
				  CHIME_NOTIFY_PREF_ALWAYS,
				  G_PARAM_READWRITE |
				  G_PARAM_CONSTRUCT |
				  G_PARAM_STATIC_STRINGS);

	props[PROP_DESKTOP_NOTIFICATION_PREFS] =
		g_param_spec_enum("desktop-notification-prefs",
				  "desktop-notification-prefs",
				  "desktop-notification-prefs",
				  CHIME_TYPE_NOTIFY_PREF,
				  CHIME_NOTIFY_PREF_ALWAYS,
				  G_PARAM_READWRITE |
				  G_PARAM_CONSTRUCT |
				  G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(object_class, LAST_PROP, props);
}

static void chime_conversation_init(ChimeConversation *self)
{
}

const gchar *chime_conversation_get_id(ChimeConversation *self)
{
	g_return_val_if_fail(CHIME_IS_CONVERSATION(self), NULL);

	return chime_object_get_id(CHIME_OBJECT(self));
}

const gchar *chime_conversation_get_name(ChimeConversation *self)
{
	g_return_val_if_fail(CHIME_IS_CONVERSATION(self), NULL);

	return chime_object_get_name(CHIME_OBJECT(self));
}

const gchar *chime_conversation_get_channel(ChimeConversation *self)
{
	g_return_val_if_fail(CHIME_IS_CONVERSATION(self), NULL);

	return self->channel;
}

gboolean chime_conversation_get_favourite(ChimeConversation *self)
{
	g_return_val_if_fail(CHIME_IS_CONVERSATION(self), FALSE);

	return self->favourite;
}

gboolean chime_conversation_get_visibility(ChimeConversation *self)
{
	g_return_val_if_fail(CHIME_IS_CONVERSATION(self), FALSE);

	return self->visibility;
}

static gboolean parse_boolean(JsonNode *node, const gchar *member, gboolean *val)
{
	gint64 intval;

	if (!parse_int(node, member, &intval))
		return FALSE;

	*val = !!intval;
	return TRUE;
}

static ChimeConversation *chime_connection_parse_conversation(ChimeConnection *cxn, JsonNode *node,
							      GError **error)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE(cxn);
	const gchar *id, *name, *channel, *created_on, *updated_on,
		*last_sent = NULL;
	gboolean favourite, visibility;
	ChimeNotifyPref desktop, mobile;

	if (!parse_string(node, "ConversationId", &id) ||
	    !parse_string(node, "Name", &name) ||
	    !parse_string(node, "Channel", &channel) ||
	    !parse_boolean(node, "Favorite", &favourite) ||
	    !parse_visibility(node, "Visibility", &visibility) ||
	    !parse_string(node, "CreatedOn", &created_on) ||
	    !parse_string(node, "UpdatedOn", &updated_on)) {
	eparse:
		g_set_error(error, CHIME_ERROR, CHIME_ERROR_BAD_RESPONSE,
			    _("Failed to parse Conversation node"));
		return NULL;
	}
	parse_string(node, "LastSent", &last_sent);

	JsonObject *obj = json_node_get_object(node);
	node = json_object_get_member(obj, "Preferences");
	if (!node)
		goto eparse;
	obj = json_node_get_object(node);
	node = json_object_get_member(obj, "NotificationPreferences");
	if (!node)
		goto eparse;
	if (!parse_notify_pref(node, "DesktopNotificationPreferences", &desktop) ||
	    !parse_notify_pref(node, "MobileNotificationPreferences", &mobile))
		goto eparse;

	ChimeConversation *conversation = g_hash_table_lookup(priv->conversations.by_id, id);
	if (!conversation) {
		conversation = g_object_new(CHIME_TYPE_CONVERSATION,
				    "id", id,
				    "name", name,
				    "channel", channel,
				    "favourite", favourite,
				    "visibility", visibility,
				    "last-sent", last_sent,
				    "created-on", created_on,
				    "updated-on", updated_on,
				    "desktop-notification-prefs", desktop,
				    "mobile-notification-prefs", mobile,
				    NULL);

		chime_object_collection_hash_object(&priv->conversations, CHIME_OBJECT(conversation), TRUE);

		/* Emit signal on ChimeConnection to admit existence of new conversation */
		printf("new conv %s %p\n", id, conversation);
		chime_connection_new_conversation(cxn, conversation);

		return conversation;
	}

	if (name && g_strcmp0(name, chime_object_get_name(CHIME_OBJECT(conversation)))) {
		chime_object_rename(CHIME_OBJECT(conversation), name);
		g_object_notify(G_OBJECT(conversation), "name");
	}
	if (channel && g_strcmp0(channel, conversation->channel)) {
		g_free(conversation->channel);
		conversation->channel = g_strdup(channel);
		g_object_notify(G_OBJECT(conversation), "channel");
	}
	if (favourite != conversation->favourite) {
		conversation->favourite = favourite;
		g_object_notify(G_OBJECT(conversation), "favourite");
	}
	if (visibility != conversation->visibility) {
		conversation->visibility = visibility;
		g_object_notify(G_OBJECT(conversation), "visibility");
	}
	if (last_sent && g_strcmp0(last_sent, conversation->last_sent)) {
		g_free(conversation->last_sent);
		conversation->last_sent = g_strdup(last_sent);
		g_object_notify(G_OBJECT(conversation), "last-sent");
	}
	if (created_on && g_strcmp0(created_on, conversation->created_on)) {
		g_free(conversation->created_on);
		conversation->created_on = g_strdup(created_on);
		g_object_notify(G_OBJECT(conversation), "created-on");
	}
	if (updated_on && g_strcmp0(updated_on, conversation->updated_on)) {
		g_free(conversation->updated_on);
		conversation->updated_on = g_strdup(updated_on);
		g_object_notify(G_OBJECT(conversation), "updated-on");
	}
	if (desktop != conversation->desktop_notification) {
		conversation->desktop_notification = desktop;
		g_object_notify(G_OBJECT(conversation), "desktop-notification-prefs");
	}
	if (mobile != conversation->mobile_notification) {
		conversation->mobile_notification = mobile;
		g_object_notify(G_OBJECT(conversation), "mobile-notification-prefs");
	}

	chime_object_collection_hash_object(&priv->conversations, CHIME_OBJECT(conversation), TRUE);

	return conversation;
}

static void fetch_conversations(ChimeConnection *cxn, const gchar *next_token);

static void conversations_cb(ChimeConnection *cxn, SoupMessage *msg, JsonNode *node,
			gpointer _unused)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	/* If it got invalidated while in transit, refetch */
	if (priv->conversations_sync != CHIME_SYNC_FETCHING) {
		priv->conversations_sync = CHIME_SYNC_IDLE;
		fetch_conversations(cxn, NULL);
		return;
	}

	if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code) && node) {
		JsonObject *obj = json_node_get_object(node);
		JsonNode *conversations_node = json_object_get_member(obj, "Conversations");
		if (!conversations_node) {
			chime_connection_fail(cxn, CHIME_ERROR_BAD_RESPONSE,
					      _("Failed to find Conversations node in response"));
			return;
		}
		JsonArray *arr = json_node_get_array(conversations_node);
		guint i, len = json_array_get_length(arr);

		for (i = 0; i < len; i++) {
			chime_connection_parse_conversation(cxn,
							    json_array_get_element(arr, i),
							    NULL);
		}

		const gchar *next_token;
		if (parse_string(node, "NextToken", &next_token))
			fetch_conversations(cxn, next_token);
		else {
			priv->conversations_sync = CHIME_SYNC_IDLE;

			chime_object_collection_expire_outdated(&priv->conversations);

			if (!priv->convs_online) {
				priv->convs_online = TRUE;
				chime_connection_calculate_online(cxn);
			}
		}
	} else {
		const gchar *reason = msg->reason_phrase;

		parse_string(node, "error", &reason);

		chime_connection_fail(cxn, CHIME_ERROR_NETWORK,
				      _("Failed to fetch conversations (%d): %s\n"),
				      msg->status_code, reason);
	}
}

static void fetch_conversations(ChimeConnection *cxn, const gchar *next_token)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	if (!next_token) {
		/* Actually we could listen for the 'starting' flag on the message,
		 * and as long as *that* hasn't happened yet we don't need to refetch
		 * as it'll get up-to-date information. */
		switch(priv->conversations_sync) {
		case CHIME_SYNC_FETCHING:
			priv->conversations_sync = CHIME_SYNC_STALE;
		case CHIME_SYNC_STALE:
			return;

		case CHIME_SYNC_IDLE:
			priv->conversations.generation++;
			priv->conversations_sync = CHIME_SYNC_FETCHING;
		}
	}

	SoupURI *uri = soup_uri_new_printf(priv->messaging_url, "/conversations");
	soup_uri_set_query_from_fields(uri, "max-results", "50",
				       next_token ? "next-token" : NULL, next_token,
				       NULL);
	chime_connection_queue_http_request(cxn, NULL, uri, "GET", conversations_cb,
					    NULL);
}

static gboolean conv_jugg_cb(ChimeConnection *cxn, gpointer _unused, JsonNode *data_node)
{
	JsonObject *obj = json_node_get_object(data_node);
	JsonNode *record = json_object_get_member(obj, "record");
	if (!record)
		return FALSE;

	return !!chime_connection_parse_conversation(cxn, record, NULL);
}

void chime_init_conversations(ChimeConnection *cxn)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	chime_object_collection_init(&priv->conversations);

	chime_jugg_subscribe(cxn, priv->device_channel, "Conversation",
			     conv_jugg_cb, NULL);
	fetch_conversations(cxn, NULL);
}

void chime_destroy_conversations(ChimeConnection *cxn)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	chime_jugg_unsubscribe(cxn, priv->device_channel, "Conversation",
			       conv_jugg_cb, NULL);

	chime_object_collection_destroy(&priv->conversations);
}

ChimeConversation *chime_connection_conversation_by_name(ChimeConnection *cxn,
							 const gchar *name)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	return g_hash_table_lookup(priv->conversations.by_name, name);
}

ChimeConversation *chime_connection_conversation_by_id(ChimeConnection *cxn,
						       const gchar *id)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);

	return g_hash_table_lookup(priv->conversations.by_id, id);
}

void chime_connection_foreach_conversation(ChimeConnection *cxn, ChimeConversationCB cb,
					   gpointer cbdata)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE(cxn);

	chime_object_collection_foreach_object(cxn, &priv->conversations, (ChimeObjectCB)cb, cbdata);
}