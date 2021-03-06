/*-
 * Copyright 2019 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "lua_common.h"
#include "libcryptobox/cryptobox.h"
#include "unix-std.h"

/***
 * @module rspamd_text
 * This module provides access to opaque text structures used widely to prevent
 * copying between Lua and C for various concerns: performance, security etc...
 *
 * You can convert rspamd_text into string but it will copy data.
 */

/**
 * @function rspamd_text.fromstring(str)
 * Creates rspamd_text from Lua string (copied to the text)
 * @param {string} str string to use
 * @return {rspamd_text} resulting text
 */
LUA_FUNCTION_DEF (text, fromstring);
/**
 * @function rspamd_text.fromtable(tbl[, delim])
 * Same as `table.concat` but generates rspamd_text instead of the Lua string
 * @param {table} tbl table to use
 * @param {string} delim optional delimiter
 * @return {rspamd_text} resulting text
 */
LUA_FUNCTION_DEF (text, fromtable);
/***
 * @method rspamd_text:len()
 * Returns length of a string
 * @return {number} length of string in **bytes**
 */
LUA_FUNCTION_DEF (text, len);
/***
 * @method rspamd_text:str()
 * Converts text to string by copying its content
 * @return {string} copy of text as Lua string
 */
LUA_FUNCTION_DEF (text, str);
/***
 * @method rspamd_text:ptr()
 * Converts text to lightuserdata
 * @return {lightuserdata} pointer value of rspamd_text
 */
LUA_FUNCTION_DEF (text, ptr);
/***
 * @method rspamd_text:save_in_file(fname[, mode])
 * Saves text in file
 * @return {boolean} true if save has been completed
 */
LUA_FUNCTION_DEF (text, save_in_file);
/***
 * @method rspamd_text:span(start[, len])
 * Returns a span for lua_text starting at pos [start] (1 indexed) and with
 * length `len` (or to the end of the text)
 * @param {integer} start start index
 * @param {integer} len length of span
 * @return {rspamd_text} new rspamd_text with span (must be careful when using with owned texts...)
 */
LUA_FUNCTION_DEF (text, span);
/***
 * @method rspamd_text:lines([stringify])
 * Returns an iter over all lines as rspamd_text objects or as strings if `stringify` is true
 * @param {boolean} stringify stringify lines
 * @return {iterator} iterator triplet
 */
LUA_FUNCTION_DEF (text, lines);
/***
 * @method rspamd_text:split(regexp, [stringify])
 * Returns an iter over all encounters of the specific regexp as rspamd_text objects or as strings if `stringify` is true
 * @param {rspamd_regexp} regexp regexp (pcre syntax) used for splitting
 * @param {boolean} stringify stringify lines
 * @return {iterator} iterator triplet
 */
LUA_FUNCTION_DEF (text, split);
/***
 * @method rspamd_text:at(pos)
 * Returns a byte at the position `pos`
 * @param {integer} pos index
 * @return {integer} byte at the position `pos` or nil if pos out of bound
 */
LUA_FUNCTION_DEF (text, at);
/***
 * @method rspamd_text:bytes()
 * Converts text to an array of bytes
 * @return {table|integer} bytes in the array (as unsigned char)
 */
LUA_FUNCTION_DEF (text, bytes);
LUA_FUNCTION_DEF (text, take_ownership);
LUA_FUNCTION_DEF (text, gc);
LUA_FUNCTION_DEF (text, eq);

static const struct luaL_reg textlib_f[] = {
		LUA_INTERFACE_DEF (text, fromstring),
		LUA_INTERFACE_DEF (text, fromtable),
		{NULL, NULL}
};

static const struct luaL_reg textlib_m[] = {
		LUA_INTERFACE_DEF (text, len),
		LUA_INTERFACE_DEF (text, str),
		LUA_INTERFACE_DEF (text, ptr),
		LUA_INTERFACE_DEF (text, take_ownership),
		LUA_INTERFACE_DEF (text, save_in_file),
		LUA_INTERFACE_DEF (text, span),
		LUA_INTERFACE_DEF (text, lines),
		LUA_INTERFACE_DEF (text, split),
		LUA_INTERFACE_DEF (text, at),
		LUA_INTERFACE_DEF (text, bytes),
		{"write", lua_text_save_in_file},
		{"__len", lua_text_len},
		{"__tostring", lua_text_str},
		{"__gc", lua_text_gc},
		{"__eq", lua_text_eq},
		{NULL, NULL}
};

struct rspamd_lua_text *
lua_check_text (lua_State * L, gint pos)
{
	void *ud = rspamd_lua_check_udata (L, pos, "rspamd{text}");
	luaL_argcheck (L, ud != NULL, pos, "'text' expected");
	return ud ? (struct rspamd_lua_text *)ud : NULL;
}

struct rspamd_lua_text *
lua_new_text (lua_State *L, const gchar *start, gsize len, gboolean own)
{
	struct rspamd_lua_text *t;

	t = lua_newuserdata (L, sizeof (*t));
	t->flags = 0;

	if (own) {
		gchar *storage;

		if (len > 0) {
			storage = g_malloc (len);
			memcpy (storage, start, len);
			t->start = storage;
			t->flags = RSPAMD_TEXT_FLAG_OWN;
		}
		else {
			t->start = "";
		}
	}
	else {
		t->start = start;
	}

	t->len = len;
	rspamd_lua_setclass (L, "rspamd{text}", -1);

	return t;
}


static gint
lua_text_fromstring (lua_State *L)
{
	LUA_TRACE_POINT;
	const gchar *str;
	gsize l = 0;
	gboolean transparent = FALSE;

	str = luaL_checklstring (L, 1, &l);

	if (str) {
		if (lua_isboolean (L, 2)) {
			transparent = lua_toboolean (L, 2);
		}

		lua_new_text (L, str, l, !transparent);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}


	return 1;
}

static gint
lua_text_fromtable (lua_State *L)
{
	LUA_TRACE_POINT;
	const gchar *delim = "", *st;
	struct rspamd_lua_text *t, *elt;
	gsize textlen = 0, dlen, stlen, tblen;
	gchar *dest;

	if (!lua_istable (L, 1)) {
		return luaL_error (L, "invalid arguments");
	}

	if (lua_type (L, 2) == LUA_TSTRING) {
		delim = lua_tolstring (L, 2, &dlen);
	}
	else {
		dlen = 0;
	}

	/* Calculate length needed */
	tblen = rspamd_lua_table_size (L, 1);

	for (guint i = 0; i < tblen; i ++) {
		lua_rawgeti (L, 1, i + 1);

		if (lua_type (L, -1) == LUA_TSTRING) {
#if LUA_VERSION_NUM >= 502
			stlen = lua_rawlen (L, -1);
#else
			stlen = lua_objlen (L, -1);
#endif
			textlen += stlen;
		}
		else {
			elt = lua_check_text (L, -1);

			if (elt) {
				textlen += elt->len;
			}
		}

		if (i != tblen - 1) {
			textlen += dlen;
		}

		lua_pop (L, 1);
	}

	/* Allocate new text */
	t = lua_newuserdata (L, sizeof (*t));
	dest = g_malloc (textlen);
	t->start = dest;
	t->len = textlen;
	t->flags = RSPAMD_TEXT_FLAG_OWN;
	rspamd_lua_setclass (L, "rspamd{text}", -1);

	for (guint i = 0; i < tblen; i ++) {
		lua_rawgeti (L, 1, i + 1);

		if (lua_type (L, -1) == LUA_TSTRING) {
			st = lua_tolstring (L, -1, &stlen);
			memcpy (dest, st, stlen);
			dest += stlen;
		}
		else {
			elt = lua_check_text (L, -1);

			if (elt) {
				memcpy (dest, elt->start, elt->len);
				dest += elt->len;
			}
		}

		if (dlen && i != tblen - 1) {
			memcpy (dest, delim, dlen);
			dest += dlen;
		}

		lua_pop (L, 1);
	}

	return 1;
}

static gint
lua_text_len (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	gsize l = 0;

	if (t != NULL) {
		l = t->len;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushinteger (L, l);

	return 1;
}

static gint
lua_text_str (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t != NULL) {
		lua_pushlstring (L, t->start, t->len);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_ptr (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t != NULL) {
		lua_pushlightuserdata (L, (gpointer)t->start);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_take_ownership (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	gchar *dest;

	if (t != NULL) {
		if (t->flags & RSPAMD_TEXT_FLAG_OWN) {
			/* We already own it */
			lua_pushboolean (L, true);
		}
		else {
			dest = g_malloc (t->len);
			memcpy (dest, t->start, t->len);
			t->start = dest;
			t->flags |= RSPAMD_TEXT_FLAG_OWN;
			lua_pushboolean (L, true);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_span (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	gint64 start = lua_tointeger (L, 2), len = -1;

	if (t && start >= 1 && start <= t->len) {
		if (lua_isnumber (L, 3)) {
			len = lua_tonumber (L, 3);
		}

		if (len == -1) {
			len = t->len - (start - 1);
		}
		else if (len > (t->len - (start - 1))) {
			return luaL_error (L, "invalid length");
		}

		lua_new_text (L, t->start + (start - 1), len, FALSE);
	}
	else {
		if (!t) {
			return luaL_error (L, "invalid arguments, text required");
		}
		else {
			return luaL_error (L, "invalid arguments: start offset %d "
						 "is larger than text len %d", (int)start, (int)t->len);
		}
	}

	return 1;
}

static gint64
rspamd_lua_text_push_line (lua_State *L,
						   struct rspamd_lua_text *t,
						   gint64 start_offset,
						   const gchar *sep_pos,
						   gboolean stringify)
{
	const gchar *start;
	gsize len;
	gint64 ret;

	start = t->start + start_offset;
	len = sep_pos ? (sep_pos - start) : (t->len - start_offset);
	ret = start_offset + len;

	/* Trim line */
	while (len > 0) {
		if (start[len - 1] == '\r' || start[len - 1] == '\n') {
			len --;
		}
		else {
			break;
		}
	}

	if (stringify) {
		lua_pushlstring (L, start, len);
	}
	else {
		struct rspamd_lua_text *ntext;

		ntext = lua_newuserdata (L, sizeof (*ntext));
		ntext->start = start;
		ntext->len = len;
		ntext->flags = 0; /* Not own as it must be owned by a top object */
	}

	return ret;
}

static gint
rspamd_lua_text_readline (lua_State *L)
{
	struct rspamd_lua_text *t = lua_touserdata (L, lua_upvalueindex (1));
	gboolean stringify = lua_toboolean (L, lua_upvalueindex (2));
	gint64 pos = lua_tointeger (L, lua_upvalueindex (3));

	if (pos < 0) {
		return luaL_error (L, "invalid pos: %d", (gint)pos);
	}

	if (pos >= t->len) {
		/* We are done */
		return 0;
	}

	const gchar *sep_pos;

	/* We look just for `\n` ignoring `\r` as it is very rare nowadays */
	sep_pos = memchr (t->start + pos, '\n', t->len - pos);

	if (sep_pos == NULL) {
		/* Either last `\n` or `\r` separated text */
		sep_pos = memchr (t->start + pos, '\r', t->len - pos);
	}

	pos = rspamd_lua_text_push_line (L, t, pos, sep_pos, stringify);

	/* Skip separators */
	while (pos < t->len) {
		if (t->start[pos] == '\n' || t->start[pos] == '\r') {
			pos ++;
		}
		else {
			break;
		}
	}

	/* Update pos */
	lua_pushinteger (L, pos);
	lua_replace (L, lua_upvalueindex (3));

	return 1;
}

static gint
lua_text_lines (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	gboolean stringify = FALSE;

	if (t) {
		if (lua_isboolean (L, 2)) {
			stringify = lua_toboolean (L, 2);
		}

		lua_pushvalue (L, 1);
		lua_pushboolean (L, stringify);
		lua_pushinteger (L, 0); /* Current pos */
		lua_pushcclosure (L, rspamd_lua_text_readline, 3);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
rspamd_lua_text_regexp_split (lua_State *L) {
	struct rspamd_lua_text *t = lua_touserdata (L, lua_upvalueindex (1)),
			*new_t;
	struct rspamd_lua_regexp *re = *(struct rspamd_lua_regexp **)
			lua_touserdata (L, lua_upvalueindex (2));
	gboolean stringify = lua_toboolean (L, lua_upvalueindex (3));
	gint64 pos = lua_tointeger (L, lua_upvalueindex (4));
	gboolean matched;

	if (pos < 0) {
		return luaL_error (L, "invalid pos: %d", (gint) pos);
	}

	if (pos >= t->len) {
		/* We are done */
		return 0;
	}

	const gchar *start, *end, *old_start;

	end = t->start + pos;

	for (;;) {
		old_start = end;

		matched = rspamd_regexp_search (re->re, t->start, t->len, &start, &end, FALSE,
				NULL);

		if (matched) {
			if (start - old_start > 0) {
				if (stringify) {
					lua_pushlstring (L, old_start, start - old_start);
				}
				else {
					new_t = lua_newuserdata (L, sizeof (*t));
					rspamd_lua_setclass (L, "rspamd{text}", -1);
					new_t->start = old_start;
					new_t->len = start - old_start;
					new_t->flags = 0;
				}

				break;
			}
			else {
				if (start == end) {
					matched = FALSE;
					break;
				}
				/*
				 * All match separators (e.g. starting separator,
				 * we need to skip it). Continue iterations.
				 */
			}
		}
		else {
			/* No match, stop */
			break;
		}
	}

	if (!matched && (t->len > 0 && (end == NULL || end < t->start + t->len))) {
		/* No more matches, but we might need to push the last element */
		if (end == NULL) {
			end = t->start;
		}
		/* No separators, need to push the whole remaining part */
		if (stringify) {
			lua_pushlstring (L, end, (t->start + t->len) - end);
		}
		else {
			new_t = lua_newuserdata (L, sizeof (*t));
			rspamd_lua_setclass (L, "rspamd{text}", -1);
			new_t->start = end;
			new_t->len = (t->start + t->len) - end;
			new_t->flags = 0;
		}

		pos = t->len;
	}
	else {

		pos = end - t->start;
	}

	/* Update pos */
	lua_pushinteger (L, pos);
	lua_replace (L, lua_upvalueindex (4));

	return 1;
}

static gint
lua_text_split (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	struct rspamd_lua_regexp *re;
	gboolean stringify = FALSE, own_re = FALSE;

	if (lua_type (L, 2) == LUA_TUSERDATA) {
		re = lua_check_regexp (L, 2);
	}
	else {
		rspamd_regexp_t *c_re;
		GError *err = NULL;

		c_re = rspamd_regexp_new (lua_tostring (L, 2), NULL, &err);
		if (c_re == NULL) {

			gint ret = luaL_error (L, "cannot parse regexp: %s, error: %s",
					lua_tostring (L, 2),
					err == NULL ? "undefined" : err->message);
			if (err) {
				g_error_free (err);
			}

			return ret;
		}

		re = g_malloc0 (sizeof (struct rspamd_lua_regexp));
		re->re = c_re;
		re->re_pattern = g_strdup (lua_tostring (L, 2));
		re->module = rspamd_lua_get_module_name (L);
		own_re = TRUE;
	}

	if (t && re) {
		if (lua_isboolean (L, 3)) {
			stringify = lua_toboolean (L, 3);
		}

		/* Upvalues */
		lua_pushvalue (L, 1); /* text */

		if (own_re) {
			struct rspamd_lua_regexp **pre;
			pre = lua_newuserdata (L, sizeof (struct rspamd_lua_regexp *));
			rspamd_lua_setclass (L, "rspamd{regexp}", -1);
			*pre = re;
		}
		else {
			lua_pushvalue (L, 2); /* regexp */
		}

		lua_pushboolean (L, stringify);
		lua_pushinteger (L, 0); /* Current pos */
		lua_pushcclosure (L, rspamd_lua_text_regexp_split, 4);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}


static gint
lua_text_at (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	gint pos = lua_tointeger (L, 2);

	if (t) {
		if (pos > 0 && pos <= t->len) {
			lua_pushinteger (L, t->start[pos - 1]);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_bytes (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t) {
		lua_createtable (L, t->len, 0);

		for (gsize i = 0; i < t->len; i ++) {
			lua_pushinteger (L, (guchar)t->start[i]);
			lua_rawseti (L, -2, i + 1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_save_in_file (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	const gchar *fname = NULL;
	guint mode = 00644;
	gint fd = -1;
	gboolean need_close = FALSE;

	if (t != NULL) {
		if (lua_type (L, 2) == LUA_TSTRING) {
			fname = luaL_checkstring (L, 2);

			if (lua_type (L, 3) == LUA_TNUMBER) {
				mode = lua_tonumber (L, 3);
			}
		}
		else if (lua_type (L, 2) == LUA_TNUMBER) {
			/* Created fd */
			fd = lua_tonumber (L, 2);
		}

		if (fd == -1) {
			if (fname) {
				fd = rspamd_file_xopen (fname, O_CREAT | O_WRONLY | O_EXCL, mode, 0);

				if (fd == -1) {
					lua_pushboolean (L, false);
					lua_pushstring (L, strerror (errno));

					return 2;
				}
				need_close = TRUE;
			}
			else {
				fd = STDOUT_FILENO;
			}
		}

		if (write (fd, t->start, t->len) == -1) {
			if (fd != STDOUT_FILENO) {
				close (fd);
			}

			lua_pushboolean (L, false);
			lua_pushstring (L, strerror (errno));

			return 2;
		}

		if (need_close) {
			close (fd);
		}

		lua_pushboolean (L, true);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_gc (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t != NULL) {
		if (t->flags & RSPAMD_TEXT_FLAG_OWN) {
			if (t->flags & RSPAMD_TEXT_FLAG_WIPE) {
				rspamd_explicit_memzero ((guchar *)t->start, t->len);
			}

			if (t->flags & RSPAMD_TEXT_FLAG_MMAPED) {
				munmap ((gpointer)t->start, t->len);
			}
			else {
				if (t->flags & RSPAMD_TEXT_FLAG_SYSMALLOC) {
					free ((gpointer) t->start);
				}
				else {
					g_free ((gpointer) t->start);
				}
			}
		}

	}

	return 0;
}

static gint
lua_text_eq (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t1 = lua_check_text (L, 1),
			*t2 = lua_check_text (L, 2);

	if (t1->len == t2->len) {
		lua_pushboolean (L, memcmp (t1->start, t2->start, t1->len) == 0);
	}
	else {
		lua_pushboolean (L, false);
	}

	return 1;
}

static gint
lua_text_wipe (lua_State *L)
{
	LUA_TRACE_POINT;
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t != NULL) {
		if (t->flags & RSPAMD_TEXT_FLAG_OWN) {
			rspamd_explicit_memzero ((guchar *)t->start, t->len);
		}
		else {
			return luaL_error (L, "cannot wipe not owned text");
		}

	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_load_text (lua_State * L)
{
	lua_newtable (L);
	luaL_register (L, NULL, textlib_f);

	return 1;
}

void
luaopen_text (lua_State *L)
{
	rspamd_lua_new_class (L, "rspamd{text}", textlib_m);
	lua_pop (L, 1);

	rspamd_lua_add_preload (L, "rspamd_text", lua_load_text);
}
