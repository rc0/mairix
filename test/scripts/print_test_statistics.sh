#!/bin/bash
set -e

############################################################
#
# This script prints statistics about the tests (whether they did not run,
# they failed, or they were passed) to stdout
#
# Usage:
#   script/print_test_statistics.sh
#
# Return value:
#   0     upon success
#   >0 represents the number of failed tests
#
#-----------------------------------------------------------

#-----------------------------------------------------------
# Prints a visible separator to stdout
#-----------------------------------------------------------
separator () {
    cat <<EOF

========================================================

EOF
}


TESTS=0
SUCCEEDED_TESTS=0

separator #-------------------------------------------------

PREVIOUS_NAME_START=""

# Print the information for each test separately
for FILE in $( ls -1 *.test-spec | sort )
do
    NAME_STUB=${FILE%.test-spec}
    NAME_START=${NAME_STUB:0:1}
    STATUS_FILE="$NAME_STUB".status
    if [ -e "$STATUS_FILE" ]
    then
	RESULT=$(cat "$NAME_STUB".status)
    else
	RESULT="did not run"
    fi

    if [ "$PREVIOUS_NAME_START" != "$NAME_START" ] #-a ! -z "$PREVIOUS_NAME_START" ]
    then
	echo "--------$NAME_START*----------------------------------------------"
    fi

    echo "$RESULT: ${FILE%.test-spec}"

    TESTS=$((TESTS+1))

    if [ "passed" = "$RESULT" ]
    then
	SUCCEEDED_TESTS=$((SUCCEEDED_TESTS+1))
    fi
    PREVIOUS_NAME_START="${NAME_START}"
done

separator #-------------------------------------------------

# Print the accumulated statistics
cat <<EOF
  Total # of tests           : $TESTS
  Total # of succeeded tests : $SUCCEEDED_TESTS
  Total # of failed tests    : $((TESTS-SUCCEEDED_TESTS))
EOF

separator #-------------------------------------------------

#return the number of failed tests
exit $((TESTS-SUCCEEDED_TESTS))
