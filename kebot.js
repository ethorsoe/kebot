var msgre = /:(\S+) PRIVMSG (\S+) :(.+)/i
var joinre = /:(\S+) JOIN :(\S+)/i
var cmdre = /%(\S+)( +\S.+)?/i
var pingre = /PING( .+)/i
var saytoken = /(\S+) (.*)/
var hostmaskre = /([^!@: ]+)!([^!@: ]+)@([^!@: ]+)/
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

function cmdevent(command, parameters, who, context){
	if ("timer" == command) {
		var timers = / *([0-9]+) *(.*)/.exec(parameters)
		if (timers) {
			cppSetTimer(timers[1], "KEBOTCMD TIMER "+context+" " + timers[2]) + "\n"
			return ""
		}
	}

	var hostmask = hostmaskre.exec(who)
	if (getDBValue("master", ["ident", hostmask[2]], ["host"].concat(getHosts(hostmask[3]))) != 'yes')
		return ""

	if ("join" == command)
		return join(parameters)
	if ("say" == command) {
		var targets = saytoken.exec(parameters)
		return msg(targets[1],targets[2])
	}
	if ("reload" == command)
		exit(4)
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

	var table = new Array(hosts.length-1)
	for (i=0;i<hosts.length-1;i++) {
		table[i]=hosts.slice(i).join(".")
	}

	return table
}

function joinevent(who, where) {
	var hostmask = hostmaskre.exec(who)
	if (hostmask && getDBValue("op."+where, ["ident", hostmask[2]], ["host"].concat(getHosts(hostmask[3]))) == "yes") {
		return op(where,hostmask[1])
	}
	return ''
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
		return nick(getDBValue("conf", "altnick"))
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
			retval += joinevent(join[1],join[2])
			continue
		}
		var timer = timerre.exec(b[i])
		if (timer) {
			retval += "PRIVMSG " + timer[1] + " :" + timer[2] + "\n"
			script_retval = false
			continue
		}
		if ("INIT" == b[i]) {
			retval += "USER " + getDBValue("conf", "ident") + " * * :" + getDBValue("conf","realname") + "\n"
			retval += "NICK " + getDBValue("conf", "nick") + "\n"
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

