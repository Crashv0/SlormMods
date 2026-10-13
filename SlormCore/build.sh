#!/usr/bin/env bash
# The build lives at the top of the repo; this runs it for the runtime.
exec "$(dirname "$0")/../build.sh" SlormCore "$@"
