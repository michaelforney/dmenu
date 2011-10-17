/* See LICENSE file for copyright and license details. */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static void lsx(const char *dir);

static int status = EXIT_SUCCESS;

int
main(int argc, char *argv[]) {
	int i;

	if(argc < 2)
		lsx(".");
	else for(i = 1; i < argc; i++)
		lsx(argv[i]);
	return status;
}

void
lsx(const char *dir) {
	char buf[PATH_MAX];
	struct dirent *d;
	struct stat st;
	DIR *dp;

	for(dp = opendir(dir); dp && (d = readdir(dp)); errno = 0)
		if(snprintf(buf, sizeof buf, "%s/%s", dir, d->d_name) < (int)sizeof buf
		&& access(buf, X_OK) == 0 && stat(buf, &st) == 0 && S_ISREG(st.st_mode))
			puts(d->d_name);

	if(errno != 0) {
		status = EXIT_FAILURE;
		perror(dir);
	}
	if(dp)
		closedir(dp);
}
