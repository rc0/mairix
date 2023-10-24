#!/bin/bash
set -e

###########################################################################
#
# Overview
#
###########################################################################
#
# This file is divided into the following sections (given in order of
# appearance):
#           -- Description --
#    * Overview (i.e.: this section)
#    * Design decisions and conventions
#           -- Functions --
#    * Low level logging and errors
#    * Helper functions for setting up of variables
#    * Assertions about files (existence, non-existence, ...)
#    * Functions for managing the mairix rc file
#    * Adding, removing, and searching messages
#    * Managing the mairix database itself
#           -- Top Level Parts --
#    * Setting up the environment for the test
#    * Functions for executing a test
#
# The farther down the list, the more higher level the function is. The main
# entrance point of this script (ignoring the "set -e" at the top of this
# script) is the Section "Setting up the environment for the test". All of
# the sections before this main entrance point just contain functions, which
# are called by the final two sections.
#
#


###########################################################################
#
# Design decisions and conventions
#
###########################################################################
#
# This section tries to explain disign decisions and conventions. It is
# divided into the following subsections:
#
#  * Names of functions
#  * Names of variables
#  * Logging
#
#
# -- Names of functions --
#
# Functions that are available for direct use by test-specifications have
# published_ prepended to their name.
# This shall not assert that the function without published_ are somewhat
# magically hidden. But highlight places where extra care is needed, as
# the functions are accessible directly to the test developers.
#
#
# -- Names of variables --
#
# We employ Hungarian notation for file name variables:
#   names of variables for files or directories end in either _ABS,
#       _REL{B,D,U}, or _UNSP.
#   If they end in _ABS, they are absolute.
#   If they end in _RELB, they are relative to the base directory of the tests
#       (i.e.: the test subdirectory of mairix)
#   If they end in _RELD, they are relative to $DATA_DIR_ABS (i.e.: the data
#       directory for the current test)
#   If they end in _RELU, they are relative to some unspecified directory.
#   If they end in _UNSP, it is not known, whether or not the file is
#       relative or absolute
#
# names of files that are not directories carry _FILE immediately before the
#       trailing _ABS, _REL{B,D,U}, _UNSP
# names of files that are directories carry _DIR immediately before the
#       trailing _ABS, _REL{B,D,U}, _UNSP
# names of files whose type is not known carry _UNSP immediately before the
#       trailing _ABS, _REL{B,D,U}, _UNSP
#
# -- Logging --
#
# We decided to log combined stdout and stderr into a single file. Thereby we
# obtain a log file carrying the messages in /correct chronological order/,
# although we loose the separation between stdout and stderr. As tests
# should be short, their output is typically short and the benefit of having
# the log message chronologically correctly ordered outweighs the problem of
# the loss of the separation.
#
# The log file is stored in the tests data subdirectory and can therefore
# only be established after this directory has been set up. Hence error
# messages resulting from parameter checks for the script invocation cannot
# be logged. Nevertheless, such errors stop the test and the errors show up
# on stderr.
#


###########################################################################
#
# Low level logging and errors
#
###########################################################################

exec 6>&1 # file descriptor 6 refers to unlogged stdout
exec 7>&2 # file descriptor 7 refers to unlogged stderr

#--------------------------------------------------------------------------
# Adds a message to the log
#--
# $@ : the log message
#
log () {
    echo "log:" "$@"
}

#--------------------------------------------------------------------------
# Adds an error message to the log and aborts the test
#--
# $@ : the error message
#
error () {
    echo "**Error:**" "$@" >&2
    exit 1
}

###########################################################################
#
# Helper functions for setting up of variables
#
###########################################################################

#--------------------------------------------------------------------------
# Sets _RELD and _ABS variables for files that are not directories.
#--
# $1 : first part of the variable name. $1_FILE_RELD, and $1_FILE_ABS are generated
# $2 : the file to set the variable to. $2 has to be relative to $DATA_DIR_ABS
#
set_file_variable_RELD () {
    [[ $# -eq 2 ]] || error "set_file_variable_RELD requires exactls two arguments, but received $# arguments."
    local VARIABLE_STUB="$1"
    local NAME_FILE_RELD="$2"
    eval "${VARIABLE_STUB}_FILE_RELD=$NAME_FILE_RELD"
    eval "${VARIABLE_STUB}_FILE_ABS=$DATA_DIR_ABS/$NAME_FILE_RELD"
}

#--------------------------------------------------------------------------
# Sets _RELD and _ABS variables for directories.
#--
# $1 : first part of the variable name. $1_DIR_RELD, and $1_DIR_ABS are generated
# $2 : the file to set the variable to. $2 has to be relative to $DATA_DIR_ABS
#
set_dir_variable_RELD () {
    [[ $# -eq 2 ]] || error "set_dir_variable_RELD requires exactls two arguments, but received $# arguments."
    local VARIABLE_STUB="$1"
    local NAME_DIR_RELD="$2"
    eval "${VARIABLE_STUB}_DIR_RELD=$NAME_DIR_RELD"
    eval "${VARIABLE_STUB}_DIR_ABS=$DATA_DIR_ABS/$NAME_DIR_RELD"
}

###########################################################################
#
# Assertions about files (existence, non-existence, ...)
#
###########################################################################

#--------------------------------------------------------------------------
# asserts that a file does not exist.
# If the given file exists, the whole test is aborted
#--
# $1 : file to test whether or not it exists
#
assert_file_does_not_exist () {
    [[ $# -eq 1 ]] || error "assert_file_does_not_exist requires exactls one arguments, but received $# arguments."
    local FILE_UNSP="$1"
    [[ ! -e "${FILE_UNSP}" ]] || error "The file \"${FILE_UNSP}\" already exists"
}

#--------------------------------------------------------------------------
# asserts that a file does exist.
# If the given file does not exists, the whole test is aborted
#--
# $1 : file to test whether or not it exists
#
assert_file_exists () {
    [[ $# -eq 1 ]] || error "assert_file_exists requires exactls one arguments, but received $# arguments."
    local FILE_UNSP="$1"
    [[ -e "${FILE_UNSP}" ]] || error "The file \"${FILE_UNSP}\" does not exist"
}

#--------------------------------------------------------------------------
# asserts that a file does exist and is a file (e.g.: not directory or
# symbolic link).
# If the given file does not exists, or is not a proper file, the whole
# test is aborted.
#--
# $1 : the name to test for whether or not it is an existing, proper file
#
assert_file_exists_is_file () {
    [[ $# -eq 1 ]] || error "assert_file_exists_is_file requires exactls one arguments, but received $# arguments."
    local FILE_UNSP="$1"
    assert_file_exists "$FILE_UNSP"
    [[ -f "${FILE_UNSP}" ]] || error "\"${FILE_UNSP}\" does not denote a file"
}

#--------------------------------------------------------------------------
# asserts that a file exists and is a directory
# If the given file does not exist, or is not a directory, the whole
# test is aborted.
#--
# $1 : the name to test for whether or not it is an existing, directory
#
assert_file_exists_is_directory () {
    [[ $# -eq 1 ]] || error "assert_file_exists_is_directory requires exactls one arguments, but received $# arguments."
    local FILE_UNSP="$1"
    assert_file_exists "$FILE_UNSP"
    [[ -d "${FILE_UNSP}" ]] || error "\"${FILE_UNSP}\" does not denote a directory"
}

###########################################################################
#
# Functions for managing the mairix rc file
#
###########################################################################

#--------------------------------------------------------------------------
# generates the mairix rc file
# The mairix rc file is stored in ${MAIRIX_RC_FILE_ABS}
#--
# <no parameters>
#
generate_mairix_rc ()
{
    [[ $# -eq 0 ]] || error "generate_mairix_rc does not require arguments, but received $# arguments."

    cat >"${MAIRIX_RC_FILE_ABS}" <<EOF
database=${DATA_DIR_ABS}/database
base=${DATA_DIR_ABS}
maildir=${CONF_MAILDIR#:}
mh=${CONF_MH#:}
mbox=${CONF_MBOX#:}
mfolder=${SEARCH_RESULT_DIR_ABS}
mformat=${SEARCH_RESULT_FORMAT}
EOF
}

#--------------------------------------------------------------------------
# adds a maildir directory to the mairix configuration, and regenerates
# the mairix rc file.
#
# Note: This function does not add the messages to the database, but only
# adds the directory name to the configuration file. To add messages to the
# database, use the published_add_messages function.
#--
# $1 - a maildir directory, relative to ${DATA_DIR}
#
conf_add_maildir () {
    [[ $# -eq 1 ]] || error "conf_add_maildir requires exactls one arguments, but received $# arguments."
    local TO_ADD_DIR_RELD="$1"
    assert_file_exists_is_directory "$TO_ADD_DIR_RELD"
    assert_file_exists_is_directory "$TO_ADD_DIR_RELD/cur"
    assert_file_exists_is_directory "$TO_ADD_DIR_RELD/new"
    assert_file_exists_is_directory "$TO_ADD_DIR_RELD/tmp"
    CONF_MAILDIR="${CONF_MAILDIR}:${TO_ADD_DIR_RELD}"
    generate_mairix_rc
}

#--------------------------------------------------------------------------
# adds a mh directory to the mairix configuration, and regenerates
# the mairix rc file.
#
# Note: This function does not add the messages to the database, but only
# adds the directory name to the configuration file. To add messages to the
# database, use the published_add_messages function.
#--
# $1 - a mh directory, relative to ${DATA_DIR}
#
conf_add_mh () {
    [[ $# -eq 1 ]] || error "conf_add_mh requires exactls one arguments, but received $# arguments."
    local TO_ADD_DIR_RELD="$1"
    assert_file_exists_is_directory "$TO_ADD_DIR_RELD"
    CONF_MH="${CONF_MH}:${TO_ADD_DIR_RELD}"
    generate_mairix_rc
}

#--------------------------------------------------------------------------
# adds an mbox to the mairix configuration, and regenerates
# the mairix rc file.
#
# Note: This function does not add the messages to the database, but only
# adds the mbox to the configuration file. To add messages to the
# database, use the published_add_messages function.
#--
# $1 - an mbox file, relative to ${DATA_DIR}
#
conf_add_mbox () {
    [[ $# -eq 1 ]] || error "conf_add_mbox requires exactls one arguments, but received $# arguments."
    local TO_ADD_FILE_RELD="$1"
    assert_file_exists_is_file "$TO_ADD_FILE_RELD"
    CONF_MBOX="${CONF_MBOX}:${TO_ADD_FILE_RELD}"
    generate_mairix_rc
}

#--------------------------------------------------------------------------
# conf_add_mmdf is the same as conf_add_mbox: all that's different is the
# format of the file it loads.
conf_add_mmdf () {
echo conf_add_mmdf "$@"
    conf_add_mbox "$@"
    echo done.
}

#--------------------------------------------------------------------------
# removes a maildir directory from the mairix configuration, and regenerates
# the mairix rc file.
#
# Note: This function does not remove the messages from the database, but only
# removes the directory name from the configuration file. To remove messages
# from to the database, use the published_remove_messages function.
#--
# $1 - a maildir directory, relative to ${DATA_DIR}
#
conf_remove_maildir () {
    [[ $# -eq 1 ]] || error "conf_remove_maildir requires exactls one arguments, but received $# arguments."
    local TO_REMOVE_DIR_RELD="$1"
    assert_file_exists_is_directory "$TO_REMOVE_DIR_RELD"
    assert_file_exists_is_directory "$TO_REMOVE_DIR_RELD/cur"
    assert_file_exists_is_directory "$TO_REMOVE_DIR_RELD/new"
    assert_file_exists_is_directory "$TO_REMOVE_DIR_RELD/tmp"
    CONF_MAILDIR="$(echo "${CONF_MAILDIR}" | sed -e 's@:'"${TO_REMOVE_DIR_RELD}"'\(:\|$\)@\1@')"
    generate_mairix_rc
}

#--------------------------------------------------------------------------
# removes a mh directory from the mairix configuration, and regenerates
# the mairix rc file.
#
# Note: This function does not remove the messages from the database, but only
# removes the directory name from the configuration file. To remove messages
# from to the database, use the published_remove_messages function.
#--
# $1 - a mh directory, relative to ${DATA_DIR}
#
conf_remove_mh () {
    [[ $# -eq 1 ]] || error "conf_remove_mh requires exactls one arguments, but received $# arguments."
    local TO_REMOVE_DIR_RELD="$1"
    assert_file_exists_is_directory "$TO_REMOVE_DIR_RELD"
    CONF_MH="$(echo "${CONF_MH}" | sed -e 's@:'"${TO_REMOVE_DIR_RELD}"'\(:\|$\)@\1@')"
    generate_mairix_rc
}

#--------------------------------------------------------------------------
# removes an mbox from the mairix configuration, and regenerates the mairix
# rc file.
#
# Note: This function does not remove the messages from the database, but only
# removes the directory name from the configuration file. To remove messages
# from to the database, use the published_remove_messages function.
#--
# $1 - an mbox file, relative to ${DATA_DIR}
#
conf_remove_mbox () {
    [[ $# -eq 1 ]] || error "conf_remove_mbox requires exactls one arguments, but received $# arguments."
    local TO_REMOVE_FILE_RELD="$1"
    assert_file_exists_is_file "$TO_REMOVE_FILE_RELD"
    CONF_MBOX="$(echo "${CONF_MBOX}" | sed -e 's@:'"${TO_REMOVE_FILE_RELD}"'\(:\|$\)@\1@')"
    generate_mairix_rc
}

#--------------------------------------------------------------------------
# sets the desired output format for matched messages
#--
# $1 - either "maildir", "mh", or "mbox"
#
published_conf_set_mformat () {
    [[ $# -eq 1 ]] || error "The published conf_set_mformat requires exactls one arguments, but received $# arguments."
    local MFORMAT="$1"
    [[ "maildir" = "$MFORMAT" || "mh" = "$MFORMAT" || "mbox" = "$MFORMAT" ]] || error "Unknown format \"$MFORMAT\" in conf_set_mformat"
    SEARCH_RESULT_FORMAT="$MFORMAT"
    generate_mairix_rc
}

###########################################################################
#
# Adding, removing and searching messages
#
###########################################################################

#--------------------------------------------------------------------------
# adds messages to the database and configuration file.
#--
# $1 - The format of the messages "maildir", "mh", or "mbox"
# $2, $3, $4, ... - The messages to add, relative to
#           $MAILDIR_DIR_ABS, if $1 = "maildir",
#           $MH_DIR_ABS,      if $1 = "mh", or
#           $MBOX_DIR_ABS,    if $1 = "mbox".
#
published_add_messages() {
    [[ $# -ge 2 ]] || error "add_messages requires at least two arguments, but received $# arguments."
    local SOURCE_FORMAT="$1"
    shift

    local FORMAT_DIR_ABS  # used for the absolute directory to which $2, $3,
                          # ... are relative to.
    local FORMAT_DIR_RELD # $FORMAT_DIR_ABS relative to $DATA_DIR_ABS
    case "$SOURCE_FORMAT" in
	"maildir" )
	    set_dir_variable_RELD FORMAT "$MAILDIR_DIR_RELD"
	    ;;
	"mh" )
	    set_dir_variable_RELD FORMAT "$MH_DIR_RELD"
	    ;;
	"mbox" )
	    set_dir_variable_RELD FORMAT "$MBOX_DIR_RELD"
	    ;;
	"mmdf" )
	    set_dir_variable_RELD FORMAT "$MMDF_DIR_RELD"
	    ;;
	*)
	    error "unknown message kind $SOURCE_FORMAT"
	    ;;
    esac

    # FORMAT_DIR_ABS and FORMAT_DIR_RELD have been set

    local SOURCE_UNSP_RELU # a single mbox, maildir, mh relative to $FORMAT_DIR_ABS
    for SOURCE_UNSP_RELU in "$@"
    do
	local SOURCE_UNSP_RELB="${FORMAT_DIR_RELD}/${SOURCE_UNSP_RELU}"

        # The source relative to the test base directory equals the target
        # relative to the data directory. Hence, we may use reuse
        # $SOURCE_UNSP_RELB .
	conf_add_"$SOURCE_FORMAT" "${SOURCE_UNSP_RELB}"

	assert_file_exists "$SOURCE_UNSP_RELB"

	if [ -d "$SOURCE_UNSP_RELB" ]
	then
	    # SOURCE_UNSP_* is a directory. We explicitize this additional
	    # semantic by using SOURCE_DIR_* variables
	    local SOURCE_DIR_RELU="$SOURCE_UNSP_RELU"
	    local SOURCE_DIR_RELB="$SOURCE_UNSP_RELB"

	    case "$SOURCE_FORMAT" in

		"maildir" | "mh" ) #-------------- maildir / mh directory -----
		    local TARGET_DIR_ABS="$FORMAT_DIR_ABS/$SOURCE_DIR_RELU"

		    # We generate the required directories via find instead of
		    # a simple mkdir, to also create empty cur, tmp, new
		    # directories for maildirs
		    find "$SOURCE_DIR_RELB" -type d -printf "$TARGET_DIR_ABS/%P\n" | xargs mkdir -p

		    local SOURCE_FILE_RELU # a single message of a maildir / mh
		                       # folder, relative to "$SOURCE_DIR_RELB"

		    # We have to pipe the output of find through sort, to
		    # ensure the message order is consistent.
		    for SOURCE_FILE_RELU in $(find "$SOURCE_DIR_RELB" -type f -printf "%P\n" | sort )
		    do
			cp "$SOURCE_DIR_RELB/$SOURCE_FILE_RELU" "$TARGET_DIR_ABS/$SOURCE_FILE_RELU"
			# After copying the file, we immediately update the
			# database. Thereby we assure that the databases are
			# built up in the same manner for every invocation
			# of a test, regardless in which order the system
			# returns the files.
			update_database
		    done
		    ;; #-------------------------- maildir / mh directory -end-

		* )
		    error "directories not implemented for $SOURCE_FORMAT"
		    ;;

	    esac

	else
	    assert_file_exists_is_file "$SOURCE_UNSP_RELB"
	    # SOURCE_UNSP_* is a file. We explicitize this additional semantic
            # by using SOURCE_FILE_* variables
	    local SOURCE_FILE_RELB="$SOURCE_UNSP_RELB"
	    local SOURCE_FILE_RELU="$SOURCE_UNSP_RELU"

	    case "$SOURCE_FORMAT" in

		"mbox" ) #------------------------------------- mbox file -----
		    local TARGET_FILE_ABS="$FORMAT_DIR_ABS/$SOURCE_FILE_RELU"

		    mkdir -p "$(dirname "$TARGET_FILE_ABS" )"
		    cp "$SOURCE_FILE_RELB" "$TARGET_FILE_ABS"

		    # immediately update the database, after copying the
		    # message. This is especially importart for $# > 2, to
		    # assure that the databases are built up in the same
		    # manner for every invocation of a test, regardless of
		    # the order the system returns the files from disk.
		    update_database

		    # For mboxen, we split them, to allow references to
		    # individual messages within the mboxen
		    if [ manymessages != "$SOURCE_UNSP_RELU" ]; then
			"${SCRIPT_DIR_ABS}/split_mbox.sh" "$TARGET_FILE_ABS"
		    fi
		    ;; #--------------------------------------- mbox file -end-

		"mmdf" ) #------ mmdf same as mbox but splits differently -----
		    local TARGET_FILE_ABS="$FORMAT_DIR_ABS/$SOURCE_FILE_RELU"
		    mkdir -p "$(dirname "$TARGET_FILE_ABS" )"
		    cp "$SOURCE_FILE_RELB" "$TARGET_FILE_ABS"
		    update_database
		    "${SCRIPT_DIR_ABS}/split_mmdf.sh" "$TARGET_FILE_ABS"
		    ;; #--------------------------------------- mmdf file -end-

		* )
		    error "single files not implemented for $SOURCE_FORMAT"
		    ;;

	    esac
	fi
    done
}

#--------------------------------------------------------------------------
# removes messages from the database and configuration file.
# No purging is done
#--
# $1 - The format of the messages "maildir", "mh", or "mbox"
# $2, $3, $4, ... - The messages to add, relative to
#           $MAILDIR_DIR_ABS, if $1 = "maildir",
#           $MH_DIR_ABS,      if $1 = "mh", or
#           $MBOX_DIR_ABS,    if $1 = "mbox".
#
published_remove_messages() {
    [[ $# -ge 2 ]] || error "remove_messages requires at least two arguments, but received $# arguments."
    local FORMAT="$1"
    shift

    local FORMAT_DIR_ABS  # used for the absolute directory to which $2, $3,
                          # ... are relative to.
    local FORMAT_DIR_RELD # $FORMAT_DIR_ABS relative to $DATA_DIR_ABS
    case "$FORMAT" in
	"maildir" )
	    set_dir_variable_RELD FORMAT "$MAILDIR_DIR_RELD"
	    ;;
	"mh" )
	    set_dir_variable_RELD FORMAT "$MH_DIR_RELD"
	    ;;
	"mbox" )
	    set_dir_variable_RELD FORMAT "$MBOX_DIR_RELD"
	    ;;
	*)
	    error "unknown message kind $FORMAT"
	    ;;
    esac

    # FORMAT_DIR_ABS and FORMAT_DIR_RELD have been set

    local TARGET_UNSP_RELU # a single mbox, maildir, mh relative to $FORMAT_DIR_ABS
                           # this is what we want to remove
    for TARGET_UNSP_RELU in "$@"
    do
	local TARGET_UNSP_RELD="${FORMAT_DIR_RELD}/${TARGET_UNSP_RELU}"
	local TARGET_UNSP_ABS="${DATA_DIR_ABS}/${TARGET_UNSP_RELD}"

	# We do not allow to remove single messages, but only folders (or
        # folders of mail folders). Hence we assure, that TARGET_UNSP_RELU
        # does not refer to a single message
	case "$FORMAT" in
	    "maildir" )
		assert_file_exists_is_directory "$TARGET_UNSP_ABS"
		;;
	    "mh" )
		assert_file_exists_is_file "$TARGET_UNSP_ABS/.mh_sequences"
		;;
	    "mbox" )
		assert_file_exists "$TARGET_UNSP_ABS"
		;;
	    *)
		error "unknown message kind $FORMAT"
		;;
	esac

	# removing the messages
	rm -rf "$TARGET_UNSP_ABS"

	# removing it from the configuration
	conf_remove_"$FORMAT" "${TARGET_UNSP_RELD}"
    done

    update_database

}

#--------------------------------------------------------------------------
# searches for messages in the database
#--
# $1 - the dump that has to represent the database /after/ conducting the
#      search. Relative to dumps in the base directory.
# $2, $3, $4, ... - The search expressions in the format understood
#                       by mairix
#
published_search_messages() {
    [[ $# -ge 2 ]] || error "search_messages requires at least two arguments, but received $# arguments."

    log "Searching for" \""${@:2}"\"

    # We assert that there are no more unasserted matches from the previous
    # search.
    # This is not a problem, if the current search is the test's first search,
    # and it is not a problem if the previous search already called
    # assert_no_more_matches.
    # However, if the previous search returned more messages than intended and
    # the tested forgot to call assert_no_more_matches, we would not notice,
    # if we omitted the next line.
    published_assert_no_more_matches


    # cleaning up the previous search result.
    rm -rf "${SEARCH_RESULT_DIR_ABS}"
    rm -rf "${SEARCH_RESULT_SPLIT_DIR_ABS}"


    # Avoid skipping the database validity check, by assuring that $1 is
    # the specially reserved marker. Tests /have/ to assert the validity
    # of the database for each search.
    if [ "$1" = "$MARKER_NO_DUMP" ]
    then
	error "\"$MARKER_NO_DUMP\" is reserved and may not be used as name for a dump."
    fi

    # It may be tempting for tests to tunnel commands through to mairix via
    # searches, as for example:
    #    search_messages --purge
    # We try to eliminate such uses, by forbidding search expressions starting
    # in "-", if they do not refer to thread searching.
    for PARAM in "${@:2}"
    do
	if [ "${PARAM:0:1}" = "-" -a "$PARAM" != "-t"  -a "$PARAM" != "--threads" ]
	then
	    error "Search pattern starts with \"-\". Do not try to sneak parameters to mairix through searches."
	fi
    done

    # Perform the search. We briefly suspend stopping the whole test on an
    # non-zero return value of mairix, as empty search results (which mairix
    # reports by a non-zero return value) may be intended.
    ABORT_ON_FAILING_MAIRIX=0
    run_mairix "$@"
    ABORT_ON_FAILING_MAIRIX=1

    # When asserting matched messages later on, it may be that the
    # SEARCH_RESULT_FORMAT has already been changed by set_mformat. Hence, we
    # need to keep track of the format used for the search.
    USED_SEARCH_RESULT_FORMAT="$SEARCH_RESULT_FORMAT"


    # Updating the array of messages that need to be asserted
    MATCHED_NOT_YET_ASSERTED_MESSAGES=( )
    case "$USED_SEARCH_RESULT_FORMAT" in
	"maildir" | "mh" )
	    MATCHED_NOT_YET_ASSERTED_MESSAGES=( $(find "${SEARCH_RESULT_DIR_ABS}" -type f -printf "file:%p " \, -type l -printf "link:%l ") )
	    ;;
	"mbox" )
	    log "Splitting mbox of search result"
	    "${SCRIPT_DIR_ABS}/split_mbox.sh" --split-to "${SEARCH_RESULT_SPLIT_DIR_ABS}" "${SEARCH_RESULT_DIR_ABS}"
	    MATCHED_NOT_YET_ASSERTED_MESSAGES=( $(find "${SEARCH_RESULT_SPLIT_DIR_ABS}" -type f -printf "file:%p " \, -type l -printf "link:%l ") )
	    ;;
	* )
	    error "Unknown mformat used in search_message"
	    ;;
    esac
}

#--------------------------------------------------------------------------
# takes an element (by a given index) out of the
# MATCHED_NOT_YET_ASSERTED_MESSAGES array.
#--
# $1 ... - The index of the element to remove from
#          MATCHED_NOT_YET_ASSERTED_MESSAGES
#
remove_idx_from_MATCHED_NOT_YET_ASSERTED_MESSAGES ()
{
    [[ $# -eq 1 ]] || error "remove_idx_from_MATCHED_NOT_YET_ASSERTED_MESSAGES requires exactly one argument, but received $# arguments."
    local IDX="$1"
    local ELEMENT_COUNT=${#MATCHED_NOT_YET_ASSERTED_MESSAGES[@]}

    MATCHED_NOT_YET_ASSERTED_MESSAGES=(
	${MATCHED_NOT_YET_ASSERTED_MESSAGES[@]:0:$IDX} # slice before $IDX
	${MATCHED_NOT_YET_ASSERTED_MESSAGES[@]:$((IDX+1)):$((ELEMENT_COUNT-IDX))} # slice after $IDX
    )
}


#--------------------------------------------------------------------------
# asserts that a message has been matched by the previous search result, but has not yet been asserted.
# This function poses stricter requirements on its parameters. For a less strict variant, refer to published_assert_match.
#--
# $1 - either "file" or "link", depending on whether a file or link target should be asserted
# $2 - if $1 = "file", the file to search for
#      if $1 = "link", the link target to search for
# $3 - the source file for the message.
#        if $1 = "file", $3 is the source of the message
#        if $1 = "link", $3 is not used
#
assert_match () {
    [[ $# -eq 3 ]] || error "assert_match requires exactly three argument, but received $# arguments."
    local MATCH_VIA="$1"
    local GOOD_FILE_ABS="$2"
    local SOURCE_OF_FILE_ABS="$3"

    # the number of matched but not yet asserted messages
    local ELEMENT_COUNT=${#MATCHED_NOT_YET_ASSERTED_MESSAGES[@]}

    local FOUND=0 # set to a non-zero value, if the message has been found in
                  # the array of matched, but not yet asserted messages.

    assert_file_exists_is_file "$GOOD_FILE_ABS"
    [[ "${GOOD_FILE_ABS:0:1}" = "/" ]] || error "the good file is not given by an absolute path"
    assert_file_exists_is_file "$SOURCE_OF_FILE_ABS"
    [[ "${SOURCE_OF_FILE_ABS:0:1}" = "/" ]] || error "the source file is not given by an absolute path"

    case "$MATCH_VIA" in
	"link" ) # searching for link -------------------------------------
	    local NEEDLE="link:${GOOD_FILE_ABS}" # we search for this in the haystack
	    local IDX=0
	    while [ $FOUND != 1 -a $IDX -lt $ELEMENT_COUNT ]
	    do
		if [ "$NEEDLE" = "${MATCHED_NOT_YET_ASSERTED_MESSAGES[$IDX]}" ]
		then
		    # found the needle in the haystack. Removing it and mark
		    # as done
		    remove_idx_from_MATCHED_NOT_YET_ASSERTED_MESSAGES $IDX
		    FOUND=1
		fi
		IDX=$((IDX + 1))
	    done

	    # Result in error, if the required message could not be found
	    if [ "$FOUND" = "0" ]
	    then
		error "search result does not contain a link to \"${GOOD_FILE_ABS}\""
	    fi

	    ;;
	"file" ) # searching for file -------------------------------------
	    # Mairix prepends an X-source-folder header when messages are
	    # copied instead of linked. Hence, we cannot match
	    # GOOD_FILE_ABS directly, but have to match against a message
	    # containing the X-source-folder and the contents of
	    # GOOD_FILE_ABS. We build such a file at
	    # ASSERT_MATCH_TEMP_FILE_ABS
	    set_file_variable_RELD ASSERT_MATCH_TEMP assert_match.temp

	    # creating the header prepended copy of GOOD_FILE_ABS
	    echo "X-source-folder: $SOURCE_OF_FILE_ABS" >"$ASSERT_MATCH_TEMP_FILE_ABS"
	    cat "$GOOD_FILE_ABS" >>"$ASSERT_MATCH_TEMP_FILE_ABS"

	    # the matching part
	    local IDX=0
	    while [ $FOUND != 1 -a $IDX -lt $ELEMENT_COUNT ]
	    do

		# Omit matches, that are links and search only for results,
		# that are files
		if [ "file:" = "${MATCHED_NOT_YET_ASSERTED_MESSAGES[$IDX]:0:5}" ]
		then

		    if diff -q "$ASSERT_MATCH_TEMP_FILE_ABS" "${MATCHED_NOT_YET_ASSERTED_MESSAGES[$IDX]:5}" &>/dev/null
		    then
			# found the file in the search result. Removing it and
			# mark as done
			remove_idx_from_MATCHED_NOT_YET_ASSERTED_MESSAGES $IDX
			FOUND=1
		    fi
		fi
		IDX=$((IDX + 1))
	    done

	    # Result in error, if the required message could not be found
	    if [ "$FOUND" = "0" ]
	    then
		error "search result does not contain a file equivalent to \"${GOOD_FILE_ABS}\""
	    fi

	    # Cleaning up the header prepended message
	    rm -f "$ASSERT_MATCH_TEMP_FILE_ABS"

	    ;;

	* ) # -------------------------------------------------------------
	    error "assert_match for type \"$MATCH_VIA\" not implemented"
	    ;;
    esac
}

#--------------------------------------------------------------------------
# asserts that a message has been matched by the previous search result, but
# has not yet been asserted.
#--
# $1 - either "maildir", "mh", or "mbox", indicating the format of the message
#      to assert.
# $2 - name of the file (or the target of the link, if file can be linked) to
#      assert in search result. Relative to the corresponding subdirectory of
#      the base directory:
#          messages/maildir/,     if $1 == "maildir"
#          messages/mh/,          if $1 == "mh"
#          messages/mbox_split/,  if $1 == "mbox"
#
published_assert_match () {
    [[ $# -eq 2 ]] || error "published assert_match requires exactly two argument, but received $# arguments."
    local GOOD_FILE_MAIL_FORMAT="$1"
    local GOOD_FILE_RELU="$2"

    local GOOD_FILE_ABS= # This is the file we want to assert matched. For
                         # mboxen, this is a part.* file in the split
                         # subdirectory.
    local GOOD_IS_MATCHED_VIA= # Indicates whether the matched message shoud
                         # be a link to the good file, or a file equivalent
                         # to the good file.
    local SOURCE_OF_FILE_ABS= # The originating file for the good file. This
                         # is mainly used for mboxen, that are given by a
                         # split part.*. There, it refers to the unsplit mbox.


    # assure that for mboxen, the file ends in part* to be able to properly
    # infer SOURCE_OF_FILE_ABS
    if [ "$GOOD_FILE_MAIL_FORMAT" = "mbox" ]
    then
	if [ "$(echo "${GOOD_FILE_RELU}" | grep -c '\(^\|/\)part\.[0-9][0-9]*$')" != "1" ]
	then
	    error "The name of files for mboxes in assert_match has to be \"part.\" followed by a number as name (eg \"animals/part.0\")."
	fi
    fi

    case "$USED_SEARCH_RESULT_FORMAT" in
	"" )
	    error "Unset USED_SEARCH_RESULT_FORMAT. Maybe you tried to use assert_match before searching for messages"
	    ;;
	"maildir" | "mh" ) #-------------- result in maildir or mh format ----
	    case "$GOOD_FILE_MAIL_FORMAT" in
		"maildir" )
		    GOOD_IS_MATCHED_VIA="link"
		    GOOD_FILE_ABS="$MAILDIR_DIR_ABS/$GOOD_FILE_RELU"
		    ;;
		"mh" )
		    GOOD_IS_MATCHED_VIA="link"
		    GOOD_FILE_ABS="$MH_DIR_ABS/$GOOD_FILE_RELU"
		    ;;
		"mbox" )
		    GOOD_IS_MATCHED_VIA="file"
		    GOOD_FILE_ABS="$MBOX_SPLIT_DIR_ABS/$GOOD_FILE_RELU"
		    SOURCE_OF_FILE_ABS="$MBOX_DIR_ABS/${GOOD_FILE_RELU%/part*}"
		    ;;
		* )
		    error "Unknown file format \"$GOOD_FILE_MAIL_FORMAT\" in assert_match for a search conducted in $USED_SEARCH_RESULT_FORMAT format"
		    ;;
	    esac
	    ;;

	"mbox" ) #--------------------------------- result in mbox format ----
	    case "$GOOD_FILE_MAIL_FORMAT" in
		"maildir" )
		    GOOD_IS_MATCHED_VIA="file"
		    GOOD_FILE_ABS="$MAILDIR_DIR_ABS/$GOOD_FILE_RELU"
		    ;;
		"mh" )
		    GOOD_IS_MATCHED_VIA="file"
		    GOOD_FILE_ABS="$MH_DIR_ABS/$GOOD_FILE_RELU"
		    ;;
		"mbox" )
		    GOOD_IS_MATCHED_VIA="file"
		    GOOD_FILE_ABS="$MBOX_SPLIT_DIR_ABS/$GOOD_FILE_RELU"
		    SOURCE_OF_FILE_ABS="$MBOX_DIR_ABS/${GOOD_FILE_RELU%/part*}"
		    ;;
		* )
		    error "Unknown file format \"$GOOD_FILE_MAIL_FORMAT\" in assert_match for a search conducted in $USED_SEARCH_RESULT_FORMAT format"
		    ;;
	    esac
	    ;;

	* ) # ----------------------------------------------------------------
	    error "Unknown search format \"$USED_SEARCH_RESULT_FORMAT\""
	    ;;
    esac

    if [ -z "$SOURCE_OF_FILE_ABS" ]
    then
	SOURCE_OF_FILE_ABS="$GOOD_FILE_ABS"
    fi

    [[ ! -z "$GOOD_IS_MATCHED_VIA" ]] || error "It is not specified, how the asserted message is referenced"
    [[ ! -z "$GOOD_FILE_ABS" ]] || error "asserted file name is empty"
    [[ ! -z "$SOURCE_OF_FILE_ABS" ]] || error "asserted source file name is empty"

    assert_match "$GOOD_IS_MATCHED_VIA" "$GOOD_FILE_ABS" "$SOURCE_OF_FILE_ABS"
}

#--------------------------------------------------------------------------
# logs the messages matched in the last search that have not yet been
# asserted by the test
#
# This function is typically only useful when developing tests.
#--
# <no parameters>
#
published_log_remaining_matched_unasserted_messages ()
{
    [[ $# -eq 0 ]] || error "published log_remaining_matched_unasserted_messages does not requires any arguments, but received $# arguments."
    local ELEMENT_COUNT=${#MATCHED_NOT_YET_ASSERTED_MESSAGES[@]}
    local IDX
    log "$ELEMENT_COUNT messages have been matched, but not yet asserted:"
    for IDX in $(seq 0 $((ELEMENT_COUNT-1)) )
    do
	log "  ${MATCHED_NOT_YET_ASSERTED_MESSAGES[$IDX]}"
    done
    log "-- end of list of messages --"
}

#--------------------------------------------------------------------------
# asserts that all messages matched by the previous search have been
# asserted
#--
# <no parameters>
#
published_assert_no_more_matches () {
    [[ $# -eq 0 ]] || error "published assert_no_more_matches does not requires any arguments, but received $# arguments."

    [[ ${#MATCHED_NOT_YET_ASSERTED_MESSAGES[@]} = 0 ]] || error "There are unasserted results from previous search. Eg: ${MATCHED_NOT_YET_ASSERTED_MESSAGES}"
}

###########################################################################
#
# Managing the mairix database itself
#
###########################################################################

#--------------------------------------------------------------------------
# runs mairix and after dumping the current database state, checks it for
# validity
#--
# $1 - filename of a dump known to be good (relative to dumps in the base
#      directory). If $1 = $MARKER_NO_DUMP, then the dumping and test for
#      the validity of the database are skipped.
# $2, $3, ... - the parameters passed to mairix. No checking is done on
#               those parameters
#
run_mairix() {
    [[ $# -ge 1 ]] || error "run_mairix requires at least one argument, but received $# arguments."
    local GOOD_DUMP_FILE_RELU="$1" # relative to dump in the base directory

    "$MAIRIX_EXE_FILE_RELB" \
	--force-hash-key-new-database "$HASH_KEY" \
	--rcfile "$MAIRIX_RC_FILE_ABS" \
	"${@:2}" || if [ "$ABORT_ON_FAILING_MAIRIX" != "0" ] ; then error "mairix crashed internally" ; fi


    #checking for the validity of the database (skipped, if the marker has been passed)
    if [ "$1" != "$MARKER_NO_DUMP" ]
    then
	published_assert_dump "$1"
    fi
}

#--------------------------------------------------------------------------
# updates the database by adding newly adjoining messages and marking no
# longer existing messages as deleted.
#--
# <no parameters>
#
update_database () {
    [[ $# -eq 0 ]] || error "update_database does not accept arguments, but received $# arguments."
    run_mairix "$MARKER_NO_DUMP" 2>"$DATA_DIR_ABS/update_database.stderr"
    if [ -s "$DATA_DIR_ABS/update_database.stderr" ]; then
      echo "mairix unexpectedly emited error output:"
      echo "<<<<<"
      cat "$DATA_DIR_ABS/update_database.stderr"
      echo ">>>>>"
      false
    fi
}

#--------------------------------------------------------------------------
# creates a dump of the current state of the mairix database
#
# Although being used internally, this function is mainly useful when
# developing tests.
#--
# $1 - filename to store the dump into (either absolute, or relative to the
#      base directory)
#
published_dump_database() {
    [[ $# -eq 1 ]] || error "dump_database requires exactly one arguments, but received $# arguments."
    local DUMP_FILE_UNSP="$1" # absolute or relative to the base directory

    # Dumping the database and striping out the data dir, so the dumps
    # become portable
    run_mairix "$MARKER_NO_DUMP" --dump | sed -e 's#^\([[:space:]]*[0-9]*:[[:space:]]*\(FILE\|[0-9]* msgs in\) \)'"$DATA_DIR_ABS"'/#\1#' -e 's/^\(Dump of \).*\(database\)$/\1\2/' >"$DUMP_FILE_UNSP"
}

#--------------------------------------------------------------------------
# assert that the given dump reflects the current state of the database
#--
# $1 - filename of the dump to assert (relative to dumps in the base
#      directory)
#
published_assert_dump() {
    [[ $# -eq 1 ]] || error "assert_database requires exactly one argument, but received $# arguments."

    # GOOD_DUMP_FILE_RELB is the dump, we want to assert
    local GOOD_DUMP_FILE_RELB="dumps/$1" # relative to the base directory
    assert_file_exists_is_file "$GOOD_DUMP_FILE_RELB"

    published_dump_database "$CURRENT_STATE_DUMP_FILE_ABS"

    # Continue only, if the towdumps match
    diff -q "$GOOD_DUMP_FILE_RELB" "$CURRENT_STATE_DUMP_FILE_ABS" &>/dev/null || error "The current state of the database does not match the asserted dump \"$GOOD_DUMP_FILE_RELB\". A dump of the current database con be found in \"$CURRENT_STATE_DUMP_FILE_ABS\""
}

#--------------------------------------------------------------------------
# purges the database and asserts that the given dump reflects the state of
# the database after purging
#--
# $1 - dump to assert after purging (relative to dumps in the base
#      directory)
#
published_purge_database() {
    [[ $# -eq 1 ]] || error "published purge_database requires exactly one argument, but received $# arguments."

    # Avoid skipping the database validity check, by assuring that $1 is
    # the specially reserved marker. Tests /have/ to assert the validity
    # of the database for each search.
    if [ "$1" = "$MARKER_NO_DUMP" ]
    then
	error "\"$MARKER_NO_DUMP\" is reserved and may not be used as name for a dump."
    fi

    run_mairix "$1" --purge
}


###########################################################################
#
# Setting up the environment for the test
#
###########################################################################


#--------------------------------------------------------------------------
# print invocation information
#--
# <no parameters>
print_help() {
    cat <<EOF
Usage:
$0 [ --quiet ] FILE

runs the mairix test specified in the file FILE in the test-specification format documented in the file README.format of the mairix test subdirectory.

If the test succeeds, the script's return value is 0.
If the test fails, the script's return value is non-zero.

If the --quiet option is supplied, the output of the test is suppressed
EOF
    exit 1
}

QUIET=0 # when setting up the logging,
        # QUIET = 0  means to just store it in a file.
        # QUIET != 0 means to print the log in addition to storing it in a file.

# checking for correct invokation. Dumping invokation information if required
while [ $# -ge 1 ]
do
    case "$1" in
	"--help" )
	    print_help
	    ;;
	"--quiet" )
	    QUIET=1
	    ;;
	* )
	    if [ $# -gt 1 ]
	    then
		print_help
	    fi
	    TEST_SPEC_FILE_UNSP="$1"
	    ;;
	esac
    shift
done


#turning the TEST_SPEC file into an absolute path
#we postpone testing for a proper file, until after we have set-up the logging
if [ "${TEST_SPEC_FILE_UNSP:0:1}" = "/" ]
then
    TEST_SPEC_FILE_ABS="$TEST_SPEC_FILE_UNSP"
else
    TEST_SPEC_FILE_ABS="${PWD}/$TEST_SPEC_FILE_UNSP"
fi
assert_file_exists_is_file "${TEST_SPEC_FILE_ABS}" # error of this command
          # is not captured in the log file, as logging is not yet set up.

# change to the base directory and store the directory for the scripts
cd "$(dirname "$0")"/..
SCRIPT_DIR_ABS="${PWD}"/"scripts"

MAIRIX_EXE_FILE_RELB=../mairix

HASH_KEY=1 # the hash key used for new databases.
           # This is set to a fixed non-random number to allow
           # consistent dumps during different invocations of a test.

MARKER_NO_DUMP="<none>" # This is used to start mairix (see run_mairix
           # function) without asserting the validity of the database via
           # a dump.
ABORT_ON_FAILING_MAIRIX=1 # Typically, the function run_mairix fails, if mairix
	   # returns a non-zero value. There are however situations, when a
	   # non-zero return value makes sense and should not abort the running
	   # test (e.g.: asking for an empty search result on purpose).
	   # ABORT_ON_FAILING_MAIRIX allows to control this behaviour.
	   # If ABORT_ON_FAILING_MAIRIX is 0, a non-zero return value of mairix
	   # is does not abort the current test.
	   # If ABORT_ON_FAILING_MAIRIX is non-zero, a non-zero return value of
	   # mairix aborts the current test.

MATCHED_NOT_YET_ASSERTED_MESSAGES=( ) # this array holds pointers to those
	   # messages that have been matched in the last search, but have not
	   # yet been asserted by the current test. The array is built up of
	   # strings, that either contain "link:FILE" to denote links to the
	   # file FILE, or they contain "file:source" to denote a file
	   # equivalent to the file "FILE".



# Upon running a test, all relevant information (dumps, logs, messages, etc)
# are stored within DATA_DIR_ABS
DATA_DIR_ABS="${PWD}/$(basename "${TEST_SPEC_FILE_ABS%.test-spec}.data")"
assert_file_does_not_exist "${DATA_DIR_ABS}" # error of this command is not
                  # captured in the log file, as logging is not yet set up.
mkdir -p "$DATA_DIR_ABS"

set_file_variable_RELD LOG "log" # the file to store the log
# setting up the logging
if [ "$QUIET" = "0" ]
then
    exec 5> >( tee -a "$LOG_FILE_ABS" )
else
    exec 5> "$LOG_FILE_ABS"
fi
exec &> >(cat >&5)

set_file_variable_RELD MAIRIX_RC "mairixrc" # the mairix configuration file
                                            # for the test
set_file_variable_RELD CURRENT_STATE_DUMP "database.dump" # the place to
                                                          # store dumps to

set_dir_variable_RELD MAILDIR    "messages/maildir"
set_dir_variable_RELD MH         "messages/mh"
set_dir_variable_RELD MBOX       "messages/mbox"
set_dir_variable_RELD MMDF       "messages/mmdf"
set_dir_variable_RELD MBOX_SPLIT "messages/mbox_split" # for the automatically
                                     # split messages originating from an mbox

set_dir_variable_RELD SEARCH_RESULT       "search_result"
set_dir_variable_RELD SEARCH_RESULT_SPLIT "search_result_split" # used only if
                  # matched messages are requested in mbox format. Then the
                  # mbox of matched messages gets split into this folder.

CONF_MAILDIR=   # the maildir entries for the mairix configuration file
CONF_MH=        # the mh      entries for the mairix configuration file
CONF_MBOX=      # the mbox    entries for the mairix configuration file

SEARCH_RESULT_FORMAT="maildir"
generate_mairix_rc
USED_SEARCH_RESULT_FORMAT="$SEARCH_RESULT_FORMAT" # the result format for
                                        # the previously conducted search.

###########################################################################
#
# Functions for executing a test
#
###########################################################################

#--------------------------------------------------------------------------
# Given a test-specification file, this function outputs test specification
# after stripping the comments, and adding a final assertion that all
# matched messages have been asserted.
#--
# $1 : the test specification file to filter (absolute file name)
#
prepare_test_file () {
    local INPUT_FILE_ABS="$1"
    assert_file_exists_is_file "$INPUT_FILE_ABS"

    # We strip leading white spaces and comments of the file. But we do not
    # actually remove empty lines, to allow proper line counting in the file
    # interpreting loop
    sed -e 's/^[[:space:]]*//' -e 's/^\([^#]*\)#.*/\1/' "$INPUT_FILE_ABS"

    # Adding some sanity checks at the end. We prepend them by #auto#, so the
    # file interpreting loop can detect those commands (commands beginning
    # with # would have been discarded as comment) as being automatically
    # added and not actually part of the source file.
    echo ""
    echo "#auto#assert_no_more_matches"
}

#--------------------------------------------------------------------------
# Executing the test.
#
# Iteration over the (prepared) test file, while delegating the individual
# actions to the relevant published_* functions.
#--
#
LINE_NR=1
prepare_test_file "${TEST_SPEC_FILE_ABS}" | while read COMMAND OPTIONS
do
    if [ ! -z "$COMMAND" -o ! -z "$OPTIONS" ]
    then
	if [ "${COMMAND:0:6}" = "#auto#" ]
	then
	    COMMAND=${COMMAND:6}
	    LINE_DESC="(implicitly added)"
	else
	    LINE_DESC="(line: $LINE_NR)"
	fi
	log "== $COMMAND $OPTIONS == $LINE_DESC"

	case "$COMMAND" in
	    "add_messages" | \
		"assert_dump" | \
		"assert_no_more_matches" | \
		"assert_match" | \
		"conf_set_mformat" | \
		"dump_database" | \
		"log_remaining_matched_unasserted_messages" | \
		"purge_database" | \
		"remove_messages" | \
		"search_messages" )
		published_$COMMAND $OPTIONS
		;;
	    * )
		error "Unknown test directive: $COMMAND"
		;;
	esac
    fi
    LINE_NR=$(($LINE_NR+1))
done
