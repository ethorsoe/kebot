#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <seccomp.h>
#include <glib.h>
#include <v8.h>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sqlite3.h>

#define MAXDATASIZE 40960
#define SOURCESIZE 409600

using namespace v8;

char buf[MAXDATASIZE];
sqlite3 *db;

Handle<Value> XGetter(Local<String> property, 
	const AccessorInfo& info) {
	return String::New(buf);
}

void XSetter(Local<String> property, Local<Value> value,
	const AccessorInfo& info) {
}

static Handle<Value> LogCallback(const Arguments& args) {
	if (args.Length() < 1) return v8::Undefined();
	HandleScope scope;
	Handle<Value> arg = args[0];
	String::Utf8Value value(arg);
	printf("LOG: %s\n", *value);

	return v8::Undefined();
}

static int sqlite_callback(void *target, int argc, char **argv, char **azColName){
	if (1 <= argc) {
		strcpy((char*)target, argv[0]);
	}
	return 0;
}

static Handle<Value> getDBValue(const Arguments& args) {
	char result[1024];
	char *errMsg;
	result[0]=0;
	if (args.Length() < 1) return v8::Undefined();
	HandleScope scope;
	Handle<Value> arg = args[0];
	String::Utf8Value value(arg);
	int err = sqlite3_exec(db, *value, sqlite_callback, result, &errMsg);
	if (err)
		printf("Error %s in SQL\n", errMsg);

	return String::New(result);
}

int s;

int writes(int fd, const char *a) {
	write(fd, a, strlen(a));
}

gboolean irc_callback(GIOChannel *source, GIOCondition cond, gpointer ptr)
{
	int numbytes=0;
	do {
		if (MAXDATASIZE <= numbytes + 1) exit(1);
		numbytes += read(s,buf+numbytes,MAXDATASIZE-1-numbytes);
		buf[numbytes]='\0';
	}
	while ('\n' != buf[numbytes-1]);
	printf("%s", buf);

	Handle<Script> *script = (Handle<Script>*)ptr;

	// Run the script to get the result.
	Handle<Value> result = (*script)->Run();

	// Convert the result to an ASCII string and print it.
	String::AsciiValue ascii(result);
	printf("ascii %s\n", *ascii);
	writes(s, *ascii);

	return TRUE;
}

char source[SOURCESIZE];

int main()
{
	struct addrinfo hints, *servinfo;

	//Ensure that servinfo is clear
	memset(&hints, 0, sizeof hints); // make sure the struct is empty

	//setup hints
	hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets

	//Setup the structs if error print why
	int res;
	if ((res = getaddrinfo("irc.inet.fi", "6667",&hints,&servinfo)) != 0)
	{
		fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(res));
	}


	//setup the socket
	if ((s = socket(servinfo->ai_family,servinfo->ai_socktype,servinfo->ai_protocol)) == -1)
	{
		perror("client: socket");
	}

	//Connect
	if (connect(s,servinfo->ai_addr, servinfo->ai_addrlen) == -1)
	{
		close (s);
		perror("Client Connect");
	}

	memset(source,0,SOURCESIZE);
	FILE* sourcefp = fopen("kebot.js", "r");
	fread(source, 1, SOURCESIZE, sourcefp);
	fclose(sourcefp);

	// Create a stack-allocated handle scope.
	HandleScope handle_scope;

	// Create a new context.
	Handle<ObjectTemplate> global = ObjectTemplate::New();
	global->Set(String::New("log"), FunctionTemplate::New(LogCallback));
	global->Set(String::New("cppGetDBValue"), FunctionTemplate::New(getDBValue));
	global->SetAccessor(String::New("x"), XGetter, XSetter);
	Persistent<Context> context = Context::New(NULL, global);

	// Enter the created context for compiling and
	// running the hello world script. 
 	Context::Scope context_scope(context);

	// Create a string containing the JavaScript source code.
	Handle<String> sourcehandle = String::New(source);

	// Compile the source code.
	Handle<Script> script = Script::Compile(sourcehandle);

	//We dont need this anymore
	freeaddrinfo(servinfo);

	int ret = sqlite3_open("ircnet.db", &db);
	if( ret ){
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	}

	GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);
	GIOChannel *ircchan = g_io_channel_unix_new(s);
	g_io_add_watch(ircchan, G_IO_IN, irc_callback, &script);
	
#ifndef NOSECCOMP
	ret = seccomp_init(SCMP_ACT_KILL);
	if (ret < 0)
		printf("Error from seccomp_init\n");
	ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(read), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(write), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(fstat), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(exit), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(poll), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(mmap), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(clone), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(brk), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(futex), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(nanosleep), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(munmap), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(gettid), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(set_robust_list), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ERRNO(5), SCMP_SYS(open), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(prctl), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(lseek), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ERRNO(5), SCMP_SYS(access), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(fsync), 0);
	if (!ret)
		ret = seccomp_rule_add(SCMP_ACT_ALLOW, SCMP_SYS(close), 0);
	if (!ret)
		ret = seccomp_load();
	if (ret)
		printf("error setting seccomp\n");
#endif

	writes(s, "USER MyRealName * * :My Description\n");
	writes(s, "NICK Lipschitz-test\n");

	g_main_loop_run(main_loop);
	
	// Dispose the persistent context.
	context.Dispose();
	sqlite3_close(db);
}

