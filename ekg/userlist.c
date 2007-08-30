/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Wojtek Kaniewski <wojtekka@irc.pl>
 *                          Robert J. Wo�ny <speedy@ziew.org>
 *                          Piotr Domagalski <szalik@szalik.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ekg2-config.h"
#include "win32.h"

#ifndef __FreeBSD__
#define _XOPEN_SOURCE 600
#define __EXTENSIONS__
#endif

#include <sys/types.h>
#include <sys/stat.h>
#ifndef NO_POSIX_SYSTEM
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "commands.h"
#include "dynstuff.h"
#ifndef HAVE_STRLCAT
#  include "compat/strlcat.h"
#endif
#ifndef HAVE_STRLCPY
#  include "compat/strlcpy.h"
#endif
#include "plugins.h"
#include "stuff.h"
#include "themes.h"
#include "userlist.h"
#include "vars.h"
#include "windows.h"
#include "xmalloc.h"
#include "log.h"

#include "queries.h"

#ifndef PATH_MAX
#  define PATH_MAX _POSIX_PATH_MAX
#endif

list_t userlist = NULL;

struct ignore_label ignore_labels[IGNORE_LABELS_MAX] = {
	{ IGNORE_STATUS, "status" },
	{ IGNORE_STATUS_DESCR, "descr" },
	{ IGNORE_NOTIFY, "notify" },
	{ IGNORE_MSG, "msg" },
	{ IGNORE_DCC, "dcc" },
	{ IGNORE_EVENTS, "events" },
	{ IGNORE_XOSD, "xosd" },
	{ 0, NULL }
};

/*
 * userlist_compare()
 *
 * funkcja pomocna przy list_add_sorted().
 *
 *  - data1, data2 - dwa wpisy userlisty do por�wnania.
 *
 * zwraca wynik xstrcasecmp() na nazwach user�w.
 */
static int userlist_compare(void *data1, void *data2)
{
	userlist_t *a = data1, *b = data2;
	
	if (!a || !a->nickname || !b || !b->nickname)
		return 1;

	return xstrcasecmp(a->nickname, b->nickname);
}

static int userlist_resource_compare(void *data1, void *data2)
{
	ekg_resource_t *a = data1, *b = data2;
	
	if (!a || !b)
		return 0;

	if (a->prio != b->prio) return (b->prio - a->prio);		/* first sort by prio,	first users with larger prio! */

	return xstrcmp(a->name, b->name);				/* than sort by name */
}

/*
 * userlist_add_entry()
 *
 * dodaje do listy kontakt�w pojedyncz� lini� z pliku lub z serwera.
 */
void userlist_add_entry(session_t *session, const char *line)
{
	char **entry = array_make(line, ";", 8, 0, 0);
	userlist_t *u;
	int count, i;

	if ((count = array_count(entry)) < 7) {
		array_free(entry);
		return;
	}
	
	u = xmalloc(sizeof(userlist_t));
	if (atoi(entry[6])) 
		u->uid = saprintf("gg:%s", entry[6]);
	else
		u->uid = xstrdup(entry[6]);

	for (i = 0; i < 6; i++) {
		if (!xstrcmp(entry[i], "(null)") || !xstrcmp(entry[i], "")) {
			xfree(entry[i]);
			entry[i] = NULL;
		}
	}
			
	u->first_name = xstrdup(entry[0]);
	u->last_name = xstrdup(entry[1]);

	if (entry[3] && !valid_nick(entry[3]))
		u->nickname = saprintf("_%s", entry[3]);
	else
		u->nickname = xstrdup(entry[3]);

	u->mobile = xstrdup(entry[4]);
	u->groups = group_init(entry[5]);
	u->status = xstrdup(EKG_STATUS_NA);
	
	if (entry[7])
		u->foreign = saprintf(";%s", entry[7]);
	else
		u->foreign = xstrdup("");

	for (i = 0; i < count; i++)
		xfree(entry[i]);

	xfree(entry);

	list_add_sorted(&(session->userlist), u, 0, userlist_compare);
}

/*
 * userlist_dump()
 *
 * zapisuje list� kontakt�w w postaci tekstowej.
 *
 * zwraca zaalokowany bufor, kt�ry nale�y zwolni�.
 */
char *userlist_dump(session_t *session)
{
	string_t s;
	list_t l;
/*	if (!session->userlist) 
 *		return NULL;	
 */
	s = string_init(NULL);
	for (l = session->userlist; l; l = l->next) {
		userlist_t *u = l->data;
		char *line;
		const char *uid;
		char *groups;

		uid = (!strncmp(u->uid, "gg:", 3)) ? u->uid + 3 : u->uid;

		groups = group_to_string(u->groups, 1, 0);
		
		line = saprintf(("%s;%s;%s;%s;%s;%s;%s%s\r\n"),
			(u->first_name) ? u->first_name : "",
			(u->last_name) ? u->last_name : "",
			(u->nickname) ? u->nickname : "",
			(u->nickname) ? u->nickname : "",
			(u->mobile) ? u->mobile : "",
			groups,
			uid,
			(u->foreign) ? u->foreign : "");
		
		string_append(s, line);

		xfree(line);
		xfree(groups);
	}	

	return string_free(s, 0);
}

/**
 * userlist_read()
 *
 * wczytuje list� kontakt�w z pliku uid_sesji-userlist w postaci eksportu
 * tekstowego listy kontakt�w windzianego klienta.
 *
 * @param session
 * @return 0 on success, -1 file not found
 */
int userlist_read(session_t *session)
{
        const char *filename;
        char *buf;
        FILE *f;
        char *tmp=saprintf("%s-userlist", session->uid);

        if (!(filename = prepare_path(tmp, 0))) {
                xfree(tmp);
                return -1;
        }       
        xfree(tmp);
        
        if (!(f = fopen(filename, "r")))
                return -1;
                        
        while ((buf = read_file(f, 0))) {
                if (buf[0] == '#' || (buf[0] == '/' && buf[1] == '/'))
                        continue;
                
                userlist_add_entry(session, buf);
        }

        fclose(f);
                
        return 0;
} 

/*
 * userlist_write()
 *
 * zapisuje list� kontakt�w w pliku ~/.ekg/gg:NUMER-userlist
 */
int userlist_write(session_t *session)
{
	const char *filename;
	char *contacts;
	FILE *f;
	char *tmp = saprintf("%s-userlist", session->uid); 

	if (!(contacts = userlist_dump(session))) {
		xfree(tmp);
		return -1;
	}
	
	if (!(filename = prepare_path(tmp, 1))) {
		xfree(contacts);
		xfree(tmp);
		return -1;
	}
	
	if (!(f = fopen(filename, "w"))) {
		xfree(contacts);
		return -2;
	}
	fchmod(fileno(f), 0600);
	fputs(contacts, f);
	fclose(f);
	
	xfree(contacts);
	xfree(tmp);

	return 0;
}

/*
 * userlist_write_crash()
 *
 * zapisuje list� kontakt�w w sytuacji kryzysowej jak najmniejszym
 * nak�adem pami�ci i pracy.
 */
void userlist_write_crash()
{
	list_t l;
	char name[32];
	FILE *f;

	chdir(config_dir);
	
	snprintf(name, sizeof(name), "userlist.%d", (int) getpid());
	if (!(f = fopen(name, "w")))
		return;

	chmod(name, 0400);
		
	for (l = userlist; l; l = l->next) {
		userlist_t *u = l->data;
		list_t m;
		
		fprintf(f, "%s;%s;%s;%s;%s;", 
			(u->first_name) ? u->first_name : "",
			(u->last_name) ? u->last_name : "",
			(u->nickname) ? u->nickname : "",
			(u->nickname) ? u->nickname : "",
			(u->mobile) ? u->mobile : "");
		
		for (m = u->groups; m; m = m->next) {
			struct ekg_group *g = m->data;

			if (m != u->groups)
				fprintf(f, ",");

			fprintf(f, "%s", g->name);
		}
		
		fprintf(f, ";%s%s\r\n", u->uid, u->foreign);
	}	

	fclose(f);
}

/*
 * userlist_clear_status()
 *
 * czy�ci stan u�ytkownik�w na li�cie. je�li uin != 0 to
 * to czy�ci danego u�ytkownika.
 *
 *  - uin.
 */
void userlist_clear_status(session_t *session, const char *uid)
{
        list_t l;

	if (!session)
		return;

        for (l = session->userlist; l; l = l->next) {
                userlist_t *u = l->data;

		if (!uid || !xstrcasecmp(uid, u->uid)) {
			xfree(u->status);
			u->status = xstrdup(EKG_STATUS_NA);
			memset(&u->ip, 0, sizeof(struct in_addr));
			u->port = 0;
			xfree(u->descr);
			u->descr = NULL;

			userlist_resource_free(u);
		}
        }
}

/*
 * userlist_free()
 *
 * czy�ci list� u�ytkownik�w i zwalnia pami��.
 */
void userlist_free(session_t *session)
{
	if (!session)
		return;

	userlist_free_u(&(session->userlist));
}

/* 
 * userlist_free_u()
 *
 * clear and remove from memory given userlist
 */
void userlist_free_u (list_t *userlist)
{
        list_t l;

        if (!*userlist)
                return;

        for (l = *userlist; l; l = l->next) {
                userlist_t *u = l->data;
                list_t lp;

                xfree(u->first_name);
                xfree(u->last_name);
                xfree(u->nickname);
                xfree(u->uid);
                xfree(u->mobile);
                xfree(u->status);
                xfree(u->descr);
                xfree(u->authtype);
                xfree(u->foreign);
                xfree(u->last_status);
                xfree(u->last_descr);

                for (lp = u->groups; lp; lp = lp->next) {
                        struct ekg_group *g = lp->data;

                        xfree(g->name);
                }
                list_destroy(u->groups, 1);
		userlist_resource_free(u);
        }

        list_destroy(*userlist, 1);
        *userlist = NULL;
}

ekg_resource_t *userlist_resource_add(userlist_t *u, const char *name, int prio) {
	ekg_resource_t *r;

	if (!u) return NULL;

	r	= xmalloc(sizeof(ekg_resource_t));
	r->name		= xstrdup(name);		/* resource name */
	r->prio		= prio;				/* resource prio */
	r->status	= xstrdup(EKG_STATUS_NA);	/* this is quite stupid but we must be legal with ekg2 ABI */

	list_add_sorted(&(u->resources), r, 0, userlist_resource_compare);	/* add to list sorted by prio && than by name */
	return r;
}

ekg_resource_t *userlist_resource_find(userlist_t *u, const char *name) {
	list_t l;
	if (!u) return NULL;

	for (l = u->resources; l; l = l->next) {
		ekg_resource_t *r = l->data;

		if (!xstrcmp(r->name, name))
			return r;
	}
	return NULL;
}

void userlist_resource_remove(userlist_t *u, ekg_resource_t *r) {
	if (!u || !r) return;
	
	xfree(r->name);
	xfree(r->descr);
	xfree(r->status);

	list_remove(&(u->resources), r, 1);
}

void userlist_resource_free(userlist_t *u) {
	list_t l;
	if (!u) return;

	for (l = u->resources; l; l = l->next) {
		ekg_resource_t *r = l->data;

		xfree(r->name);
		xfree(r->status);
		xfree(r->descr);
	}
	list_destroy(u->resources, 1);
	u->resources = NULL;
}

/*
 * userlist_add()
 *
 * dodaje u�ytkownika do listy.
 *
 *  - uin,
 *  - display.
 */
userlist_t *userlist_add(session_t *session, const char *uid, const char *nickname)
{
	if (!session)
		return NULL;

	return userlist_add_u(&(session->userlist), uid, nickname);
}

/*
 * userlist_add_u()
 *
 * adds user to window's userlist
 * uid - uid,
 * nickname - display.
 */
userlist_t *userlist_add_u(list_t *userlist, const char *uid, const char *nickname)
{
        userlist_t *u = xmalloc(sizeof(userlist_t));

        u->uid = xstrdup(uid);
        u->nickname = xstrdup(nickname);
        u->status = xstrdup(EKG_STATUS_NA);
#if 0 /* if 0 != NULL */
        u->first_name = NULL;
        u->last_name = NULL;
        u->mobile = NULL;
        u->descr = NULL;
        u->authtype = NULL;
        u->foreign = NULL;
        u->last_status = NULL;
        u->last_descr = NULL;
        u->resources = NULL;
#endif
        return list_add_sorted(userlist, u, 0, userlist_compare);
}

/*
 * userlist_remove()
 *
 * usuwa danego u�ytkownika z listy kontakt�w.
 *
 *  - u.
 */
int userlist_remove(session_t *session, userlist_t *u)
{
	return userlist_remove_u(&(session->userlist), u);
}

/*
 * userlist_remove_u()
 *
 * removes given user from window's userlist
 *
 *  - u.
 */
int userlist_remove_u(list_t *userlist, userlist_t *u)
{
        list_t l;

        if (!u)
                return -1;

        xfree(u->first_name);
        xfree(u->last_name);
        xfree(u->nickname);
        xfree(u->uid);
        xfree(u->mobile);
        xfree(u->status);
        xfree(u->descr);
        xfree(u->authtype);
        xfree(u->foreign);
        xfree(u->last_status);
        xfree(u->last_descr);

        for (l = u->groups; l; l = l->next) {
                struct ekg_group *g = l->data;

                xfree(g->name);
        }
        list_destroy(u->groups, 1);
	userlist_resource_free(u);

        list_remove(userlist, u, 1);

        return 0;
}

/*
 * userlist_replace()
 *
 * usuwa i dodaje na nowo u�ytkownika, �eby zosta� umieszczony na odpowiednim
 * (pod wzgl�dem kolejno�ci alfabetycznej) miejscu. g�upie to troch�, ale
 * przy listach jednokierunkowych nie za bardzo jest sens komplikowa� spraw�
 * z przesuwaniem element�w listy.
 * 
 *  - u.
 *
 * 0/-1
 */
int userlist_replace(session_t *session, userlist_t *u)
{
	if (!u)
		return -1;
	if (list_remove(&(session->userlist), u, 0))
		return -1;
	if (!list_add_sorted(&(session->userlist), u, 0, userlist_compare))
		return -1;

	return 0;
}

/*
 * userlist_find()
 *
 * znajduje odpowiedni� struktur� `userlist' odpowiadaj�c� danemu
 * identyfikatorowi lub jego opisowi.
 *
 *  - uid,
 */
userlist_t *userlist_find(session_t *session, const char *uid)
{
	if (!uid || !session)
		return NULL;

        return userlist_find_u(&(session->userlist), uid);
}

/* 
 * userlist_find_u()
 *
 * finds and returns pointer to userlist_t which includes given
 * uid
 */
userlist_t *userlist_find_u(list_t *userlist, const char *uid)
{
	list_t l;

	if (!uid || !userlist)
		return NULL;

	for (l = *userlist; l; l = l->next) {
		userlist_t *u = l->data;
		const char *tmp;
		int len;

		if (!xstrcasecmp(u->uid, uid))
			return u;

		if (u->nickname && !xstrcasecmp(u->nickname, uid))
			return u;

		/* por�wnujemy resource; if (len > 0) */

		if (!(tmp = xstrchr(uid, '/')) || xstrncmp(uid, "jid:", 4))
			continue;

		len = (int)(tmp - uid);

		if (len > 0 && !xstrncasecmp(uid, u->uid, len))
			return u;

	}

	return NULL;
}

int userlist_set(session_t *session, const char *contacts)
{
	char **entries;
	int i;

	if (!session)
		return -1;

	entries = array_make(contacts, "\r\n", 0, 1, 0);

	userlist_free(session);

	for (i = 0; entries[i]; i++)
		userlist_add_entry(session, entries[i]);

	array_free(entries);

	return 0;
}

/*
 * valid_nick()
 *
 * sprawdza, czy nick nie zawiera znak�w specjalnych,
 * kt�re mog�yby powodowa� problemy.
 *
 * zwraca 1 je�li nick jest w porz�dku, w przeciwnym razie 0.
 */
int valid_nick(const char *nick)
{
	int i;
	const char *wrong[] = { "(null)", "__debug", "__status",
				 "__current", "__contacts", "*", "$", NULL };

	if (!nick)
		return 0;

	for (i = 0; wrong[i]; i++) {
		if (!xstrcmp(nick, wrong[i]))
			return 0;
	}

	if (nick[0] == '@' || nick[0] == '#' || xstrchr(nick, ','))
		return 0;

	return 1;
}

/*
 * valid_uid()
 *
 * sprawdza, czy uid jest obs�ugiwany przez jaki� plugin i czy jest
 * poprawny.
 *
 * zwraca 1 je�li nick jest w porz�dku, w przeciwnym razie 0.
 */
int valid_uid(const char *uid)
{
	int valid = 0;
	char *tmp;
	tmp = xstrdup(uid);

	query_emit_id(NULL, PROTOCOL_VALIDATE_UID, &tmp, &valid);
	xfree(tmp);

	return (valid > 0);
}


/*
 * valid_plugin_uid()
 *
 * sprawdza, czy uid jest obs�ugiwany przez podany plugin i czy jest
 * poprawny.
 *
 * zwraca 1 je�li nick jest w porz�dku, w przeciwnym razie 0.
 * natoamiast zwraca -1 gdy pogadany plugin jest pusty 
 */

int valid_plugin_uid(plugin_t *plugin, const char *uid)
{
        int valid = 0;
        char *tmp;

	if (!plugin)
		return -1;

        tmp = xstrdup(uid);

        query_emit_id(plugin, PROTOCOL_VALIDATE_UID, &tmp, &valid);
        xfree(tmp);

        return (valid > 0);

}

/*
 * get_uid()
 *
 * je�li podany tekst jest uid (ale nie jednocze�nie nazw� u�ytkownika),
 * zwraca jego warto��. je�li jest nazw� u�ytkownika w naszej li�cie kontakt�w,
 * zwraca jego uid. je�li tekstem jestem znak ,,$'', zwraca uid aktualnego
 * rozm�wcy. inaczej zwraca NULL.
 *
 *  - text.
 */
char *get_uid(session_t *session, const char *text)
{
	userlist_t *u;

	if (text && !xstrcmp(text, "$"))
		return window_current->target;
	
	u = userlist_find(session, text);

	if (u && u->uid)
		return u->uid;

	if (valid_uid(text))
		return (char *)text;

	return NULL;
}

/* 
 * get_nickname()
 *
 * if given text is nickname it returns the same, if it is 
 * an uid it returns its nickname (if exists), if there is 
 * no nickname it returns uid, else if contacts doesnt exist
 * it returns text if it is a correct uid, else NULL
 */
char *get_nickname(session_t *session, const char *text)
{
        userlist_t *u;

        u = userlist_find(session, text);

        if (u && u->nickname)
                return u->nickname;

	if (u && u->uid)
		return u->uid;

	if (valid_uid(text))
	        return (char *)text;

        return NULL;
}

/*
 * format_user()
 *
 * zwraca �adny (ew. kolorowy) tekst opisuj�cy dany numerek. je�li jest
 * w naszej li�cie kontakt�w, formatuje u�ywaj�c `known_user', w przeciwnym
 * wypadku u�ywa `unknown_user'. wynik jest w statycznym buforze.
 *
 *  - uin - numerek danej osoby.
 */
const char *format_user(session_t *session, const char *uid)
{
	userlist_t *u = userlist_find(session, uid);
	static char buf[256], *tmp;
/* 	
	if (uid && xstrchr(uid, ':'))
		uid = xstrchr(uid, ':') + 1;
 */

	if (!u || !u->nickname)
		tmp = format_string(format_find("unknown_user"), uid, uid);
	else
		tmp = format_string(format_find("known_user"), u->nickname, uid);
	
	strlcpy(buf, tmp, sizeof(buf));
	
	xfree(tmp);

	return buf;
}

/*
 * ignored_remove()
 *
 * usuwa z listy ignorowanych numerk�w.
 *
 *  - uin.
 */
int ignored_remove(session_t *session, const char *uid)
{
	userlist_t *u = userlist_find(session, uid);
	char *tmps, *tmp;
	list_t l;
	int level, tmp2 = 0;

	if (!u)
		return -1;

	if (!(level = ignored_check(session,uid)))
		return -1;

	for (l = u->groups; l; ) {
		struct ekg_group *g = l->data;

		l = l->next;

		if (xstrncasecmp(g->name, "__ignored", 9))
			continue;

		xfree(g->name);
		list_remove(&u->groups, g, 1);
	}

	if (!u->nickname && !u->groups) {
		userlist_remove(session, u);
		return 0;
	}

	tmps	= xstrdup(session->uid);
	tmp	= xstrdup(u->uid);
	query_emit_id(NULL, PROTOCOL_IGNORE, &tmps, &tmp, &level, &tmp2);
	xfree(tmps);
	xfree(tmp);

	if ((level & IGNORE_STATUS || level & IGNORE_STATUS_DESCR)) {
		query_emit_id(NULL, PROTOCOL_UNIGNORE, &u, &session);
	}

	return 0;
}

/*
 * ignored_add()
 *
 * dopisuje do listy ignorowanych numerk�w.
 *
 *  - uin.
 *  - level.
 */
int ignored_add(session_t *session, const char *uid, int level)
{
	userlist_t *u;
	char *tmps, *tmp;
	int oldlevel = 0;

	if (ignored_check(session, uid))
		return -1;
	
	if (!(u = userlist_find(session, uid)))
		u = userlist_add(session, uid, NULL);

	tmp = saprintf("__ignored_%d", level);
	ekg_group_add(u, tmp);
	xfree(tmp);

	if (level & IGNORE_STATUS) {
		xfree(u->status);
		u->status = xstrdup(EKG_STATUS_NA);
	}

	if (level & IGNORE_STATUS_DESCR) {
		xfree(u->descr);
		u->descr = NULL;
	}

	tmps	= xstrdup(session->uid);
	tmp	= xstrdup(u->uid);
	query_emit_id(NULL, PROTOCOL_IGNORE, &tmps, &tmp, &oldlevel, &level);
	xfree(tmps);
	xfree(tmp);
	
	return 0;
}

/**
 * ignored_check()
 *
 * czy dany numerek znajduje si� na li�cie ignorowanych.
 *
 * @param session - sesja w ktorej mamy szukac uzytkownika
 * @param uid - uid uzytkownika
 *
 */
int ignored_check(session_t *session, const char *uid)
{
	userlist_t *u = userlist_find(session, uid);
	list_t l;

	if (!u)
		return 0;

	for (l = u->groups; l; l = l->next) {
		struct ekg_group *g = l->data;

		if (!xstrcasecmp(g->name, "__ignored"))
			return IGNORE_ALL;

		if (!xstrncasecmp(g->name, "__ignored_", 10))
			return atoi(g->name + 10);
	}

	return 0;
}

/**
 * ignore_flags()
 *
 * zamienia �a�cuch znak�w na odpowiedni
 * poziom ignorowania w postaci liczby.
 *
 * @param str
 * @sa ignore_format
 * @sa ignore_t
 * @sa ignore_labels
 *
 * @return zwraca bitmaske opisana przez str
 */
int ignore_flags(const char *str)
{
	int x, y, ret = 0;
	char **arr;

	if (!str)
		return ret;

	arr = array_make(str, "|,:", 0, 1, 0);

	for (x = 0; arr[x]; x++) {
		if (!xstrcmp(arr[x], "*")) {
			ret = IGNORE_ALL;
			break;
		}

		for (y = 0; ignore_labels[y].name; y++)
			if (!xstrcasecmp(arr[x], ignore_labels[y].name))
				ret |= ignore_labels[y].level;
	}

	array_free(arr);

	return ret;
}

/**
 * ignore_format()
 *
 * zwraca statyczny �a�cuch znak�w reprezentuj�cy
 * dany poziom ignorowania.
 *
 * @param level - poziom ignorowania, bitmaska z `enum ignore_t'
 * @sa ignore_flags
 * @sa ignore_t
 * @sa ignore_labels
 *
 * @return zwraca <b>statyczny</b> bufor opisujacy bitmaske za pomoca `ignore_labels`
 */
const char *ignore_format(int level)
{
	static char buf[200];
	int i, comma = 0;

	buf[0] = 0;

	if (level == IGNORE_ALL)
		return "*";

	for (i = 0; ignore_labels[i].name; i++) {
		if (level & ignore_labels[i].level) {
			if (comma++)
				strlcat(buf, ",", sizeof(buf));

			strlcat(buf, ignore_labels[i].name, sizeof(buf));
		}
	}

	return buf;
}

/**
 * group_compare()
 *
 * wewnetrzna funkcja pomocna przy list_add_sorted().
 *
 * @param data1 - pierwszy wpis do porownania
 * @param data2 - drugi wpis do porownania
 * @sa list_add_sorted
 *
 * @return zwraca wynik xstrcasecmp() na nazwach grup.
 */
static int group_compare(void *data1, void *data2)
{
	struct ekg_group *a = data1, *b = data2;
	
	if (!a || !a->name || !b || !b->name)
		return 0;

	return xstrcasecmp(a->name, b->name);
}

/**
 * ekg_group_add()
 *
 * dodaje u�ytkownika do podanej grupy.
 *
 * @param u - wpis usera,
 * @param group - nazwa grupy.
 *
 * @return -1 jesli juz user jest w tej grupie, lub zle parametry. 0 gdy dodano.
 */
int ekg_group_add(userlist_t *u, const char *group)
{
	struct ekg_group *g;
	list_t l;

	if (!u || !group)
		return -1;

	for (l = u->groups; l; l = l->next) {
		g = l->data;

		if (!xstrcasecmp(g->name, group))
			return -1;
	}
	g = xmalloc(sizeof(struct ekg_group));
	g->name = xstrdup(group);

	list_add_sorted(&u->groups, g, 0, group_compare);

	return 0;
}

/**
 * ekg_group_remove()
 *
 * usuwa u�ytkownika z podanej grupy.
 *
 * @param u - wpis usera,
 * @param group - nazwa grupy.
 *
 * @return 0 je�li si� uda�o, inaczej -1.
 */
int ekg_group_remove(userlist_t *u, const char *group)
{
	list_t l;

	if (!u || !group)
		return -1;
	
	for (l = u->groups; l; l = l->next) {
		struct ekg_group *g = l->data;

		if (!xstrcasecmp(g->name, group)) {
			xfree(g->name);
			list_remove(&u->groups, g, 1);
			
			return 0;
		}
	}
	
	return -1;
}

/**
 * ekg_group_member()
 *
 * sprawdza czy u�ytkownik jest cz�onkiem danej grupy.
 *
 * @param u - uzytkownik, ktorego chcemy sprawdzic
 * @param group - grupa ktora chcemy sprawdzic
 *
 * @return 1 je�li tak, 0 je�li nie.
 */
int ekg_group_member(userlist_t *u, const char *group)
{
	list_t l;

	if (!u || !group)
		return 0;

	for (l = u->groups; l; l = l->next) {
		struct ekg_group *g = l->data;

		if (!xstrcasecmp(g->name, group))
			return 1;
	}

	return 0;
}

/**
 * group_init()
 *
 * inicjuje list� grup u�ytkownika na podstawie danego ci�gu znak�w,
 * w kt�rym kolejne nazwy grup s� rozdzielone przecinkiem.
 * 
 *  @param names - nazwy grup.
 *
 *  @return zwraca list� `struct group' je�li si� uda�o, inaczej NULL.
 */
list_t group_init(const char *names)
{
	list_t l = NULL;
	char **groups;
	int i;

	if (!names)
		return NULL;

	groups = array_make(names, ",", 0, 1, 0);

	for (i = 0; groups[i]; i++) {
		struct ekg_group *g = xmalloc(sizeof(struct ekg_group));

		g->name = groups[i];
		list_add_sorted(&l, g, 0, group_compare);
	}
	/* NOTE: we don't call here array_free() cause we use items of this
	 * 	array @ initing groups. We don't use strdup()
	 */
	xfree(groups);
	
	return l;
}

/**
 * group_to_string()
 *
 * zmienia list� grup na ci�g znak�w rodzielony przecinkami.
 *
 *  @param groups - lista grup.
 *  @param meta - czy do��czy� ,,meta-grupy''?
 *  @param sep - czy oddziela� przecinkiem _i_ spacj�?
 *
 *  @return zwraca zaalokowany ci�g znak�w lub NULL w przypadku b��du.
 */
char *group_to_string(list_t groups, int meta, int sep)
{
	string_t foo = string_init(NULL);
	list_t l;
	int comma = 0;

	for (l = groups; l; l = l->next) {
		struct ekg_group *g = l->data;

		if (!meta && !xstrncmp(g->name, "__", 2)) {
			comma = 0;
			continue;
		}

		if (comma)
			string_append(foo, (sep) ? ", " : ",");

		comma = 1;

		string_append(foo, g->name);
	}

	return string_free(foo, 0);
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */

