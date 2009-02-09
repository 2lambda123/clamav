/*
 *  Copyright (C) 2009 Sourcefire, Inc.
 *  Author: Tomasz Kojm <tkojm@clamav.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
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
 */

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "shared/optparser.h"
#include "shared/misc.h"

#include "libclamav/str.h"

static struct _cfgfile {
    const char *name;
    int tool;
} cfgfile[] = {
    { "clamd.conf",	    OPT_CLAMD	    },
    { "freshclam.conf",	    OPT_FRESHCLAM   },
    { "clamav-milter.conf", OPT_MILTER	    },
    { NULL,		    0		    }
};

static void printopts(struct optstruct *opts, int nondef)
{
	const struct optstruct *opt;

    while(opts) {
	if(!opts->name) {
	    opts = opts->next;
	    continue;
	}
	if(clam_options[opts->idx].owner & OPT_DEPRECATED) {
	    if(opts->active)
		printf("*** %s is DEPRECATED ***\n", opts->name);
	    opts = opts->next;
	    continue;
	}
	if(nondef && (opts->numarg == clam_options[opts->idx].numarg) && ((opts->strarg == clam_options[opts->idx].strarg) || (opts->strarg && clam_options[opts->idx].strarg && !strcmp(opts->strarg, clam_options[opts->idx].strarg)))) {
	    opts = opts->next;
	    continue;
	}
	if(!opts->enabled) 
	    printf("%s disabled\n", opts->name);
	else switch(clam_options[opts->idx].argtype) {
	    case TYPE_STRING:
		printf("%s = \"%s\"", opts->name, opts->strarg);
		opt = opts;
		while((opt = opt->nextarg))
		    printf(", \"%s\"", opt->strarg);
		printf("\n");
		break;

	    case TYPE_NUMBER:
	    case TYPE_SIZE:
		printf("%s = \"%d\"", opts->name, opts->numarg);
		opt = opts;
		while((opt = opt->nextarg))
		    printf(", \"%d\"", opt->numarg);
		printf("\n");
		break;

	    case TYPE_BOOL:
		printf("%s = \"yes\"\n", opts->name);
		break;

	    default:
		printf("!!! %s: UNKNOWN INTERNAL TYPE !!!\n", opts->name);
	}
	opts = opts->next;
    }
}

static int printconf(const char *name)
{
	int i, j, tool = 0, tokens_count;
	char buffer[1025];
	const char *tokens[128];
	const struct clam_option *cpt;

    for(i = 0; cfgfile[i].name; i++) {
	if(!strcmp(name, cfgfile[i].name)) {
	    tool = cfgfile[i].tool;
	    break;
	}
    }
    if(!tool) {
	printf("ERROR: Unknown config file\nAvailable options:");
	for(i = 0; cfgfile[i].name; i++)
	    printf(" %s", cfgfile[i].name);
	printf("\n");
	return 1;
    }

    printf("##\n## %s - automatically generated by clamconf "VERSION"\n##\n", name);
    printf("\n# Comment out or remove the line below.\nExample\n");

    for(i = 0; clam_options[i].owner; i++) {
	cpt = &clam_options[i];
	if(cpt->name && (cpt->owner & tool) && !(cpt->owner & OPT_DEPRECATED) && !(cpt->flags & 4)) {
	    strncpy(buffer, cpt->description, 1024);
	    buffer[1024] = 0;
	    tokens_count = cli_strtokenize(buffer, '\n', 128, tokens);
	    printf("\n");
	    for(j = 0; j < tokens_count; j++)
		printf("# %s\n", tokens[j]);

	    switch(cpt->argtype) {
		case TYPE_STRING:
		    if(cpt->strarg)
			printf("# Default: %s\n", cpt->strarg);
		    else
			printf("# Default: disabled\n");
		    break;

		case TYPE_NUMBER:
		    if(cpt->numarg != -1)
			printf("# Default: %d\n", cpt->numarg);
		    else
			printf("# Default: disabled\n");
		    break;

		case TYPE_SIZE:
		    if(cpt->numarg != -1)
			printf("# Default: %d\n", cpt->numarg);
		    else
			printf("# Default: disabled\n");
		    break;

		case TYPE_BOOL:
		    if(cpt->numarg != -1)
			printf("# Default: %s\n", cpt->numarg ? "yes" : "no");
		    else
			printf("# Default: disabled\n");
		    break;

		default:
		    printf("!!! %s: UNKNOWN INTERNAL TYPE !!!\n", cpt->name);
	    }

	    if(cpt->suggested && strchr(cpt->suggested, '\n')) {
		strncpy(buffer, cpt->suggested, 1024);
		buffer[1024] = 0;
		tokens_count = cli_strtokenize(buffer, '\n', 128, tokens);
		for(j = 0; j < tokens_count; j++)
		    printf("#%s %s\n", cpt->name, tokens[j]);
	    } else {
		printf("#%s %s\n", cpt->name, cpt->suggested ? cpt->suggested : "ARG");
	    }
	}
    }

    return 0;
}

static void help(void)
{
    printf("\n");
    printf("           Clam AntiVirus: Configuration Tool %s\n", get_version());
    printf("           (C) 2009 Sourcefire, Inc.\n\n");

    printf("    --help                 -h         Show help\n");
    printf("    --version              -V         Show version\n");
    printf("    --config-dir=DIR       -c DIR     Read configuration files from DIR\n");
    printf("    --non-default          -n         Only display non-default settings\n");
    printf("    --generate-config=NAME -g NAME    Generate example config file\n");
    printf("\n");
    return;
}

int main(int argc, char **argv)
{
	const char *dir;
	char path[512];
	struct optstruct *opts, *toolopts;
	const struct optstruct *opt;
	unsigned int i, j;


    opts = optparse(NULL, argc, argv, 1, OPT_CLAMCONF, 0, NULL);
    if(!opts) {
	printf("ERROR: Can't parse command line options\n");
	return 1;
    }

    if(optget(opts, "help")->enabled) {
	help();
	optfree(opts);
	return 0;
    }

    if(optget(opts, "version")->enabled) {
	printf("Clam AntiVirus Configuration Tool %s\n", get_version());
	optfree(opts);
	return 0;
    }

    if((opt = optget(opts, "generate-config"))->enabled) {
	printconf(opt->strarg);
	optfree(opts);
	return 0;
    }

    printf("ClamAV engine version: %s\n", get_version());
    /* TODO: db information */

    dir = optget(opts, "config-dir")->strarg;
    printf("Checking configuration files in %s\n", dir);
    for(i = 0; cfgfile[i].name; i++) {
	snprintf(path, sizeof(path), "%s/%s", dir, cfgfile[i].name);
	path[511] = 0;
	if(access(path, R_OK)) {
	    printf("\n%s not found\n", cfgfile[i].name);
	    continue;
	}
	printf("\nConfig file: %s\n", cfgfile[i].name);
	for(j = 0; j < strlen(cfgfile[i].name) + 13; j++)
	    printf("-");
	printf("\n");
	toolopts = optparse(path, 0, NULL, 1, cfgfile[i].tool | OPT_DEPRECATED, 0, NULL);
	if(!toolopts)
	    continue;
	printopts(toolopts, optget(opts, "non-default")->enabled);
	optfree(toolopts);
    }
    optfree(opts);
    return 0;
}
