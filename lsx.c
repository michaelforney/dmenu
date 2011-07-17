/* See LICENSE file for copyright and license details. */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static void lsx(const char *dir);

int
main(int argc, char *argv[]) {
	int i;

	if(argc < 2)
		lsx(".");
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
	while((d = readdir(dp)))
		if(snprintf(buf, sizeof buf, "%s/%s", dir, d->d_name) < (int)sizeof buf
		&& !stat(buf, &st) && S_ISREG(st.st_mode) && access(buf, X_OK) == 0)
			puts(d->d_name);
	closedir(dp);
}
