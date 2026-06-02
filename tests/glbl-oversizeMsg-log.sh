#!/bin/bash
# add 2018-05-03 by PascalWithopf, released under ASL 2.0
. ${srcdir:=.}/diag.sh init
./have_relpSrvSetOversizeMode
if [ $? -eq 1 ]; then
  echo "imrelp parameter oversizeMode not available. Test stopped"
  exit 77
fi;
generate_conf
add_conf '
module(load="../plugins/imrelp/.libs/imrelp")
global(maxMessageSize="230"
	oversizemsg.errorfile=`echo $RSYSLOG2_OUT_LOG`)


input(type="imrelp" port="'$TCPFLOOD_PORT'" maxdatasize="300")

template(name="outfmt" type="string" string="%rawmsg%\n")
action(type="omfile" template="outfmt"
				 file=`echo $RSYSLOG_OUT_LOG`)
'
# TODO: add tcpflood option to specific EXACT test message size!
startup
tcpflood -Trelp-plain -p'$TCPFLOOD_PORT' -m1 -d 240
shutdown_when_empty # shut down rsyslogd when done processing messages
wait_shutdown

# Verify required fields are present; each checked independently so the
# test is not sensitive to JSON key ordering (undefined by spec).
check_field() {
    local field="$1" pattern="$2"
    if ! grep -q "$pattern" "${RSYSLOG2_OUT_LOG}"; then
        echo "FAIL: oversize log missing field '$field' (pattern: $pattern)"
        echo "${RSYSLOG2_OUT_LOG} contents:"
        cat "${RSYSLOG2_OUT_LOG}"
        error_exit 1
    fi
}

check_field "rawmsg"       "rawmsg"
check_field "input/imrelp" '"input"'
check_field "fromhost-ip"  '"fromhost-ip"'
check_field "timereported" '"timereported"'
check_field "timegenerated" '"timegenerated"'
check_field "hostname"     '"hostname"'
check_field "syslogtag"    '"syslogtag"'


exit_test
