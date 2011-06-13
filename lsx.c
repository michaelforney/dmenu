/* See LICENSE file for copyright and license details. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static void lsx(const char *dir);

int
main(int argc, char *argv[]) {
	int i;

	if(argc < 2)
		lsx(".");
	else if(!strcmp(argv[1], "-v"))
		puts("lsx-0.2, © 2006-2011 dmenu engineers, see LICENSE for details");
	else for(i = 1; i < argc; i++)
		lsx(argv[i]);
	return EXIT_SUCCESS;
}

void
lsx(const char *dir) {
	char buf[PATH_MAX];
	struct dirent *d;
	struct stat st;
	DIR *dp;

	if(!(dp = opendir(dir))) {
		perror(dir);
		return;
	}
	while((d = readdir(dp))) {
		snprintf(buf, sizeof buf, "%s/%s", dir, d->d_name);
		if(stat(buf, &st) == 0 && S_ISREG(st.st_mode) && access(buf, X_OK) == 0)
			puts(d->d_name);
	}
	closedir(dp);
}
