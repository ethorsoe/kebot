var msgre = /:(\S+) PRIVMSG (\S+) :(.+)/i
var joinre = /:(\S+) JOIN :(\S+)/i
var cmdre = /%(\S+)( +\S.+)?/i
var pingre = /PING( .+)/i
var saytoken = /(\S+) (.*)/
var hostmaskre = /([^!@ ]+)!([^!@ ]+)@([^!@ ]+)/
var timerre = /KEBOTCMD TIMER (\S+) +(.+)/
var numericre = /\S+ ([0-9]+) \S+ .*/

var lines = x.split("\n")

/*
 * Auxiliary functions.
 */

function join(channel) {
	return "JOIN " + channel + "\n"
}
function op(channel, user) {
	return "MODE " + channel + " +o " + user + "\n"
}
function msg(whom, what) {
	return "PRIVMSG "+whom+" :"+what+"\n"
}
function nick(newnick){
	return "NICK " + newnick + "\n"
}

function escapesqls(s) {
	return s.replace(/'/g,"''")
}
function setDBValue() {
	var input = "PRAGMA SQLITE_TEMP_STORE=3; insert into '" + arguments[0] + "' values('"
	var init_i = 1
	var is_volatile = true
	var inputs = new Array(arguments.length - init_i)

	for (i=init_i;i<arguments.length;i++) {
		inputs[i-init_i]=escapesqls(arguments[i])
	}
	input += inputs.join("','")

	input += "');"
	return cppGetDBValue(input, is_volatile);
}
function getDBValue() {
	var input = "PRAGMA SQLITE_TEMP_STORE=3; select data from '" + arguments[0] + "' where "
	var init_i = 1
	var is_volatile = false
	if (typeof(arguments[1]) == "boolean") {
		is_volatile = arguments[1]
		init_i = 2
	}
	var inputs = new Array(arguments.length - init_i)

	for (i=init_i; i < arguments.length; i++) {
		var key = " key == "
		var thisarg=arguments[i]
		if (Array.isArray(thisarg)) {
			if (thisarg.length > 2) {
				var thisarray = new Array(thisarg.length-1)
				for (j=1;j<thisarg.length;j++) {
					thisarray[j-1]=escapesqls(thisarg[j])
				}
				inputs[i-init_i] = "" + thisarg[0] + " IN ('" + thisarray.join("','") + "')"
			}
			else {
				inputs[i-init_i] = "" + thisarg[0] + " == '" + escapesqls(thisarg[1]) + "'"
			}
		}
		else {
			inputs[i-init_i] = "key == '" + escapesqls(thisarg) + "'"
		}
	}
	input += inputs.join(" AND ")
	input += ";"
	return cppGetDBValue(input, is_volatile);
}

/*
 * Command handling code
 */
var commands = {}
var privCommands = {}

function addCommand(f, h) {
	var retval = new Object
	retval.help = h
	retval.func = f
	return retval
}

function timerCmd(parameters, who, context) {
	var timers = / *([0-9]+) *(.*)/.exec(parameters)
	if (timers) {
		cppSetTimer(timers[1], "KEBOTCMD TIMER "+context+" " + who + ": " + timers[2]) + "\n"
		return ""
	}
}
function helpCmd(parameters, who, context) {
	var cmd = parameters.trim()
	if (commands[cmd])
		return msg(context, commands[cmd].help)
	if (privCommands[cmd])
		return msg(context, privCommands[cmd].help)
	return msg(context, "No help for command " + cmd + "\n")
}
function sayCmd(parameters, who, context) {
	var targets = saytoken.exec(parameters)
	if (targets)
		return msg(targets[1],targets[2])
}
function joinCmd(parameters, who, context) {
	return join(parameters)
}
function reloadCmd(parameters, who, context) {
	exit("RELOAD")
}
function dieCmd(parameters, who, context) {
	exit("EXIT")
}
commands["timer"]         =addCommand(timerCmd,"timer <time in secs> <message>, send a <message> to me in this context\n")
commands["help"]          =addCommand(helpCmd,"help <cmd>, print help for <cmd>\n")
privCommands["say"]       =addCommand(sayCmd,"say <whom> <what>, send a message <what> to <whom>\n")
privCommands["join"]      =addCommand(joinCmd,"join <#channel>, join channel\n")
privCommands["reload"]    =addCommand(reloadCmd,"Reload client script\n")
privCommands["die"]       =addCommand(dieCmd,"Exit IRC session permanently\n")

function cmdevent(command, parameters, who, context){
	if (typeof(parameters) == "undefined")
		parameters = ""

	if (commands[command])
		return commands[command].func(parameters,who,context)

	var hostmask = hostmaskre.exec(who)
	if (!hostmask || getDBValue("master", ["ident", hostmask[2]], ["host"].concat(getHosts(hostmask[3]))) != 'yes')
		return ""

	if (privCommands[command])
		return privCommands[command].func(parameters,who,context)

	return "";
}

function msgevent(who,whom,message){
	var cmd = cmdre.exec(message)
	if (cmd) {
		if (/^[#!]/.exec(whom))
			return cmdevent(cmd[1],cmd[2],who,whom)
		else
			return cmdevent(cmd[1],cmd[2],who,who)
	}
	return ""
}

function getHosts(host) {
	var hosts = host.split('.')

	var table = new Array(hosts.length)
	for (i=0;i<hosts.length;i++) {
		table[i]=hosts.slice(i).join(".")
	}

	return table
}

function myjoinevent(where) {
	return getDBValue("joinaction",where)
}
function joinevent(who, where) {
	var hostmask = hostmaskre.exec(who)
	if (hostmask) {
		if (getDBValue("op."+where, ["ident", hostmask[2]], ["host"].concat(getHosts(hostmask[3]))) == "yes") {
			return op(where,hostmask[1])
		}
		var mynick = getDBValue("state",true,"nick")
		if (hostmask[1] == mynick) {
			return myjoinevent(where)
		}
	}
	return ""
}

function connectevent() {
	var channels = getDBValue("conf", "channels").split(" ")
	var retval = ""
	for (i in channels)
		retval += join(channels[i])
	return retval
}

function numericevent(number) {
	switch (Number(number)) {
	case 1:
		return connectevent()
	case 433:
		var mynick = getDBValue("conf", "altnick")
		setDBValue("state","nick", mynick)
		return nick(mynick)
	}
	return ""
}

function f(b){
	var retval=''
	for (i in b) {
		var msg = msgre.exec(b[i])
		if (msg) {
			retval += msgevent(msg[1],msg[2],msg[3])
			continue
		}
		var ping = pingre.exec(b[i])
		if (ping) {
			retval += 'PONG' + ping[1] + '\n'
			continue
		}
		var join = joinre.exec(b[i])
		if (join) {
			retval += joinevent(join[1],join[2].toLowerCase())
			continue
		}
		var timer = timerre.exec(b[i])
		if (timer) {
			retval += "PRIVMSG " + timer[1] + " :" + timer[2] + "\n"
			script_retval = false
			continue
		}
		if ("INIT" == b[i]) {
			var mynick = getDBValue("conf", "nick")
			cppGetDBValue("create table state (key TEXT, data TEXT);", true);
			setDBValue("state","nick", mynick)
			retval += "USER " + getDBValue("conf", "ident") + " * * :" + getDBValue("conf","realname") + "\n"
			retval += nick(mynick)
			continue
		}
		var numeric = numericre.exec(b[i])
		if (numeric) {
			retval += numericevent(numeric[1])
			continue
		}
	}
	return retval
}

f(lines)

