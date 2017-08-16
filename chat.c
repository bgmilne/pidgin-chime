/*
 * Pidgin/libpurple Chime client plugin
 *
 * Copyright © 2017 Amazon.com, Inc. or its affiliates.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
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

#include <string.h>

#include <glib/gi18n.h>
#include <glib/glist.h>

#include <prpl.h>
#include <blist.h>
#include <roomlist.h>

#include "chime.h"
#include "chime-connection-private.h"
#include "chime-room.h"

#include <libsoup/soup.h>

struct chime_chat {
	/* msgs first as it's a "subclass". Really ought to do proper GTypes here... */
	struct chime_msgs msgs;

	ChimeRoom *room;
	const gchar *id;

	PurpleConversation *conv;
	/* For cancellation */
	SoupMessage *members_msg;
	gboolean got_members;
	GHashTable *members;

	GRegex *mention_regex;

	GHashTable *sent_msgs;
};

struct chat_member {
	gchar *id;
	gchar *email;
	gchar *display_name;
};

/*
 * Examples:
 *
 * <@all|All members> becomes All members
 * <@present|Present members> becomes Present members
 * <@75f50e24-d59d-40e4-996b-6ba3ff3f371f|Surname, Name> becomes Surname, Name
 */
#define MENTION_PATTERN "&lt;@([\\w\\-]+)\\|(.*?)&gt;"
#define MENTION_REPLACEMENT "<b>\\2</b>"

/*
 * Returns whether `me` was mentioned in the Chime `message`, and allocates a
 * new string in `*parsed`.
 */
static int parse_inbound_mentions(ChimeConnection *cxn, GRegex *mention_regex, const char *message, char **parsed)
{
	*parsed = g_regex_replace(mention_regex, message, -1, 0, MENTION_REPLACEMENT, 0, NULL);
	return strstr(message, chime_connection_get_profile_id(cxn)) || strstr(message, "&lt;@all|") ||
		strstr(message, "&lt;@present|");
}

static void replace(gchar **dst, const gchar *a, const gchar *b)
{
       gchar **parts = g_strsplit(*dst, a, 0);
       gchar *replaced = g_strjoinv(b, parts);
       g_strfreev(parts);
       g_free(*dst);
       *dst = replaced;
}

static void expand_member_cb(gpointer _member_id, gpointer _member, gpointer _dest)
{
       gchar *member_id = _member_id;
       struct chat_member *member = _member;
       gchar *chime_mention = g_strdup_printf("<@%s|%s>", member_id, member->display_name);
       replace((gchar **) _dest, member->display_name, chime_mention);
       g_free(chime_mention);
}

/*
 * This will simple look for all chat members mentions and replace them with
 * the Chime format for mentioning. As a special case we expand "@all" and
 * "@present".
 */
static gchar *parse_outbound_mentions(GHashTable *members, const gchar *message)
{
       gchar *parsed = g_strdup(message);
       replace(&parsed, "@all", "<@all|All Members>");
       replace(&parsed, "@present", "<@present|Present Members>");
       g_hash_table_foreach(members, expand_member_cb, &parsed);
       return parsed;
}

static void parse_incoming_msg(ChimeConnection *cxn, struct chime_chat *chat,
			       JsonNode *node, time_t msg_time)
{
	PurpleConnection *conn = chat->conv->account->gc;
	int id = purple_conv_chat_get_id(PURPLE_CONV_CHAT(chat->conv));
	const gchar *content, *sender;

	if (!parse_string(node, "Content", &content) ||
	    !parse_string(node, "Sender", &sender))
		return;

	const gchar *from = _("Unknown sender");
	int msg_flags;

	if (!strcmp(sender, chime_connection_get_profile_id(cxn))) {
		from = purple_connection_get_display_name(conn);
		msg_flags = PURPLE_MESSAGE_SEND;
	} else {
		struct chat_member *who = g_hash_table_lookup(chat->members, sender);
		if (who)
			from = who->display_name;
		msg_flags = PURPLE_MESSAGE_RECV;
	}

	gchar *escaped = g_markup_escape_text(content, -1);

	gchar *parsed = NULL;
	if (parse_inbound_mentions(cxn, chat->mention_regex, escaped, &parsed) &&
	    (msg_flags & PURPLE_MESSAGE_RECV)) {
		// Presumably this will trigger a notification.
		msg_flags |= PURPLE_MESSAGE_NICK;
	}
	g_free(escaped);
	serv_got_chat_in(conn, id, from, msg_flags, parsed, msg_time);
	g_free(parsed);
}

static void chat_deliver_msg(ChimeConnection *cxn, struct chime_msgs *msgs,
			     JsonNode *node, time_t msg_time)
{
	struct chime_chat *chat = (struct chime_chat *)msgs; /* Really */
	const gchar *msg_id;

	/* Eliminate duplicates with outbound messages */
	if (parse_string(node, "MessageId", &msg_id) &&
	    g_hash_table_remove(chat->sent_msgs, msg_id))
		return;

	parse_incoming_msg(cxn, chat, node, msg_time);
}

static gboolean add_chat_member(struct chime_chat *chat, JsonNode *node)
{
	const char *id, *email, *display_name, *role;
	PurpleConvChatBuddyFlags flags;
	JsonObject *obj = json_node_get_object(node);
	JsonNode *member = json_object_get_member(obj, "Member");
	if (!member)
		return FALSE;

	const gchar *presence;
	if (!parse_string(node, "Presence", &presence))
		return FALSE;
	if (!strcmp(presence, "notPresent"))
		flags = PURPLE_CBFLAGS_AWAY;
	else if (!strcmp(presence, "present"))
		flags = PURPLE_CBFLAGS_VOICE;
	else {
		printf("Unknown presence %s\n", presence);
		return FALSE;
	}
	if (parse_string(node, "Role", &role) && !strcmp(role, "administrator"))
		flags |= PURPLE_CBFLAGS_OP;

	if (!parse_string(member, "ProfileId", &id) ||
	    !parse_string(member, "Email", &email) ||
	    !parse_string(member, "DisplayName", &display_name))
		return FALSE;

	if (!g_hash_table_lookup(chat->members, id)) {
		struct chat_member *m = g_new0(struct chat_member, 1);
		m->id = g_strdup(id);
		m->email = g_strdup(email);
		m->display_name = g_strdup(display_name);
		g_hash_table_insert(chat->members, m->id, m);

		purple_conv_chat_add_user(PURPLE_CONV_CHAT(chat->conv), m->email,
					  NULL, flags, chat->msgs.members_done);
	} else {
		purple_conv_chat_user_set_flags(PURPLE_CONV_CHAT(chat->conv), email, flags);
	}
	return TRUE;
}

static gboolean chat_msg_jugg_cb(ChimeConnection *cxn, gpointer _chat, JsonNode *data_node)
{
	struct chime_chat *chat = _chat;
	JsonObject *obj = json_node_get_object(data_node);
	JsonNode *record = json_object_get_member(obj, "record");
	if (!record)
		return FALSE;

	const gchar *msg_id;
	if (!parse_string(record, "MessageId", &msg_id))
		return FALSE;

	if (chat->msgs.messages) {
		/* Still gathering messages. Add to the table, to avoid dupes */
		g_hash_table_insert(chat->msgs.messages, (gchar *)msg_id,
				    json_node_ref(record));
		return TRUE;
	}

	const gchar *msg_time;
	GTimeVal tv;
	if (!parse_time(record, "CreatedOn", &msg_time, &tv))
		return FALSE;

	chime_update_last_msg(cxn, TRUE, chat->id, msg_time, msg_id);

	chat_deliver_msg(cxn, &chat->msgs, record, tv.tv_sec);
	return TRUE;
}

static gboolean chat_membership_jugg_cb(ChimeConnection *cxn, gpointer _chat, JsonNode *data_node)
{
	struct chime_chat *chat = _chat;
	JsonObject *obj = json_node_get_object(data_node);
	JsonNode *record = json_object_get_member(obj, "record");
	if (!record)
		return FALSE;

	/* What abpout removal? */
	return add_chat_member(chat, record);
}

void chime_destroy_chat(struct chime_chat *chat)
{
	PurpleConnection *conn = chat->conv->account->gc;
	struct purple_chime *pc = purple_connection_get_protocol_data(conn);
	ChimeConnection *cxn = PURPLE_CHIME_CXN(conn);
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);
	ChimeRoom *room = chat->room;
	int id = purple_conv_chat_get_id(PURPLE_CONV_CHAT(chat->conv));

	if (chat->msgs.soup_msg) {
		soup_session_cancel_message(priv->soup_sess, chat->msgs.soup_msg, 1);
		chat->msgs.soup_msg = NULL;
	}
	if (chat->members_msg) {
		soup_session_cancel_message(priv->soup_sess, chat->members_msg, 1);
		chat->members_msg = NULL;
	}

	const gchar *channel;
	g_object_get(room, "channel", &channel, NULL);
	chime_jugg_unsubscribe(cxn, channel, "RoomMessage", chat_msg_jugg_cb, chat);
	chime_jugg_unsubscribe(cxn, channel, "RoomMembership", chat_membership_jugg_cb, chat);

	serv_got_chat_left(conn, id);
	g_hash_table_remove(pc->live_chats, GUINT_TO_POINTER(id));
	g_hash_table_remove(pc->chats_by_room, room);

	if (chat->msgs.messages)
		g_hash_table_destroy(chat->msgs.messages);
	if (chat->members)
		g_hash_table_destroy(chat->members);
	if (chat->sent_msgs)
		g_hash_table_destroy(chat->sent_msgs);
	g_object_unref(chat->room);
	g_regex_unref(chat->mention_regex);
	g_free(chat);
	printf("Destroyed chat %p\n", chat);
}

static void one_member_cb(JsonArray *array, guint index_,
			  JsonNode *node, gpointer _chat)
{
	add_chat_member(_chat, node);
}

void fetch_chat_memberships(ChimeConnection *cxn, struct chime_chat *chat, const gchar *next_token);
static void fetch_members_cb(ChimeConnection *cxn, SoupMessage *msg, JsonNode *node, gpointer _chat)
{
	struct chime_chat *chat = _chat;
	const gchar *next_token;

	JsonObject *obj = json_node_get_object(node);
	JsonNode *members_node = json_object_get_member(obj, "RoomMemberships");
	JsonArray *members_array = json_node_get_array(members_node);

	chat->members_msg = NULL;
	json_array_foreach_element(members_array, one_member_cb, chat);

	if (parse_string(node, "NextToken", &next_token))
		fetch_chat_memberships(cxn, _chat, next_token);
	else {
		chat->msgs.members_done = TRUE;
		if (chat->msgs.msgs_done)
			chime_complete_messages(cxn, &chat->msgs);
	}
}

void fetch_chat_memberships(ChimeConnection *cxn, struct chime_chat *chat, const gchar *next_token)
{
	ChimeConnectionPrivate *priv = CHIME_CONNECTION_GET_PRIVATE (cxn);
	SoupURI *uri = soup_uri_new_printf(priv->messaging_url, "/rooms/%s/memberships", chat->id);

	soup_uri_set_query_from_fields(uri, "max-results", "50", next_token ? "next-token" : NULL, next_token, NULL);
	chat->members_msg = chime_connection_queue_http_request(cxn, NULL, uri, "GET", fetch_members_cb, chat);
}

static void kill_member(gpointer _member)
{
	struct chat_member *member = _member;

	g_free(member->id);
	g_free(member->email);
	g_free(member->display_name);
	g_free(member);
}

static struct chime_chat *do_join_chat(PurpleConnection *conn, ChimeConnection *cxn, ChimeRoom *room)
{
	if (!room)
		return NULL;

	struct purple_chime *pc = purple_connection_get_protocol_data(conn);
	struct chime_chat *chat = g_hash_table_lookup(pc->chats_by_room, room);
	if (chat)
		return chat;

	chat = g_new0(struct chime_chat, 1);
	chat->room = g_object_ref(room);
	gchar *name, *channel;
	g_object_get(G_OBJECT(room), "id", &chat->id,
		     "name", &name, "channel", &channel, NULL);

	int chat_id = ++pc->chat_id;
	chat->conv = serv_got_joined_chat(conn, chat_id, name);
	g_free(name);

	g_hash_table_insert(pc->live_chats, GUINT_TO_POINTER(chat_id), chat);
	g_hash_table_insert(pc->chats_by_room, room, chat);
	chat->sent_msgs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	chat->members = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, kill_member);

	chime_jugg_subscribe(cxn, channel, "RoomMessage", chat_msg_jugg_cb, chat);
	chime_jugg_subscribe(cxn, channel, "RoomMembership", chat_membership_jugg_cb, chat);
	g_free(channel);

	chat->mention_regex = g_regex_new(MENTION_PATTERN, G_REGEX_EXTENDED, 0, NULL);

	chat->msgs.is_room = TRUE;
	chat->msgs.id = chat->id;
	chat->msgs.cb = chat_deliver_msg;

	const gchar *after = NULL;
	if (chime_read_last_msg(conn, CHIME_OBJECT(chat->room), &after, &chat->msgs.last_msg) &&
	    after && after[0])
		chat->msgs.last_msg_time = g_strdup(after);

	fetch_messages(cxn, &chat->msgs, NULL);
	fetch_chat_memberships(cxn, chat, NULL);

	return chat;
}

void chime_purple_join_chat(PurpleConnection *conn, GHashTable *data)
{
	ChimeConnection *cxn = PURPLE_CHIME_CXN(conn);
	const gchar *roomid = g_hash_table_lookup(data, "RoomId");

	printf("join_chat %p %s %s\n", data, roomid, (gchar *)g_hash_table_lookup(data, "Name"));

	ChimeRoom *room = chime_connection_room_by_id(cxn, roomid);
	if (!room)
		return;
	do_join_chat(conn, cxn, room);
}

void chime_purple_chat_leave(PurpleConnection *conn, int id)
{
	struct purple_chime *pc = purple_connection_get_protocol_data(conn);
	struct chime_chat *chat = g_hash_table_lookup(pc->live_chats, GUINT_TO_POINTER(id));

	chime_destroy_chat(chat);
}

static void sent_msg_cb(GObject *source, GAsyncResult *result, gpointer _chat)
{
	struct chime_chat *chat = _chat;
	ChimeConnection *cxn = CHIME_CONNECTION(source);
	GError *error = NULL;

	JsonNode *msgnode = chime_connection_send_message_finish(cxn, result, &error);
	if (!msgnode) {
		purple_conversation_write(chat->conv, NULL, error->message, PURPLE_MESSAGE_ERROR, time(NULL));
		g_clear_error(&error);
		return;
	} else {
		const gchar *msg_time, *msg_id, *last_seen;
		GTimeVal tv, seen_tv;

		if (!parse_time(msgnode, "CreatedOn", &msg_time, &tv))
			tv.tv_sec = time(NULL);

		/* If we have already received a message at least this new before
		 * the response to the creation arrived, then don't deliver it to
		 * Pidgin again.... */
		if (chime_read_last_msg(purple_conversation_get_gc(chat->conv), CHIME_OBJECT(chat->room), &last_seen, NULL) &&
		    g_time_val_from_iso8601(last_seen, &seen_tv) &&
		    (seen_tv.tv_sec > tv.tv_sec ||
		     (seen_tv.tv_sec == tv.tv_sec && seen_tv.tv_usec >= tv.tv_usec)))
			return;

		/* If we're doing it, then stick it into the hash table so that
		 * chat_deliver_msg won't do it again when it does come back in. */
		if (parse_string(msgnode, "MessageId", &msg_id))
			g_hash_table_add(chat->sent_msgs, g_strdup(msg_id));

		parse_incoming_msg(cxn, chat, msgnode, tv.tv_sec);
	}
}

int chime_purple_chat_send(PurpleConnection *conn, int id, const char *message, PurpleMessageFlags flags)
{
	struct purple_chime *pc = purple_connection_get_protocol_data(conn);
	struct chime_chat *chat = g_hash_table_lookup(pc->live_chats, GUINT_TO_POINTER(id));

	/* Chime does not understand HTML. */
	gchar *unescaped = purple_unescape_html(message);

	/* Expand member names into the format Chime understands */
	gchar *expanded = parse_outbound_mentions(chat->members, unescaped);
	g_free(unescaped);

	chime_connection_send_message_async(pc->cxn, CHIME_OBJECT(chat->room), expanded, NULL, sent_msg_cb, chat);

	g_free(expanded);
	return 0;
}

void purple_chime_init_chats(struct purple_chime *pc)
{
	pc->live_chats = g_hash_table_new(g_direct_hash, g_direct_equal);
	pc->chats_by_room = g_hash_table_new(g_direct_hash, g_direct_equal);
}

void purple_chime_destroy_chats(struct purple_chime *pc)
{
	g_clear_pointer(&pc->live_chats, g_hash_table_unref);
	g_clear_pointer(&pc->chats_by_room, g_hash_table_unref);
}

static void on_chime_room_mentioned(ChimeConnection *cxn, ChimeRoom *room, JsonNode *node, PurpleConnection *conn)
{
	struct purple_chime *pc = purple_connection_get_protocol_data(conn);
	struct chime_chat *chat = g_hash_table_lookup(pc->chats_by_room, room);

	if (!chat)
		chat = do_join_chat(conn, cxn, room);
	if (chat)
		chat_msg_jugg_cb(cxn, chat, node);
}

static void on_chime_new_room(ChimeConnection *cxn, ChimeRoom *room, PurpleConnection *conn)
{
	const gchar *id, *last_mentioned;
	GTimeVal mention_tv;

	/* If no LastMentioned or we can't parse it, nothing to do */
	g_object_get(room, "id", &id, "last-mentioned", &last_mentioned, NULL);
	if (!last_mentioned || !g_time_val_from_iso8601(last_mentioned, &mention_tv))
		return;

	const gchar *msg_time;
	GTimeVal msg_tv;

	if (chime_read_last_msg(conn, CHIME_OBJECT(room), &msg_time, NULL) &&
	    g_time_val_from_iso8601(msg_time, &msg_tv) &&
	    (mention_tv.tv_sec < msg_tv.tv_sec ||
	     (mention_tv.tv_sec == msg_tv.tv_sec && mention_tv.tv_usec <= msg_tv.tv_usec))) {
		/* LastMentioned is older than we've already seen. Nothing to do. */
		return;
	}

	/* We have been mentioned since we last looked at this room. Open it now. */
	do_join_chat(conn, cxn, room);
}

void purple_chime_init_chats_post(PurpleConnection *conn)
{
	struct purple_chime *pc = purple_connection_get_protocol_data(conn);

	chime_connection_foreach_room(pc->cxn, (ChimeRoomCB)on_chime_new_room, conn);

	g_signal_connect(pc->cxn, "room-mention", G_CALLBACK(on_chime_room_mentioned), conn);
}
