#!/bin/bash

RC="$(dirname $0)/rc"
[ -z "$RTAGS_DISABLED" ] && [ -x "`which $RC`" ] && $RC --silent --compile "$@" -Wno-error &
[ -n "$RTAGS_RMAKE" ] && exit 0
compiler="$1"
shift
$compiler "$@"
exit $?
