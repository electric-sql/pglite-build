#pragma once



#define pclose(stream) pg_pclose(stream)
#include <stdio.h> // FILE

FILE* IDB_PIPE_FP = NULL;
int IDB_STAGE = 0;

int
pg_pclose(FILE *stream) {
    if (IDB_STAGE==1)
        fprintf(stderr,"# pg_pclose(%s) 133:" __FILE__ "\n" , IDB_PIPE_BOOT);
    if (IDB_STAGE==2)
        fprintf(stderr,"# pg_pclose(%s) 135:" __FILE__ "\n" , IDB_PIPE_SINGLE);

    if (IDB_PIPE_FP) {
        fflush(IDB_PIPE_FP);
        fclose(IDB_PIPE_FP);
        IDB_PIPE_FP = NULL;
    }
    return 0;
}


// #endif // PG_EXEC
