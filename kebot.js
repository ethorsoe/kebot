var msgre = /:(\S+) PRIVMSG (\S+) :(.+)/i
var joinre = /:(\S+) JOIN :(\S+)/i
var cmdre = /%(\S+)( +\S.+)?/i
var pingre = /PING( .+)/i
var saytoken = /(\S+) (.*)/
var hostmaskre = /([^!@: ]+)!([^!@: ]+)@([^!@: ]+)/

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
	if ("say" == command) {
		var targets = saytoken.exec(parameters)
		return "PRIVMSG "+targets[1]+" :"+ targets[2] +"\n"
	}
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

function joinevent(who, where) {
	if (getDBValue("op." +where+ ":" + who) == 'yes') {
		var hostmask = hostmaskre.exec(who)
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
		log(retval)
	}
	return retval
}

f(lines)

