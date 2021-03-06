#! /bin/bash

export Experiment=UDP
launch_error=''
VERBOSE=''
memoname=/var/run/linkeng/$Experiment/memo

function waitfor {
  name=$1
  duration=$2
  if [ -n "$3" ]; then
    decrement=$3
    delay=1
  else
    decrement=0
    delay=5
  fi
  while [ $duration -gt 0 ]; do
    [ -e $name ] && return 0
    sleep 1
    let duration=duration-$decrement
  done
  return 1
}

function Launch {
  name=$1
  shift
  [ -n "$launch_error" ] && return 1
  [ -n "$VERBOSE" ] && msg "[DEBUG] Launch: $*"
  if { $* & }; then
    Launch_pid=$!
    msg "Launch: $Launch_pid $*"
    # if [ "$name" = "DG/cmd" ]; then
      # parent_loop="-q -M $Launch_pid -t 5"
    # fi
    if [ "$name" != "-" ]; then
      [ "$name" = "-TMC-" ] && name="/var/run/linkeng/run/$Experiment/$!"
      [ "${name#/}" = "$name" ] && name="/var/run/linkeng/$Experiment/$name"
      [ -n "$VERBOSE" ] &&
        msg "[DEBUG] Launch: Waiting for $Launch_pid:$name"
      waitfor $name 10 || {
        msg "[ERROR] Launch: namewait failure: $*"
        launch_error=yes
        return 1
      }
    fi
  else
    msg "[ERROR] Launch: $*"
    launch_error=yes
    return 1
  fi
  return 0
}

function Dispatch_nc {
  [ "x$1" = "x-" ] && shift 1
  [ -n "$VERBOSE" ] && msg "[DEBUG] Dispatch_nc: $*"
  cygstart mintty cyg_nc.sh $*
}

rm -rf /var/run/linkeng/${Experiment}*
rm -f $Experiment.log *.stackdump
# rm -rf LOG
memoname=/var/run/linkeng/$Experiment/memo
# ls -l $memoname
[ -e $memoname ] || {
  echo "Launching memo for $memoname"
  cygstart mintty memo -o Bootstrap.log -l2 -V
  waitfor $memoname 2 || nl_error "Memo launch failed"
}

Launch      tm_bfr bfr -v
Launch      -TMC-  lgr -N `mlf_find LOG`
Dispatch_nc -      UDPdispnc -a
Launch      tm_gen UDPcol -v
Launch      cmd    UDPsrvr -v
Launch      -      UDPdiag -v
Dispatch_nc -      UDPcltnc