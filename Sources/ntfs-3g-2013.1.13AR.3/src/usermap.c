/*
 *               Windows to Linux user mapping for ntfs-3g
 *
 * 
 * Copyright (c) 2007-2008 Jean-Pierre Andre
 *
 *    A quick'n dirty program scanning owners of files in
 *      "c:\Documents and Settings" (and "c:\Users")
 *     and asking user to map them to Linux accounts
 *
 *          History
 *
 *  Sep 2007
 *     - first version, limited to Win32
 *
 *  Oct 2007
 *     - ported to Linux (rewritten would be more correct)
 *
 *  Nov 2007 Version 1.0.0
 *     - added more defaults
 *
 *  Nov 2007 Version 1.0.1
 *     - avoided examining files whose name begin with a '$'
 *
 *  Jan 2008 Version 1.0.2
 *     - moved user mapping file to directory .NTFS-3G (hidden for Linux)
 *     - fixed an error case in Windows version
 *
 *  Nov 2008 Version 1.1.0
 *     - fixed recursions for account in Linux version
 *     - searched owner in c:\Users (standard location for Vista)
 *
 *  May 2009 Version 1.1.1
 *     - reordered mapping records to limit usage of same SID for user and group
 *     - fixed decoding SIDs on 64-bit systems
 *     - fixed a pointer to dynamic data in mapping tables
 *     - fixed default mapping on Windows
 *     - fixed bug for renaming UserMapping on Windows
 *
 *  May 2009 Version 1.1.2
 *     - avoided selecting DOS names on Linux
 *
 *  Nov 2009 Version 1.1.3
 *     - shutdown compiler warnings for unused parameters
 *
 *  Jan 2010 Version 1.1.4
 *     - fixed compilation problems for Mac OSX (Erik Larsson)
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the NTFS-3G
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *		General parameters which may have to be adapted to needs
 */

#ifdef HAVE_CONFIG_H
#define USESTUBS 1 /* API stubs generated at link time */
#else
#define USESTUBS 0 /* direct calls to API, based on following definitions */
#define ENVNTFS3G "NTFS3G"
#define LIBFILE64 "/lib64/libntfs-3g.so.843"
#define LIBFILE "/lib/libntfs-3g.so.843"
#endif

#define GET_FILE_SECURITY "ntfs_get_file_security"
#define SET_FILE_SECURITY "ntfs_set_file_security"
#define READ_DIRECTORY "ntfs_read_directory"
#define INIT_FILE_SECURITY "ntfs_initialize_file_security"
#define LEAVE_FILE_SECURITY "ntfs_leave_file_security"

#define VERSION "1.1.4"
#define MAPDIR ".NTFS-3G"
#define MAPFILE "UserMapping"
#define MAXATTRSZ 2048
#define MAXSIDSZ 80
#define MAXNAMESZ 256
#define OWNERS1 "Documents and Settings"
#define OWNERS2 "Users"

/*
 *		Define WIN32 for a Windows execution
 *	may have to be adapted to compiler or something else
 */

#ifndef WIN32
#if defined(__WIN32) | defined(__WIN32__) | defined(WNSC)
#define WIN32 1
#endif
#endif

#ifdef WIN32
#define BANNER "Generated by usermap for Windows, v " VERSION
#else
#define BANNER "Generated by usermap for Linux, v " VERSION
#endif


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/*
 *	Define the security API according to platform
 */

#ifdef WIN32

#include <fcntl.h>
#include <windows.h>

#define STATIC

typedef enum { DENIED, AGREED } boolean;

#else

#include <unistd.h>
#include <dlfcn.h>

typedef enum { DENIED, AGREED } boolean, BOOL;
typedef unsigned int DWORD; /* must be 32 bits whatever the platform */
typedef DWORD *LPDWORD;

enum {  OWNER_SECURITY_INFORMATION = 1,
        GROUP_SECURITY_INFORMATION = 2,
        DACL_SECURITY_INFORMATION = 4,
        SACL_SECURITY_INFORMATION = 8
} ;

struct CALLBACK {
	const char *accname;
	const char *dir;
	int levels;
	int docset;
} ;

typedef int (*dircallback)(struct CALLBACK *context, char *ntfsname,
	int length, int type, long long pos, unsigned long long mft_ref,
	unsigned int dt_type);

#if USESTUBS

#define STATIC static

BOOL ntfs_get_file_security(void *scapi,
                const char *path, DWORD selection,
                char *buf, DWORD buflen, LPDWORD psize);
BOOL ntfs_set_file_security(void *scapi,
                const char *path, DWORD selection, const char *attr);
BOOL ntfs_read_directory(void *scapi,
		const char *path, dircallback callback, void *context);
void *ntfs_initialize_file_security(const char *device,
                                int flags);
BOOL ntfs_leave_file_security(void *scapi);

#else

#define STATIC

BOOL (*ntfs_get_file_security)(void *scapi,
                const char *path, DWORD selection,
                char *buf, DWORD buflen, LPDWORD psize);
BOOL (*ntfs_set_file_security)(void *scapi,
                const char *path, DWORD selection, const char *attr);
BOOL (*ntfs_read_directory)(void *scapi,
		const char *path, dircallback callback, void *context);
void *(*ntfs_initialize_file_security)(const char *device,
                                int flags);
BOOL (*ntfs_leave_file_security)(void *scapi);

#endif

STATIC boolean open_security_api(void);
STATIC boolean close_security_api(void);
STATIC boolean open_volume(const char *volume);
STATIC boolean close_volume(const char *volume);

#endif

struct MAPPING {
	struct MAPPING *next;
	const char *uidstr;
	const char *gidstr;
	const char *sidstr;
	const unsigned char *sid;
	const char *login;
	boolean defined;
};

struct MAPPING *firstmapping;
struct MAPPING *lastmapping;

#ifdef WIN32
char *currentwinname;
char *currentdomain;
unsigned char *currentsid;
#endif

#ifndef WIN32

void *ntfs_handle;
void *ntfs_context = (void*)NULL;

/*
 *		Shut down compiler warnings for unused parameters
 */

static long unused(const void *p)
{
return ((long)p);
}

/*
 *		Open and close the security API (platform dependent)
 */

STATIC boolean open_security_api(void)
{
#if USESTUBS
	return (AGREED);
#else
	char *error;
	boolean err;
	const char *libfile;

	err = AGREED;
	libfile = getenv(ENVNTFS3G);
	if (!libfile)
		libfile = (sizeof(char*) == 8 ? LIBFILE64 : LIBFILE);
	ntfs_handle = dlopen(libfile,RTLD_LAZY);
	if (ntfs_handle) {
		ntfs_initialize_file_security =
				dlsym(ntfs_handle,INIT_FILE_SECURITY);
		error = dlerror();
		if (error)
			fprintf(stderr," %s\n",error);
		else {
			ntfs_leave_file_security =
					dlsym(ntfs_handle,LEAVE_FILE_SECURITY);
			ntfs_get_file_security =
					dlsym(ntfs_handle,GET_FILE_SECURITY);
			ntfs_set_file_security =
					dlsym(ntfs_handle,SET_FILE_SECURITY);
			ntfs_read_directory =
					dlsym(ntfs_handle,READ_DIRECTORY);
			err = !ntfs_initialize_file_security
				|| !ntfs_leave_file_security
				|| !ntfs_get_file_security
				|| !ntfs_set_file_security
				|| !ntfs_read_directory;
			if (error)
				fprintf(stderr,"ntfs-3g API not available\n");
		}
	} else {
		fprintf(stderr,"Could not open ntfs-3g library\n");
		fprintf(stderr,"\nPlease set environment variable \"" ENVNTFS3G "\"\n");
		fprintf(stderr,"to appropriate path and retry\n");
	}
	return (!err);
#endif
}

STATIC boolean close_security_api(void)
{
#if USESTUBS
	return (0);
#else
	return (!dlclose(ntfs_handle));
#endif
}

/*
 *		Open and close a volume (platform dependent)
 *	assuming a single volume needs to be opened at any time
 */

STATIC boolean open_volume(const char *volume)
{
	boolean ok;

	ok = DENIED;
	if (!ntfs_context) {
		ntfs_context = ntfs_initialize_file_security(volume,0);
		if (ntfs_context) {
			fprintf(stderr,"\"%s\" opened\n",volume);
			ok = AGREED;
		} else {
			fprintf(stderr,"Could not open \"%s\"\n",volume);
			fprintf(stderr,"Make sure \"%s\" is not mounted\n",volume);
		}
	} else
		fprintf(stderr,"A volume is already open\n");
	return (ok);
}

STATIC boolean close_volume(const char *volume)
{
	boolean r;

	r = ntfs_leave_file_security(ntfs_context);
	if (r)
		fprintf(stderr,"\"%s\" closed\n",volume);
	else
		fprintf(stderr,"Could not close \"%s\"\n",volume);
	ntfs_context = (void*)NULL;
	return (r);
}

/*
 *		A poor man's conversion of Unicode to UTF8
 *	We are assuming outputs to terminal expect UTF8
 */

STATIC void to_utf8(char *dst, const char *src, unsigned int cnt)
{
	unsigned int ch;
	unsigned int i;

	for (i=0; i<cnt; i++) {
		ch = *src++ & 255;
		ch += (*src++ & 255) << 8;
		if (ch < 0x80)
			*dst++ = ch;
		else
			if (ch < 0x1000) {
				*dst++ = 0xc0 + (ch >> 6);
				*dst++ = 0x80 + (ch & 63);
			} else {
				*dst++ = 0xe0 + (ch >> 12);
				*dst++ = 0x80 + ((ch >> 6) & 63);
				*dst++ = 0x80 + (ch & 63);
			}
	}
	*dst = 0;
}

STATIC int utf8_size(const char *src, unsigned int cnt)
{
	unsigned int ch;
	unsigned int i;
	int size;

	size = 0;
	for (i=0; i<cnt; i++) {
		ch = *src++ & 255;
		ch += (*src++ & 255) << 8;
		if (ch < 0x80)
			size++;
		else
			if (ch < 0x1000)
				size += 2;
			else
				size += 3;
	}
	return (size);
}

#endif


STATIC void welcome(void)
{
	printf("\nThis tool will help you to build a mapping of Windows users\n");
	printf("to Linux users.\n");
	printf("Be prepared to give Linux user id (uid) and group id (gid)\n");
	printf("for owners of files which will be selected.\n");
}

STATIC unsigned int get2l(const unsigned char *attr, int p)
{
	int i;
	unsigned int v;

	v = 0;
	for (i = 0; i < 2; i++)
		v += (attr[p + i] & 255) << (8 * i);
	return (v);
}

STATIC unsigned long get4l(const unsigned char *attr, int p)
{
	int i;
	unsigned long v;

	v = 0;
	for (i = 0; i < 4; i++)
		v += (attr[p + i] & 255L) << (8 * i);
	return (v);
}

STATIC unsigned long long get6h(const unsigned char *attr, int p)
{
	int i;
	unsigned long long v;

	v = 0;
	for (i = 0; i < 6; i++)
		v = (v << 8) + (attr[p + i] & 255L);
	return (v);
}

STATIC char *decodesid(const unsigned char *sid)
{
	char *str;
	int i;
	unsigned long long auth;
	unsigned long subauth;

	str = (char *)malloc(MAXSIDSZ);
	if (str) {
		strcpy(str, "S");
		sprintf(&str[strlen(str)], "-%d", sid[0]);	/* revision */
		auth = get6h(sid, 2);
#ifdef WIN32
		sprintf(&str[strlen(str)], "-%I64u", auth);	/* main authority */
#else
		sprintf(&str[strlen(str)], "-%llu", auth);	/* main authority */
#endif
		for (i = 0; (i < 8) && (i < sid[1]); i++) {
			subauth = get4l(sid, 8 + 4 * i);
			sprintf(&str[strlen(str)], "-%lu", subauth);	/* sub-authority */
		}
	}
	return (str);
}

/*
 *        Test whether a generic group (S-1-5-21- ... -513)
 */

STATIC boolean isgenericgroup(const char *sid)
{
	boolean yes;

	yes = !strncmp(sid,"S-1-5-21-",9)
		&& !strcmp(strrchr(sid,'-'),"-513");
	return (yes);
}

STATIC unsigned char *makegroupsid(const unsigned char *sid)
{
	static unsigned char groupsid[MAXSIDSZ];
	int size;

	size = 8 + 4*sid[1];
	memcpy(groupsid, sid, size);
		/* replace last level by 513 */
	groupsid[size - 4] = 1;
	groupsid[size - 3] = 2;
	groupsid[size - 2] = 0;
	groupsid[size - 1] = 0;
	return (groupsid);
}

STATIC void domapping(const char *accname, const char *filename,
		const unsigned char *sid, int type)
{
	char buf[81];
	char *sidstr;
	char *idstr;
	int sidsz;
	boolean reject;
	struct MAPPING *mapping;
	char *login;
	char *p;

	if ((get6h(sid, 2) == 5) && (get4l(sid, 8) == 21)) {
		sidstr = decodesid(sid);
		mapping = firstmapping;
		while (mapping && strcmp(mapping->sidstr, sidstr))
			mapping = mapping->next;
		if (mapping
		    && (mapping->defined
			 || !accname
			 || !strcmp(mapping->login, accname)))
			free(sidstr);	/* decision already known */
		else {
			do {
				reject = DENIED;
				printf("\n");
				if (accname)
					printf("Under Windows login \"%s\"\n", accname);
				printf("   file \"%s\" has no mapped %s\n",
					       filename,(type ? "group" : "owner"));
				printf("By which Linux login should this file be owned ?\n");
				printf("Enter %s of login, or just press \"enter\" if this file\n",
					(type ? "gid" : "uid"));
				printf("does not belong to a user, or you do not known to whom\n");
				printf("\n");
				if (type)
					printf("Group : ");
				else
					printf("User : ");
				p = fgets(buf, 80, stdin);
				if (p && p[0] && (p[strlen(p) - 1] == '\n'))
					p[strlen(p) - 1] = '\0';

				if (p && p[0]
					 && ((p[0] == '0') || !strcmp(p, "root"))) {
					printf("Please do not map users to root\n");
					printf("Administrators will be mapped automatically\n");
					reject = AGREED;
				}
				if (reject)
					printf("Please retry\n");
			} while (reject);
			if (!mapping) {
				mapping =
				    (struct MAPPING *)
				    malloc(sizeof(struct MAPPING));
				mapping->next = (struct MAPPING *)NULL;
				mapping->defined = DENIED;
				if (lastmapping)
					lastmapping->next = mapping;
				else
					firstmapping = mapping;
				lastmapping = mapping;
			}
			if (mapping) {
				if (p && p[0]) {
					idstr = (char *)malloc(strlen(p) + 1);
					if (idstr) {
						strcpy(idstr, p);
						if (type) {
							mapping->uidstr = "";
							mapping->gidstr = idstr;
						} else {
							mapping->uidstr = idstr;
							mapping->gidstr = idstr;
						}
						mapping->defined = AGREED;
					}
				}
				mapping->sidstr = sidstr;
				if (accname) {
					login = (char*)malloc(strlen(accname) + 1);
					if (login)
						strcpy(login,accname);
					mapping->login = login;
				} else
					mapping->login = (char*)NULL;
				sidsz = 8 + sid[1]*4;
				p = (char*)malloc(sidsz);
				if (p) {
					memcpy(p, sid, sidsz);
				}
				mapping->sid = (unsigned char*)p;
			}
		}
	}
}

STATIC void listaclusers(const char *accname, const unsigned char *attr, int off)
{
	int i;
	int cnt;
	int x;

	cnt = get2l(attr, off + 4);
	x = 8;
	for (i = 0; i < cnt; i++) {
		domapping(accname, (char *)NULL, &attr[off + x + 8], 2);
		x += get2l(attr, off + x + 2);
	}
}

#ifdef WIN32

STATIC void account(const char *accname, const char *dir, const char *name, int type)
{
	unsigned char attr[MAXATTRSZ];
	unsigned long attrsz;
	char *fullname;
	int attrib;

	fullname = (char *)malloc(strlen(dir) + strlen(name) + 2);
	if (fullname) {
		strcpy(fullname, dir);
		strcat(fullname, "\\");
		strcat(fullname, name);
		attrib = GetFileAttributes(fullname);
		if (attrib & 0x10) {	/* only directories processed */
			if (GetFileSecurity
			    (fullname, OWNER_SECURITY_INFORMATION, attr, MAXATTRSZ,
			     &attrsz)) {
				domapping(accname, name, &attr[20], 0);
				attrsz = 0;
				if (GetFileSecurity
				    (fullname, GROUP_SECURITY_INFORMATION, attr,
				     MAXATTRSZ, &attrsz))
					domapping(accname, name, &attr[20], 1);
				else
					printf("   No group SID\n");
				attrsz = 0;
				if (GetFileSecurityA
				    (fullname, DACL_SECURITY_INFORMATION, attr,
				     MAXATTRSZ, &attrsz)) {
					if (type == 0)
						listaclusers(accname, attr, 20);
				} else
					printf
					    ("   No discretionary access control list\n");
			}
		}
	free(fullname);
	}
}

#else

STATIC void account(const char *accname, const char *dir, const char *name, int type)
{
	unsigned char attr[MAXATTRSZ];
	DWORD attrsz;
	char *fullname;

	fullname = (char *)malloc(strlen(dir) + strlen(name) + 2);
	if (fullname) {
		strcpy(fullname, dir);
		strcat(fullname, "/");
		strcat(fullname, name);
		if (ntfs_get_file_security(ntfs_context,
			fullname, OWNER_SECURITY_INFORMATION,
			(char*)attr, MAXATTRSZ, &attrsz)) {
			domapping(accname, name, &attr[20], 0);
			attrsz = 0;
			if (ntfs_get_file_security(ntfs_context,
			     fullname, GROUP_SECURITY_INFORMATION,
			     (char*)attr, MAXATTRSZ, &attrsz))
				domapping(accname, name, &attr[20], 1);
			else
				printf("   No group SID\n");
			attrsz = 0;
			if (ntfs_get_file_security(ntfs_context,
			     fullname, DACL_SECURITY_INFORMATION,
			     (char*)attr, MAXATTRSZ, &attrsz)) {
				if (type == 0)
					listaclusers(accname, attr, 20);
			} else
				printf("   No discretionary access control list for %s !\n",
					dir);
		}
	free(fullname);
	}
}

#endif


/*
 *		recursive search of file owners and groups in a directory
 */

#ifdef WIN32

STATIC boolean recurse(const char *accname, const char *dir, int levels)
{
	WIN32_FIND_DATA found;
	HANDLE search;
	char *filter;
	char *fullname;
	boolean err;

	err = DENIED;
	filter = (char *)malloc(strlen(dir) + 5);
	if (filter) {
		strcpy(filter, dir);
		strcat(filter, "\\*.*");
		search = FindFirstFile(filter, &found);
		if (search != INVALID_HANDLE_VALUE) {
			do {
				if (found.cFileName[0] != '.') {
					account(accname, dir, found.cFileName,1);
					if (levels > 0) {
						fullname =
						    (char *)malloc(strlen(dir) +
								   strlen(found.cFileName)
								   + 2);
						if (fullname) {
							strcpy(fullname, dir);
							strcat(fullname, "\\");
							strcat(fullname,
							       found.cFileName);
							recurse(accname,
								fullname,
								levels - 1);
							free(fullname);
						}
					}
				}
			} while (FindNextFile(search, &found));
			FindClose(search);
		}
		free(filter);
	} else {
		printf("Directory %s not found\n",dir);
		err = AGREED;
	}
	return (!err);
}

#else

STATIC boolean recurse(const char *accname, const char *dir, int levels, int docset);

STATIC int callback(struct CALLBACK *context, char *ntfsname,
	int length, int type, long long pos, unsigned long long mft_ref,
	unsigned int dt_type)
{
	char *fullname;
	char *accname;
	char *name;

	unused((void*)&pos);
	unused((void*)&mft_ref);
	unused((void*)&dt_type);
	fullname = (char *)malloc(strlen(context->dir)
			 + utf8_size(ntfsname, length) + 2);
	if (fullname) {
		if (strcmp(context->dir,"/")) {
			strcpy(fullname, context->dir);
			strcat(fullname, "/");
		} else
			strcpy(fullname,"/");
			/* Unicode to ascii conversion by a lazy man */
		name = &fullname[strlen(fullname)];
		to_utf8(name, ntfsname, length);
			/* ignore special files and DOS names */
		if ((type != 2)
		   && strcmp(name,".")
		   && strcmp(name,"..")
		   && (name[0] != '$')) {
			switch (context->docset) {
			case 2 :
					/*
					 * only "Documents and Settings"
					 * or "Users"
					 */
				if (!strcmp(name,OWNERS1)
				   || !strcmp(name,OWNERS2)) {
					recurse((char*)NULL, fullname, 2, 1);
				}
				break;
					/*
					 * within "Documents and Settings"
					 * or "Users"
					 */
			case 1 :
				accname = (char*)malloc(strlen(name) + 1);
				if (accname) {
					strcpy(accname, name);
					if (context->levels > 0)
						recurse(name, fullname,
							context->levels - 1, 0);
				}
				break;
					/*
					 * not related to "Documents and Settings"
					 * or "Users"
					 */
			case 0 :
				account(context->accname, context->dir,
					name, 1);
				if (context->levels > 0)
					recurse(context->accname, fullname,
						context->levels - 1, 0);
				break;
			}
		}
		free(fullname);
	}
/* check expected return value */
	return (0);
}

STATIC boolean recurse(const char *accname, const char *dir, int levels, int docset)
{
	struct CALLBACK context;
	boolean err;

	err = DENIED;
	context.dir = dir;
	context.accname = accname;
	context.levels = levels;
	context.docset = docset;
	ntfs_read_directory(ntfs_context,dir,callback,&context);
	return (!err);
}
#endif

/*
 *          Search directory "Documents and Settings" for user accounts
 */

#ifdef WIN32

STATIC boolean getusers(const char *dir, int levels)
{
	WIN32_FIND_DATA found;
	HANDLE search;
	char *filter;
	char *fullname;
	char *accname;
	boolean err;
	const char *docset;

	/* first get files from "Documents and Settings" */
	err = DENIED;
	if (sizeof(OWNERS1) > sizeof(OWNERS2))
		filter = (char *)malloc(strlen(dir) + strlen(OWNERS1) + 6);
	else
		filter = (char *)malloc(strlen(dir) + strlen(OWNERS2) + 6);
	if (filter) {
		docset = OWNERS1;
		strcpy(filter, dir);
		strcat(filter, "\\");
		strcat(filter, docset);
		strcat(filter, "\\*.*");
		search = FindFirstFile(filter, &found);
			/* if failed, retry with "Users" */
		if (search == INVALID_HANDLE_VALUE) {
			docset = OWNERS2;
			strcpy(filter, dir);
			strcat(filter, "\\");
			strcat(filter, docset);
			strcat(filter, "\\*.*");
			search = FindFirstFile(filter, &found);
		}
		if (search != INVALID_HANDLE_VALUE) {
			do {
				if (found.cFileName[0] != '.') {
					fullname =
					    (char *)malloc(strlen(dir)
							 + strlen(docset)
							 + strlen(found.cFileName) + 3);
					accname = (char *)
					       malloc(strlen(found.cFileName) + 1);
					if (fullname && accname) {
						strcpy(accname,
						       found.cFileName);
						
						strcpy(fullname, dir);
						strcat(fullname, "\\");
						strcat(fullname, docset);
						strcat(fullname, "\\");
						strcat(fullname,
						       found.cFileName);
						recurse(accname, fullname, 2);

						free(fullname);
					}
				}
			} while (FindNextFile(search, &found));
			FindClose(search);
		} else {
			printf("No subdirectory found in %s\\%s\n",dir,docset);
		}
			/* now search in other directories */
		strcpy(filter, dir);
		strcat(filter, "\\*.*");
		search = FindFirstFile(filter, &found);
		if (search != INVALID_HANDLE_VALUE) {
			do {
				if ((found.cFileName[0] != '.')
					&& strcmp(found.cFileName,OWNERS1)
					&& strcmp(found.cFileName,OWNERS2)) {
					fullname =
					    (char *)malloc(strlen(dir)
							 + strlen(found.cFileName) + 2);
					if (fullname) {
						strcpy(fullname, dir);
						strcat(fullname, "\\");
						strcat(fullname,
						       found.cFileName);
						recurse((char*)NULL, fullname, 2);
						free(fullname);
					}
				}
			} while (FindNextFile(search, &found));
			FindClose(search);
		} else {
			printf("No directory found in %s\n",dir);
			err = AGREED;
		}
	}
	return (!err);
}

#else

STATIC boolean getusers(const char *dir, int levels)
{
	boolean err;
	struct CALLBACK context;

	printf("* Search for \"" OWNERS1 "\" and \"" OWNERS2 "\"\n");
	err = DENIED;
	context.dir = dir;
	context.accname = (const char*)NULL;
	context.levels = levels;
	context.docset = 2;
	ntfs_read_directory(ntfs_context,dir,callback,&context);
	printf("* Search for other directories %s\n",dir);
	context.docset = 0;
	ntfs_read_directory(ntfs_context,dir,callback,&context);

	return (!err);
}

#endif

#ifdef WIN32
/*
 *		Get the current login name (Win32 only)
 */

STATIC void loginname(boolean silent)
{
	char *winname;
	char *domain;
	unsigned char *sid;
	unsigned long namesz;
	unsigned long sidsz;
	unsigned long domainsz;
	int nametype;
	boolean ok;
	int r;

	ok = FALSE;
	winname = (char*)malloc(MAXNAMESZ);
	domain = (char*)malloc(MAXNAMESZ);
	sid = (char*)malloc(MAXSIDSZ);

	namesz = MAXNAMESZ;
	domainsz = MAXNAMESZ;
	sidsz = MAXSIDSZ;
	if (winname
	    && domain
	    && sid
	    && GetUserName(winname,&namesz)) {
		winname[namesz] = '\0';
		if (!silent)
			printf("Your current user name is %s\n",winname);
		nametype = 1;
		r = LookupAccountName((char*)NULL,winname,sid,&sidsz,
			domain,&domainsz,&nametype);
		if (r) {
			domain[domainsz] = '\0';
			if (!silent)
				printf("Your account domain is %s\n",domain);
			ok = AGREED;
		}
	   }
	if (ok) {
		currentwinname = winname;
		currentdomain = domain;
		currentsid = sid;
	} else {
		currentwinname = (char*)NULL;
		currentdomain = (char*)NULL;
		currentsid = (unsigned char*)NULL;
	}
}

/*
 *		Minimal output on stdout
 */

boolean minimal(unsigned char *sid)
{
	const unsigned char *groupsid;
	boolean ok;

	ok = DENIED;
	if (sid) {
		groupsid = makegroupsid(sid);
		printf("# %s\n",BANNER);
		printf("# For Windows account \"%s\" in domain \"%s\"\n",
			currentwinname, currentdomain);
		printf("# Replace \"user\" and \"group\" hereafter by matching Linux login\n");
		printf("user::%s\n",decodesid(sid));
		printf(":group:%s\n",decodesid(groupsid));
		ok = AGREED;
	}
	return (ok);
}

#endif

STATIC boolean outputmap(const char *volume, const char *dir)
{
	char buf[256];
	int fn;
	char *fullname;
	char *backup;
	struct MAPPING *mapping;
	boolean done;
	boolean err;
	boolean undecided;
#ifdef WIN32
#else
	struct stat st;
	int s;
#endif

	done = DENIED;
	fullname = (char *)malloc(strlen(MAPFILE) + 1
				+ strlen(volume) + 1
				+ (dir ? strlen(dir) + 1 : 0));
	if (fullname) {
#ifdef WIN32
		strcpy(fullname, volume);
		if (dir && dir[0]) {
			strcat(fullname, "\\");
			strcat(fullname,dir);
		}

			/* build directory, if not present */
		if (GetFileAttributes(fullname) & 0x80000000) {
			printf("* Creating directory %s\n", fullname);
			mkdir(fullname);
		}

		strcat(fullname, "\\");
		strcat(fullname, MAPFILE);
		printf("\n");

		if (!(GetFileAttributes(fullname) & 0x80000000)) {
			backup = (char*)malloc(strlen(fullname) + 5);
			strcpy(backup,fullname);
			strcat(backup,".bak");
			unlink(backup);
			if (!rename(fullname,backup))
				printf("* Old mapping file moved to %s\n",backup);
		}
#else
		strcpy(fullname, MAPFILE);
		printf("\n");

		s = stat(fullname,&st);
		if (!s) {
			backup = (char*)malloc(strlen(fullname + 5));
			strcpy(backup,fullname);
			strcat(backup,".bak");
			if (rename(fullname,backup))
				printf("* Old mapping file moved to %s\n",backup);
		}
#endif

		printf("* Creating file %s\n", fullname);
		err = DENIED;
#ifdef WIN32
		fn = open(fullname,O_CREAT + O_TRUNC + O_WRONLY + O_BINARY, 
			S_IREAD + S_IWRITE);
#else
		fn = open(fullname,O_CREAT + O_TRUNC + O_WRONLY,
			S_IREAD + S_IWRITE);
#endif
		if (fn > 0) {
			sprintf(buf,"# %s\n",BANNER);
			if (!write(fn,buf,strlen(buf)))
				err = AGREED;
			printf("%s",buf);
			undecided = DENIED;
				/* records for owner only or group only */
			for (mapping = firstmapping; mapping && !err;
			     mapping = mapping->next)
				if (mapping->defined
				    && (!mapping->uidstr[0] || !mapping->gidstr[0])) {
					sprintf(buf,"%s:%s:%s\n",
						mapping->uidstr,
						mapping->gidstr,
						mapping->sidstr);
					if (!write(fn,buf,strlen(buf)))
						err = AGREED;
					printf("%s",buf);
				} else
					undecided = AGREED;
				/* records for both owner and group */
			for (mapping = firstmapping; mapping && !err;
			     mapping = mapping->next)
				if (mapping->defined
				    && mapping->uidstr[0] && mapping->gidstr[0]) {
					sprintf(buf,"%s:%s:%s\n",
						mapping->uidstr,
						mapping->gidstr,
						mapping->sidstr);
					if (!write(fn,buf,strlen(buf)))
						err = AGREED;
					printf("%s",buf);
				} else
					undecided = AGREED;
			done = !err;
			close(fn);
			if (undecided) {
				printf("Undecided :\n");
				for (mapping = firstmapping; mapping;
				     mapping = mapping->next)
					if (!mapping->defined) {
						printf("   %s\n", mapping->sidstr);
					}
			}
#ifndef WIN32
			printf("\n* You will have to move the file \"" MAPFILE "\"\n");
			printf("  to directory \"" MAPDIR "\" after mounting\n");
#endif
		}
	}
	if (!done)
		fprintf(stderr, "* Could not create mapping file \"%s\"\n", fullname);
	return (done);
}

STATIC boolean sanitize(void)
{
	char buf[81];
	boolean ok;
	int ownercnt;
	int groupcnt;
	struct MAPPING *mapping;
	struct MAPPING *firstowner;
	struct MAPPING *genericgroup;
	struct MAPPING *group;
	char *sidstr;

		/* count owners and groups */
		/* and find first user, and a generic group */
	ownercnt = 0;
	groupcnt = 0;
	firstowner = (struct MAPPING*)NULL;
	genericgroup = (struct MAPPING*)NULL;
	for (mapping=firstmapping; mapping; mapping=mapping->next) {
		if (mapping->defined && mapping->uidstr[0]) {
			if (!ownercnt)
				firstowner = mapping;
			ownercnt++;
		}
		if (mapping->defined && mapping->gidstr[0] && !mapping->uidstr[0]) {
			groupcnt++;
		}
		if (!mapping->defined && isgenericgroup(mapping->sidstr)) {
			genericgroup = mapping;
		}
	}
#ifdef WIN32
		/* no user defined, on Windows, suggest a mapping */
		/* based on account currently used */
	if (!ownercnt && currentwinname && currentsid) {
		char *owner;
		char *p;

		printf("\nYou have defined no file owner,\n");
		printf("   please enter the Linux login which should be mapped\n");
		printf("   to account you are currently using\n");
		printf("   Linux user ? ");
		p = fgets(buf, 80, stdin);
		if (p && p[0] && (p[strlen(p) - 1] == '\n'))
			p[strlen(p) - 1] = '\0';
		if (p && p[0]) {
			firstowner = (struct MAPPING*)malloc(sizeof(struct MAPPING));
			owner = (char*)malloc(strlen(p) + 1);
			if (firstowner && owner) {
				strcpy(owner, p);
				firstowner->next = firstmapping;
				firstowner->uidstr = owner;
				firstowner->gidstr = "";
				firstowner->sidstr = decodesid(currentsid);
				firstowner->sid = currentsid;
				firstmapping = firstowner;
				ownercnt++;
				/* prefer a generic group with the same authorities */
				for (mapping=firstmapping; mapping;
						mapping=mapping->next)
					if (!mapping->defined
					    && isgenericgroup(mapping->sidstr)
					    && !memcmp(firstowner->sidstr,
							mapping->sidstr,
							strlen(mapping->sidstr)-3))
						genericgroup = mapping;
			}
		}
	}
#endif
	if (ownercnt) {
			/*
			 *   No group was selected, but there were a generic group
			 *   insist in using it, associated to the first user
			 */
		if (!groupcnt) {
			printf("\nYou have defined no group, this can cause problems\n");
			printf("Do you accept defining a standard group ?\n");
			if (!fgets(buf,80,stdin)
			   || ((buf[0] != 'n')
			      && (buf[0] != 'N'))) {
				if (genericgroup) {
					genericgroup->uidstr = "";
					genericgroup->gidstr = firstowner->uidstr;
					genericgroup->defined = AGREED;
				} else {
					group = (struct MAPPING*)
						malloc(sizeof(struct MAPPING));
					sidstr = decodesid(
						makegroupsid(firstowner->sid));
					if (group && sidstr) {
						group->uidstr = "";
						group->gidstr = firstowner->
								uidstr;
						group->sidstr = sidstr;
						group->defined = AGREED;
						group->next = firstmapping;
						firstmapping = group;
					}
				}
			}
		}
		ok = AGREED;
	} else {
		printf("\nYou have defined no user, no mapping can be built\n");
		ok = DENIED;
	}

	return (ok);
}

STATIC boolean checkoptions(int argc, char *argv[], boolean silent)
{
	boolean err;
#ifdef WIN32
	int xarg;
	const char *pvol;

	if (silent)
		err = (argc != 1);
	else {
		err = (argc < 2);
		for (xarg=1; (xarg<argc) && !err; xarg++) {
			pvol = argv[xarg];
			if (pvol[0] && (pvol[1] == ':') && !pvol[2]) {
				err = !(((pvol[0] >= 'A') && (pvol[0] <= 'Z'))
					|| ((pvol[0] >= 'a') && (pvol[0] <= 'z')));
			}
		}
	}
	if (err) {
		fprintf(stderr, "Usage : usermap [vol1: [vol2: ...]]\n");
		fprintf(stderr, "    \"voln\" are the letters of the partition to share with Linux\n");
		fprintf(stderr, "        eg C:\n");
		fprintf(stderr, "    the Windows system partition should be named first\n");
	}
#else
	unused((void*)argv);
	unused((void*)&silent);
	err = (argc < 2);
	if (err) {
		fprintf(stderr, "Usage : usermap dev1 [dev2 ...]\n");
		fprintf(stderr, "    \"dev.\" are the devices to share with Windows\n");
		fprintf(stderr, "        eg /dev/sdb1\n");
		fprintf(stderr, "    the devices should not be mounted\n");
		fprintf(stderr, "    the Windows system partition should be named first\n");
	} else
		if (getuid()) {
			fprintf(stderr, "\nSorry, only root can start usermap\n");
			err = AGREED;
		}
#endif
	return (!err);
}

STATIC boolean process(int argc, char *argv[])
{
	boolean ok;
	int xarg;
	int targ;

	firstmapping = (struct MAPPING *)NULL;
	lastmapping = (struct MAPPING *)NULL;
	ok = AGREED;
#ifdef WIN32
	for (xarg=1; (xarg<argc) && ok; xarg++) {
		printf("\n* Scanning \"%s\" (two levels)\n",argv[xarg]);
		ok = getusers(argv[xarg],2);
	}
#else
	for (xarg=1; (xarg<argc) && ok; xarg++)
		if (open_volume(argv[xarg])) {
			printf("\n* Scanning \"%s\" (two levels)\n",argv[xarg]);
			ok = getusers("/",2);
			close_volume(argv[xarg]);
		} else
			ok = DENIED;
#endif
	if (ok && sanitize()) {
		targ = (argc > 2 ? 2 : 1);
		if (!outputmap(argv[targ],MAPDIR)) {
			printf("Trying to write file on root directory\n");
			if (outputmap(argv[targ],(const char*)NULL)) {
				printf("\nNote : you will have to move the file to directory \"%s\" on Linux\n",
					MAPDIR);
			} else
				ok = DENIED;
		} else
			ok = DENIED;
	} else
		ok = DENIED;
	return (ok);
}

int main(int argc, char *argv[])
{
	boolean ok;
	boolean silent;

	silent = !isatty(1);
	if (!silent)
		welcome();
	if (checkoptions(argc, argv, silent)) {
#ifdef WIN32
		loginname(silent);
		if (silent)
			ok = minimal(currentsid);
		else
			ok = process(argc, argv);
#else
		if (open_security_api()) {
			ok = process(argc,argv);
			if (!close_security_api()) ok = DENIED;
		}
#endif
	} else
		ok = DENIED;
	if (!ok)
		exit(1);
	return (0);
}
