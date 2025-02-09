#define PGL_MAIN
#define PGL_INITDB_MAIN

// MEMFS files for os pipe simulation
#define IDB_PIPE_BOOT "/tmp/initdb.boot.txt"
#define IDB_PIPE_SINGLE "/tmp/initdb.single.txt"

#include "pgl_os.h"

// ----------------------- pglite ----------------------------
#include "postgres.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"
#include "tcop/tcopprot.h"

#include <unistd.h>        /* chdir */
#include <sys/stat.h>      /* mkdir */

// globals

int g_argc;
char **g_argv;
extern char ** environ;

const char *PREFIX;
const char *PGDATA;
const char * progname;

volatile bool is_repl = true;
volatile bool is_node = true;
volatile bool is_embed = false;
volatile int pg_idb_status;


#define IDB_OK  0b11111110
#define IDB_FAILED  0b0001
#define IDB_CALLED  0b0010
#define IDB_HASDB   0b0100
#define IDB_HASUSER 0b1000


#define WASM_PGDATA WASM_PREFIX "/base"
#define CMA_FD 1

extern bool IsPostmasterEnvironment;

#define help(name) name

#define BREAKV(x) { printf("BREAKV : %d\n",__LINE__);return x; }
#define BREAK { printf("BREAK : %d\n",__LINE__);return; }


extern int pgl_initdb_main(void);
extern void pg_proc_exit(int code);
extern int BootstrapModeMain(int, char **, int);


// PostgresSingleUserMain / PostgresMain

#include "miscadmin.h"
#include "access/xlog.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "utils/timestamp.h"
#include "utils/guc.h"
#include "pgstat.h"
#include "replication/walsender.h"
#include "libpq/pqformat.h"




volatile bool send_ready_for_query = true;
volatile bool idle_in_transaction_timeout_enabled = false;
volatile bool idle_session_timeout_enabled = false;
/*
bool quote_all_identifiers = false;
FILE* SOCKET_FILE = NULL;
int SOCKET_DATA = 0;
*/

#include "../backend/tcop/postgres.c"


// initdb + start on fd (pipe emulation)


static bool force_echo = false;


#include "pgl_mains.c"

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



#include "pgl_stubs.h"

#include "pgl_tools.h"

#include "pgl_initdb.c"


// interactive_one


/* TODO : prevent multiple write and write while reading ? */
volatile int cma_wsize = 0;
volatile int cma_rsize = 0;  // defined in postgres.c



__attribute__((export_name("interactive_read")))
int
interactive_read() {
    /* should cma_rsize should be reset here ? */
    return cma_wsize;
}


#if defined(__wasi__)
#   include "./interactive_one_wasi.c"
#else
#   include "./interactive_one_emsdk.c"
#endif




















static void
main_pre(int argc, char *argv[]) {


    char key[256];
    int i=0;
// extra env is always after normal args
    PDEBUG("# ============= extra argv dump ==================");
    {
        for (;i<argc;i++) {
            const char *kv = argv[i];
            for (int sk=0;sk<strlen(kv);sk++)
                if(kv[sk]=='=')
                    goto extra_env;
#if PGDEBUG
            printf("%s ", kv);
#endif
        }
    }
extra_env:;
    PDEBUG("\n# ============= arg->env dump ==================");
    {
        for (;i<argc;i++) {
            const char *kv = argv[i];
            for (int sk=0;sk<strlen(kv);sk++) {
                if (sk>255) {
                    puts("buffer overrun on extra env at:");
                    puts(kv);
                    continue;
                }
                if (kv[sk]=='=') {
                    memcpy(key, kv, sk);
                    key[sk] = 0;
#if PGDEBUG
                    printf("%s='%s'\n", &(key[0]), &(kv[sk+1]));
#endif
                    setenv(key, &kv[sk+1], 1);
                }
            }
        }
    }

    {
        // set default
        setenv("PREFIX", WASM_PREFIX, 0);
        PREFIX = getenv("PREFIX");
        argv[0] = strcat_alloc( PREFIX, "/bin/postgres");
    }


    {
        // set defautl
    	setenv("PGDATA", WASM_PGDATA , 0);
        PGDATA = getenv("PGDATA");
#if PGDEBUG
puts("    ----------- test ----------------" );
        puts(PGDATA);
puts("    ----------- /test ----------------" );
#endif
    }


#if defined(__EMSCRIPTEN__)
    EM_ASM({
        Module.is_worker = (typeof WorkerGlobalScope !== 'undefined') && self instanceof WorkerGlobalScope;
        Module.FD_BUFFER_MAX = $0;
        Module.emscripten_copy_to = console.warn;
    }, (CMA_MB*1024*1024) / CMA_FD);  /* ( global mem start / num fd max ) */

    if (is_node) {
    	setenv("ENVIRONMENT", "node" , 1);
        EM_ASM({
#if PGDEBUG
            console.warn("prerun(C-node) worker=", Module.is_worker);
#endif
            Module['postMessage'] = function custom_postMessage(event) {
                console.log("# pg_main_emsdk.c:544: onCustomMessage:", event);
            };
        });

    } else {
    	setenv("ENVIRONMENT", "web" , 1);
#if PGDEBUG
        EM_ASM({
            console.warn("prerun(C-web) worker=", Module.is_worker);
        });
#endif
        is_repl = true;
    }

    EM_ASM({
        if (Module.is_worker) {
#if PGDEBUG
            console.log("Main: running in a worker, setting onCustomMessage");
#endif
            function onCustomMessage(event) {
                console.log("onCustomMessage:", event);
            };
            Module['onCustomMessage'] = onCustomMessage;
        } else {
#if PGDEBUG
            console.log("Running in main thread, faking onCustomMessage");
#endif
            Module['postMessage'] = function custom_postMessage(event) {
                switch (event.type) {
                    case "raw" :  {
                        //stringToUTF8( event.data, shm_rawinput, Module.FD_BUFFER_MAX);
                        break;
                    }

                    case "stdin" :  {
                        stringToUTF8( event.data, 1, Module.FD_BUFFER_MAX);
                        break;
                    }
                    case "rcon" :  {
                        //stringToUTF8( event.data, shm_rcon, Module.FD_BUFFER_MAX);
                        break;
                    }
                    default : console.warn("custom_postMessage?", event);
                }
            };
            //if (!window.vm)
              //  window.vm = Module;
        };
    });

#endif // __EMSCRIPTEN__
	chdir("/");
    mkdirp("/tmp");
    mkdirp(PREFIX);

	// postgres does not know where to find the server configuration file.
    // also we store the fake locale file there.
	// postgres.js:1605 You must specify the --config-file or -D invocation option or set the PGDATA environment variable.

    /* enforce ? */
	setenv("PGSYSCONFDIR", PREFIX, 1);
	setenv("PGCLIENTENCODING", "UTF8", 1);

    // default is to run a repl loop
    setenv("REPL", "Y", 0);
/*
 * we cannot run "locale -a" either from web or node. the file getenv("PGSYSCONFDIR") / "locale"
 * serves as popen output
 */

	setenv("LC_CTYPE", "C" , 1);

    /* defaults */

    setenv("TZ", "UTC", 0);
    setenv("PGTZ", "UTC", 0);
	setenv("PGUSER", WASM_USERNAME , 0);
	setenv("PGDATABASE", "template1" , 0);
    setenv("PG_COLOR", "always", 0);

#if PGDEBUG
    puts("# ============= env dump ==================");
    for (char **env = environ; *env != 0; env++) {
        char *drefp = *env;
        printf("# %s\n", drefp);
    }
    puts("# =========================================");
#endif
} // main_pre



void main_post() {
PDEBUG("# 622: main_post()");
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
        set_pglocale_pgservice(g_argv[0], PG_TEXTDOMAIN("postgres"));

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
} // main_post



#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#else
__attribute__((export_name("pg_initdb")))
#endif
int
pg_initdb() {
    PDEBUG("# 695: pg_initdb()");
    optind = 1;
    int async_restart = 1;
    pg_idb_status |= IDB_FAILED;

    if (!chdir(PGDATA)){
        if (access("PG_VERSION", F_OK) == 0) {
        	chdir("/");

            pg_idb_status |= IDB_HASDB;

            /* assume auth success for now */
            pg_idb_status |= IDB_HASUSER;
#if PGDEBUG
            printf("# 202: pg_initdb: db exists at : %s TODO: test for db name : %s \n", getenv("PGDATA"), getenv("PGDATABASE"));
//            print_bits(sizeof(pg_idb_status), &pg_idb_status);
#endif // PGDEBUG
            main_post();

            async_restart = 0;
            {
                char *single_argv[] = {
                    g_argv[0],
                    "--single",
                    "-d", "1", "-B", "16", "-S", "512", "-f", "siobtnmh",
                    "-D", getenv("PGDATA"),
                    "-F", "-O", "-j",
                    WASM_PGOPTS,
                    getenv("PGDATABASE"),
                    NULL
                };
                int single_argc = sizeof(single_argv) / sizeof(char*) - 1;
                optind = 1;
                AsyncPostgresSingleUserMain(single_argc, single_argv, strdup(getenv("PGUSER")), async_restart);
            }

            goto initdb_done;
        }
    	chdir("/");
#if PGDEBUG
        printf("pg_initdb: no db found at : %s\n", getenv("PGDATA") );
#endif // PGDEBUG
    }


#if PGDEBUG
    PDEBUG("# 589:" __FILE__);
    printf("# pgl_initdb_main result = %d\n", pgl_initdb_main() );
#else
    pgl_initdb_main();
#endif // PGDEBUG
    PDEBUG("# 594:" __FILE__);
    /* save stdin and use previous initdb output to feed boot mode */
    int saved_stdin = dup(STDIN_FILENO);
    {
        PDEBUG("# 598: restarting in boot mode for initdb");
        freopen(IDB_PIPE_BOOT, "r", stdin);

        char *boot_argv[] = {
            g_argv[0],
            "--boot",
            "-D", getenv("PGDATA"),
            "-d","3",
            WASM_PGOPTS,
            "-X", "1048576",
            NULL
        };
        int boot_argc = sizeof(boot_argv) / sizeof(char*) - 1;

	    set_pglocale_pgservice(boot_argv[0], PG_TEXTDOMAIN("initdb"));

        optind = 1;
        BootstrapModeMain(boot_argc, boot_argv, false);
        fclose(stdin);
#if PGDEBUG
        puts("# 618: keep " IDB_PIPE_BOOT );
#else
        remove(IDB_PIPE_BOOT);
#endif
        stdin = fdopen(saved_stdin, "r");
        /* fake a shutdown to comlplete WAL/OID states */
        pg_proc_exit(66);
    }

    /* use previous initdb output to feed single mode */


    /* or resume a previous db */
    //IsPostmasterEnvironment = true;
    if (ShmemVariableCache->nextOid < ((Oid) FirstNormalObjectId)) {
#if PGDEBUG
        puts("# 891: warning oid base too low, will need to set OID range after initdb(bootstrap/single)");
#endif
    }

    {
        PDEBUG("# 791: restarting in single mode for initdb");

        char *single_argv[] = {
            WASM_PREFIX "/bin/postgres",
            "--single",
            "-d", "1", "-B", "16", "-S", "512", "-f", "siobtnmh",
            "-D", getenv("PGDATA"),
            "-F", "-O", "-j",
            WASM_PGOPTS,
            "template1",
            NULL
        };
        int single_argc = sizeof(single_argv) / sizeof(char*) - 1;
        optind = 1;
        RePostgresSingleUserMain(single_argc, single_argv, strdup( getenv("PGUSER")));
    }

initdb_done:;
    pg_idb_status |= IDB_CALLED;
    IsPostmasterEnvironment = true;
    if (ShmemVariableCache->nextOid < ((Oid) FirstNormalObjectId)) {
        /* IsPostmasterEnvironment is now true
         these will be executed when required in varsup.c/GetNewObjectId
    	 ShmemVariableCache->nextOid = FirstNormalObjectId;
	     ShmemVariableCache->oidCount = 0;
        */
#if PGDEBUG
        puts("# 922: initdb done, oid base too low but OID range will be set because IsPostmasterEnvironment");
#endif
    }

    if (optind>0) {
        /* RESET getopt */
        optind = 1;
        /* we did not fail, clear the default failed state */
        pg_idb_status &= IDB_OK;
    } else {
        PDEBUG("# exiting on initdb-single error");
        // TODO raise js exception
    }
    return pg_idb_status;
} // pg_initdb





int
main_repl() {
    bool hadloop_error = false;

    whereToSendOutput = DestNone;

    if (!mkdir(WASM_PGDATA, 0700)) {
        /* no db : run initdb now. */
#if PGDEBUG
        fprintf(stderr, "PGDATA=%s not found, running initdb with defaults\n", WASM_PGDATA );
#endif
        #if defined(PG_INITDB_MAIN)
            #warning "web build"
puts("# 851");
            hadloop_error = pg_initdb() & IDB_FAILED;
        #else
            #warning "node build"
            #if defined(__wasi__)
                hadloop_error = pg_initdb() & IDB_FAILED;
            #endif
        #endif

    } else {
        // download a db case ?
    	mkdirp(WASM_PGDATA);

        // db fixup because empty dirs are not packaged
	    /*
	    mkdirp(WASM_PREFIX "/lib");
	    mkdirp(WASM_PREFIX "/lib/postgresql");
	    */
	    mksub_dir(PGDATA, "/pg_wal");
	    mksub_dir(PGDATA, "/pg_wal/archive_status");
	    mksub_dir(PGDATA, "/pg_wal/summaries");

	    mksub_dir(PGDATA, "/pg_tblspc");
	    mksub_dir(PGDATA, "/pg_snapshots");
	    mksub_dir(PGDATA, "/pg_commit_ts");
	    mksub_dir(PGDATA, "/pg_notify");
	    mksub_dir(PGDATA, "/pg_replslot");
	    mksub_dir(PGDATA, "/pg_twophase");


	    mksub_dir(PGDATA, "/pg_logical");
	    mksub_dir(PGDATA, "/pg_logical/snapshots");
	    mksub_dir(PGDATA, "/pg_logical/mappings");

    }

    if (!hadloop_error) {
        main_post();

        /*
         * Catch standard options before doing much else, in particular before we
         * insist on not being root.
         */
        if (g_argc > 1) {
	        if (strcmp(g_argv[1], "--help") == 0 || strcmp(g_argv[1], "-?") == 0)
	        {
		        help(progname);
		        exit(0);
	        }
	        if (strcmp(g_argv[1], "--version") == 0 || strcmp(g_argv[1], "-V") == 0)
	        {
		        fputs(PG_BACKEND_VERSIONSTR, stdout);
		        exit(0);
	        }

        }

        if (g_argc > 1 && strcmp(g_argv[1], "--check") == 0) {
	        BootstrapModeMain(g_argc, g_argv, true);
            return 0;
        }

        if (g_argc > 1 && strcmp(g_argv[1], "--boot") == 0) {
            PDEBUG("# 1410: boot: " __FILE__ );
            BootstrapModeMain(g_argc, g_argv, false);
            return 0;
        }

        PDEBUG("# 1415: single: " __FILE__ );
        AsyncPostgresSingleUserMain(g_argc, g_argv, strdup(getenv("PGUSER")), 0);
    }
    return 0;
}







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




// ???? __attribute__((export_name("_main")))
int
main(int argc, char **argv)
{
    int exit_code = 0;
    main_pre(argc,argv);
#if PGDEBUG
    printf("# 1249: argv0 (%s) PGUSER=%s PGDATA=%s\n PGDATABASE=%s REPL=%s\n",
        argv[0], getenv("PGUSER"), getenv("PGDATA"),  getenv("PGDATABASE"), getenv("REPL") );
#endif
	progname = get_progname(argv[0]);
    startup_hacks(progname);
    g_argv = argv;
    g_argc = argc;

    is_repl = strlen(getenv("REPL")) && getenv("REPL")[0]!='N';
    is_embed = true;

    if (!is_repl) {
        PDEBUG("# 312: exit with live runtime (nodb)");
        return 0;
    }

    PDEBUG("# 970: repl");
    // so it is repl
    main_repl();

    if (is_node) {
        PDEBUG("# 975: node repl");
        pg_repl_raf();
    }

    emscripten_force_exit(exit_code);
	return exit_code;
}
















