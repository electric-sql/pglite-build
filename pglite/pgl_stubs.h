#pragma once

static void
init_locale(const char *categoryname, int category, const char *locale)
{
	if (pg_perm_setlocale(category, locale) == NULL &&
		pg_perm_setlocale(category, "C") == NULL)
		elog(FATAL, "could not adopt \"%s\" locale nor C locale for %s",
			 locale, categoryname);
}


void
PostgresMain(const char *dbname, const char *username) {
    // unused
}


void
startup_hacks(const char *progname) {
    SpinLockInit(&dummy_spinlock);
}

/*
void
RePostgresSingleUserMain(int single_argc, char *single_argv[], const char *username) {
    puts("RePostgresSingleUserMain: STUB");
}
*/
void pg_repl_raf() {
    puts("pg_repl_raf: STUB");
}



// embedded initdb requirements

void
get_restricted_token(void) {
    // stub
}

void *
pg_malloc(size_t size) {
	return malloc(size);
}

void *
pg_malloc_extended(size_t size, int flags) {
    return malloc(size);
}

void *
pg_realloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

char *
pg_strdup(const char *in) {
	char	   *tmp;

	if (!in)
	{
		fprintf(stderr,
				_("cannot duplicate null pointer (internal error)\n"));
		exit(EXIT_FAILURE);
	}
	tmp = strdup(in);
	if (!tmp)
	{
		fprintf(stderr, _("out of memory\n"));
		exit(EXIT_FAILURE);
	}
	return tmp;
}


char *
simple_prompt(const char *prompt, bool echo) {
    return pg_strdup("");
}
