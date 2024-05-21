#include <unistd.h>  // access, unlink

static void pg_prompt() {
    fprintf(stdout,"pg> %c\n", 4);
}

extern void AbortTransaction(void);
extern void CleanupTransaction(void);

extern FILE* SOCKET_FILE;
extern int SOCKET_DATA;

/*
init sequence
___________________________________
SubPostmasterMain / (forkexec)
    InitPostmasterChild
    shm attach
    preload

    BackendInitialize(Port *port) -> collect initial packet

	    pq_init();
	    whereToSendOutput = DestRemote;
	    status = ProcessStartupPacket(port, false, false);
            pq_startmsgread
            pq_getbytes from pq_recvbuf
            TODO: place PqRecvBuffer (8K) in lower mem for zero copy

    InitShmemAccess/InitProcess/CreateSharedMemoryAndSemaphores

    BackendRun(port)





*/
extern int	ProcessStartupPacket(Port *port, bool ssl_done, bool gss_done);
extern void pq_recvbuf_fill(FILE* fp, int packetlen);

EMSCRIPTEN_KEEPALIVE void
interactive_one() {
	int			firstchar;
	int			c;				/* character read from getc() */
	StringInfoData input_message;
	StringInfoData *inBuf;
    FILE *stream ;
    int packetlen;
    bool is_socket = false;
/*
    https://github.com/postgres/postgres/blob/master/src/common/stringinfo.c#L28

*/
    int busy=0;
    while (access(PGS_OLOCK, F_OK) == 0) {
        if (!(busy++ % 200))
            printf("FIXME: busy wait lock removed %d", busy);
    }

    if (!MyProcPort) {
        ClientAuthInProgress = false;
    	pq_init();					/* initialize libpq to talk to client */
    	whereToSendOutput = DestRemote; /* now safe to ereport to client */
        MyProcPort = (Port *) calloc(1, sizeof(Port));
        if (!MyProcPort) {
            puts("      --------- NO CLIENT (oom) ---------");
            abort();
        }
        MyProcPort->canAcceptConnections = CAC_OK;


        SOCKET_FILE = NULL;
        SOCKET_DATA = 0;
        puts("      --------- CLIENT (ready) ---------");
    }

    if (SOCKET_DATA>0) {
        puts("flushing data");
        if (SOCKET_FILE)
            fclose(SOCKET_FILE);

        puts("setting lock");
        FILE *c_lock;
        c_lock = fopen(PGS_OLOCK, "w");
        fclose(c_lock);
        SOCKET_FILE = NULL;
        SOCKET_DATA = 0;
        return;
    }

    if (!SOCKET_FILE) {
        SOCKET_FILE =  fopen(PGS_OUT,"w") ;
        MyProcPort->sock = fileno(SOCKET_FILE);
    }

    doing_extended_query_message = false;
    MemoryContextSwitchTo(MessageContext);
    MemoryContextResetAndDeleteChildren(MessageContext);

    initStringInfo(&input_message);
    inBuf = &input_message;

    DoingCommandRead = true;


    #define IO ((char *)(1))


    if (access(PGS_ILOCK, F_OK) != 0) {
        packetlen = 0;
        FILE *fp;
// TODO: lock file
        fp = fopen(PGS_IN, "r");
        if (fp) {
            fseek(fp, 0L, SEEK_END);
            packetlen = ftell(fp);
            if (packetlen) {
                resetStringInfo(inBuf);
                rewind(fp);
                firstchar = getc(fp);

                // first packet
                if (!firstchar) {
                    puts("auth");
                    whereToSendOutput == DestRemote;
                    rewind(fp);
                    pq_recvbuf_fill(fp, packetlen);
                    if (ProcessStartupPacket(MyProcPort, true, true) != STATUS_OK)
                        puts("ProcessStartupPacket !OK");
                } else {
                    fprintf(stderr, "incoming=%d [%d, ", packetlen, firstchar);
                    for (int i=1;i<packetlen;i++) {
                        int b = getc(fp);
                        if (i>4) {
                            appendStringInfoChar(inBuf, (char)b);
                            fprintf(stderr, "%d, ", b);
                        }
                    }
                    fprintf(stderr, "]\n");
                }
                // when using lock
                //ftruncate(filenum(fp), 0);
            }
            fclose(fp);
            unlink(PGS_IN);
            if (packetlen) {
                if (!firstchar || (firstchar==112)) {
                    puts("auth/nego skipped");
                    return;
                }
                is_socket = true;
                whereToSendOutput == DestRemote;
                goto incoming;
            }
        }
        //no use on node. usleep(10);
    }

    c = IO[0];

// TODO: use a msg queue length
    if (!c)
        return;

    // zero copy buffer ( lower wasm memory segment )
    packetlen = strlen(IO);
    if (packetlen<2) {
        pg_prompt();
        // always free kernel buffer !!!
        IO[0] = 0;
        return;
    }


// buffer query TODO: direct access ?
	resetStringInfo(inBuf);

    for (int i=0; i<packetlen; i++) {
        appendStringInfoChar(inBuf, IO[i]);
    }

    // always free kernel buffer !!!
    IO[0] = 0;

incoming:

	if (sigsetjmp(local_sigjmp_buf, 1) != 0)
	{
        error_context_stack = NULL;
        HOLD_INTERRUPTS();

        disable_all_timeouts(false);	/* do first to avoid race condition */
        QueryCancelPending = false;
        idle_in_transaction_timeout_enabled = false;
        idle_session_timeout_enabled = false;
        DoingCommandRead = false;

        pq_comm_reset();
        EmitErrorReport();
        debug_query_string = NULL;

        AbortCurrentTransaction(); // <- hang here

        if (am_walsender)
            WalSndErrorCleanup();

        PortalErrorCleanup();  // <- inf loop.
        if (MyReplicationSlot != NULL)
            ReplicationSlotRelease();

        ReplicationSlotCleanup();

        MemoryContextSwitchTo(TopMemoryContext);
        FlushErrorState();

        if (doing_extended_query_message)
            ignore_till_sync = true;

        xact_started = false;

        if (pq_is_reading_msg())
            ereport(FATAL,
	                (errcode(ERRCODE_PROTOCOL_VIOLATION),
	                 errmsg("terminating connection because protocol synchronization was lost")));
        pg_prompt();
        RESUME_INTERRUPTS();
        return;
    }

	PG_exception_stack = &local_sigjmp_buf;

if (!is_socket) {
	if (whereToSendOutput == DestRemote) {

    		firstchar = SocketBackend(&input_message);
        fprintf(stdout, "QUERY[%c]: %s", firstchar, inBuf->data);
    } else {
        if (c == EOF && inBuf->len == 0) {
            fprintf(stderr, "858-EOF !!!\n");
            firstchar = EOF;

        } else {
            appendStringInfoChar(inBuf, (char) '\0');
            if (force_echo && inBuf->len >2)
                fprintf(stdout, "QUERY: %s", inBuf->data);
        	firstchar = 'Q';
        }
    }
} else {

    fprintf(stdout, "\nSOCKET: %s", inBuf->data);
    fprintf(stdout, "\n");

}
    #include "pg_proto.c"

    #undef IO
}



