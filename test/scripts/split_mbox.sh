#!/bin/bash

set -e

FORMAIL=formail

SPLIT_MBOX_DIR=

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
	    [[ $# -ge 2 ]] || error "--split-to requires a further parameter: the directory for the split mboxen"
	    SPLIT_MBOX_DIR="$2"
	    shift
	    ;;
	* )
	    [[ $# -le 1 ]] || error "Further parameters follow the mbox parameter"
	    MBOX_FILE="$1"
	    ;;
	esac
    shift
done

#--------------------------------------------------

[[ ! -z "$MBOX_FILE" ]] || error "No mbox file given"
[[ -e "$MBOX_FILE" ]] || error "The given mbox file \"${MBOX_FILE}\" does not exist"
[[ ! -d "$MBOX_FILE" ]] || error "The given file \"${MBOX_FILE}\" should be an mbox, but is a directory"

#--------------------------------------------------

if [ -z "$SPLIT_MBOX_DIR" ]
then
    SPLIT_MBOX_DIR="$(echo "$MBOX_FILE" | sed -e 's@\(\(.*/\|^\)mbox\)/@\1_split/@')"

    [[ "$MBOX_FILE" != "$SPLIT_MBOX_DIR" ]] || error "Determining directory for split mbox failed"

    [[ ! -e "$SPLIT_MBOX_DIR" ]] || error "The directory for the split mboxen \"${SPLIT_MBOX_DIR}\" already exists"
fi

mkdir -p "$SPLIT_MBOX_DIR"

PART=0 # the suffix number for the split files
PREVIOUS_SEPARATION_POS=1 # the position of the newline before the mbox From
  # separator for the previous message. We initialize it to the beginning of
  # the file to get a good start (the first Message does not have a prepented
  # newline).
LINE_OFFSET=2 # the parameter passed to tail to strip the correct number of lines.
  # This is two for the first iteration, and three for the following iterations

# SEPARATION_POS is the byte offset from the beginning of the file to a newline
# before a "From" mbox separator. We add the file size as final separation
# point to not omit the last message of the mbox.
for SEPARATION_POS in $( grep -b -B 1 '^From[[:space:]]' "$MBOX_FILE" | grep '^[0-9]*-$' | sed -e 's/-//' ) $(($(stat "-c%s" "$MBOX_FILE")-1))
do

    # We first chop off the part after the message, then the part before the
    # message and finally "tail" away the remains the the mbox format.
    head -c $SEPARATION_POS "$MBOX_FILE" | \
	tail -c $((SEPARATION_POS-PREVIOUS_SEPARATION_POS)) | \
	tail -n +${LINE_OFFSET} >"${SPLIT_MBOX_DIR}"/part."$PART"

    PART=$((PART+1))
    PREVIOUS_SEPARATION_POS=$SEPARATION_POS
    LINE_OFFSET=3
done
