/*
 *  common.h -- defines and structs used by the config files.
 *
 *  Copyright (C) 2003 Maik Broemme <mbroemme@plusserver.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id: common.h,v 1.1 2005/04/09 22:09:18 ufoz Exp $
 */

#define LIBMPQ_CONF_FL_INCREMENT	512			/* i hope we did not need more :) */
#define LIBMPQ_CONF_EXT			".conf"			/* listdb file seems to be valid with this extension */
#define LIBMPQ_CONF_HEADER		"LIBMPQ_VERSION"	/* listdb file must include this entry to be valid */
#define LIBMPQ_CONF_BUFSIZE		4096			/* maximum number of bytes a line in the file could contain */

#define LIBMPQ_CONF_TYPE_CHAR		1			/* value in config file is from type char */
#define LIBMPQ_CONF_TYPE_INT		2			/* value in config file is from type int */

#define LIBMPQ_CONF_EOPEN_DIR		-1			/* error on open directory */
#define LIBMPQ_CONF_EVALUE_NOT_FOUND	-2			/* value for the option was not found */

extern int libmpq_init_buffer(mpq_archive *mpq_a);
extern int libmpq_read_hashtable(mpq_archive *mpq_a);
extern int libmpq_read_blocktable(mpq_archive *mpq_a);
extern int libmpq_file_read_block(mpq_archive *mpq_a, mpq_file *mpq_f, unsigned int blockpos, char *buffer, unsigned int blockbytes);
extern int libmpq_file_read_file(mpq_archive *mpq_a, mpq_file *mpq_f, unsigned int filepos, char *buffer, unsigned int toread);
extern unsigned int libmpq_hash_string(mpq_archive *mpq_a, unsigned int type, const unsigned char *pbKey);
