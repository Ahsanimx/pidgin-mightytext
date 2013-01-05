/* * MightyText plugin for libpurple/Pidgin * Written by: Eion Robb <eionrobb@gmail.com> * * This program is free software: you can redistribute it and/or modify * it under the terms of the GNU General Public License as published by * the Free Software Foundation, either version 3 of the License, or * (at your option) any later version. * * This program is distributed in the hope that it will be useful, * but WITHOUT ANY WARRANTY; without even the implied warranty of * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the * GNU General Public License for more details. * * You should have received a copy of the GNU General Public License * along with this program.  If not, see <http://www.gnu.org/licenses/>. */ #define TEXTYSERVER "https://textyserver.appspot.com"#define TALKSERVERHOST "talkgadget.google.com"#define TALKSERVER "https://" TALKSERVERHOST // https://textyserver.appspot.com/_ah/OAuthGetRequestToken#ifndef PURPLE_PLUGINS#	define PURPLE_PLUGINS#endif// Glib#include <glib.h>// GNU C libraries#include <stdio.h>#include <string.h>#ifdef __GNUC__	#include <unistd.h>#endif#include <json-glib/json-glib.h>#ifndef G_GNUC_NULL_TERMINATED# if __GNUC__ >= 4#  define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))# else#  define G_GNUC_NULL_TERMINATED# endif#endif// Libpurple functions#include "util.h"#include "accountopt.h"#include "debug.h"#include "version.h"#ifndef _#define _(a) (a)#define N_(a) (a)#endif#include <time.h>#ifdef _WIN32#	include "strptime.c"#endifstatic const gchar *mt_cookie_data(PurpleAccount *account){	static GString *cookie = NULL;	const gchar *sacsid;	const gchar *googappuid;		if (cookie)		g_string_free(cookie, TRUE);	cookie = g_string_new(NULL);		sacsid = purple_account_get_string(account, "sacsid", NULL);	if (sacsid)		g_string_append_printf(cookie, "SACSID=%s;", sacsid);		googappuid = purple_account_get_string(account, "googappuid", NULL);	if (googappuid)		g_string_append_printf(cookie, "GOOGAPPUID=%s;", googappuid);		return cookie->str;}static voidmt_dummy_callback(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){}static PurpleUtilFetchUrlData *mt_fetch_url(PurpleAccount *account, const gchar *url, gboolean include_headers, const gchar *postdata, const gchar *cookiestring, PurpleUtilFetchUrlCallback callback, gpointer user_data){    PurpleUtilFetchUrlData *ret;	GString *headers = g_string_new(NULL);    gchar *host = NULL, *page = NULL, *user = NULL, *password = NULL;    int port;	PurpleProxyInfo *proxy_info;	gchar *proxy_url;        purple_url_parse(url, &host, &port, &page, &user, &password);		if (purple_account_is_disconnected(account)) return NULL;		purple_debug_info("mightytext", "Fetching url %s\n", url);		proxy_info = purple_proxy_get_setup(account);	if (purple_proxy_info_get_type(proxy_info) == PURPLE_PROXY_USE_GLOBAL)		proxy_info = purple_global_proxy_get_info();	if (purple_proxy_info_get_type(proxy_info) == PURPLE_PROXY_HTTP)	{		g_string_append_printf(headers, "%s %s HTTP/1.0\r\n", (postdata ? "POST" : "GET"), url);		proxy_url = g_strdup_printf("http://%s:%d", purple_proxy_info_get_host(proxy_info), purple_proxy_info_get_port(proxy_info));	} else {		g_string_append_printf(headers, "%s /%s HTTP/1.0\r\n", (postdata ? "POST" : "GET"), page);		proxy_url = g_strdup(url);	}    g_string_append_printf(headers, "Connection: close\r\n");    g_string_append_printf(headers, "Host: %s\r\n", host);        if(cookiestring && *cookiestring) {        g_string_append_printf(headers, "Cookie: %s\r\n", cookiestring);    }        if(postdata) {		purple_debug_info("mightytext", "With postdata %s\n", postdata);		        g_string_append(headers, "Content-Type: application/x-www-form-urlencoded\r\n");        g_string_append_printf(headers, "Content-Length: %d\r\n", strlen(postdata));        g_string_append(headers, "\r\n");                g_string_append(headers, postdata);    } else {        g_string_append(headers, "\r\n");    }        g_free(host);    g_free(page);    g_free(user);    g_free(password);		if (callback == NULL)	{		callback = mt_dummy_callback;  // Callback function is required by libpurple :(		purple_debug_misc("mightytext", "Using dummy callback\n");	}        ret = purple_util_fetch_url_request(proxy_url, FALSE, NULL, FALSE, headers->str, include_headers, callback, user_data);		g_string_free(headers, TRUE);	g_free(proxy_url);		return ret;}static void mt_get_messages(PurpleAccount *);static void mt_get_messages_polling(PurpleAccount *);typedef struct _MightyTextLongPollInfo{	PurpleAccount *account;		gchar *clid;	gchar *gsessionid;	gchar *token;	gchar *sid;	guint aid;		PurpleSslConnection *ssl;	guint inpa;} MightyTextLongPollInfo;static voidmt_longpoll_recv_cb(gpointer url_data, gint source, PurpleInputCondition cond){	MightyTextLongPollInfo *pollinfo = url_data;	int len;	char buf[4096];	//Read headers, if not yet read (who cares about headers!)	//if headers have been read:	//read one line - this is the length of bytes to read ahead	//read number of bytes	//chop up response into segments to pass onto another function for handling			//while ((gfud->is_ssl && ((len = purple_ssl_read(gfud->ssl_connection, buf, sizeof(buf))) > 0)) ||	//		(!gfud->is_ssl && (len = read(source, buf, sizeof(buf))) > 0))	//{}static voidmt_longpoll_recv_ssl_cb(gpointer data, PurpleSslConnection *ssl_connection, PurpleInputCondition cond){	mt_longpoll_recv_cb(data, ssl_connection->fd, cond);}static voidmt_longpoll_send_cb(gpointer data, gint source, PurpleInputCondition cond){	MightyTextLongPollInfo *pollinfo = data;	PurpleProxyInfo *proxy_info;	GString *request = g_string_new(NULL);		g_string_append(request, "GET ");		proxy_info = purple_proxy_get_setup(pollinfo->account);	if (purple_proxy_info_get_type(proxy_info) == PURPLE_PROXY_USE_GLOBAL)		proxy_info = purple_global_proxy_get_info();	if (purple_proxy_info_get_type(proxy_info) == PURPLE_PROXY_HTTP)	{		g_string_append(request, TALKSERVER);	}		g_string_append(request, "/talkgadget/dch/bind?VER=8&");	g_string_append_printf(request, "clid=%s&", purple_url_encode(pollinfo->clid));	g_string_append_printf(request, "gsessionid=%s&", purple_url_encode(pollinfo->gsessionid));	g_string_append(request, "prop=data&");	g_string_append_printf(request, "token=%s&", purple_url_encode(pollinfo->token));	g_string_append_printf(request, "ec=%s&", purple_url_encode("[]"));	g_string_append(request, "RID=rpc&");	g_string_append_printf(request, "SID=%s&", purple_url_encode(pollinfo->sid));	g_string_append(request, "CI=0&");	g_string_append_printf(request, "AID=%u&", pollinfo->aid);	g_string_append(request, "TYPE=xmlhttp&");	g_string_append_printf(request, "zx=%012x&", g_random_int());	g_string_append(request, "t=1");		g_string_append(request, " HTTP/1.1\r\n");	g_string_append(request, "Host: " TALKSERVERHOST "\r\n");	g_string_append(request, "Connection: keep-alive\r\n");	g_string_append(request, "Accept: */*\r\n");	g_string_append(request, "\r\n");		if (pollinfo->ssl)		purple_ssl_write(pollinfo->ssl, request->str, request->len);	else		write(source, request->str, request->len);		g_string_free(request, TRUE);		/* We're done writing our request, now start reading the response */	purple_input_remove(pollinfo->inpa);	if (pollinfo->ssl) {		pollinfo->inpa = 0;		purple_ssl_input_add(pollinfo->ssl, mt_longpoll_recv_ssl_cb, pollinfo);	} else {		pollinfo->inpa = purple_input_add(source, PURPLE_INPUT_READ, mt_longpoll_recv_cb, pollinfo);	}}static voidmt_longpoll_connect_cb(gpointer url_data, gint source, const gchar *error_message){	MightyTextLongPollInfo *pollinfo = url_data;	pollinfo->inpa = purple_input_add(source, PURPLE_INPUT_WRITE, mt_longpoll_send_cb, pollinfo);	mt_longpoll_send_cb(pollinfo, source, PURPLE_INPUT_WRITE);}static void mt_longpoll_connect_ssl_cb(gpointer data, PurpleSslConnection *ssl_connection, PurpleInputCondition cond){	MightyTextLongPollInfo *pollinfo = data;	pollinfo->ssl = ssl_connection;		pollinfo->inpa = purple_input_add(ssl_connection->fd, PURPLE_INPUT_WRITE, mt_longpoll_send_cb, pollinfo);	mt_longpoll_send_cb(pollinfo, ssl_connection->fd, PURPLE_INPUT_WRITE);}static voidmt_longpoll_run(PurpleAccount *account, const gchar *clid, const gchar *gsessionid, const gchar *token, const gchar *sid, guint aid){	//Build header and connect manually as we need to keep the connection open for a long time	PurpleProxyInfo *proxy_info;	MightyTextLongPollInfo *pollinfo = g_new0(MightyTextLongPollInfo, 1);		pollinfo->account = account;	pollinfo->clid = g_strdup(clid);	pollinfo->gsessionid = g_strdup(gsessionid);	pollinfo->token = g_strdup(token);	pollinfo->sid = g_strdup(sid);	pollinfo->aid = aid;		proxy_info = purple_proxy_get_setup(account);	if (purple_proxy_info_get_type(proxy_info) == PURPLE_PROXY_USE_GLOBAL)		proxy_info = purple_global_proxy_get_info();	if (purple_proxy_info_get_type(proxy_info) == PURPLE_PROXY_HTTP)	{		purple_proxy_connect(NULL, account, purple_proxy_info_get_host(proxy_info), purple_proxy_info_get_port(proxy_info), mt_longpoll_connect_cb, pollinfo);	} else {		purple_ssl_connect(account, TALKSERVERHOST, 443, mt_longpoll_connect_ssl_cb, NULL, pollinfo);	}}static voidmt_longpoll_getsid_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){	MightyTextLongPollInfo *pollinfo = user_data;	const gchar *sid_start = ",[\"c\",\"";	gchar *sid;		sid = g_strstr_len(url_text, len, sid_start);	if (!sid)	{		// um... ?		return;	}	sid += strlen(sid_start);	sid = g_strndup(sid, strchr(sid, '"') - sid);		mt_longpoll_run(pollinfo->account, pollinfo->clid, pollinfo->gsessionid, pollinfo->token, sid, 2);		g_free(sid);	g_free(pollinfo->clid);	g_free(pollinfo->gsessionid);	g_free(pollinfo->token);	g_free(pollinfo);}static voidmt_longpoll_getsid(PurpleAccount *account, const gchar *clid, const gchar *gsessionid, const gchar *token){	//https://332.talkgadget.google.com/talkgadget/dch/bind?VER=8&clid=D061D1DB7078FDDB&gsessionid&prop=data&token=AHRlWrpoKvevubjymcByvHqkyC4-b4QtDu-CS3Yn7IOC54mAa4nSuVHVh4rHJHWzghRvojGE-MmYWp4aZadv1vBDYM87uzRVco9KlE6TqyMVgqX0J03gZd41cs6SqPJkHJBr4dCzqdHA&ec=%5B%5D&RID=35539&CVER=1&zx=4woi9dtrrsnu&t=1	GString *url = g_string_new(TALKSERVER "/talkgadget/dch/bind?");	const gchar *postdata = "count=0";	MightyTextLongPollInfo *pollinfo = g_new0(MightyTextLongPollInfo, 1);		g_string_append(url, "VER=8&");	g_string_append_printf(url, "clid=%s&", purple_url_encode(clid));	g_string_append_printf(url, "gsessionid=%s&", purple_url_encode(gsessionid));	g_string_append(url, "prop=data&");	g_string_append_printf(url, "token=%s&", purple_url_encode(token));	g_string_append_printf(url, "ec=%s&", purple_url_encode("[]"));	g_string_append(url, "RID=35539&");	g_string_append(url, "CVER=1&");	g_string_append_printf(url, "zx=%012x&", g_random_int());	g_string_append(url, "t=1");		pollinfo->account = account;	pollinfo->clid = g_strdup(clid);	pollinfo->gsessionid = g_strdup(gsessionid);	pollinfo->token = g_strdup(token);		mt_fetch_url(account, url->str, FALSE, postdata, NULL, mt_longpoll_getsid_cb, pollinfo);		g_string_free(url, TRUE);}static voidmt_get_channel_token_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){	PurpleAccount *account = user_data;	const gchar *channel_token_start = "valid_token:";	gchar *channel_token;		channel_token = g_strstr_len(url_text, len, channel_token_start);	if (!channel_token)	{		// um... ?		return;	}	channel_token += strlen(channel_token_start);		purple_account_set_string(account, "channel_token", channel_token);		mt_get_messages(account);}static voidmt_get_data_client_info_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){/*relevant part of /d response looks like this:        we extract clid and gsid from itvar a = new chat.WcsDataClient("http://talkgadget.google.com/talkgadget/",		"",		"3496699hh4591E19", # clid		"-p__ZFNDJmm-VozEjdST0A", #gsid		timestamp,		"WCX",		token);*/	PurpleAccount *account = user_data;	const gchar * client_info_start = "new chat.WcsDataClient(";	gchar *next_token, *end_token;	gchar *clientid = NULL;	gchar *googlesessionid = NULL;	guint i;		next_token = g_strstr_len(url_text, len, client_info_start);	if (!next_token)	{		//access token probably timed out - TODO check that theres a 401 error		purple_account_set_string(account, "channel_token", NULL);		//Fallback to the polling mechanism for now		mt_get_messages_polling(account);		return;	}	next_token += strlen(client_info_start);		for(i = 0; i < 7; i++)	{		next_token = strchr(next_token, '"');		if (!next_token) break; // :(		next_token++;		end_token = strchr(next_token, '"');		if (!end_token) break; // :S				if (i == 2)		{			clientid = g_strndup(next_token, end_token - next_token);		} else if (i == 3)		{			googlesessionid = g_strndup(next_token, end_token - next_token);		}				next_token = end_token + 2;	}		purple_debug_info("mightytext", "Client ID is %s\n", clientid);	purple_debug_info("mightytext", "Google SID is %s\n", googlesessionid);		mt_longpoll_getsid(account, clientid, googlesessionid, purple_account_get_string(account, "channel_token", NULL));}static voidmt_get_messages_longpoll(PurpleAccount *account){	const gchar *channel_token;	GString *data_client_url;	gchar *xpc;		channel_token = purple_account_get_string(account, "channel_token", NULL);	//if we have a channel token, use that to start the channel session, otherwise, request a channel token	if (!channel_token)	{		const gchar *channel_token_url = TEXTYSERVER "/getJson?function=getChannelToken&mins=120";		mt_fetch_url(account, channel_token_url, FALSE, NULL, mt_cookie_data(account), mt_get_channel_token_cb, account);		return;	}		data_client_url = g_string_new(TALKSERVER);	g_string_append_printf(data_client_url, "/talkgadget/d?token=%s&", purple_url_encode(channel_token));		xpc = g_strdup_printf("{\"cn\":\"%010x\",\"tp\":null,\"osh\":null,\"ppu\":\"https://mightytext.net/_ah/channel/xpc_blank\",\"lpu\":\"" TALKSERVER "/talkgadget/xpc_blank\"}", g_random_int());	g_string_append_printf(data_client_url, "xpc=%s", purple_url_encode(xpc));	g_free(xpc);		mt_fetch_url(account, data_client_url->str, FALSE, NULL, NULL, mt_get_data_client_info_cb, account);		g_string_free(data_client_url, TRUE);}static time_tmt_string_to_time(const gchar *string){	//convert from timestamp like	//Jan 5, 2013 5:34:35 PM	struct tm time;		strptime(string, "%b %d, %Y %I:%M:%S %p", &time);		//purple_debug_info("mightytext","String '%s' converts to timestamp '%d'\n", string, (int)mktime(&time));		return mktime(&time);}static voidmt_get_messages(PurpleAccount *account){#if 0	return mt_get_messages_longpoll(account);#endif	//Poll periodically for messages, since we can't get the Google Channel API code to work yet	return mt_get_messages_polling(account);}static voidmt_got_mms_image(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){	PurpleConversation *conv = user_data;	gint icon_id;	gchar *msg_tmp;		icon_id = purple_imgstore_add_with_id((gpointer)url_text, len, NULL);		msg_tmp = g_strdup_printf("<img id='%d'>", icon_id);	purple_conversation_write(conv, conv->name, msg_tmp, PURPLE_MESSAGE_SYSTEM, time(NULL));	g_free(msg_tmp);		purple_imgstore_unref_by_id(icon_id);}static voidmt_get_messages_timeout_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){/*{  "messages": [    {      "id": "19b7cc45-6869-49fd-9825-4b14d3d059ed",      "email": "eionrobb@gmail.com",      "ts_carrier": "Jan 5, 2013 5:34:35 PM",      "ts_server": "Jan 5, 2013 4:34:45 AM",      "phone_num": "+6421478252",      "body": "test message",      "ts_phone_forward": "Jan 5, 2013 5:34:36 PM",      "mighty_id": "1079459312-1357360475000",      "type": 10,      "source_client": 30,      "is_read": false,      "is_starred": false,      "status_route": 0,      "msgid_phone_db": "1079459312",      "phone_num_clean": 1478252,      "inbox_outbox": 60    }  ]}*/	PurpleAccount *account = user_data;	PurpleConnection *pc = purple_account_get_connection(account);	JsonParser *parser = json_parser_new();	if (!json_parser_load_from_data(parser, url_text, len, NULL))	{		purple_debug_error("mightytext", "Error parsing response :(\n");	} else {		JsonNode *root = json_parser_get_root(parser);		JsonObject *rootobj = json_node_get_object(root);		JsonArray *messages = json_object_get_array_member(rootobj, "messages");		guint messages_length = json_array_get_length(messages);		gint message_index;		//time_t last_timestamp = (time_t) purple_account_get_int(account, "last_timestamp", 0);		//time_t newest_timestamp = last_timestamp;		const gchar *newest_message_id = purple_account_get_string(account, "newest_message_id", NULL);				if (newest_message_id != NULL)			for(message_index = 0; message_index < messages_length; message_index++)			{				//Only get message id's after the last one we saw				JsonObject *message = json_array_get_object_element(messages, message_index);				const gchar *id = json_object_get_string_member(message, "id");								if (g_str_equal(id, newest_message_id))				{					messages_length = message_index;					break;				}			}				//Run through the messages backwards, since theyre in order of newest -> oldest		for(message_index = messages_length - 1; message_index >= 0; message_index--)		{			JsonObject *message = json_array_get_object_element(messages, message_index);			PurpleConversation *conv;			const gchar *timestring;			time_t timestamp;			const gchar *from;			gchar *body, *body_html;			gint64 outgoing;			gint64 type;						timestring = json_object_get_string_member(message, "ts_server");  // GMT!			timestamp = mt_string_to_time(timestring);						//purple_debug_info("mightytext", "String '%s' converts to timestamp '%d'\n", timestring, (int)timestamp);						/*if (timestamp < last_timestamp)				continue;			if (timestamp > newest_timestamp)				newest_timestamp = timestamp;*/						from = json_object_get_string_member(message, "phone_num");			body = g_strstrip(g_strdup(json_object_get_string_member(message, "body")));			outgoing = json_object_get_int_member(message, "inbox_outbox");			type = json_object_get_int_member(message, "type");						conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, from, account);			if (conv == NULL)			{				conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, from);			}						body_html = purple_strdup_withhtml(body);			if (type >= 80)			{				purple_conversation_write(conv, from, body_html, PURPLE_MESSAGE_SYSTEM, timestamp);			} else if (outgoing == 60)			{				serv_got_im(pc, from, body_html, PURPLE_MESSAGE_RECV, timestamp);			} else {				purple_conversation_write(conv, from, body_html, PURPLE_MESSAGE_SEND, timestamp);			}			if (type == 11)			{				//image attached at				const gchar *imageurlfmt = TEXTYSERVER "/imageserve?function=fetchFile&id=%s";				const gchar *msgid = json_object_get_string_member(message, "id");				gchar *imageurl = g_strdup_printf(imageurlfmt, msgid);								mt_fetch_url(account, imageurl, FALSE, NULL, mt_cookie_data(account), mt_got_mms_image, conv);								g_free(imageurl);			}			g_free(body);			g_free(body_html);		}		if (TRUE)		{			JsonObject *newest_message = json_array_get_object_element(messages, 0);			purple_account_set_string(account, "newest_message_id", json_object_get_string_member(newest_message, "id"));		}		//purple_account_set_int(account, "last_timestamp", newest_timestamp); 	}	g_object_unref(parser);}static gbooleanmt_get_messages_timeout(PurpleAccount *account){	const gchar *url = TEXTYSERVER "/api?function=GetMessages&start_range=0&end_range=100";		mt_fetch_url(account, url, FALSE, NULL, mt_cookie_data(account), mt_get_messages_timeout_cb, account);		if (purple_account_is_disconnected(account))		return FALSE;	return TRUE;}static voidmt_get_messages_polling(PurpleAccount *account){	mt_get_messages_timeout(account);	purple_timeout_add_seconds(20, (GSourceFunc)mt_get_messages_timeout, account);}static PurpleContact *mt_find_contact(const gchar *contactId){	PurpleBlistNode *cur;		cur = purple_blist_get_root();	while((cur = purple_blist_node_next(cur, TRUE)))	{		if (PURPLE_BLIST_NODE_IS_CONTACT(cur))		{			const gchar *id = purple_blist_node_get_string(cur, "mightytext_contactid");			if (id && *id && g_str_equal(id, contactId))			{				return (PurpleContact *)cur;			}		}	}		return NULL;}static voidmt_get_contact_photo(PurpleAccount *account, const gchar *who){	const gchar *url = TEXTYSERVER "/phonecontact?function=getPhoneContactPhotos&phone_num_clean=%s";}static voidmt_get_contacts_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){	PurpleAccount *account = user_data;	/*[  {    "contactId": "3410",    "displayName": "Eion",    "emailList": [],    "phoneList": [      {        "phoneNumber": "+6421478252",        "type": "7"      }    ]  }]	*/	JsonParser *parser = json_parser_new();	PurpleGroup *mtgroup;		mtgroup = purple_find_group("MightyText");	if (!mtgroup)	{		mtgroup = purple_group_new("MightyText");		purple_blist_add_group(mtgroup, NULL);	}		//purple_debug_misc("mightytext", "Contacts data is %s\n", url_text);		if (!json_parser_load_from_data(parser, url_text, len, NULL))	{		purple_debug_error("mightytext", "Error parsing response :(\n");	} else {		JsonNode *root = json_parser_get_root(parser);		JsonArray *contacts = json_node_get_array(root);		guint contacts_length = json_array_get_length(contacts);		guint contact_index;		purple_debug_info("mightytext", "Number of contacts %u\n", contacts_length);		for(contact_index = 0; contact_index < contacts_length; contact_index++)		{			JsonObject *contact = json_array_get_object_element(contacts, contact_index);			const gchar *name = json_object_get_string_member(contact, "displayName");			const gchar *id = json_object_get_string_member(contact, "contactId");			PurpleContact *pcontact = mt_find_contact(id);			JsonArray *numbers = json_object_get_array_member(contact, "phoneList");			guint numbers_length = json_array_get_length(numbers);			guint numbers_index;			for(numbers_index = 0; numbers_index < numbers_length; numbers_index++)			{				JsonObject *number = json_array_get_object_element(numbers, numbers_index);				const gchar *phone = json_object_get_string_member(number, "phoneNumber");				const gchar *type = json_object_get_string_member(number, "type");				PurpleBuddy *pbuddy;								//TODO only handle the right 'type' of phone number.  2 & 7 ?								pbuddy = purple_find_buddy(account, phone);				if (!pbuddy)				{					pbuddy = purple_buddy_new(account, phone, name);					// Look for an existing contact to add this buddy to					if (!pcontact)					{						pcontact = purple_contact_new();						purple_blist_node_set_string((PurpleBlistNode *)pcontact, "mightytext_contactid", id);						purple_blist_add_contact(pcontact, mtgroup, NULL);					}					purple_blist_add_buddy(pbuddy, pcontact, NULL, NULL);					purple_debug_info("mightytext", "Added buddy %s %s\n", phone, name);				} else {				/* //Dont do this for now, if the user moved the 'buddy' somewhere else					//check that the contact id's match					if (!g_str_equal(purple_blist_node_get_string(purple_buddy_get_contact(pbuddy), "mightytext_contactid"), id))					{						// Move the buddy to the right contact					}				*/				}								purple_prpl_got_user_status(account, phone, purple_primitive_get_id_from_type(PURPLE_STATUS_AVAILABLE), NULL);								// TODO download photos			}		}	}	g_object_unref(parser);}static voidmt_get_contacts(PurpleAccount *account){	const gchar *url = TEXTYSERVER "/phonecontact?function=getPhoneContacts";		purple_debug_info("mightytext", "Fetching contacts from server\n");		mt_fetch_url(account, url, FALSE, NULL, mt_cookie_data(account), mt_get_contacts_cb, account);}static voidmt_auth_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){	PurpleAccount *account = user_data;	const gchar *sacsidcookieprefix = "Set-Cookie: SACSID=";	const gchar *googappuidprefix = "Set-Cookie: GOOGAPPUID=";	gchar *sacsid, *googappuid;		sacsid = g_strstr_len(url_text, len, sacsidcookieprefix);	if (!sacsid)	{		purple_connection_error_reason(purple_account_get_connection(account), PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED, "Authentication failed");		return;	}	sacsid += strlen(sacsidcookieprefix);	sacsid = g_strndup(sacsid, strchr(sacsid, ';') - sacsid);		purple_account_set_string(account, "sacsid", sacsid);		g_free(sacsid);		/*googappuid = g_strstr_len(url_text, len, googappuidprefix);	if (!googappuid)	{		purple_connection_error_reason(purple_account_get_connection(account), PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED, "Authentication failed");		return;	}	googappuid += strlen(googappuidprefix);	googappuid = g_strndup(googappuid, strchr(googappuid, ';') - googappuid);		purple_account_set_string(account, "googappuid", googappuid);		g_free(googappuid);*/			purple_connection_set_state(purple_account_get_connection(account), PURPLE_CONNECTED);	mt_get_contacts(account);	mt_get_messages(account);}static voidmt_login_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){	PurpleAccount *account = user_data;	const gchar *urlpartial = TEXTYSERVER "/_ah/login?auth=%s&continue=javascript:";	gchar *url, *auth;	const gchar *authprefix = "Auth=";		auth = g_strstr_len(url_text, len, authprefix);	if (!auth)	{		purple_connection_error_reason(purple_account_get_connection(account), PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED, "Authentication failed");		return;	}	auth += strlen(authprefix);	auth = g_strndup(auth, strchr(auth, '\n') - auth);		purple_debug_info("mightytext", "Auth is %s\n", auth);		url = g_strdup_printf(urlpartial, auth);	mt_fetch_url(account, url, TRUE, NULL, NULL, mt_auth_cb, account);		g_free(url);	g_free(auth);		purple_connection_update_progress(purple_account_get_connection(account), _("Logging in to MightyText"), 1, 4);}static void mt_login(PurpleAccount *account){	const gchar *url = "https://www.google.com/accounts/ClientLogin";	GString *postdata = g_string_new(NULL);		if(purple_account_get_string(account, "sacsid", NULL))	{		purple_connection_set_state(purple_account_get_connection(account), PURPLE_CONNECTED);		mt_get_contacts(account);		mt_get_messages(account);		return;	}		g_string_append_printf(postdata, "Email=%s&", purple_url_encode(purple_account_get_username(account)));	g_string_append_printf(postdata, "Passwd=%s&", purple_url_encode(purple_account_get_password(account)));	g_string_append(postdata, "service=ah&");	g_string_append(postdata, "source=Pidgin&");	g_string_append(postdata, "accountType=HOSTED_OR_GOOGLE&");		mt_fetch_url(account, url, FALSE, postdata->str, NULL, mt_login_cb, account);		g_string_free(postdata, TRUE);		purple_connection_update_progress(purple_account_get_connection(account), _("Logging in to Google"), 0, 4);}static void mt_close(PurpleConnection *pc){}static const char *mt_list_icon(PurpleAccount *account, PurpleBuddy *buddy){	return "mightytext";}static GList *mt_status_types(PurpleAccount *account){	GList *types = NULL;	PurpleStatusType *status;	status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, NULL, "Online", TRUE, TRUE, FALSE);	types = g_list_append(types, status);		status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, NULL, "Offline", TRUE, TRUE, FALSE);	types = g_list_append(types, status);		return types;}static voidmt_send_im_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message){	}static int mt_send_im(PurpleConnection *pc, const gchar *who, const gchar *message, PurpleMessageFlags flags){	gchar *stripped;	const gchar *url = TEXTYSERVER "/client?function=send";	GString *postdata = g_string_new(NULL);		if (g_str_has_prefix(message, "?OTR"))		return 0;		stripped = g_strstrip(purple_markup_strip_html(message));		g_string_append_printf(postdata, "phone=%s&", purple_url_encode(who));	g_string_append(postdata, "type=10&");	g_string_append(postdata, "source_client=31&");	g_string_append(postdata, "deviceType=ac2dm&");	g_string_append(postdata, "action=send_sms&");	g_string_append_printf(postdata, "action_data=%s&", purple_url_encode(message));		mt_fetch_url(pc->account, url, FALSE, postdata->str, mt_cookie_data(pc->account), mt_send_im_cb, NULL);		g_string_free(postdata, TRUE);	g_free(stripped);		//Don't display message here, display from callback	return 0;}static gbooleanmt_offline_msg(const PurpleBuddy *buddy){	return TRUE;}static voidmt_keepalive(PurpleConnection *pc){	const gchar *url = TEXTYSERVER "/test?function=capi&body=CAPI_HEALTH&phone_num=123456789";}static const gchar *mt_normalise(const PurpleAccount *acct, const char *who){	//Phone numbers are only numbers with an optional + at the front 	static gchar normalised[100];	guint i, next = 0;		memset(normalised, 0, sizeof(normalised));	if (who[0] == '+')		normalised[next++] = '+';	for(i = 0; i < strlen(who) && next < 100; i++)	{		//strip out anything not a number		if (who[i] >= '0' && who[i] <= '9')			normalised[next++] = who[i];	}		return normalised;}static const gchar *mt_list_emblem(PurpleBuddy *buddy){	return "mobile";}/************************************************/static gbooleanplugin_load(PurplePlugin *plugin){	return TRUE;}static gbooleanplugin_unload(PurplePlugin *plugin){	return TRUE;}static voidplugin_init(PurplePlugin *plugin){	//purple_signal_connect(purple_get_core(), "uri-handler", plugin, PURPLE_CALLBACK(mightytext_uri_handler), NULL);}PurplePluginProtocolInfo prpl_info = {	/* options */	OPT_PROTO_SLASH_COMMANDS_NATIVE,	NULL,                /* user_splits */	NULL,                /* protocol_options */	{"png,gif,jpeg", 0, 0, 96, 96, 0, PURPLE_ICON_SCALE_SEND}, /* icon_spec */	mt_list_icon,        /* list_icon */	mt_list_emblem,      /* list_emblem */	NULL,                /* status_text */	NULL,                /* tooltip_text */	mt_status_types,     /* status_types */	NULL,                /* blist_node_menu */	NULL,                /* chat_info */	NULL,                /* chat_info_defaults */	mt_login,            /* login */	mt_close,            /* close */	mt_send_im,          /* send_im */	NULL,                /* set_info */	NULL,                /* send_typing */	NULL,                /* get_info */	NULL,                /* set_status */	NULL,                /* set_idle */	NULL,                /* change_passwd */	NULL,                /* add_buddy */	NULL,                /* add_buddies */	NULL,                /* remove_buddy */	NULL,                /* remove_buddies */	NULL,                /* add_permit */	NULL,                /* add_deny */	NULL,                /* rem_permit */	NULL,                /* rem_deny */	NULL,                /* set_permit_deny */	NULL,                /* join_chat */	NULL,                /* reject chat invite */	NULL,                /* get_chat_name */	NULL,                /* chat_invite */	NULL,                /* chat_leave */	NULL,                /* chat_whisper */	NULL,                /* chat_send */	mt_keepalive,        /* keepalive */	NULL,                /* register_user */	NULL,                /* get_cb_info */	NULL,                /* get_cb_away */	NULL,                /* alias_buddy */	NULL,                /* group_buddy */	NULL,                /* rename_group */	NULL,                /* buddy_free */	NULL,                /* convo_closed */	mt_normalise,        /* normalize */	NULL,                /* set_buddy_icon */	NULL,                /* remove_group */	NULL,                /* get_cb_real_name */	NULL,                /* set_chat_topic */	NULL,				 /* find_blist_chat */	NULL,                /* roomlist_get_list */	NULL,                /* roomlist_cancel */	NULL,                /* roomlist_expand_category */	NULL,                /* can_receive_file */	NULL,                /* send_file */	NULL,                /* new_xfer */	mt_offline_msg,      /* offline_message */	NULL,                /* whiteboard_prpl_ops */	NULL,                /* send_raw */	NULL,                /* roomlist_room_serialize */	NULL,                /* unregister_user */	NULL,                /* send_attention */	NULL,                /* attention_types */#if PURPLE_MAJOR_VERSION == 2 && PURPLE_MINOR_VERSION == 1	(gpointer)#endif	sizeof(PurplePluginProtocolInfo), /* struct_size */	NULL,                /* get_account_text_table */	NULL,                /* initiate_media */	NULL,                /* can_do_media */	NULL,                /* get_moods */	NULL,                /* set_public_alias */	NULL                 /* get_public_alias */#if PURPLE_MAJOR_VERSION == 2 && PURPLE_MINOR_VERSION >= 8,	NULL,                /* add_buddy_with_invite */	NULL                 /* add_buddies_with_invite */#endif};static PurplePluginInfo info = {	PURPLE_PLUGIN_MAGIC,/*	PURPLE_MAJOR_VERSION,	PURPLE_MINOR_VERSION,*/	2, 1,	PURPLE_PLUGIN_PROTOCOL, /* type */	NULL, /* ui_requirement */	0, /* flags */	NULL, /* dependencies */	PURPLE_PRIORITY_DEFAULT, /* priority */	"prpl-eionrobb-mightytext", /* id */	"MightyText", /* name */	"1.0", /* version */	"Send SMS through your Android mobile via the MightyText service", /* summary */	"", /* description */	"Eion Robb <eion@robbmob.com>", /* author */	"", /* homepage */	plugin_load, /* load */	plugin_unload, /* unload */	NULL, /* destroy */	NULL, /* ui_info */	&prpl_info, /* extra_info */	NULL, /* prefs_info */	NULL, /* actions */	NULL, /* padding */	NULL,	NULL,	NULL};PURPLE_INIT_PLUGIN(mightytext, plugin_init, info);