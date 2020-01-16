#!/bin/bash

set -e

FORMAIL=formail

SPLIT_MMDF_DIR=

#--------------------------------------------------

error () {
    echo "**Error:**" "$@" >&2
    exit 1
}

#--------------------------------------------------

while [ $# -ge 1 ]
do
    case "$1" in
	"--split-to" )
	    [[ $# -ge 2 ]] || error "--split-to requires a further parameter: the directory for the split mmdf"
	    SPLIT_MMDF_DIR="$2"
	    shift
	    ;;
	* )
	    [[ $# -le 1 ]] || error "Further parameters follow the mmdf parameter"
	    MMDF_FILE="$1"
	    ;;
	esac
    shift
done

#--------------------------------------------------

[[ ! -z "$MMDF_FILE" ]] || error "No mmdf file given"
[[ -e "$MMDF_FILE" ]] || error "The given mmdf file \"${MMDF_FILE}\" does not exist"
[[ ! -d "$MMDF_FILE" ]] || error "The given file \"${MMDF_FILE}\" should be an mmdf, but is a directory"

#--------------------------------------------------

if [ -z "$SPLIT_MMDF_DIR" ]
then
    SPLIT_MMDF_DIR="$(echo "$MMDF_FILE" | sed -e 's@\(\(.*/\|^\)mmdf\)/@\1_split/@')"

    [[ "$MMDF_FILE" != "$SPLIT_MMDF_DIR" ]] || error "Determining directory for split mmdf failed"

    [[ ! -e "$SPLIT_MMDF_DIR" ]] || error "The directory for the split mmdf \"${SPLIT_MMDF_DIR}\" already exists"
fi

mkdir -p "$SPLIT_MMDF_DIR"

PART=0 # the suffix number for the split files
PREVIOUS_SEPARATION_POS=5 # position of start of first message.

# SEPARATION_POS is the byte offset from the beginning of the file to the
# closing ^A^A^A^A (i.e. every second ^A^A^A^A) for each message.
for SEPARATION_POS in $( grep -bP '^\01\01\01\01$' "$MMDF_FILE" | awk '!(NR%2)' | sed -e 's/:.*//' )
do

    # We first chop off the part after the message, then the part before the
    # message and finally "tail" away the remains the the mbox format.
    head -c $SEPARATION_POS "$MMDF_FILE" | \
	tail -c $((SEPARATION_POS-PREVIOUS_SEPARATION_POS)) > \
	"${SPLIT_MMDF_DIR}"/part."$PART"

    PART=$((PART+1))
    PREVIOUS_SEPARATION_POS=$(($SEPARATION_POS+10))
done

