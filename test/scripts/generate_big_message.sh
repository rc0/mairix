#!/bin/bash
set -e

############################################################
#
# This script generates emails in the BigMessages directory
#
# Usage:
#   script/generate_big_message.sh FILE
#
# where
#
#   FILE is the name of the file to generate.
#     Parent directories will not be generated
#     The basename of FILE has to be a number (1-4) and
#     selects the message to be stored in FILE
#
# Return value:
#   0     upon success
#   != 0  upon failure
#
#-----------------------------------------------------------

#-----------------------------------------------------------
# Prints the body text of an email to stdout
#-----------------------------------------------------------
# $TEXT is printed $REPEATS*1000 times to stdout
generate_text () {
    # TEXT with newline
    TEXT="$TEXT
"
    # 10 times $TEXT
    TEXT="${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}"
    # 100 times $TEXT
    TEXT="${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}"
    # 1000 times $TEXT
    TEXT="${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}${TEXT}"
    for i in `seq 1 $REPEATS`
    do
	echo -n "$TEXT"
    done
}

#-----------------------------------------------------------
# Prints the header of a message to stdout
#-----------------------------------------------------------
generate_header () {
    cat <<EOF
Date: Tue, 4 Jan 2011 16:39:49 +0100
From: generator@bash.script
To: mairix@test.suite
Subject: message with $SIZE bytes ($SUBJECT)

EOF
}

#-----------------------------------------------------------
# Prints a valid message to stdout
#-----------------------------------------------------------
generate_message()
{
    generate_header
    generate_text
}
#-----------------------------------------------------------



#processing the parameters
FILE="$1"
BASENAME="$(basename "$FILE")"
test -d "$(dirname "$FILE")"

#selecting the message to generate
case "$BASENAME" in
    "1")
	SUBJECT=285kb
	REPEATS=5
	TEXT="Some repeating text quite close to three hundred kilobytes."
	SIZE=300131
	;;

    "2")
	SUBJECT=530kb
	REPEATS=10
	TEXT="Some repeating text for fivehundred thirty kilobytes"
	SIZE=530131
	;;

    "3")
	SUBJECT=2MB
	REPEATS=46
	TEXT="Some repeating text yielding a bit more than 2 MB..."
	SIZE=2438130
	;;

    "4")
	SUBJECT=5MB
	REPEATS=95
	TEXT="Some text that amoung for roughly five megabytes.   "
	SIZE=5035130
	;;
    *)
	echo "Unknown kind of file to create" >&2
	exit 1
esac

#generating the message and dumping it into FILE
generate_message >"$FILE"

#asserting the message has the correct size
if test "$SIZE" != "$(stat -c%s "$FILE")"
then
    rm -f "$FILE"
    exit 1
fi
