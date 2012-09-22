/*
 *  mpq.c -- functions for developers using libmpq.
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
 *  $Id: mpq.cpp,v 1.1 2005/04/09 22:09:18 ufoz Exp $
 */

#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "mpq.h"
#include "common.h"

/*
 *  This function returns version information.
 *  format: MAJOR.MINOR.PATCH
 */
char *libmpq_version() {
	static char version[10];
	snprintf(version, sizeof(version), "%i.%i.%i", LIBMPQ_MAJOR_VERSION, LIBMPQ_MINOR_VERSION, LIBMPQ_PATCH_VERSION);
	return version;
}

/*
 *  This function reads a file and verify if it is a legit MPQ archive
 *  or not. Then it fills the mpq_header structure and reads the hash
 *  table.
 */
int libmpq_archive_open(mpq_archive *mpq_a, unsigned char *mpq_filename) {
	int fd = 0;
	int rb = 0;
	int ncnt = FALSE;
	struct stat fileinfo;

	memset((void*)mpq_a, 0, sizeof(mpq_archive));

	/* allocate memory */
	mpq_a->header = static_cast<mpq_header*>(malloc(sizeof(mpq_header)));
	memset(mpq_a->header, 0, sizeof(mpq_header));

	/* Check if file exists and is readable */
	fd = open((const char*)mpq_filename, O_RDONLY|O_BINARY);
	if (fd == LIBMPQ_EFILE) {
		return LIBMPQ_EFILE;
	}

	/* fill the structures with informations */
	strncpy((char*)mpq_a->filename, (const char*)mpq_filename, strlen((const char*)mpq_filename));
	libmpq_init_buffer(mpq_a);
	mpq_a->fd               = fd;
	mpq_a->header->id       = 0;
	mpq_a->maxblockindex    = 0;
	mpq_a->header->offset	= 0;

	while (!ncnt) {
		mpq_a->header->id = 0;

		//lseek(mpq_a->fd, mpq_a->mpqpos, SEEK_SET);
		// Fixed support for large / 64bit addresses
		_lseeki64(mpq_a->fd, mpq_a->mpqpos, SEEK_SET);

		rb = read(mpq_a->fd, mpq_a->header, sizeof(mpq_header));

		/* if different number of bytes read, break the loop */
		if (rb != sizeof(mpq_header)) {
			return LIBMPQ_EFILE_FORMAT;
		}

		/* special offset for protected MPQs */
		if (mpq_a->header->offset == LIBMPQ_HEADER_W3M) {
			mpq_a->flags |= LIBMPQ_FLAG_PROTECTED;
			mpq_a->header->offset = sizeof(mpq_header);
		}

		/* if valid signature has been found, break the loop */
		if (mpq_a->header->id == LIBMPQ_ID_MPQ && mpq_a->header->hashtablepos < mpq_a->header->archivesize && mpq_a->header->blocktablepos < mpq_a->header->archivesize) { // && mpq_a->header->offset == sizeof(mpq_header)
			ncnt = TRUE;
		}

		/* move to the next possible offset */
		if (!ncnt) {
			mpq_a->mpqpos += 0x200;
		}
	}

	if (mpq_a->header->offset != sizeof(mpq_header)) {
		//lseek(mpq_a->fd, mpq_a->header->offset, SEEK_SET);
		// Fixed support for large / 64bit addresses
		_lseeki64(mpq_a->fd, mpq_a->header->offset, SEEK_SET);
	}

	/* get the right positions of the hash table and the block table. */
	mpq_a->blocksize = (0x200 << mpq_a->header->blocksize);
	fstat(mpq_a->fd, &fileinfo);

	/* Normal MPQs must have position of */
	if ((mpq_a->header->hashtablepos + mpq_a->mpqpos < (unsigned int)fileinfo.st_size) && (mpq_a->header->blocktablepos + mpq_a->mpqpos < (unsigned int)fileinfo.st_size)) {
		mpq_a->header->hashtablepos  += mpq_a->mpqpos;
		mpq_a->header->blocktablepos += mpq_a->mpqpos;
	} else {
		return LIBMPQ_EFILE_FORMAT;
	}

	/* Try to read and decrypt the hashtable */
	if (libmpq_read_hashtable(mpq_a) != 0) {
		return LIBMPQ_EHASHTABLE;
	}

	/* Try to read and decrypt the blocktable */
	if (libmpq_read_blocktable(mpq_a) != 0) {
		return LIBMPQ_EBLOCKTABLE;
	}

	return LIBMPQ_TOOLS_SUCCESS;
}

/*
 *  This function closes the file descriptor opened by
 *  mpq_open_archive(); and frees the decryption buffer.
 */
int libmpq_archive_close(mpq_archive *mpq_a) {
	/* free the allocated memory. */

	if (mpq_a->blockbuf) {
		free(mpq_a->blockbuf);
		mpq_a->blockbuf = NULL;
	}

	if (mpq_a->header) {
		free(mpq_a->header);
		mpq_a->header = NULL;
	}

	if (mpq_a->hashtable) {
		free(mpq_a->hashtable);	/* Hash table */
		mpq_a->hashtable = NULL;
	}

	if (mpq_a->blocktable) {
		free(mpq_a->blocktable);	/* Block table */
		mpq_a->blocktable = NULL;
	}

	/* Check if file descriptor is valid. */
	if (mpq_a->fd>0 && (close(mpq_a->fd)) == LIBMPQ_EFILE) {
		return LIBMPQ_EFILE;
	}

	/* // this isn't really needed
	memset(mpq_a->buf, 0, sizeof(mpq_a->buf));
	memset(mpq_a->filename, 0, sizeof(mpq_a->filename));
	*/

	free(mpq_a);
	mpq_a = NULL;

	return LIBMPQ_TOOLS_SUCCESS;
}

/*
 * This function returns the value for the given infotype.
 * If an error occurs something < 0 is returned.
 */
int libmpq_archive_info(mpq_archive *mpq_a, unsigned int infotype) {
	unsigned int filecount = 0;
	unsigned int fsize = 0;
	unsigned int csize = 0;
	mpq_block *mpq_b_end = mpq_a->blocktable + mpq_a->header->blocktablesize;
	mpq_block *mpq_b = NULL;

	switch (infotype) {
		case LIBMPQ_MPQ_ARCHIVE_SIZE:
			return mpq_a->header->archivesize;
		case LIBMPQ_MPQ_HASHTABLE_SIZE:
			return mpq_a->header->hashtablesize;
		case LIBMPQ_MPQ_BLOCKTABLE_SIZE:
			return mpq_a->header->blocktablesize;
		case LIBMPQ_MPQ_BLOCKSIZE:
			return mpq_a->blocksize;
		case LIBMPQ_MPQ_NUMFILES:
			for (mpq_b = mpq_a->blocktable; mpq_b < mpq_b_end; mpq_b++) {
				filecount++;
			}
			return filecount;
		case LIBMPQ_MPQ_COMPRESSED_SIZE:
			for (mpq_b = mpq_a->blocktable; mpq_b < mpq_b_end; mpq_b++) {
				csize += mpq_b->csize;
			}
			return csize;
		case LIBMPQ_MPQ_UNCOMPRESSED_SIZE:
			for (mpq_b = mpq_a->blocktable; mpq_b < mpq_b_end; mpq_b++) {
				fsize += mpq_b->fsize;
			}
			return fsize;
		default:
			return LIBMPQ_TOOLS_SUCCESS;
	}
}

/*
 * This function returns some useful file information.
 */
int libmpq_file_info(mpq_archive *mpq_a, unsigned int infotype, const int number) {
	int blockindex = -1;
	unsigned int i = 0;
	mpq_block *mpq_b = NULL;
	mpq_hash *mpq_h = NULL;

	/* check if given number is not out of range */
	if (number < 1 || (unsigned int)number > mpq_a->header->blocktablesize) {
		return LIBMPQ_EINV_RANGE;
	}

	/* search for correct hashtable */
	for (i = 0; i < mpq_a->header->hashtablesize; i++) {
		if ((number - 1) == (int)(mpq_a->hashtable[i]).blockindex) {
			blockindex = (mpq_a->hashtable[i]).blockindex;
			mpq_h = &(mpq_a->hashtable[i]);
			break;
		}
	}

	/* check if file was found */
	if (blockindex == -1 || blockindex > (int)mpq_a->header->blocktablesize) {
		return LIBMPQ_EFILE_NOT_FOUND;
	}

	/* check if sizes are correct */
	mpq_b = mpq_a->blocktable + blockindex;
	if (mpq_b->filepos > (mpq_a->header->archivesize + mpq_a->mpqpos) || mpq_b->csize > mpq_a->header->archivesize) {
		return LIBMPQ_EFILE_CORRUPT;
	}

	/* check if file exists */
	if ((mpq_b->flags & LIBMPQ_FILE_EXISTS) == 0) {
		return LIBMPQ_EFILE_NOT_FOUND;
	}
	switch (infotype) {
		case LIBMPQ_FILE_COMPRESSED_SIZE:
			return mpq_b->csize;
		case LIBMPQ_FILE_UNCOMPRESSED_SIZE:
			return mpq_b->fsize;
		case LIBMPQ_FILE_HASH1:
			return mpq_h->name1;
		case LIBMPQ_FILE_HASH2:
			return mpq_h->name2;
		case LIBMPQ_FILE_COMPRESSION_TYPE:
			if (mpq_b->flags & LIBMPQ_FILE_COMPRESS_PKWARE) {
				return LIBMPQ_FILE_COMPRESS_PKWARE;
			}
			if (mpq_b->flags & LIBMPQ_FILE_COMPRESS_MULTI) {
				return LIBMPQ_FILE_COMPRESS_MULTI;
			}
		default:
			return LIBMPQ_TOOLS_SUCCESS;
	}
}


/*
 *  This function returns the number to the given
 *  filename.
 */
int libmpq_file_number(mpq_archive *mpq_a, const char *name) {
	unsigned int i;
	unsigned int hash1, hash2, hashstart;

	hashstart = libmpq_hash_string(mpq_a, 0, (unsigned char *)name) % mpq_a->header->hashtablesize;
	hash1 = libmpq_hash_string(mpq_a, 1, (unsigned char *)name);
	hash2 = libmpq_hash_string(mpq_a, 2, (unsigned char *)name);

	for (i=hashstart; i<mpq_a->header->hashtablesize; i++) {
		if ((mpq_a->hashtable[i]).name1 == hash1 &&
			(mpq_a->hashtable[i]).name2 == hash2) {
			return (mpq_a->hashtable[i]).blockindex + 1;
		} else if ((mpq_a->hashtable[i]).blockindex == LIBMPQ_HASH_ENTRY_EMPTY)
			return LIBMPQ_EFILE_NOT_FOUND;
	}

	/* if no matching entry found return LIBMPQ_EFILE_NOT_FOUND */
	return LIBMPQ_EFILE_NOT_FOUND;
}


/*
 *  This function verifies if a given file (by number
 *  or name) is in the opened mpq archive. On success
 *  it returns 0, otherwise LIBMPQ_EFILE_NOT_FOUND.
 */
int libmpq_file_check(mpq_archive *mpq_a, void *file, int type) {
	switch (type) {
		case LIBMPQ_FILE_TYPE_INT:

			/* check if we are in the range of available files. */
			if (*(int *)file > libmpq_archive_info(mpq_a, LIBMPQ_MPQ_NUMFILES) || *(int *)file < 1) {
				return LIBMPQ_EFILE_NOT_FOUND;
			} else {
				return LIBMPQ_TOOLS_SUCCESS;
			}
		case LIBMPQ_FILE_TYPE_CHAR:
			/* if a file was found return 0 */
			if (libmpq_file_number(mpq_a, (char *)file) != LIBMPQ_EFILE_NOT_FOUND) {
				return LIBMPQ_TOOLS_SUCCESS;
			} else {
				return LIBMPQ_EFILE_NOT_FOUND;
			}
		default:
			return LIBMPQ_TOOLS_SUCCESS;
	}
}

#if 0
/*
 *  This function extracts a file from a MPQ archive
 *  by the given number.
 */
int libmpq_file_extract(mpq_archive *mpq_a, const int number) {
	int blockindex = -1;
	int fd = 0;
	int i = 0;
	char buffer[0x1000];
	char tempfile[PATH_MAX];
	unsigned int transferred = 1;
	mpq_file *mpq_f = NULL;
	mpq_block *mpq_b = NULL;
	mpq_hash *mpq_h = NULL;

	if (number < 1 || number > mpq_a->header->blocktablesize) {
		return LIBMPQ_EINV_RANGE;
	}

	snprintf(tempfile, PATH_MAX, libmpq_file_name(mpq_a, number));

	/* check if mpq_f->filename could be written here. */
	//fd = open(tempfile, O_RDWR|O_CREAT|O_TRUNC, 0644);
	fd = CreateFile(tempfile, FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY);
	if (fd == LIBMPQ_EFILE) {
		return LIBMPQ_EFILE;
	}

	/* search for correct hashtable */
	for (i = 0; i < mpq_a->header->hashtablesize; i++) {
		if ((number - 1) == (mpq_a->hashtable[i]).blockindex) {
			blockindex = (mpq_a->hashtable[i]).blockindex;
			mpq_h = &(mpq_a->hashtable[i]);
			break;
		}
	}

	/* check if file was found */
	if (blockindex == -1 || blockindex > mpq_a->header->blocktablesize) {
		return LIBMPQ_EFILE_NOT_FOUND;
	}

	/* check if sizes are correct */
	mpq_b = mpq_a->blocktable + blockindex;
	if (mpq_b->filepos > (mpq_a->header->archivesize + mpq_a->mpqpos) || mpq_b->csize > mpq_a->header->archivesize) {
		return LIBMPQ_EFILE_CORRUPT;
	}

	/* check if file exists */
	if ((mpq_b->flags & LIBMPQ_FILE_EXISTS) == 0) {
		return LIBMPQ_EFILE_NOT_FOUND;
	}

	/* allocate memory for file structure */
	mpq_f = (mpq_file *)malloc(sizeof(mpq_file));
	if (!mpq_f) {
		return LIBMPQ_EALLOCMEM;
	}

	/* initialize file structure */
	memset(mpq_f, 0, sizeof(mpq_file));
	mpq_f->fd             = fd;
	mpq_f->mpq_b          = mpq_b;
	mpq_f->nblocks        = (mpq_f->mpq_b->fsize + mpq_a->blocksize - 1) / mpq_a->blocksize;
	mpq_f->mpq_h          = mpq_h;
	mpq_f->accessed       = FALSE;
	mpq_f->blockposloaded = FALSE;
	snprintf((char*)mpq_f->filename, PATH_MAX, tempfile);

	/* allocate buffers for decompression. */
	if (mpq_f->mpq_b->flags & LIBMPQ_FILE_COMPRESSED) {

		/*
		 *  Allocate buffer for block positions. At the begin of file are stored
		 *  unsigned ints holding positions of each block relative from begin of
		 *  file in the archive.
		 */
		if ((mpq_f->blockpos = (unsigned int*)malloc(sizeof(int) * mpq_f->nblocks + 1)) == NULL) {
			return LIBMPQ_EALLOCMEM;
		}
	}

	while (transferred > 0) {
		transferred = libmpq_file_read_file(mpq_a, mpq_f, mpq_f->filepos, buffer, sizeof(buffer));
		if (transferred == 0) {
			break;
		} else {
			mpq_f->accessed  = TRUE;
			mpq_f->filepos  += transferred;
		}

		transferred = write(mpq_f->fd, buffer, transferred);
		if (transferred == 0) {
			break;
		}
	}

	close(fd);

	/* freeing the file structure */
	free(mpq_f);
	return LIBMPQ_TOOLS_SUCCESS;
}
#endif


int libmpq_file_getdata(mpq_archive *mpq_a, const int file_number, unsigned char *dest) {
	int blockindex = -1;
	unsigned int i=0;
	mpq_file *mpq_f = NULL;
	mpq_block *mpq_b = NULL;
	mpq_hash *mpq_h = NULL;
    int success = 0;

	if (file_number < 1 || (unsigned int)file_number > mpq_a->header->blocktablesize) {
		return LIBMPQ_EINV_RANGE;
	}

	/* search for correct hashtable */
	for (i=0; i<mpq_a->header->hashtablesize; i++) {
		if ((file_number - 1) == (mpq_a->hashtable[i]).blockindex) {
			blockindex = (mpq_a->hashtable[i]).blockindex;
			mpq_h = &(mpq_a->hashtable[i]);
			break;
		}
	}

	/* check if file was found */
	if (blockindex == -1 || (unsigned int)blockindex > mpq_a->header->blocktablesize) {
		return LIBMPQ_EFILE_NOT_FOUND;
	}

	/* check if sizes are correct */
	mpq_b = mpq_a->blocktable + blockindex;
	if (mpq_b->filepos > (mpq_a->header->archivesize + mpq_a->mpqpos) || mpq_b->csize > mpq_a->header->archivesize) {
		return LIBMPQ_EFILE_CORRUPT;
	}

	/* check if file exists */
	if ((mpq_b->flags & LIBMPQ_FILE_EXISTS) == 0) {
		return LIBMPQ_EFILE_NOT_FOUND;
	}

	/* allocate memory for file structure */
	mpq_f = (mpq_file*)malloc(sizeof(mpq_file));
	if (!mpq_f) {
		return LIBMPQ_EALLOCMEM;
	}

	/* initialize file structure */
	memset(mpq_f, 0, sizeof(mpq_file));
	mpq_f->mpq_b          = mpq_b;
	if ((mpq_f->mpq_b->flags & LIBMPQ_FILE_SINGLE_UNIT) != 0)
		mpq_f->nblocks        = 1;
	else
		mpq_f->nblocks        = (mpq_f->mpq_b->fsize + mpq_a->blocksize - 1) / mpq_a->blocksize;
	mpq_f->mpq_h          = mpq_h;
	mpq_f->accessed       = FALSE;
	mpq_f->blockposloaded = FALSE;

	/* allocate buffers for decompression. */
	if (mpq_f->mpq_b->flags & LIBMPQ_FILE_COMPRESSED) {

		/*
		 *  Allocate buffer for block positions. At the begin of file are stored
		 *  unsigned ints holding positions of each block relative from begin of
		 *  file in the archive.
		 */
		if ((mpq_f->blockpos = (unsigned int*)malloc(sizeof(int) * (mpq_f->nblocks + 1))) == NULL) {
			free(mpq_f);
			return LIBMPQ_EALLOCMEM;
		}
	}

    if(libmpq_file_read_file(mpq_a, mpq_f, 0, (char*)dest, mpq_b->fsize) == mpq_b->fsize)
        success = 1;

    if (mpq_f->mpq_b->flags & LIBMPQ_FILE_COMPRESSED) {
        // Free buffer for block positions
		
	       free(mpq_f->blockpos);
	}
	/* freeing the file structure */
	free(mpq_f);
	return success?LIBMPQ_TOOLS_SUCCESS:LIBMPQ_EFILE_CORRUPT;
}
