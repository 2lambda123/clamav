/*
 *  Copyright (C) 2002 - 2006 Tomasz Kojm <tkojm@clamav.net>
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 *
 *  Sat May 18 15:23:21 CEST 2002: included cpu autodetection from Magnus Ekdahl
 *  Wed Mar  5 03:45:31 CET 2003: included --move code from Damien Curtain
 */

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <utime.h>
#include <grp.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <clamav.h>
#include <errno.h>

#include "defaults.h"
#include "others.h"
#include "options.h"
#include "manager.h"
#include "treewalk.h"
#include "shared.h"
#include "str.h"
#include "memory.h"
#include "output.h"
#include "misc.h"
#include "../libclamav/others.h"
#include "../libclamav/matcher-ac.h"

#ifdef C_LINUX
dev_t procdev;
#endif


int scanmanager(const struct optstruct *opt)
{
	mode_t fmode;
	int ret = 0, compression = 0, fmodeint, options = 0, i, x;
	unsigned int dboptions = 0;
	struct cl_node *trie = NULL;
	struct cl_limits *limits = NULL;
	struct passwd *user = NULL;
	struct stat sb;
	char *fullpath = NULL, cwd[1024];


/* njh@bandsman.co.uk: BeOS */
#if !defined(C_CYGWIN) && !defined(C_OS2) && !defined(C_BEOS)
    if(!geteuid()) {
	if((user = getpwnam(UNPUSER)) == NULL) {
	    logg("^Can't get information about user "UNPUSER"\n");
	    exit(60); /* this is critical problem, so we just exit here */
	}
    }
#endif

    if(opt_check(opt, "unzip") || opt_check(opt, "unrar") || opt_check(opt, "arj") ||
       opt_check(opt, "unzoo") || opt_check(opt, "jar") || opt_check(opt, "lha") ||
       opt_check(opt, "tar") || opt_check(opt, "tgz") || opt_check(opt, "deb"))
	    compression = 1;


    if(opt_check(opt, "ncore"))
	dboptions |= CL_DB_NCORE;

    if(opt_check(opt, "no-phishing"))
	dboptions |= CL_DB_NOPHISHING;

#ifdef CL_EXPERIMENTAL
    if(opt_check(opt,"no-phishing-scan-urls"))
	options |= CL_SCAN_NOPHISHING;
    if(opt_check(opt,"phishing-strict-url-check"))
	options |= CL_PHISH_NO_DOMAINLIST;
#endif

    if(opt_check(opt, "dev-ac-only")) {
	dboptions |= CL_DB_ACONLY;

	if(opt_check(opt, "dev-ac-depth"))
	    cli_ac_setdepth(atoi(opt_arg(opt, "dev-ac-depth")));
    }

    if(opt_check(opt, "database")) {
	if((ret = cl_load(opt_arg(opt, "database"), &trie, &claminfo.signs, dboptions))) {
	    logg("^%s\n", cl_strerror(ret));
	    return 50;
	}

    } else {
	    char *dbdir = freshdbdir();

	if((ret = cl_load(dbdir, &trie, &claminfo.signs, dboptions))) {
	    logg("^%s\n", cl_strerror(ret));
	    free(dbdir);
	    return 50;
	}
	free(dbdir);
    }

    if(!trie) {
	logg("^Can't initialize the virus database\n");
	return 50;
    }

    if((ret = cl_build(trie)) != 0) {
	logg("^Database initialization error: %s\n", cl_strerror(ret));;
	return 50;
    }

    /* set (default) limits */

    limits = (struct cl_limits *) calloc(1, sizeof(struct cl_limits));

    if(opt_check(opt, "max-space")) {
	char *cpy, *ptr;
	ptr = opt_arg(opt, "max-space");
	if(tolower(ptr[strlen(ptr) - 1]) == 'm') {
	    cpy = mcalloc(strlen(ptr), sizeof(char));
	    strncpy(cpy, ptr, strlen(ptr) - 1);
	    limits->maxfilesize = atoi(cpy) * 1024 * 1024;
	    free(cpy);
	} else
	    limits->maxfilesize = atoi(ptr) * 1024;
    } else
	limits->maxfilesize = 10485760;

    if(opt_check(opt, "max-files"))
	limits->maxfiles = atoi(opt_arg(opt, "max-files"));
    else
        limits->maxfiles = 500;

    if(opt_check(opt, "max-recursion"))
        limits->maxreclevel = atoi(opt_arg(opt, "max-recursion"));
    else
        limits->maxreclevel = 8;

    if(opt_check(opt, "max-ratio"))
        limits->maxratio = atoi(opt_arg(opt, "max-ratio"));
    else
        limits->maxratio = 250;

    /* set options */

    if(opt_check(opt, "disable-archive") || opt_check(opt, "no-archive"))
	options &= ~CL_SCAN_ARCHIVE;
    else
	options |= CL_SCAN_ARCHIVE;

    if(opt_check(opt, "detect-broken"))
	options |= CL_SCAN_BLOCKBROKEN;

    if(opt_check(opt, "block-encrypted"))
	options |= CL_SCAN_BLOCKENCRYPTED;

    if(opt_check(opt, "block-max"))
	options |= CL_SCAN_BLOCKMAX;

    if(opt_check(opt, "no-pe"))
	options &= ~CL_SCAN_PE;
    else
	options |= CL_SCAN_PE;

    if(opt_check(opt, "no-ole2"))
	options &= ~CL_SCAN_OLE2;
    else
	options |= CL_SCAN_OLE2;

    if(opt_check(opt, "no-html"))
	options &= ~CL_SCAN_HTML;
    else
	options |= CL_SCAN_HTML;

    if(opt_check(opt, "no-mail")) {
	options &= ~CL_SCAN_MAIL;
    } else {
	options |= CL_SCAN_MAIL;

	if(opt_check(opt, "mail-follow-urls"))
#ifdef WITH_CURL
	    options |= CL_SCAN_MAILURL;
#else
	    logg("^Support for URLs downloading with libcurl not compiled in\n");
#endif
    }

    if(opt_check(opt, "no-algorithmic"))
	options &= ~CL_SCAN_ALGO;
    else
	options |= CL_SCAN_ALGO;

#ifdef C_LINUX
    procdev = (dev_t) 0;
    if(stat("/proc", &sb) != -1 && !sb.st_size)
	procdev = sb.st_dev;
#endif

    /* check filetype */
    if(opt->filename == NULL || strlen(opt->filename) == 0) {

	/* we need full path for some reasons (eg. archive handling) */
	if(!getcwd(cwd, sizeof(cwd))) {
	    logg("^Can't get absolute pathname of current working directory\n");
	    ret = 57;
	} else
	    ret = scandirs(cwd, trie, user, opt, limits, options);

    } else if(!strcmp(opt->filename, "-")) { /* read data from stdin */
	ret = checkstdin(trie, limits, options);

    } else {
	char *thefilename;
	for (x = 0; (thefilename = cli_strtok(opt->filename, x, "\t")) != NULL; x++) {
	    if((fmodeint = fileinfo(thefilename, 2)) == -1) {
		logg("^Can't access file %s\n", thefilename);
		perror(thefilename);
		ret = 56;
	    } else {
		int slash = 1;
		for(i = strlen(thefilename) - 1; i > 0 && slash; i--) {
		    if(thefilename[i] == '/')
			thefilename[i] = 0;
		    else
			slash = 0;
		}

		fmode = (mode_t) fmodeint;

		if(compression && (thefilename[0] != '/' && thefilename[0] != '\\' && thefilename[1] != ':')) {
		    /* we need to complete the path */
		    if(!getcwd(cwd, sizeof(cwd))) {
			logg("^Can't get absolute pathname of current working directory\n");
			free(limits);
			return 57;
		    } else {
			fullpath = mcalloc(512, sizeof(char));
#ifdef NO_SNPRINTF
			sprintf(fullpath, "%s/%s", cwd, thefilename);
#else
			snprintf(fullpath, 512, "%s/%s", cwd, thefilename);
#endif
			logg("*Full path: %s\n", fullpath);
		    }
		} else
		    fullpath = thefilename;

		switch(fmode & S_IFMT) {
		    case S_IFREG:
			ret = scanfile(fullpath, trie, user, opt, limits, options);
			break;

		    case S_IFDIR:
			ret = scandirs(fullpath, trie, user, opt, limits, options);
			break;

		    default:
			logg("^Not supported file type (%s)\n", thefilename);
			ret = 52;
		}

		if(compression && (thefilename[0] != '/' && thefilename[0] != '\\' && thefilename[1] != ':')) {
		    free(fullpath);
		    fullpath = NULL;
		}
	    }
	    free(thefilename);
	}
    }

    /* free the trie */
    cl_free(trie);

    free(limits);

    /* overwrite return code */
    if(claminfo.ifiles)
	ret = 1;
    else if(ret < 50) /* hopefully no error detected */ 
	ret = 0; /* just make sure it's 0 */

    return ret;
}

int scanfile(const char *filename, struct cl_node *root, const struct passwd *user, const struct optstruct *opt, const struct cl_limits *limits, int options)
{
	int ret, included, printclean = 1;
	struct optnode *optnode;
	char *argument;
#ifdef C_LINUX
	struct stat sb;

    /* argh, don't scan /proc files */
    if(procdev)
	if(stat(filename, &sb) != -1)
	    if(sb.st_dev == procdev) {
		if(!printinfected)
		    logg("%s: Excluded (/proc)\n", filename);
		return 0;
	    }
#endif    

    if(opt_check(opt, "exclude")) {
	argument = opt_firstarg(opt, "exclude", &optnode);
	while(argument) {
	    if(match_regex(filename, argument) == 1) {
		if(!printinfected)
		    logg("%s: Excluded\n", filename);
		return 0;
	    }
	    argument = opt_nextarg(&optnode, "exclude");
	}
    }

   if(opt_check(opt, "include")) {
	included = 0;
	argument = opt_firstarg(opt, "include", &optnode);
	while(argument && !included) {
	    if(match_regex(filename, argument) == 1) {
		included = 1;
		break;
	    }
	    argument = opt_nextarg(&optnode, "include");
	}

	if(!included) {
	    if(!printinfected)
		logg("%s: Excluded\n", filename);
	    return 0;
	}
    }

    if(fileinfo(filename, 1) == 0) {
	if(!printinfected)
	    logg("%s: Empty file\n", filename);
	return 0;
    }

    if(geteuid())
	if(checkaccess(filename, NULL, R_OK) != 1) {
	    if(!printinfected)
		logg("%s: Access denied\n", filename);
	    return 0;
	}

    claminfo.files++;

    /* 
     * check the extension  - this is a special case, normally we don't need to
     * do this (libclamav detects archive by its magic string), but here we
     * want to know the exit code from internal unpacker and try to use
     * external (if provided) when internal cannot extract data.
     */

    if((cli_strbcasestr(filename, ".zip") || cli_strbcasestr(filename, ".rar")) && (options & CL_SCAN_ARCHIVE)) {
	/* try to use internal archivers */
	if((ret = checkfile(filename, root, limits, options, 1)) == CL_VIRUS) {
	    if(opt_check(opt, "remove")) {
		if(unlink(filename)) {
		    logg("%s: Can't remove\n", filename);
		    claminfo.notremoved++;
		} else {
		    logg("%s: Removed\n", filename);
		}
	    } else if (opt_check(opt, "move"))
		move_infected(filename, opt);

	    return 1;

	} else if(ret == CL_CLEAN) {
	    return 0;
	} else if(ret == 54) {
	    return ret;
	}

	/* in other case try to continue with external archivers */
	options &= ~CL_SCAN_ARCHIVE; /* and disable decompression for the checkfile() below */
	printclean = 0;
    }

    if((cli_strbcasestr(filename, ".zip") && opt_check(opt, "unzip"))
    || (cli_strbcasestr(filename, ".rar") && opt_check(opt, "unrar"))
    || (cli_strbcasestr(filename, ".arj") && opt_check(opt, "arj"))
    || (cli_strbcasestr(filename, ".zoo") && opt_check(opt, "unzoo"))
    || (cli_strbcasestr(filename, ".jar") && opt_check(opt, "jar"))
    || (cli_strbcasestr(filename, ".lzh") && opt_check(opt, "lha"))
    || (cli_strbcasestr(filename, ".tar") && opt_check(opt, "tar"))
    || (cli_strbcasestr(filename, ".deb") && opt_check(opt, "deb"))
    || ((cli_strbcasestr(filename, ".tar.gz") || cli_strbcasestr(filename, ".tgz")) 
	 && (opt_check(opt, "tgz") || opt_check(opt, "deb"))) ) {

	/* check permissions */
	switch(checkaccess(filename, UNPUSER, R_OK)) {
	    case -1:
		logg("^Can't get information about user "UNPUSER"\n");
		exit(60); /* this is a critical problem so we just exit here */
	    case -2:
		logg("^Can't fork\n");
		exit(61);
	    case 0: /* read access denied */
		if(geteuid()) {
		    if(!printinfected)
			logg("%s: Access denied to archive\n", filename);
		} else {

		    if(limits && limits->maxfilesize)
			if(fileinfo(filename, 1) / 1024 > limits->maxfilesize) {
			    if(!printinfected)
				logg("%s: Archive too big\n", filename);
			    return 0;
			}

		    return(scandenied(filename, root, user, opt, limits, options));
		}
		return 0;
	    case 1:
		return(scancompressed(filename, root, user, opt, limits, options));
	}
    }

    if((ret = checkfile(filename, root, limits, options, printclean)) == CL_VIRUS) {
	if(opt_check(opt, "remove")) {
	    if(unlink(filename)) {
		logg("%s: Can't remove\n", filename);
		claminfo.notremoved++;
	    } else {
		logg("%s: Removed\n", filename);
	    }
	} else if (opt_check(opt, "move"))
            move_infected(filename, opt);
    }
    return ret;
}

/* it has guaranteed read access to the archive */
int scancompressed(const char *filename, struct cl_node *root, const struct passwd *user, const struct optstruct *opt, const struct cl_limits *limits, int options)
{
	int ret = 0;
	char *tmpdir, *gendir, *userprg;
	struct stat statbuf;


    stat(filename, &statbuf);

    if(!S_ISREG(statbuf.st_mode)) {
	logg("^Suspected archive %s is not a regular file\n", filename);
	return 0; /* hmm ? */
    }

    /* check write access */

    tmpdir = getenv("TMPDIR");

    if(tmpdir == NULL)
#ifdef P_tmpdir
	tmpdir = P_tmpdir;
#else
	tmpdir = "/tmp";
#endif

    if(checkaccess(tmpdir, UNPUSER, W_OK) != 1) {
	logg("^Can't write to the temporary directory\n");
	exit(64);
    }

    /* generate the temporary directory */

    gendir = cli_gentemp(tmpdir);
    if(mkdir(gendir, 0700)) {
	logg("^Can't create the temporary directory %s\n", gendir);
	exit(63); /* critical */
    }

#ifndef C_OS2
    if(user)
	chown(gendir, user->pw_uid, user->pw_gid);
#endif

    /* unpack file  - as unprivileged user */
    if(cli_strbcasestr(filename, ".zip")) {
	char *args[] = { "unzip", "-P", "clam", "-o", NULL, NULL };
	/* Sun's SUNWspro C compiler doesn't allow direct initialisation
	 * with a variable
	 */
	args[4] = (char *) filename;

	if((userprg = opt_arg(opt, "unzip")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("unzip", args, gendir, user, opt);

    } else if(cli_strbcasestr(filename, ".rar")) { 
	char *args[] = { "unrar", "x", "-p-", "-y", NULL, NULL };
	args[4] = (char *) filename;
	if((userprg = opt_arg(opt, "unrar")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("unrar", args, gendir, user, opt);

    } else if(cli_strbcasestr(filename, ".arj")) { 
        char *args[] = { "arj", "x","-y", NULL, NULL };
	args[3] = (char *) filename;
        if((userprg = opt_arg(opt, "arj")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("arj", args, gendir, user, opt);

    } else if(cli_strbcasestr(filename, ".zoo")) { 
	char *args[] = { "unzoo", "-x","-j","./", NULL, NULL };
	args[4] = (char *) filename;
	if((userprg = opt_arg(opt, "unzoo")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("unzoo", args, gendir, user, opt);

    } else if(cli_strbcasestr(filename, ".jar")) { 
	char *args[] = { "unzip", "-P", "clam", "-o", NULL, NULL };
	args[4] = (char *) filename;
	if((userprg = opt_arg(opt, "jar")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("unzip", args, gendir, user, opt);

    } else if(cli_strbcasestr(filename, ".lzh")) { 
	char *args[] = { "lha", "xf", NULL, NULL };
	args[2] = (char *) filename;
	if((userprg = opt_arg(opt, "lha")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("lha", args, gendir, user, opt);

    } else if(cli_strbcasestr(filename, ".tar")) { 
	char *args[] = { "tar", "-xpvf", NULL, NULL };
	args[2] = (char *) filename;
	if((userprg = opt_arg(opt, "tar")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("tar", args, gendir, user, opt);

    } else if(cli_strbcasestr(filename, ".deb")) { 
	char *args[] = { "ar", "x", NULL, NULL };
	args[2] = (char *) filename;
	if((userprg = opt_arg(opt, "deb")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("ar", args, gendir, user, opt);

    } else if((cli_strbcasestr(filename, ".tar.gz") || cli_strbcasestr(filename, ".tgz"))) {
	char *args[] = { "tar", "-zxpvf", NULL, NULL };
	args[2] = (char *) filename;
	if((userprg = opt_arg(opt, "tgz")))
	    ret = clamav_unpack(userprg, args, gendir, user, opt);
	else
	    ret = clamav_unpack("tar", args, gendir, user, opt);
    }

    /* fix permissions of extracted files */
    fixperms(gendir);

    if(!ret) { /* execute successful */
	    short oldrec = recursion;

	recursion = 1;
	ret = treewalk(gendir, root, user, opt, limits, options, 1);
	recursion = oldrec;
    }

    /* remove the directory  - as clamav */
    if(!opt_check(opt, "leave-temps"))
	clamav_rmdirs(gendir);

    /* free gendir - it's not necessary now */
    free(gendir);

    switch(ret) {
	case -1:
	    logg("^Can't fork()\n");
	    exit(61); /* this is critical problem, so we just exit here */
	case -2:
	    logg("^Can't execute some unpacker. Check paths and permissions on the temporary directory\n");
	    /* This is no longer a critical error (since 0.24). We scan
	     * raw archive.
	     */
	    if((ret = checkfile(filename, root, limits, 0, 0)) == CL_VIRUS) {
		if(opt_check(opt, "remove")) {
		    if(unlink(filename)) {
			logg("%s: Can't remove\n", filename);
			claminfo.notremoved++;
		    } else {
			logg("%s: Removed\n", filename);
		    }
		} else if (opt_check(opt, "move"))
		    move_infected(filename, opt);
	    }
	    return ret;
	case -3:
	    return 0;
	case 0:
	    /* no viruses found in archive, we scan just in case a raw file
	     */
	    if((ret = checkfile(filename, root, limits, 0, 1)) == CL_VIRUS) {
		if(opt_check(opt, "remove")) {
		    if(unlink(filename)) {
			logg("%s: Can't remove\n", filename);
			claminfo.notremoved++;
		    } else {
			logg("%s: Removed\n", filename);
		    }
		} else if (opt_check(opt, "move"))
		    move_infected(filename, opt);
	    }
	    return ret;
	case 1:
	    logg("%s: Infected.Archive FOUND\n", filename);

	    if(bell)
		fprintf(stderr, "\007");

	    if(opt_check(opt, "remove")) {
		if(unlink(filename)) {
		    logg("%s: Can't remove\n", filename);
		    claminfo.notremoved++;
		} else {
		    logg("%s: Removed\n", filename);
		}
	    } else if (opt_check(opt, "move"))
		move_infected(filename, opt);

	    return 1;
	default:
	    logg("^Strange value (%d) returned in scancompressed()\n", ret);
	    return 0;
    }
}

int scandenied(const char *filename, struct cl_node *root, const struct passwd *user, const struct optstruct *opt, const struct cl_limits *limits, int options)
{
	char *tmpdir, *gendir, *tmpfile, *pt;
	struct stat statbuf;
	int ret;

    stat(filename, &statbuf);
    if(!S_ISREG(statbuf.st_mode)) {
	logg("^Suspected archive %s is not a regular file\n", filename);
	return 0;
    }

    /* check write access */

    tmpdir = getenv("TMPDIR");

    if(tmpdir == NULL)
#ifdef P_tmpdir
	tmpdir = P_tmpdir;
#else
	tmpdir = "/tmp";
#endif


    if(checkaccess(tmpdir, UNPUSER, W_OK) != 1) {
	logg("^Can't write to the temporary directory %s\n", tmpdir);
	exit(64);
    }

    /* generate the temporary directory */
    gendir = cli_gentemp(tmpdir);
    if(mkdir(gendir, 0700)) {
	logg("^Can't create the temporary directory %s\n", gendir);
	exit(63); /* critical */
    }

    tmpfile = (char *) mcalloc(strlen(gendir) + strlen(filename) + 10, sizeof(char));
    pt = strrchr(filename, '/');
    if(!pt)
	pt = (char *) filename;
    else
	pt += 1;

    sprintf(tmpfile, "%s/%s", gendir, pt);

    if(filecopy(filename, tmpfile) == -1) {
	logg("!I/O error\n");
	perror("copyfile()");
	exit(58);
    }

    fixperms(gendir);

#ifndef C_OS2
    if(user) {
	chown(gendir, user->pw_uid, user->pw_gid);
	chown(tmpfile, user->pw_uid, user->pw_gid);
    }
#endif

    if((ret = treewalk(gendir, root, user, opt, limits, options, 1)) == 1) {
	logg("(Real infected archive: %s)\n", filename);

	if(opt_check(opt, "remove")) {
	    if(unlink(filename)) {
		logg("%s: Can't remove\n", filename);
		claminfo.notremoved++;
	    } else {
	        logg("%s: Removed\n", filename);
	    }
	} else if (opt_check(opt, "move"))
	    move_infected(filename, opt);
    }

    /* remove the directory  - as clamav */
    clamav_rmdirs(gendir);

    free(gendir);
    free(tmpfile);

    return ret;
}

int scandirs(const char *dirname, struct cl_node *root, const struct passwd *user, const struct optstruct *opt, const struct cl_limits *limits, int options)
{
	return treewalk(dirname, root, user, opt, limits, options, 1);
}

int checkfile(const char *filename, const struct cl_node *root, const struct cl_limits *limits, int options, short printclean)
{
	int fd, ret;
	const char *virname;


    logg("*Scanning %s\n", filename);

    if((fd = open(filename, O_RDONLY)) == -1) {
	logg("^Can't open file %s\n", filename);
	return 54;
    }

    if((ret = cl_scandesc(fd, &virname, &claminfo.blocks, root, limits, options)) == CL_VIRUS) {
	logg("%s: %s FOUND\n", filename, virname);
	claminfo.ifiles++;

	if(bell)
	    fprintf(stderr, "\007");

    } else if(ret == CL_CLEAN) {
	if(!printinfected && printclean)
	    mprintf("%s: OK\n", filename);
    } else
	if(!printinfected)
	    logg("%s: %s\n", filename, cl_strerror(ret));

    close(fd);
    return ret;
}

int checkstdin(const struct cl_node *root, const struct cl_limits *limits, int options)
{
	int ret;
	const char *virname, *tmpdir;
	char *file, buff[FILEBUFF];
	FILE *fs;


    /* check write access */
    tmpdir = getenv("TMPDIR");

    if(tmpdir == NULL)
#ifdef P_tmpdir
	tmpdir = P_tmpdir;
#else
	tmpdir = "/tmp";
#endif

    if(checkaccess(tmpdir, UNPUSER, W_OK) != 1) {
	logg("^Can't write to temporary directory\n");
	return 64;
    }

    file = cli_gentemp(tmpdir);

    if(!(fs = fopen(file, "wb"))) {
	logg("^Can't open %s for writing\n", file);
	return 63;
    }

    while((ret = fread(buff, 1, FILEBUFF, stdin)))
	fwrite(buff, 1, ret, fs);

    fclose(fs);

    logg("*Checking %s\n", file);
    claminfo.files++;

    if((ret = cl_scanfile(file, &virname, &claminfo.blocks, root, limits, options)) == CL_VIRUS) {
	logg("stdin: %s FOUND\n", virname);
	claminfo.ifiles++;

	if(bell)
	    fprintf(stderr, "\007");

    } else if(ret == CL_CLEAN) {
	if(!printinfected)
	    mprintf("stdin: OK\n");
    } else
	if(!printinfected)
	    logg("stdin: %s\n", cl_strerror(ret));

    unlink(file);
    free(file);
    return ret;
}

/*
 * -1 -> can't fork
 * -2 -> can't execute
 * -3 -> external signal
 * 0 -> OK
 */

int clamav_unpack(const char *prog, char **args, const char *tmpdir, const struct passwd *user, const struct optstruct *opt)
{
	pid_t pid;
	int status, wret, maxfiles, maxspace, fdevnull;
	struct s_du n;


    if(opt_check(opt, "max-files"))
	maxfiles = atoi(opt_arg(opt, "max-files"));
    else
	maxfiles = 0;

    if(opt_check(opt, "max-space")) {
	    char *cpy, *ptr;
	ptr = opt_arg(opt, "max-space");
	if(tolower(ptr[strlen(ptr) - 1]) == 'm') { /* megabytes */
	    cpy = mcalloc(strlen(ptr), sizeof(char));
	    strncpy(cpy, ptr, strlen(ptr) - 1);
	    maxspace = atoi(cpy) * 1024;
	    free(cpy);
	} else /* default - kilobytes */
	    maxspace = atoi(ptr);
    } else
	maxspace = 0;


    switch(pid = fork()) {
	case -1:
	    return -1;
	case 0:
#ifndef C_CYGWIN
	    if(!geteuid() && user) {

#ifdef HAVE_SETGROUPS
		if(setgroups(1, &user->pw_gid)) {
		    fprintf(stderr, "ERROR: setgroups() failed\n");
		    exit(1);
		}
#endif

		if(setgid(user->pw_gid)) {
		    fprintf(stderr, "ERROR: setgid(%d) failed\n", (int) user->pw_gid);
		    exit(1);
		}

		if(setuid(user->pw_uid)) {
		    fprintf(stderr, "ERROR: setuid(%d) failed\n", (int) user->pw_uid);
		    exit(1);
		}
	    }
#endif
	    chdir(tmpdir);

	    if(printinfected) {
  	        fdevnull = open("/dev/null", O_WRONLY);
		if(fdevnull == -1) {
		    logg("Non fatal error: cannot open /dev/null. Continuing with full output\n");
		    printinfected = 0;
		} else {
		    dup2(fdevnull,1);
		    dup2(fdevnull,2);
		}
	    }

	    if(strchr(prog, '/')) /* we have full path */
		execv(prog, args);
	    else
		execvp(prog, args);
	    perror("execv(p)");
	    abort();
	    break;
	default:

	    if(maxfiles || maxspace) {
		while(!(wret = waitpid(pid, &status, WNOHANG))) {
		    memset(&n, 0, sizeof(struct s_du));

		    if(!du(tmpdir, &n))
			if((maxfiles && n.files > maxfiles) || (maxspace && n.space > maxspace)) {
			    logg("*n.files: %d, n.space: %d\n", n.files, n.space);
			    kill(pid, 9); /* stop it immediately */
			}
		}
	    } else
		waitpid(pid, &status, 0);


	    if(WIFSIGNALED(status)) {
		switch(WTERMSIG(status)) {

		    case 9:
			logg("\nUnpacker process %d stopped due to exceeded limits\n", pid);
			return 0;
		    case 6: /* abort */
			logg("^Can't run %s\n", prog);
			return -2;
		    default:
			logg("^\nUnpacker stopped with external signal %d\n", WTERMSIG(status));
			return -3;
		}
	    } else if(WIFEXITED(status))
		return 0;
    }

    return 0;
}

void move_infected(const char *filename, const struct optstruct *opt)
{
	char *movedir, *movefilename, *tmp, numext[4 + 1];
	struct stat fstat, mfstat;
	int n, len, movefilename_size;
	struct utimbuf ubuf;


    if(!(movedir = opt_arg(opt, "move"))) {
        /* Should never reach here */
        logg("!opt_arg() returned NULL\n", filename);
        claminfo.notmoved++;
        return;
    }

    if(access(movedir, W_OK|X_OK) == -1) {
        logg("!Can't move file '%s': cannot write to '%s': %s\n", filename, movedir, strerror(errno));
        claminfo.notmoved++;
        return;
    }

    if(!(tmp = strrchr(filename, '/')))
	tmp = (char *) filename;

    movefilename_size = sizeof(char) * (strlen(movedir) + strlen(tmp) + sizeof(numext) + 2);

    if(!(movefilename = mmalloc(movefilename_size))) {
        logg("!mmalloc() failed\n");
	exit(71);
    }

    if(!(cli_strrcpy(movefilename, movedir))) {
        logg("!cli_strrcpy() returned NULL\n");
        claminfo.notmoved++;
        free(movefilename);
        return;
    }

    strcat(movefilename, "/");

    if(!(strcat(movefilename, tmp))) {
        logg("!strcat() returned NULL\n");
        claminfo.notmoved++;
        free(movefilename);
        return;
    }

    stat(filename, &fstat);

    if(!stat(movefilename, &mfstat)) {
        if(fstat.st_ino == mfstat.st_ino) { /* It's the same file*/
            logg("File excluded '%s'\n", filename);
            claminfo.notmoved++;
            free(movefilename);
            return;
        } else {
            /* file exists - try to append an ordinal number to the
	     * quranatined file in an attempt not to overwrite existing
	     * files in quarantine  
	     */
            len = strlen(movefilename);
            n = 0;        		        		
            do {
                /* reset the movefilename to it's initial value by
		 * truncating to the original filename length
		 */
                movefilename[len] = 0;
                /* append .XXX */
                sprintf(numext, ".%03d", n++);
                strcat(movefilename, numext);            	
            } while(!stat(movefilename, &mfstat) && (n < 1000));
       }
    }

    if(rename(filename, movefilename) == -1) {
	if(filecopy(filename, movefilename) == -1) {
	    logg("!Can't move '%s' to '%s': %s\n", filename, movefilename, strerror(errno));
	    claminfo.notmoved++;
	    free(movefilename);
	    return;
	}

	chmod(movefilename, fstat.st_mode);
#ifndef C_OS2
	chown(movefilename, fstat.st_uid, fstat.st_gid);
#endif

	ubuf.actime = fstat.st_atime;
	ubuf.modtime = fstat.st_mtime;
	utime(movefilename, &ubuf);

	if(unlink(filename)) {
	    logg("!Can't unlink '%s': %s\n", filename, strerror(errno));
	    claminfo.notremoved++;            
	    free(movefilename);
	    return;
	}
    }

    logg("%s: moved to '%s'\n", filename, movefilename);

    free(movefilename);
}
