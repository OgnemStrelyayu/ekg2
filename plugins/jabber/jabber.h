/* $Id$ */

#ifndef __EKG_JABBER_JABBER_H
#define __EKG_JABBER_JABBER_H

#include <ekg2-config.h>

#include <ekg/plugins.h>
#include <ekg/sessions.h>

#include <expat.h>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif

struct xmlnode_s {
	char *name;
	char *data;
	char **atts;

	struct xmlnode_s *parent;
	struct xmlnode_s *children;
	
	struct xmlnode_s *next;
	struct xmlnode_s *prev;
};

typedef struct xmlnode_s xmlnode_t;

typedef struct {
	int fd;				/* deskryptor po��czenia */
#ifdef HAVE_GNUTLS
	gnutls_session ssl_session;	/* sesja ssla */
	gnutls_certificate_credentials xcred;
	char using_ssl;			/* czy polaczono uzywajac ssl */
#endif
	int id;				/* id zapyta� */
	XML_Parser parser;		/* instancja parsera expata */
	char *server;			/* nazwa serwera */
	int connecting;			/* czy si� w�a�nie ��czymy? */
	char *stream_id;		/* id strumienia */

	char *obuf;			/* bufor wyj�ciowy */
	int obuf_len;			/* rozmiar bufora wyj�ciowego */

	xmlnode_t *node;		/* aktualna ga��� xmla */
} jabber_private_t;

typedef struct {
	session_t *session;
} jabber_handler_data_t;

#define jabber_private(s) ((jabber_private_t*) session_private_get(s))

plugin_t jabber_plugin;

char *jabber_attr(char **atts, const char *att);
char *jabber_digest(const char *sid, const char *password);

void jabber_handle(void *data, xmlnode_t *n);

char *jabber_escape(const char *text);
char *jabber_unescape(const char *text);

void jabber_reconnect_handler(int type, void *data);

time_t jabber_try_xdelay(xmlnode_t *xmlnode, const char *ns);

#ifdef __GNU__
int jabber_write(jabber_private_t *j, const char *format, ...) __attribute__ ((format (printf, 2, 3)));
#else
int jabber_write(jabber_private_t *j, const char *format, ...);
#endif

void xmlnode_handle_start(void *data, const char *name, const char **atts);
void xmlnode_handle_end(void *data, const char *name);
void xmlnode_handle_cdata(void *data, const char *text, int len);
xmlnode_t *xmlnode_find_child(xmlnode_t *n, const char *name);

#endif /* __EKG_JABBER_JABBER_H */
