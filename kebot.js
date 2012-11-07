var msgre = /:(\S+) PRIVMSG (\S+) :(.+)/i
var joinre = /:(\S+) JOIN :(\S+)/i
var cmdre = /%(\S+)( +\S.+)?/i
var pingre = /PING( .+)/i

var lines = x.split("\n")

function getDBValue(s) {
	var esc = s.replace(/'/g,"''")
	var input = "PRAGMA SQLITE_TEMP_STORE=3; select data from t1 where key == '" + esc + "';"
	var retval=cppGetDBValue(input);
	return retval
}

function cmdevent(command, parameters){
	log(command)
	if ("join" == command)
		return 'JOIN ' + parameters + '\n';
	return "";
}

function msgevent(who,whom,message){
	log(message)
	log(who)
	log(whom)
	if (getDBValue("master:" + who) == 'yes') {
		var cmd = cmdre.exec(message)
		if (cmd) {
			return cmdevent(cmd[1],cmd[2])
		}
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
	}
	return retval
}

f(lines)

