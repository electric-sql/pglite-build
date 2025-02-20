#include <setjmp.h>

FILE * single_mode_feed = NULL;
volatile bool inloop = false;
volatile sigjmp_buf local_sigjmp_buf;
bool repl = false;

__attribute__((export_name("pg_shutdown")))
void
pg_shutdown() {
    PDEBUG("# 11:" __FILE__": pg_shutdown");
    proc_exit(66);
}


void
interactive_file() {
	int			firstchar;
	int			c;				/* character read from getc() */
	StringInfoData input_message;
	StringInfoData *inBuf;
    FILE *stream ;

	/*
	 * At top of loop, reset extended-query-message flag, so that any
	 * errors encountered in "idle" state don't provoke skip.
	 */
	doing_extended_query_message = false;

	/*
	 * Release storage left over from prior query cycle, and create a new
	 * query input buffer in the cleared MessageContext.
	 */
	MemoryContextSwitchTo(MessageContext);
	MemoryContextResetAndDeleteChildren(MessageContext);

	initStringInfo(&input_message);
    inBuf = &input_message;
	DoingCommandRead = true;

	//firstchar = ReadCommand(&input_message);
	if (whereToSendOutput == DestRemote)
		firstchar = SocketBackend(&input_message);
	else {

	    /*
	     * display a prompt and obtain input from the user
	     */
        if (!single_mode_feed) {
	        printf("pg> %c\n", 4);
        	fflush(stdout);
            stream = stdin;
        } else {
            stream = single_mode_feed;
        }

	    resetStringInfo(inBuf);
	    while ((c = getc(stream)) != EOF)
	    {
		    if (c == '\n')
		    {
			    if (UseSemiNewlineNewline)
			    {
				    /*
				     * In -j mode, semicolon followed by two newlines ends the
				     * command; otherwise treat newline as regular character.
				     */
				    if (inBuf->len > 1 &&
					    inBuf->data[inBuf->len - 1] == '\n' &&
					    inBuf->data[inBuf->len - 2] == ';')
				    {
					    /* might as well drop the second newline */
					    break;
				    }
			    }
			    else
			    {
				    /*
				     * In plain mode, newline ends the command unless preceded by
				     * backslash.
				     */
				    if (inBuf->len > 0 &&
					    inBuf->data[inBuf->len - 1] == '\\')
				    {
					    /* discard backslash from inBuf */
					    inBuf->data[--inBuf->len] = '\0';
					    /* discard newline too */
					    continue;
				    }
				    else
				    {
					    /* keep the newline character, but end the command */
					    appendStringInfoChar(inBuf, '\n');
					    break;
				    }
			    }
		    }

		    /* Not newline, or newline treated as regular character */
		    appendStringInfoChar(inBuf, (char) c);
	    }

	    if (c == EOF && inBuf->len == 0) {
		    firstchar = EOF;
        } else {
        	/* Add '\0' to make it look the same as message case. */
	        appendStringInfoChar(inBuf, (char) '\0');
        	firstchar = 'Q';
        }

    }

	if (ignore_till_sync && firstchar != EOF)
		return;

    #include "pg_proto.c"
}

void
RePostgresSingleUserMain(int single_argc, char *single_argv[], const char *username)
{
#if PGDEBUG
printf("# 123: RePostgresSingleUserMain progname=%s for %s feed=%s\n", progname, single_argv[0], IDB_PIPE_SINGLE);
#endif
    single_mode_feed = fopen(IDB_PIPE_SINGLE, "r");

    // should be template1.
    const char *dbname = NULL;


    /* Parse command-line options. */
    process_postgres_switches(single_argc, single_argv, PGC_POSTMASTER, &dbname);
#if PGDEBUG
printf("# 134: dbname=%s\n", dbname);
#endif
    LocalProcessControlFile(false);

    process_shared_preload_libraries();

//	                InitializeMaxBackends();

// ? IgnoreSystemIndexes = true;
IgnoreSystemIndexes = false;
    process_shmem_requests();

    InitializeShmemGUCs();

    InitializeWalConsistencyChecking();

    PgStartTime = GetCurrentTimestamp();

    SetProcessingMode(InitProcessing);
PDEBUG("# 153: Re-InitPostgres");
if (am_walsender)
    PDEBUG("# 155: am_walsender == true");
//      BaseInit();

    InitPostgres(dbname, InvalidOid,	/* database to connect to */
                 username, InvalidOid,	/* role to connect as */
                 (!am_walsender) ? INIT_PG_LOAD_SESSION_LIBS : 0,
                 NULL);			/* no out_dbname */

PDEBUG("# 164:" __FILE__);

    SetProcessingMode(NormalProcessing);

    BeginReportingGUCOptions();

    if (IsUnderPostmaster && Log_disconnections)
        on_proc_exit(log_disconnections, 0);

    pgstat_report_connect(MyDatabaseId);

    /* Perform initialization specific to a WAL sender process. */
    if (am_walsender)
        InitWalSender();

#if PGDEBUG
    whereToSendOutput = DestDebug;
#endif

    if (whereToSendOutput == DestDebug)
        printf("\nPostgreSQL stand-alone backend %s\n", PG_VERSION);

    /*
     * Create the memory context we will use in the main loop.
     *
     * MessageContext is reset once per iteration of the main loop, ie, upon
     * completion of processing of each command message from the client.
     */
    MessageContext = AllocSetContextCreate(TopMemoryContext,
						                   "MessageContext",
						                   ALLOCSET_DEFAULT_SIZES);

    /*
     * Create memory context and buffer used for RowDescription messages. As
     * SendRowDescriptionMessage(), via exec_describe_statement_message(), is
     * frequently executed for ever single statement, we don't want to
     * allocate a separate buffer every time.
     */
    row_description_context = AllocSetContextCreate(TopMemoryContext,
									                "RowDescriptionContext",
									                ALLOCSET_DEFAULT_SIZES);
    MemoryContextSwitchTo(row_description_context);
    initStringInfo(&row_description_buf);
    MemoryContextSwitchTo(TopMemoryContext);

#if 1 // defined(__wasi__)
    puts("# 210: sjlj exception handler off in initdb");
#else
    if (sigsetjmp(local_sigjmp_buf, 1) != 0)
    {
        /*
         * NOTE: if you are tempted to add more code in this if-block,
         * consider the high probability that it should be in
         * AbortTransaction() instead.  The only stuff done directly here
         * should be stuff that is guaranteed to apply *only* for outer-level
         * error recovery, such as adjusting the FE/BE protocol status.
         */

        /* Since not using PG_TRY, must reset error stack by hand */
        error_context_stack = NULL;

        /* Prevent interrupts while cleaning up */
        HOLD_INTERRUPTS();

        /*
         * Forget any pending QueryCancel request, since we're returning to
         * the idle loop anyway, and cancel any active timeout requests.  (In
         * future we might want to allow some timeout requests to survive, but
         * at minimum it'd be necessary to do reschedule_timeouts(), in case
         * we got here because of a query cancel interrupting the SIGALRM
         * interrupt handler.)	Note in particular that we must clear the
         * statement and lock timeout indicators, to prevent any future plain
         * query cancels from being misreported as timeouts in case we're
         * forgetting a timeout cancel.
         */
        disable_all_timeouts(false);	/* do first to avoid race condition */
        QueryCancelPending = false;
        idle_in_transaction_timeout_enabled = false;
        idle_session_timeout_enabled = false;

        /* Not reading from the client anymore. */
        DoingCommandRead = false;

        /* Make sure libpq is in a good state */
        pq_comm_reset();

        /* Report the error to the client and/or server log */
        EmitErrorReport();

        /*
         * If Valgrind noticed something during the erroneous query, print the
         * query string, assuming we have one.
         */
        valgrind_report_error_query(debug_query_string);

        /*
         * Make sure debug_query_string gets reset before we possibly clobber
         * the storage it points at.
         */
        debug_query_string = NULL;

        /*
         * Abort the current transaction in order to recover.
         */
        AbortCurrentTransaction();

        if (am_walsender)
            WalSndErrorCleanup();

        PortalErrorCleanup();

        /*
         * We can't release replication slots inside AbortTransaction() as we
         * need to be able to start and abort transactions while having a slot
         * acquired. But we never need to hold them across top level errors,
         * so releasing here is fine. There also is a before_shmem_exit()
         * callback ensuring correct cleanup on FATAL errors.
         */
        if (MyReplicationSlot != NULL)
            ReplicationSlotRelease();

        /* We also want to cleanup temporary slots on error. */
        ReplicationSlotCleanup();

        jit_reset_after_error();

        /*
         * Now return to normal top-level context and clear ErrorContext for
         * next time.
         */
        MemoryContextSwitchTo(TopMemoryContext);
        FlushErrorState();

        /*
         * If we were handling an extended-query-protocol message, initiate
         * skip till next Sync.  This also causes us not to issue
         * ReadyForQuery (until we get Sync).
         */
        if (doing_extended_query_message)
            ignore_till_sync = true;

        /* We don't have a transaction command open anymore */
        xact_started = false;

        /*
         * If an error occurred while we were reading a message from the
         * client, we have potentially lost track of where the previous
         * message ends and the next one begins.  Even though we have
         * otherwise recovered from the error, we cannot safely read any more
         * messages from the client, so there isn't much we can do with the
         * connection anymore.
         */
        if (pq_is_reading_msg())
            ereport(FATAL,
	                (errcode(ERRCODE_PROTOCOL_VIOLATION),
	                 errmsg("terminating connection because protocol synchronization was lost")));

        /* Now we can allow interrupts again */
        RESUME_INTERRUPTS();
    }

    /* We can now handle ereport(ERROR) */
    PG_exception_stack = &local_sigjmp_buf;

#endif // sjlj

    if (!ignore_till_sync)
        send_ready_for_query = true;	/* initially, or after error */

    if (!inloop) {
        inloop = true;
        PDEBUG("# 335: REPL(initdb-single):Begin " __FILE__ );

        while (repl) { interactive_file(); }
    } else {
        // signal error
        optind = -1;
    }

    fclose(single_mode_feed);
/*
    if (strlen(getenv("REPL")) && getenv("REPL")[0]=='Y') {
        PDEBUG("# 346: REPL(initdb-single):End " __FILE__ );

        // now use stdin as source
        repl = true;
        single_mode_feed = NULL;

        force_echo = true;

        if (is_embed) {
#if PGDEBUG
            fprintf(stdout,"# 551: now in webloop(RAF)\npg> %c\n", 4);
#endif
            emscripten_set_main_loop( (em_callback_func)interactive_one, 0, 0);
        } else {

            PDEBUG("# 361: REPL(single after initdb):Begin(NORETURN)");
            while (repl) { interactive_file(); }
            PDEBUG("# 363: REPL:End Raising a 'RuntimeError Exception' to halt program NOW");
            {
                void (*npe)() = NULL;
                npe();
            }
        }

        // unreachable.
    }
*/

    PDEBUG("# 374: no line-repl requested, exiting and keeping runtime alive");
}




void
AsyncPostgresSingleUserMain(int argc, char *argv[], const char *username, int async_restart)
{
	const char *dbname = NULL;
PDEBUG("# 80");

	/* Initialize startup process environment. */
	InitStandaloneProcess(argv[0]);

	/* Set default values for command-line options.	 */
	InitializeGUCOptions();

	/* Parse command-line options. */
	process_postgres_switches(argc, argv, PGC_POSTMASTER, &dbname);

	/* Must have gotten a database name, or have a default (the username) */
	if (dbname == NULL)
	{
		dbname = username;
		if (dbname == NULL)
			ereport(FATAL,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("%s: no database nor user name specified",
							progname)));
	}

if (async_restart) goto async_db_change;

	/* Acquire configuration parameters */
	if (!SelectConfigFiles(userDoption, progname)) {
        proc_exit(1);
    }

	checkDataDir();
	ChangeToDataDir();

	/*
	 * Create lockfile for data directory.
	 */
	CreateDataDirLockFile(false);

	/* read control file (error checking and contains config ) */
	LocalProcessControlFile(false);

	/*
	 * process any libraries that should be preloaded at postmaster start
	 */
	process_shared_preload_libraries();

	/* Initialize MaxBackends */
	InitializeMaxBackends();
PDEBUG("# 127"); /* on_shmem_exit stubs call start here */
	/*
	 * Give preloaded libraries a chance to request additional shared memory.
	 */
	process_shmem_requests();

	/*
	 * Now that loadable modules have had their chance to request additional
	 * shared memory, determine the value of any runtime-computed GUCs that
	 * depend on the amount of shared memory required.
	 */
	InitializeShmemGUCs();

	/*
	 * Now that modules have been loaded, we can process any custom resource
	 * managers specified in the wal_consistency_checking GUC.
	 */
	InitializeWalConsistencyChecking();

	CreateSharedMemoryAndSemaphores();

	/*
	 * Remember stand-alone backend startup time,roughly at the same point
	 * during startup that postmaster does so.
	 */
	PgStartTime = GetCurrentTimestamp();

	/*
	 * Create a per-backend PGPROC struct in shared memory. We must do this
	 * before we can use LWLocks.
	 */
	InitProcess();

// main
	SetProcessingMode(InitProcessing);

	/* Early initialization */
	BaseInit();
async_db_change:;

PDEBUG("# 167");
	/*
	 * General initialization.
	 *
	 * NOTE: if you are tempted to add code in this vicinity, consider putting
	 * it inside InitPostgres() instead.  In particular, anything that
	 * involves database access should be there, not here.
	 */
	InitPostgres(dbname, InvalidOid,	/* database to connect to */
				 username, InvalidOid,	/* role to connect as */
                 (!am_walsender) ? INIT_PG_LOAD_SESSION_LIBS : 0,
				 NULL);			/* no out_dbname */

	/*
	 * If the PostmasterContext is still around, recycle the space; we don't
	 * need it anymore after InitPostgres completes.  Note this does not trash
	 * *MyProcPort, because ConnCreate() allocated that space with malloc()
	 * ... else we'd need to copy the Port data first.  Also, subsidiary data
	 * such as the username isn't lost either; see ProcessStartupPacket().
	 */
	if (PostmasterContext)
	{
		MemoryContextDelete(PostmasterContext);
		PostmasterContext = NULL;
	}

	SetProcessingMode(NormalProcessing);

	/*
	 * Now all GUC states are fully set up.  Report them to client if
	 * appropriate.
	 */
	BeginReportingGUCOptions();

	/*
	 * Also set up handler to log session end; we have to wait till now to be
	 * sure Log_disconnections has its final value.
	 */
	if (IsUnderPostmaster && Log_disconnections)
		on_proc_exit(log_disconnections, 0);

	pgstat_report_connect(MyDatabaseId);

	/* Perform initialization specific to a WAL sender process. */
	if (am_walsender)
		InitWalSender();

	/*
	 * Send this backend's cancellation info to the frontend.
	 */
	if (whereToSendOutput == DestRemote)
	{
		StringInfoData buf;

		pq_beginmessage(&buf, 'K');
		pq_sendint32(&buf, (int32) MyProcPid);
		pq_sendint32(&buf, (int32) MyCancelKey);
		pq_endmessage(&buf);
		/* Need not flush since ReadyForQuery will do it. */
	}

	/* Welcome banner for standalone case */
	if (whereToSendOutput == DestDebug)
		printf("\nPostgreSQL stand-alone backend %s\n", PG_VERSION);

	/*
	 * Create the memory context we will use in the main loop.
	 *
	 * MessageContext is reset once per iteration of the main loop, ie, upon
	 * completion of processing of each command message from the client.
	 */
	MessageContext = AllocSetContextCreate(TopMemoryContext, "MessageContext", ALLOCSET_DEFAULT_SIZES);

	/*
	 * Create memory context and buffer used for RowDescription messages. As
	 * SendRowDescriptionMessage(), via exec_describe_statement_message(), is
	 * frequently executed for ever single statement, we don't want to
	 * allocate a separate buffer every time.
	 */
	row_description_context = AllocSetContextCreate(TopMemoryContext, "RowDescriptionContext", ALLOCSET_DEFAULT_SIZES);
	MemoryContextSwitchTo(row_description_context);
	initStringInfo(&row_description_buf);
	MemoryContextSwitchTo(TopMemoryContext);
} // AsyncPostgresSingleUserMain


