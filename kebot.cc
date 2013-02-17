#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include <iostream>

#include <sqlite3.h>
#include <seccomp.h>
#include <glib.h>
#include <v8.h>
#include <libconfig.h++>

#include "kebot.hh"

#define MAXDATASIZE 40960
#define SOURCESIZE 409600

using namespace v8;

char buf[MAXDATASIZE];
char source[SOURCESIZE];
sqlite3 *db;
sqlite3 *volatile_db;
Handle<Script> *scriptp;
gboolean script_retval;
int s, timeout_counter = 0, timeout_max = 5;

typedef enum {
	RETVAL_EXIT,
	RETVAL_DISCONNECT,
	RETVAL_NOCONNECT,
	RETVAL_ERROR,
	RETVAL_FATAL_ERROR,
	RETVAL_RELOAD}
	SessionRetVal;

Handle<Value> XGetter(Local<String>, const AccessorInfo&) {
	return String::New(buf);
}

void retvalSetter(Local<String>, Local<Value> value, const AccessorInfo&) {
		if (value->IsTrue())
			script_retval=TRUE;
		else
			script_retval=FALSE;
}

Handle<Value> retvalGetter(Local<String>, const AccessorInfo&) {
	if (script_retval)
		return v8::True();
	return v8::False();
}

void XSetter(Local<String>, Local<Value>, const AccessorInfo&) {
}

static Handle<Value> ExitCallback(const Arguments& args) {
	if (args.Length() != 1) {
		printf("exit called incorrectly \n");
		exit(RETVAL_FATAL_ERROR);
	}
	int exitval = RETVAL_FATAL_ERROR;
	String::Utf8Value strparam(args[0]);
	printf("EXIT by clientscript %s\n", *strparam);
	if      (!strcmp("EXIT"        , *strparam)) exitval = RETVAL_EXIT;
	else if (!strcmp("DISCONNECT"  , *strparam)) exitval = RETVAL_DISCONNECT;
	else if (!strcmp("NOCONNECT"   , *strparam)) exitval = RETVAL_NOCONNECT;
	else if (!strcmp("ERROR"       , *strparam)) exitval = RETVAL_ERROR;
	else if (!strcmp("FATAL_ERROR" , *strparam)) exitval = RETVAL_FATAL_ERROR;
	else if (!strcmp("RELOAD"      , *strparam)) exitval = RETVAL_RELOAD;
	else {
		printf("Unknown exit value: %s\n", *strparam);
	}
	printf("%d\n", exitval);
	exit(exitval);
	return v8::Undefined();
}

static Handle<Value> LogCallback(const Arguments& args) {
	if (args.Length() < 1) return v8::Undefined();
	HandleScope scope;
	Handle<Value> arg = args[0];
	String::Utf8Value value(arg);
	printf("LOG: %s\n", *value);

	return v8::Undefined();
}

static int sqlite_callback(void *target, int argc, char **argv, char**){
	if (1 <= argc) {
		strncpy((char*)target, argv[0],MAXDATASIZE);
		((char*)target)[MAXDATASIZE-1]=0;
	}
	return 0;
}

static Handle<Value> getDBValue(const Arguments& args) {
	char result[MAXDATASIZE];
	char *errMsg;
	result[0]=0;
	sqlite3 *thisdb=db;
	if (args.Length() < 1) return v8::Undefined();
	if (args.Length() == 2 && args[1]->IsTrue())
		thisdb = volatile_db;
	HandleScope scope;
	Handle<Value> arg = args[0];
	String::Utf8Value value(arg);
	int err = sqlite3_exec(thisdb, *value, sqlite_callback, result, &errMsg);
	if (err)
		printf("Error %s in SQL\n", errMsg);

	return String::New(result);
}

int writes(int fd, const char *a) {
	return write(fd, a, strlen(a));
}

gboolean glib_callback(GIOChannel *source, GIOCondition, gpointer ptr)
{
	if (source) {
		timeout_counter = 0;
		int numbytes=0;
		do {
			if (MAXDATASIZE <= numbytes + 1) exit(1);
			numbytes += read(s,buf+numbytes,MAXDATASIZE-1-numbytes);
		}
		while ('\n' != buf[numbytes-1]);
		buf[numbytes]='\0';
	}
	else {
		strcpy(buf,(char*)ptr);
		free((char*)ptr);
	}
	printf("%s", buf);

	script_retval = TRUE;
	Handle<Value> result = (*scriptp)->Run();

	String::AsciiValue ascii(result);
	printf("ascii:\n%sendascii\n", *ascii);
	writes(s, *ascii);

	return script_retval;
}

gboolean timer_callback(gpointer userdata) {
	return glib_callback(NULL,G_IO_NVAL,userdata);
}

gboolean ping_timeout_callback(gpointer) {
	if (timeout_max < ++timeout_counter)
		exit(RETVAL_DISCONNECT);
	if (timeout_max == timeout_counter)
		return glib_callback(NULL,G_IO_NVAL, (gpointer)strdup("TIMEOUTSOON\n"));
	return TRUE;
}

static Handle<Value> setTimer(const Arguments& args) {
	if (args.Length() < 2) return v8::Undefined();
	int sleepTime = args[0]->Int32Value();
	HandleScope scope;
	Handle<Value> arg = args[1];
	String::Utf8Value value(arg);

	g_timeout_add_seconds (sleepTime, timer_callback, strdup(*value));

	return v8::Undefined();
}

int open_irc_connection(const char *server) {
	int sock, ret;
	struct addrinfo hints, *servinfo;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets

	if ((ret = getaddrinfo(server, "6667",&hints,&servinfo)) != 0)
	{
		fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}

	if ((sock = socket(servinfo->ai_family,servinfo->ai_socktype,servinfo->ai_protocol)) == -1)
	{
		perror("client: socket");
		freeaddrinfo(servinfo);
		return -1;
	}
	if (connect(sock,servinfo->ai_addr, servinfo->ai_addrlen) == -1)
	{
		close (sock);
		sock=-1;
		perror("Client Connect");
		freeaddrinfo(servinfo);
		return -1;
	}
	freeaddrinfo(servinfo);
	return sock;
}

int sandboxme(){
	int ret = 0;
#ifndef NOSECCOMP
	printf("Entering seccomp sandbox now\n");
	scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
	if (!ctx) {
		printf("Error from seccomp_init\n");
		ret=-1;
	}
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pread64), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwrite64), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(poll), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 0);
		//ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 1, SCMP_A0(SCMP_CMP_NE, 0));
	//if (!ret)
		//ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 1, SCMP_A3(SCMP_CMP_MASKED_EQ, MAP_FIXED, 0));
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clone), 1, SCMP_A0(SCMP_CMP_EQ, (CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID)));
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(futex), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(nanosleep), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munmap), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(gettid), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(set_robust_list), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(5), SCMP_SYS(open), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(prctl), 1, SCMP_A0(SCMP_CMP_EQ, PR_SET_NAME));
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lseek), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(5), SCMP_SYS(access), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ftruncate), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fsync), 0);
	if (!ret)
		ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(close), 0);
	if (!ret)
		ret = seccomp_load(ctx);
#else
	printf("Would enter seccomp sandbox now\n");
#endif
	return ret;
}

class IrcSession {
public:
	IrcSession() {}
	IrcSession(std::string thisnetwork, std::string thisident,
	           std::string thisnick, std::vector<std::string> thisserv):
	           network(thisnetwork),ident(thisident),nick(thisnick), servers(thisserv){
		servind=0;
	}
	std::string network;
	std::string ident;
	std::string nick;
	std::vector<std::string> servers;
	pid_t pid;
	int sock, servind;

	int connect(){
		for (unsigned i=0; servers.size() > i; i++) {
			sock = open_irc_connection(servers[(i+servind)%servers.size()].c_str());
			if (0 < sock) {
				servind=(i+servind+1)%servers.size();
				return 0;
			}
		}
		return -1;
	}

	pid_t run_session(bool connect) {
		int ret;

		if ((pid = fork()))
			return pid;

		std::string logfile=network+".log";
		int logfd = open(logfile.c_str(), O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		close(1);
		close(2);
		dup2(logfd,1);
		dup2(logfd,2);
		close(logfd);
		setvbuf(stdout, NULL, _IOLBF, 0);

		s=sock;

		std::string dbname = network + ".db";
		sqlite3_vfs_register(&kebotVfs, 1);
		open_db(dbname.c_str());
		close(0);

		GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);
		GIOChannel *ircchan = g_io_channel_unix_new(s);
		g_io_add_watch(ircchan, G_IO_IN, glib_callback, 0);

		if (sandboxme()) {
			printf("error setting seccomp\n");
			exit(1);
		}
		ret = sqlite3_open(dbname.c_str(), &db);
		if( ret ){
			fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			exit(RETVAL_FATAL_ERROR);
		}
		ret = sqlite3_open(":memory:", &volatile_db);
		if( ret ){
			fprintf(stderr, "Can't open memory database: %s\n", sqlite3_errmsg(volatile_db));
			sqlite3_close(volatile_db);
			exit(RETVAL_FATAL_ERROR);
		}

		HandleScope handle_scope;
		Handle<ObjectTemplate> global = ObjectTemplate::New();

		global->Set(String::New("exit"), FunctionTemplate::New(ExitCallback));
		global->Set(String::New("log"), FunctionTemplate::New(LogCallback));
		global->Set(String::New("cppGetDBValue"), FunctionTemplate::New(getDBValue));
		global->Set(String::New("cppSetTimer"), FunctionTemplate::New(setTimer));
		global->SetAccessor(String::New("x"), XGetter, XSetter);
		global->SetAccessor(String::New("script_retval"), retvalGetter, retvalSetter);

		Persistent<Context> context = Context::New(NULL, global);
		Context::Scope context_scope(context);
		Handle<String> sourcehandle = String::New(source);
		Handle<Script> script = Script::Compile(sourcehandle);
		scriptp = &script;

		if (connect)
			glib_callback(NULL,G_IO_NVAL,(gpointer)strdup("INIT\n"));
		g_timeout_add_seconds (60, ping_timeout_callback, 0);
		g_main_loop_run(main_loop);

		context.Dispose();
		sqlite3_close(db);
		sqlite3_close(volatile_db);
		return 0;
	}
};

void load_source() {
	memset(source,0,SOURCESIZE);
	FILE* sourcefp = fopen("kebot.js", "r");
	fread(source, 1, SOURCESIZE, sourcefp);
	fclose(sourcefp);
}

int main(int argc, char *argv[])
{
	if (2 != argc) {
		printf("Usage %s <configfile>\n", argv[0]);
		exit(1);
	}

	load_source();

	std::map<pid_t,IrcSession> sessions;

	libconfig::Config cfg;
	cfg.readFile(argv[1]);
	const libconfig::Setting& root = cfg.getRoot();
	const libconfig::Setting &networks = root["networks"];
	int count = networks.getLength();

	for (int i=0; i<count; i++) {
		const libconfig::Setting &net = networks[i];

		std::string network, nick, ident;

		if (!(net.lookupValue("network", network)
		     && net.lookupValue("nick", nick)
		     && net.lookupValue("ident", ident)
		))
			exit(1);

		libconfig::Setting &servers = net["servers"];
		int nserv = servers.getLength();
		std::vector<std::string> servec(0);
		for (int j=0; j<nserv;j++){
			servec.push_back(servers[j]);
		}

		IrcSession session(network,ident,nick,servec);
		session.connect();
		sessions[session.run_session(true)] = session;
	}
	pid_t prevpid = -1;
	while (1) {
		int status;
		pid_t pid = wait(&status);
		if (-1 == pid) {
			printf("No more chilrden left, exiting\n");
			break;
		}
		if (prevpid == pid)
			sleep(60);

		SessionRetVal childret = RETVAL_EXIT;

		if (WIFEXITED(status)) {
			childret = (SessionRetVal)WEXITSTATUS(status);
		}
		else if (WIFSIGNALED(status)){
			if (SIGPIPE == WTERMSIG(status))
				childret = RETVAL_DISCONNECT;
			else
				childret = RETVAL_ERROR;
		}
		else
			continue;
		printf ("pid %d exit with %d\n", pid, childret);

		if (RETVAL_EXIT == childret || RETVAL_FATAL_ERROR == childret)
			continue;

		bool connect = false;
		IrcSession session = sessions[pid];
		sessions.erase(pid);
		if (RETVAL_DISCONNECT == childret) {
			close(session.sock);
			session.connect();
			connect=true;
		}
		if (RETVAL_RELOAD == childret)
			load_source();
		pid_t newpid = session.run_session(connect);
		sessions[newpid] = session;
		prevpid = newpid;
	}
}

