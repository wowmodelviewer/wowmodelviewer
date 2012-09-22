/*
 *  mpq.h -- some default types and defines.
 *
 *  Copyright (C) 2003 Maik Broemme <mbroemme@plusserver.de>
 *
 *  This source was adepted from the C++ version of StormLib.h and
 *  StormPort.h included in stormlib. The C++ version belongs to
 *  the following authors,
 *
 *  Ladislav Zezula <ladik.zezula.net>
 *  Marko Friedemann <marko.friedemann@bmx-chemnitz.de>
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
 *  $Id: mpq.h,v 1.1 2005/04/09 22:09:18 ufoz Exp $
 */

#ifndef _MPQ_H
#define _MPQ_H

#ifdef _WINDOWS
    #include <io.h>

    #define snprintf _snprintf
    //inline int open(const char* x, int y)
    //{
    //	return _open(x, y);
    //}
    //#define lseek _lseek
    #define PATH_MAX 259
#else
    #ifndef _LARGEFILE64_SOURCE
    #define _LARGEFILE64_SOURCE
    #endif
    #include <dirent.h>
    //lseek ...
    #include <unistd.h>
    //PATH_MAX
    //#include <linux/limits.h>
    // size_t
    #include <stdlib.h>
    #include <sys/types.h>
    #include <stdint.h>
    #define _lseeki64 lseek
    typedef int32_t __int32;
    #define O_BINARY 0
    //#define lseek _lseek
	#ifndef PATH_MAX
		#define PATH_MAX 259
	#endif
#endif


#ifdef __cplusplus
extern "C" {
#endif

#define LIBMPQ_MAJOR_VERSION		0		/* Major version number... maybe sometimes we reach version 1 :) */
#define LIBMPQ_MINOR_VERSION		3		/* Minor version number - increased only for small changes */
#define LIBMPQ_PATCH_VERSION		2		/* Patchlevel - changed on bugfixes etc... */

#define LIBMPQ_TOOLS_SUCCESS		0		/* return value for all functions which success */
#define LIBMPQ_TOOLS_BUFSIZE		0x500	/* buffer size for the decryption engine */

#define LIBMPQ_EFILE				-1		/* error on file operation */
#define LIBMPQ_EFILE_FORMAT			-2		/* bad file format */
#define LIBMPQ_EFILE_CORRUPT		-3		/* file corrupt */
#define LIBMPQ_EFILE_NOT_FOUND		-4		/* file in archive not found */
#define LIBMPQ_EFILE_READ			-5		/* Read error in archive */
#define LIBMPQ_EALLOCMEM			-6		/* maybe not enough memory? :) */
#define LIBMPQ_EFREEMEM				-7		/* can not free memory */
#define LIBMPQ_EINV_RANGE			-8		/* Given filenumber is out of range */
#define LIBMPQ_EHASHTABLE			-9		/* error in reading hashtable */
#define LIBMPQ_EBLOCKTABLE			-10		/* error in reading blocktable */

#define LIBMPQ_FLAG_CHANGED			0x00000001	/*  */
#define LIBMPQ_FLAG_PROTECTED		0x00000002	/* Set on protected MPQs (like W3M maps) */
#define LIBMPQ_FLAG_FIXSEED			0x00020000	/*  */
#define LIBMPQ_FLAG_SINGLEUNIT		0x01000000	/*  */
#define LIBMPQ_FLAG_UNKNOWN_0200	0x02000000	/* The file is only 1 byte long and its name is a hash */
#define LIBMPQ_FLAG_UNKNOWN_0400	0x04000000	/* Appears in WoW 1.10 or newer, dunno what does it mean */


#define LIBMPQ_ID_MPQ				0x1A51504D	/* MPQ archive header ID ('MPQ\x1A') */
#define LIBMPQ_HEADER_W3M			0x6D9E4B86	/* special value used by W3M Map Protector */
#define LIBMPQ_HASH_ENTRY_DELETED	0xFFFFFFFE	/* Block index for deleted hash entry */
#define LIBMPQ_HASH_ENTRY_EMPTY		0xFFFFFFFF	/* Block index for empty hash entry */
#define LIBMPQ_LISTFILE_HASH1		0xfd657910 /* Hashes of files that are in any mpq */
#define LIBMPQ_LISTFILE_HASH2		0x4e9b98a7
#define LIBMPQ_ATTRFILE_HASH1		0xd38437cb
#define LIBMPQ_ATTRFILE_HASH2		0x07dfeaec

#define LIBMPQ_FILE_COMPRESS_PKWARE	0x00000100	/* Compression made by PKWARE Data Compression Library */
#define LIBMPQ_FILE_COMPRESS_MULTI	0x00000200	/* Multiple compressions */
#define LIBMPQ_FILE_COMPRESSED		0x0000FF00	/* File is compressed */
#define LIBMPQ_FILE_ENCRYPTED		0x00010000	/* Indicates whether file is encrypted */
#define	LIBMPQ_FILE_FIXSEED			0x00020000	/* The decryption key for the file is altered according to the position of the file in the archive */
#define LIBMPQ_FILE_SINGLE_UNIT		0x01000000	/* Instead of being divided to 0x1000-bytes blocks, the file is stored as single unit */
#define	LIBMPQ_FILE_DUMMY_FILE		0x02000000	/* The file has length of 0 or 1 byte and its name is a hash */
#define LIBMPQ_FILE_METADATA		0x04000000	/* Indicates the file has associted metadata, has extra data appended after regular data. Must be a compressed file */
#define LIBMPQ_FILE_EXISTS			0x80000000	/* Set if file exists, reset when the file was deleted */

#define LIBMPQ_FILE_COMPRESSED_SIZE	1		/* MPQ compressed filesize of given file */
#define LIBMPQ_FILE_UNCOMPRESSED_SIZE	2		/* MPQ uncompressed filesize of given file */
#define LIBMPQ_FILE_COMPRESSION_TYPE	3		/* MPQ compression type of given file */
#define LIBMPQ_FILE_TYPE_INT		4		/* file is given by number */
#define LIBMPQ_FILE_TYPE_CHAR		5		/* file is given by name */
#define LIBMPQ_FILE_HASH1			6		/* hash value 1 */
#define LIBMPQ_FILE_HASH2			7		/* hash value 2 */

#define LIBMPQ_MPQ_ARCHIVE_SIZE		1		/* MPQ archive size */
#define LIBMPQ_MPQ_HASHTABLE_SIZE	2		/* MPQ archive hashtable size */
#define LIBMPQ_MPQ_BLOCKTABLE_SIZE	3		/* MPQ archive blocktable size */
#define LIBMPQ_MPQ_BLOCKSIZE		4		/* MPQ archive blocksize */
#define LIBMPQ_MPQ_NUMFILES		5		/* Number of files in the MPQ archive */
#define LIBMPQ_MPQ_COMPRESSED_SIZE	6		/* Compressed archive size */
#define LIBMPQ_MPQ_UNCOMPRESSED_SIZE	7		/* Uncompressed archive size */

#define LIBMPQ_HUFF_DECOMPRESS		0		/* Defines that we want to decompress using huffman trees. */

#define LIBMPQ_CONF_EFILE_OPEN		-1		/* error if a specific listfile was forced and could not be opened. */
#define LIBMPQ_CONF_EFILE_CORRUPT	-2		/* listfile seems to be corrupt */
#define LIBMPQ_CONF_EFILE_LIST_CORRUPT	-3		/* listfile seems correct, but filelist is broken */
#define LIBMPQ_CONF_EFILE_NOT_FOUND	-4		/* error if no matching listfile found */
#define LIBMPQ_CONF_EFILE_VERSION	-5		/* libmpq version does not match required listfile version */

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#ifndef min
#define min(a, b) ((a < b) ? a : b)
#endif

typedef unsigned int	mpq_buffer[LIBMPQ_TOOLS_BUFSIZE];
typedef int		(*DECOMPRESS)(char *, int *, char *, int);
typedef struct {
	unsigned long	mask;		/* Decompression bit */
	DECOMPRESS	decompress;	/* Decompression function */
} decompress_table;

#pragma pack(push,1)
/* MPQ file header */
typedef struct {
	unsigned long	id;		/* The 0x1A51504D ('MPQ\x1A') signature */
	unsigned long	offset;		/* Offset of the first file (Relative to MPQ start) */
	unsigned long	archivesize;	/* Size of MPQ archive */
	unsigned short	offsetsc;	/* 0000 for SC and BW */
	unsigned short	blocksize;	/* Size of file block is (0x200 << blockSize) */
	unsigned long	hashtablepos;	/* File position of hashTable */
	unsigned long	blocktablepos;	/* File position of blockTable. Each entry has 16 bytes */
	unsigned long	hashtablesize;	/* Number of entries in hash table */
	unsigned long	blocktablesize;	/* Number of entries in the block table */
} mpq_header;// __attribute__ ((packed)) mpq_header;
#pragma pack(pop)

/* Hash entry. All files in the archive are searched by their hashes. */
typedef struct {
	unsigned int	name1;		/* The first two unsigned ints */
	unsigned int	name2;		/* are the encrypted file name */
	unsigned int	locale;		/* Locale information. */
	unsigned int	blockindex;	/* Index to file description block */
} mpq_hash;

/* File description block contains informations about the file */
typedef struct {
	unsigned int	filepos;	/* Block file starting position in the archive */
	unsigned int	csize;		/* Compressed file size */
	unsigned int	fsize;		/* Uncompressed file size */
	unsigned int	flags;		/* Flags */
} mpq_block;

/* File handle structure used since Diablo 1.00 (0x38 bytes) */
typedef struct {
	unsigned char	filename[PATH_MAX];	/* filename of the actual file in the archive */
	int		fd;		/* File handle */
	unsigned int	seed;		/* Seed used for file decrypt */
	unsigned int	filepos;	/* Current file position */
	unsigned int	offset;
	unsigned int	nblocks;	/* Number of blocks in the file (incl. the last noncomplete one) */
	unsigned int	*blockpos;	/* Position of each file block (only for compressed files) */
	int		blockposloaded;	/* TRUE if block positions loaded */
	unsigned int	offset2;	/* (Number of bytes somewhere ?) */
	mpq_hash	*mpq_h;		/* Hash table entry */
	mpq_block	*mpq_b;		/* File block pointer */

	/* Non-Storm.dll members */

	unsigned int	accessed;	/* Was something from the file already read? */
} mpq_file;

/* Archive handle structure used since Diablo 1.00 */
typedef struct {
	char	filename[PATH_MAX];	/* Opened archive file name */
	int		fd;		/* File handle */
	unsigned int	blockpos;	/* Position of loaded block in the file */
	unsigned int	blocksize;	/* Size of file block */
	unsigned char	*blockbuf;	/* Buffer (cache) for file block */
	unsigned int	bufpos;		/* Position in block buffer */
	unsigned int	mpqpos;		/* MPQ archive position in the file */
	unsigned int	filepos;	/* Current file pointer */
	unsigned int	openfiles;	/* Number of open files + 1 */
	mpq_buffer	buf;			/* MPQ buffer */
	mpq_header	*header;		/* MPQ file header */
	mpq_hash	*hashtable;		/* Hash table */
	mpq_block	*blocktable;	/* Block table */

	/* Non-Storm.dll members */

	unsigned int	flags;		/* See LIBMPQ_TOOLS_FLAG_XXXXX */
	unsigned int	maxblockindex;	/* The highest block table entry */
} mpq_archive;

char *libmpq_version();
int libmpq_archive_open(mpq_archive *mpq_a, unsigned char *mpq_filename);
int libmpq_archive_close(mpq_archive *mpq_a);
int libmpq_archive_info(mpq_archive *mpq_a, unsigned int infotype);
//int libmpq_file_extract(mpq_archive *mpq_a, const int number);
// *dest must have enough space
int libmpq_file_getdata(mpq_archive *mpq_a, const int number, unsigned char *dest);
int libmpq_file_info(mpq_archive *mpq_a, unsigned int infotype, const int number);
int libmpq_file_number(mpq_archive *mpq_a, const char *name);
int libmpq_file_number_from_hash(mpq_archive *mpq_a, unsigned int hash1, unsigned int hash2);
int libmpq_file_check(mpq_archive *mpq_a, void *file, int type);

int libmpq_pkzip_decompress(char *out_buf, int *out_length, char *in_buf, int in_length);
int libmpq_zlib_decompress(char *out_buf, int *out_length, char *in_buf, int in_length);
int libmpq_huff_decompress(char *out_buf, int *out_length, char *in_buf, int in_length);
int libmpq_bzip2_decompress(char *out_buf, int *out_length, char *in_buf, int in_length);
int libmpq_wave_decompress_stereo(char *out_buf, int *out_length, char *in_buf, int in_length);
int libmpq_wave_decompress_mono(char *out_buf, int *out_length, char *in_buf, int in_length);
int libmpq_multi_decompress(char *out_buf, int *pout_length, char *in_buf, int in_length);

#ifdef __cplusplus
};  // extern "C"
#endif



#endif					/* _MPQ_H */
