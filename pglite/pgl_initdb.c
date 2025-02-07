#pragma once

#include <stdio.h> // FILE+fprintf
#ifndef PGL_INITDB_MAIN
#define PGL_INITDB_MAIN
#endif

/*
 * and now popen will return predefined slot from a file list
 * as file handle in initdb.c
 */

/*
extern FILE* IDB_PIPE_FP;
extern FILE* SOCKET_FILE;
extern int SOCKET_DATA;
extern int IDB_STAGE;
*/

FILE *pg_popen(const char *command, const char *type) {
    if (IDB_STAGE>1) {
    	fprintf(stderr,"# popen[%s]\n", command);
    	return stderr;
    }

    if (!IDB_STAGE) {
        fprintf(stderr,"# popen[%s] (BOOT)\n", command);
        IDB_PIPE_FP = fopen( IDB_PIPE_BOOT, "w");
        IDB_STAGE = 1;
    } else {
        fprintf(stderr,"# popen[%s] (SINGLE)\n", command);
        IDB_PIPE_FP = fopen( IDB_PIPE_SINGLE, "w");
        IDB_STAGE = 2;
    }

    return IDB_PIPE_FP;

}


int
pg_chmod(const char * path, int mode_t) {
    return 0;
}


#define FRONTEND
#   include "../postgresql/src/common/logging.c"
#undef FRONTEND

#include "../postgresql/src/interfaces/libpq/pqexpbuffer.c"

#define fsync_pgdata(...)
#define icu_language_tag(loc_str) icu_language_tag_idb(loc_str)
#define icu_validate_locale(loc_str) icu_validate_locale_idb(loc_str)


#include "../postgresql/src/bin/initdb/initdb.c"

void use_socketfile(void) {
    is_repl = true;
    is_embed = false;
}



