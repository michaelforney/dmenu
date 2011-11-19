/* See LICENSE file for copyright and license details. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define OPER(x)  (oper[(x)-'a'])

static bool test(const char *);

static bool quiet = false;
static bool oper[26];
static struct stat old, new;

int
main(int argc, char *argv[]) {
	char buf[BUFSIZ], *p;
	bool match = false;
	int opt;

	while((opt = getopt(argc, argv, "C:bcdefghn:o:pqrsuwx")) != -1)
		switch(opt) {
		case 'C': /* tests relative to directory */
			if(chdir(optarg) == -1) {
				perror(optarg);
				exit(2);
			}
			break;
		case 'n': /* newer than file */
		case 'o': /* older than file */
			if(!(OPER(opt) = stat(optarg, (opt == 'n' ? &new : &old)) == 0))
				perror(optarg);
			break;
		case 'q': /* quiet (no output, just status) */
			quiet = true;
			break;
		default:  /* miscellaneous operators */
			OPER(opt) = true;
			break;
		case '?': /* error: unknown flag */
			fprintf(stderr, "usage: %s [-bcdefghpqrsuwx] [-C dir] [-n file] [-o file] [file...]\n", argv[0]);
			exit(2);
		}
	if(optind == argc)
		while(fgets(buf, sizeof buf, stdin)) {
			if(*(p = &buf[strlen(buf)-1]) == '\n')
				*p = '\0';
			match |= test(buf);
		}
	else
		while(optind < argc)
			match |= test(argv[optind++]);

	return match ? 0 : 1;
}

bool
test(const char *path) {
	struct stat st;

	if((!OPER('b') || (stat(path, &st) == 0 && S_ISBLK(st.st_mode)))        /* block special     */
	&& (!OPER('c') || (stat(path, &st) == 0 && S_ISCHR(st.st_mode)))        /* character special */
	&& (!OPER('d') || (stat(path, &st) == 0 && S_ISDIR(st.st_mode)))        /* directory         */
	&& (!OPER('e') || (access(path, F_OK) == 0))                            /* exists            */
	&& (!OPER('f') || (stat(path, &st) == 0 && S_ISREG(st.st_mode)))        /* regular file      */
	&& (!OPER('g') || (stat(path, &st) == 0 && (st.st_mode & S_ISGID)))     /* set-group-id flag */
	&& (!OPER('h') || (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)))       /* symbolic link     */
	&& (!OPER('n') || (stat(path, &st) == 0 && st.st_mtime > new.st_mtime)) /* newer than file   */
	&& (!OPER('o') || (stat(path, &st) == 0 && st.st_mtime < old.st_mtime)) /* older than file   */
	&& (!OPER('p') || (stat(path, &st) == 0 && S_ISFIFO(st.st_mode)))       /* named pipe        */
	&& (!OPER('r') || (access(path, R_OK) == 0))                            /* readable          */
	&& (!OPER('s') || (stat(path, &st) == 0 && st.st_size > 0))             /* not empty         */
	&& (!OPER('u') || (stat(path, &st) == 0 && (st.st_mode & S_ISUID)))     /* set-user-id flag  */
	&& (!OPER('w') || (access(path, W_OK) == 0))                            /* writable          */
	&& (!OPER('x') || (access(path, X_OK) == 0))) {                         /* executable        */
		if(quiet)
			exit(0);
		puts(path);
		return true;
	}
	else
		return false;
}
