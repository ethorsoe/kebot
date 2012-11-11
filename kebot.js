var msgre = /:(\S+) PRIVMSG (\S+) :(.+)/i
var joinre = /:(\S+) JOIN :(\S+)/i
var cmdre = /%(\S+)( +\S.+)?/i
var pingre = /PING( .+)/i
var saytoken = /(\S+) (.*)/
var hostmaskre = /([^!@: ]+)!([^!@: ]+)@([^!@: ]+)/
var timerre = /KEBOTCMD TIMER (\S+) +(.+)/

var lines = x.split("\n")

function getDBValue() {
	var input = "PRAGMA SQLITE_TEMP_STORE=3; select data from '" + arguments[0] + "' where "
	var inputs = new Array(arguments.length - 1)

	function escapes(s) {
		return s.replace(/'/g,"''")
	}

	for (i=1; i < arguments.length; i++) {
		var key = " key == "
		var thisarg=arguments[i]
		if (Array.isArray(thisarg)) {
			if (thisarg.length > 2) {
				var thisarray = new Array(thisarg.length-1)
				for (j=1;j<thisarg.length;j++) {
					thisarray[j-1]=escapes(thisarg[j])
				}
				inputs[i-1] = "" + thisarg[0] + " IN ('" + thisarray.join("','") + "')"
			}
			else {
				inputs[i-1] = "" + thisarg[0] + " == '" + escapes(thisarg[1]) + "'"
			}
		}
		else {
			inputs[i-1] = "key == '" + escapes(thisarg) + "'"
		}
	}
	input += inputs.join(" AND ")
	input += ";"
	return cppGetDBValue(input);
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
		return 'JOIN ' + parameters + '\n';
	if ("say" == command) {
		var targets = saytoken.exec(parameters)
		return "PRIVMSG "+targets[1]+" :"+ targets[2] +"\n"
	}
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
		return "MODE " + where + " +o " + hostmask[1] + "\n"
	}
	return ''
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
		}
	}
	return retval
}

f(lines)

