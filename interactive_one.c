#include <unistd.h>  // access, unlink

static void pg_prompt() {
    fprintf(stdout,"pg> %c\n", 4);
}

extern void AbortTransaction(void);
extern void CleanupTransaction(void);
extern void ClientAuthentication(Port *port);
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

        PerformAuthentication
        ClientAuthentication(port)
        CheckPasswordAuth SYNC!!!!  ( sendAuthRequest flush -> recv_password_packet )
    InitShmemAccess/InitProcess/CreateSharedMemoryAndSemaphores

    BackendRun(port)
        PostgresMain


-> pq_flush() is synchronous



*/
extern int	ProcessStartupPacket(Port *port, bool ssl_done, bool gss_done);
extern void pq_recvbuf_fill(FILE* fp, int packetlen);

#define PG_MAX_AUTH_TOKEN_LENGTH	65535
static char *
recv_password_packet(Port *port)
{
	StringInfoData buf;
	int			mtype;

	pq_startmsgread();

	/* Expect 'p' message type */
	mtype = pq_getbyte();
	if (mtype != 'p')
	{
		/*
		 * If the client just disconnects without offering a password, don't
		 * make a log entry.  This is legal per protocol spec and in fact
		 * commonly done by psql, so complaining just clutters the log.
		 */
		if (mtype != EOF)
			ereport(ERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("expected password response, got message type %d",
							mtype)));
		return NULL;			/* EOF or bad message type */
	}

	initStringInfo(&buf);
	if (pq_getmessage(&buf, PG_MAX_AUTH_TOKEN_LENGTH))	/* receive password */
	{
		/* EOF - pq_getmessage already logged a suitable message */
		pfree(buf.data);
		return NULL;
	}

	/*
	 * Apply sanity check: password packet length should agree with length of
	 * contained string.  Note it is safe to use strlen here because
	 * StringInfo is guaranteed to have an appended '\0'.
	 */
	if (strlen(buf.data) + 1 != buf.len)
		ereport(ERROR,
				(errcode(ERRCODE_PROTOCOL_VIOLATION),
				 errmsg("invalid password packet size")));

	/*
	 * Don't allow an empty password. Libpq treats an empty password the same
	 * as no password at all, and won't even try to authenticate. But other
	 * clients might, so allowing it would be confusing.
	 *
	 * Note that this only catches an empty password sent by the client in
	 * plaintext. There's also a check in CREATE/ALTER USER that prevents an
	 * empty string from being stored as a user's password in the first place.
	 * We rely on that for MD5 and SCRAM authentication, but we still need
	 * this check here, to prevent an empty password from being used with
	 * authentication methods that check the password against an external
	 * system, like PAM, LDAP and RADIUS.
	 */
	if (buf.len == 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PASSWORD),
				 errmsg("empty password returned by client")));

	/* Do not echo password to logs, for security. */
	elog(DEBUG5, "received password packet");
	return buf.data;
}


int md5Salt_len  = 4;
char md5Salt[4];

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
        if (!(busy++ % 6553600))
            printf("FIXME: busy wait lock removed %d\n", busy);
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


// this could be pg_flush in sync mode.
    if (SOCKET_DATA>0) {
        ReadyForQuery(whereToSendOutput);

        puts("flushing data");
        if (SOCKET_FILE)
            fclose(SOCKET_FILE);

        puts("setting lock");
        FILE *c_lock;
        c_lock = fopen(PGS_OLOCK, "w");
        fclose(c_lock);
        SOCKET_FILE = NULL;
        SOCKET_DATA = 0;
/*
    while (access(PGS_OLOCK, F_OK) == 0) {
        if (!(busy++ % 6553600))
            printf("FIXME: busy wait lock removed %d\n", busy);
    }
*/
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
                whereToSendOutput = DestRemote;
                resetStringInfo(inBuf);
                rewind(fp);
                firstchar = getc(fp);

                // first packet
                if (!firstchar || (firstchar==112)) {
                    rewind(fp);

                    if (!firstchar) {
                        pq_recvbuf_fill(fp, packetlen);
                        if (ProcessStartupPacket(MyProcPort, true, true) != STATUS_OK) {
                            puts("ProcessStartupPacket !OK");
                        } else {
                            puts("auth request");
                            //ClientAuthentication(MyProcPort);
                            md5Salt[0]=0x01;
                            md5Salt[1]=0x23;
                            md5Salt[2]=0x45;
                            md5Salt[3]=0x56;
                            {
                                StringInfoData buf;
	                            pq_beginmessage(&buf, 'R');
	                            pq_sendint32(&buf, (int32) AUTH_REQ_MD5);
	                            if (md5Salt_len > 0)
		                            pq_sendbytes(&buf, md5Salt, md5Salt_len);
	                            pq_endmessage(&buf);
                                pq_flush();
                            }
                        }
                    }
                    if (firstchar==112) {
                        pq_recvbuf_fill(fp, packetlen);
                        char *passwd = recv_password_packet(MyProcPort);
                        printf("auth recv password: %s\n", "md5***" );

/*
                    // TODO: CheckMD5Auth
                        if (passwd == NULL)
                            return STATUS_EOF;
                        if (shadow_pass)
                            result = md5_crypt_verify(port->user_name, shadow_pass, passwd, md5Salt, md5Salt_len, logdetail);
                        else
                            result = STATUS_ERROR;
*/
                        pfree(passwd);
                        puts("TODO: pg_main start flag");
/*
                        {
	                        StringInfoData buf;
	                        pq_beginmessage(&buf, 'K');
	                        pq_sendint32(&buf, (int32) MyProcPid);
	                        pq_sendint32(&buf, (int32) MyCancelKey);
	                        pq_endmessage(&buf);
                        }
*/

                    }
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



