#if defined(PG_MAIN)

EMSCRIPTEN_KEEPALIVE void interactive_one();
/* exported from postmaster.h */
EMSCRIPTEN_KEEPALIVE const char * progname;

void
PostgresMain(const char *dbname, const char *username)
{
    //(void)CurrentMemoryContext;
    puts("ERROR: PostgresMain should not be called anymore" __FILE__ );
    //abort();
}


volatile bool send_ready_for_query = true;
volatile bool idle_in_transaction_timeout_enabled = false;
volatile bool idle_session_timeout_enabled = false;
volatile sigjmp_buf local_sigjmp_buf;

volatile bool repl = true ;
volatile int pg_idb_status = 0;

EMSCRIPTEN_KEEPALIVE void
pg_initdb() {
    puts("pg_initdb called");
}

EMSCRIPTEN_KEEPALIVE void
pg_initdb_repl(const char* std_in, const char* std_out, const char* std_err, const char* js_handler) {
    printf("in=%s out=%s err=%s js=%s\n", std_in, std_out, std_err, js_handler);
}

EMSCRIPTEN_KEEPALIVE void
pg_initdb_start() {
    pg_idb_status++;
}

EMSCRIPTEN_KEEPALIVE int
pg_isready() {
    return pg_idb_status;

}


EMSCRIPTEN_KEEPALIVE void
interactive_one() {
	int			firstchar;
	int			c;				/* character read from getc() */
	StringInfoData input_message;
	StringInfoData *inBuf;

	/*
	 * At top of loop, reset extended-query-message flag, so that any
	 * errors encountered in "idle" state don't provoke skip.
	 */
	doing_extended_query_message = false;
#if 0
	/*
	 * For valgrind reporting purposes, the "current query" begins here.
	 */
#ifdef USE_VALGRIND
	old_valgrind_error_count = VALGRIND_COUNT_ERRORS;
#endif
#endif

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
// ============== firstchar = InteractiveBackend(&input_message); ========
	/*
	 * display a prompt and obtain input from the user
	 */
	printf("pg> ");
	fflush(stdout);
	resetStringInfo(inBuf);
	while ((c = interactive_getc()) != EOF)
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
// =======================================================================
    }

	if (ignore_till_sync && firstchar != EOF)
		return;

	switch (firstchar)
	{
		case 'Q':			/* simple query */
			{
				const char *query_string;

				/* Set statement_timestamp() */
				SetCurrentStatementStartTimestamp();
//puts("# 191");
				query_string = pq_getmsgstring(&input_message);
				pq_getmsgend(&input_message);

				if (am_walsender)
				{
					if (!exec_replication_command(query_string))
						exec_simple_query(query_string);
				}
				else
					exec_simple_query(query_string);
//puts("# 202");
				//valgrind_report_error_query(query_string);

				send_ready_for_query = true;
			}
			break;

		case 'P':			/* parse */
			{
				const char *stmt_name;
				const char *query_string;
				int			numParams;
				Oid		   *paramTypes = NULL;

				forbidden_in_wal_sender(firstchar);

				/* Set statement_timestamp() */
				SetCurrentStatementStartTimestamp();

				stmt_name = pq_getmsgstring(&input_message);
				query_string = pq_getmsgstring(&input_message);
				numParams = pq_getmsgint(&input_message, 2);
				if (numParams > 0)
				{
					paramTypes = palloc_array(Oid, numParams);
					for (int i = 0; i < numParams; i++)
						paramTypes[i] = pq_getmsgint(&input_message, 4);
				}
				pq_getmsgend(&input_message);

				exec_parse_message(query_string, stmt_name,
								   paramTypes, numParams);

				//valgrind_report_error_query(query_string);
			}
			break;

		case 'B':			/* bind */
			forbidden_in_wal_sender(firstchar);

			/* Set statement_timestamp() */
			SetCurrentStatementStartTimestamp();

			/*
			 * this message is complex enough that it seems best to put
			 * the field extraction out-of-line
			 */
			exec_bind_message(&input_message);

			/* exec_bind_message does valgrind_report_error_query */
			break;

		case 'E':			/* execute */
			{
				const char *portal_name;
				int			max_rows;

				forbidden_in_wal_sender(firstchar);

				/* Set statement_timestamp() */
				SetCurrentStatementStartTimestamp();

				portal_name = pq_getmsgstring(&input_message);
				max_rows = pq_getmsgint(&input_message, 4);
				pq_getmsgend(&input_message);

				exec_execute_message(portal_name, max_rows);

				/* exec_execute_message does valgrind_report_error_query */
			}
			break;

		case 'F':			/* fastpath function call */
			forbidden_in_wal_sender(firstchar);

			/* Set statement_timestamp() */
			SetCurrentStatementStartTimestamp();

			/* Report query to various monitoring facilities. */
			pgstat_report_activity(STATE_FASTPATH, NULL);
			set_ps_display("<FASTPATH>");

			/* start an xact for this function invocation */
			start_xact_command();

			/*
			 * Note: we may at this point be inside an aborted
			 * transaction.  We can't throw error for that until we've
			 * finished reading the function-call message, so
			 * HandleFunctionRequest() must check for it after doing so.
			 * Be careful not to do anything that assumes we're inside a
			 * valid transaction here.
			 */

			/* switch back to message context */
			MemoryContextSwitchTo(MessageContext);

			HandleFunctionRequest(&input_message);

			/* commit the function-invocation transaction */
			finish_xact_command();

		    // valgrind_report_error_query("fastpath function call");

			send_ready_for_query = true;
			break;

		case 'C':			/* close */
			{
				int			close_type;
				const char *close_target;

				forbidden_in_wal_sender(firstchar);

				close_type = pq_getmsgbyte(&input_message);
				close_target = pq_getmsgstring(&input_message);
				pq_getmsgend(&input_message);

				switch (close_type)
				{
					case 'S':
						if (close_target[0] != '\0')
							DropPreparedStatement(close_target, false);
						else
						{
							/* special-case the unnamed statement */
							drop_unnamed_stmt();
						}
						break;
					case 'P':
						{
							Portal		portal;

							portal = GetPortalByName(close_target);
							if (PortalIsValid(portal))
								PortalDrop(portal, false);
						}
						break;
					default:
						ereport(ERROR,
								(errcode(ERRCODE_PROTOCOL_VIOLATION),
								 errmsg("invalid CLOSE message subtype %d",
										close_type)));
						break;
				}

				if (whereToSendOutput == DestRemote)
					pq_putemptymessage('3');	/* CloseComplete */

				//valgrind_report_error_query("CLOSE message");
			}
			break;

		case 'D':			/* describe */
			{
				int			describe_type;
				const char *describe_target;

				forbidden_in_wal_sender(firstchar);

				/* Set statement_timestamp() (needed for xact) */
				SetCurrentStatementStartTimestamp();

				describe_type = pq_getmsgbyte(&input_message);
				describe_target = pq_getmsgstring(&input_message);
				pq_getmsgend(&input_message);

				switch (describe_type)
				{
					case 'S':
						exec_describe_statement_message(describe_target);
						break;
					case 'P':
						exec_describe_portal_message(describe_target);
						break;
					default:
						ereport(ERROR,
								(errcode(ERRCODE_PROTOCOL_VIOLATION),
								 errmsg("invalid DESCRIBE message subtype %d",
										describe_type)));
						break;
				}

				// valgrind_report_error_query("DESCRIBE message");
			}
			break;

		case 'H':			/* flush */
			pq_getmsgend(&input_message);
			if (whereToSendOutput == DestRemote)
				pq_flush();
			break;

		case 'S':			/* sync */
			pq_getmsgend(&input_message);
			finish_xact_command();
			//valgrind_report_error_query("SYNC message");
			send_ready_for_query = true;
			break;

			/*
			 * 'X' means that the frontend is closing down the socket. EOF
			 * means unexpected loss of frontend connection. Either way,
			 * perform normal shutdown.
			 */
		case EOF:

			/* for the cumulative statistics system */
			pgStatSessionEndCause = DISCONNECT_CLIENT_EOF;

			/* FALLTHROUGH */

		case 'X':

			/*
			 * Reset whereToSendOutput to prevent ereport from attempting
			 * to send any more messages to client.
			 */
			if (whereToSendOutput == DestRemote)
				whereToSendOutput = DestNone;

			/*
			 * NOTE: if you are tempted to add more code here, DON'T!
			 * Whatever you had in mind to do should be set up as an
			 * on_proc_exit or on_shmem_exit callback, instead. Otherwise
			 * it will fail to be called during other backend-shutdown
			 * scenarios.
			 */
puts("# 849 proc_exit"); //proc_exit(0);
            repl = false;
            return;

		case 'd':			/* copy data */
		case 'c':			/* copy done */
		case 'f':			/* copy fail */

			/*
			 * Accept but ignore these messages, per protocol spec; we
			 * probably got here because a COPY failed, and the frontend
			 * is still sending data.
			 */
			break;

		default:
			ereport(FATAL,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("invalid frontend message type %d",
							firstchar)));
	}
}



void
PostgresSingleUserMain(int argc, char *argv[],
					   const char *username)
{
	const char *dbname = NULL;
	// sigjmp_buf	local_sigjmp_buf;

	/* these must be volatile to ensure state is preserved across longjmp:
	volatile bool send_ready_for_query = true;
	volatile bool idle_in_transaction_timeout_enabled = false;
	volatile bool idle_session_timeout_enabled = false;
*/

	Assert(!IsUnderPostmaster);

	progname = get_progname(argv[0]);

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

	/* Acquire configuration parameters */
	if (!SelectConfigFiles(userDoption, progname))
		proc_exit(1);

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
#if 0
	/*
	 * Set up signal handlers.  (InitPostmasterChild or InitStandaloneProcess
	 * has already set up BlockSig and made that the active signal mask.)
	 *
	 * Note that postmaster blocked all signals before forking child process,
	 * so there is no race condition whereby we might receive a signal before
	 * we have set up the handler.
	 *
	 * Also note: it's best not to use any signals that are SIG_IGNored in the
	 * postmaster.  If such a signal arrives before we are able to change the
	 * handler to non-SIG_IGN, it'll get dropped.  Instead, make a dummy
	 * handler in the postmaster to reserve the signal. (Of course, this isn't
	 * an issue for signals that are locally generated, such as SIGALRM and
	 * SIGPIPE.)
	 */
	if (am_walsender)
		WalSndSignals();
	else
	{
		pqsignal(SIGHUP, SignalHandlerForConfigReload);
		pqsignal(SIGINT, StatementCancelHandler);	/* cancel current query */
		pqsignal(SIGTERM, die); /* cancel current query and exit */

		/*
		 * In a postmaster child backend, replace SignalHandlerForCrashExit
		 * with quickdie, so we can tell the client we're dying.
		 *
		 * In a standalone backend, SIGQUIT can be generated from the keyboard
		 * easily, while SIGTERM cannot, so we make both signals do die()
		 * rather than quickdie().
		 */
		if (IsUnderPostmaster)
			pqsignal(SIGQUIT, quickdie);	/* hard crash time */
		else
			pqsignal(SIGQUIT, die); /* cancel current query and exit */
		InitializeTimeouts();	/* establishes SIGALRM handler */

		/*
		 * Ignore failure to write to frontend. Note: if frontend closes
		 * connection, we will notice it and exit cleanly when control next
		 * returns to outer loop.  This seems safer than forcing exit in the
		 * midst of output during who-knows-what operation...
		 */
		pqsignal(SIGPIPE, SIG_IGN);
		pqsignal(SIGUSR1, procsignal_sigusr1_handler);
		pqsignal(SIGUSR2, SIG_IGN);
		pqsignal(SIGFPE, FloatExceptionHandler);

		/*
		 * Reset some signals that are accepted by postmaster but not by
		 * backend
		 */
		pqsignal(SIGCHLD, SIG_DFL); /* system() requires this on some
									 * platforms */
	}
#endif
	/* Early initialization */
	BaseInit();
#if 0
	/* We need to allow SIGINT, etc during the initial transaction */
	sigprocmask(SIG_SETMASK, &UnBlockSig, NULL);
#endif
	/*
	 * General initialization.
	 *
	 * NOTE: if you are tempted to add code in this vicinity, consider putting
	 * it inside InitPostgres() instead.  In particular, anything that
	 * involves database access should be there, not here.
	 */
	InitPostgres(dbname, InvalidOid,	/* database to connect to */
				 username, InvalidOid,	/* role to connect as */
				 !am_walsender, /* honor session_preload_libraries? */
				 false,			/* don't ignore datallowconn */
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

	/*
	 * POSTGRES main processing loop begins here
	 *
	 * If an exception is encountered, processing resumes here so we abort the
	 * current transaction and start a new one.
	 *
	 * You might wonder why this isn't coded as an infinite loop around a
	 * PG_TRY construct.  The reason is that this is the bottom of the
	 * exception stack, and so with PG_TRY there would be no exception handler
	 * in force at all during the CATCH part.  By leaving the outermost setjmp
	 * always active, we have at least some chance of recovering from an error
	 * during error recovery.  (If we get into an infinite loop thereby, it
	 * will soon be stopped by overflow of elog.c's internal state stack.)
	 *
	 * Note that we use sigsetjmp(..., 1), so that this function's signal mask
	 * (to wit, UnBlockSig) will be restored when longjmp'ing to here.  This
	 * is essential in case we longjmp'd out of a signal handler on a platform
	 * where that leaves the signal blocked.  It's not redundant with the
	 * unblock in AbortTransaction() because the latter is only called if we
	 * were inside a transaction.
	 */

#if 1
#if 1
	if (sigsetjmp(local_sigjmp_buf, 1) != 0)
#endif
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

	if (!ignore_till_sync)
		send_ready_for_query = true;	/* initially, or after error */

#endif

	/*
	 * Non-error queries loop here.
	 */
puts("# 405: REPL:Begin" __FILE__ );
//    emscripten_set_main_loop( (em_callback_func)interactive_one, 0, 1);

	while (repl)
	{
        interactive_one();
puts("\n");
	}							/* end of input-reading loop */

puts("\n\nREPL:End " __FILE__);
abort();
}



#else


#if defined(__EMSCRIPTEN__) || defined(__wasi__)
#include <unistd.h>        /* chdir */
#include <sys/stat.h>      /* mkdir */
static
void mkdirp(const char *p) {
	if (!mkdir(p, 0700)) {
		fprintf(stderr, "# no '%s' directory, creating one ...\n", p);
	}
}
#endif /* wasm */


int
main(int argc, char *argv[])
{
/*
TODO:
	postgres.js:6382 warning: unsupported syscall: __syscall_prlimit64
*/
    int ret;

    if (access("/etc/fstab", F_OK) == 0) {
    	setenv("ENVIRONMENT", "node" , 1);
    } else {
    	setenv("ENVIRONMENT", "web" , 1);
    }

	#define PGDB WASM_PREFIX "/base"
	argv[0] = strdup(WASM_PREFIX "/bin/postgres");


	// postgres does not know where to find the server configuration file.
	// postgres.js:1605 You must specify the --config-file or -D invocation option or set the PGDATA environment variable.
	// ?? setenv("PGDATABASE", WASM_PREFIX "/db" , 1 );
	setenv("PGSYSCONFDIR", WASM_PREFIX, 1);
	setenv("PGCLIENTENCODING", "UTF8", 1);

	/* default username */
	setenv("PGUSER", WASM_USERNAME , 0);

	/* default path */
	setenv("PGDATA", PGDB , 0);


    printf("# argv0 (%s) PGDATA=%s\n PGUSER=%s ", argv[0], getenv("PGUSER"), getenv("PGDATA"));


puts("# ============= env dump ==================");
  for (char **env = environ; *env != 0; env++)
  {
    char *drefp = *env;
    printf("# %s\n", drefp);
  }
puts("# =========================================");
	chdir("/");
	mkdirp(WASM_PREFIX);
	mkdirp(PGDB);
	/*
	mkdirp(WASM_PREFIX "/lib");
	mkdirp(WASM_PREFIX "/lib/postgresql");
	*/
	mkdirp(PGDB "/pg_wal");
	mkdirp(PGDB "/pg_wal/archive_status");
	mkdirp(PGDB "/pg_wal/summaries");

	mkdirp(PGDB "/pg_tblspc");
	mkdirp(PGDB "/pg_snapshots");
	mkdirp(PGDB "/pg_commit_ts");
	mkdirp(PGDB "/pg_notify");
	mkdirp(PGDB "/pg_replslot");
	mkdirp(PGDB "/pg_twophase");


	mkdirp(PGDB "/pg_logical");
	mkdirp(PGDB "/pg_logical/snapshots");
	mkdirp(PGDB "/pg_logical/mappings");


	/*
	PGDATESTYLE
	TZ
	PG_SHMEM_ADDR

	PGCTLTIMEOUT
	PG_TEST_USE_UNIX_SOCKETS
	INITDB_TEMPLATE
	PSQL_HISTORY
	TMPDIR
	PGOPTIONS
	*/

	//reached_main = true;

	progname = get_progname(argv[0]);

	/*
	 * Platform-specific startup hacks
	 */
	startup_hacks(progname);

	/*
	 * Remember the physical location of the initially given argv[] array for
	 * possible use by ps display.  On some platforms, the argv[] storage must
	 * be overwritten in order to set the process title for ps. In such cases
	 * save_ps_display_args makes and returns a new copy of the argv[] array.
	 *
	 * save_ps_display_args may also move the environment strings to make
	 * extra room. Therefore this should be done as early as possible during
	 * startup, to avoid entanglements with code that might save a getenv()
	 * result pointer.
	 */
	argv = save_ps_display_args(argc, argv);

	/*
	 * Fire up essential subsystems: error and memory management
	 *
	 * Code after this point is allowed to use elog/ereport, though
	 * localization of messages may not work right away, and messages won't go
	 * anywhere but stderr until GUC settings get loaded.
	 */
	MemoryContextInit();

	/*
	 * Set up locale information
	 */
	set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("postgres"));

	/*
	 * In the postmaster, absorb the environment values for LC_COLLATE and
	 * LC_CTYPE.  Individual backends will change these later to settings
	 * taken from pg_database, but the postmaster cannot do that.  If we leave
	 * these set to "C" then message localization might not work well in the
	 * postmaster.
	 */
	init_locale("LC_COLLATE", LC_COLLATE, "");
	init_locale("LC_CTYPE", LC_CTYPE, "");

	/*
	 * LC_MESSAGES will get set later during GUC option processing, but we set
	 * it here to allow startup error messages to be localized.
	 */
#ifdef LC_MESSAGES
	init_locale("LC_MESSAGES", LC_MESSAGES, "");
#endif

	/*
	 * We keep these set to "C" always, except transiently in pg_locale.c; see
	 * that file for explanations.
	 */
	init_locale("LC_MONETARY", LC_MONETARY, "C");
	init_locale("LC_NUMERIC", LC_NUMERIC, "C");
	init_locale("LC_TIME", LC_TIME, "C");

	/*
	 * Now that we have absorbed as much as we wish to from the locale
	 * environment, remove any LC_ALL setting, so that the environment
	 * variables installed by pg_perm_setlocale have force.
	 */
	unsetenv("LC_ALL");

	/*
	 * Catch standard options before doing much else, in particular before we
	 * insist on not being root.
	 */
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			help(progname);
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			fputs(PG_BACKEND_VERSIONSTR, stdout);
			exit(0);
		}

	}

	if (argc > 1 && strcmp(argv[1], "--check") == 0)
		return BootstrapModeMain(argc, argv, true);


    if (argc > 1 && strcmp(argv[1], "--boot") == 0) {
        puts("235: boot: " __FILE__ );
        return BootstrapModeMain(argc, argv, false);
    }

    puts("# 170: single: " __FILE__ );
    PostgresSingleUserMain(argc, argv, strdup( getenv("PGUSER")));

    puts("# 173: " __FILE__);
    emscripten_force_exit(ret);
	return ret;
}

#endif // PG_MAIN
