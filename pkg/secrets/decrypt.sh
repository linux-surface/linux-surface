#!/bin/sh

PASS=""
FILE=""

while getopts ":p:f:" args; do
	case "$args" in
	p)
		PASS=$OPTARG
		;;
	f)
		FILE=$OPTARG
		;;
	esac
done
shift $((OPTIND-1))

OUTPUT=$(echo $FILE | sed 's/.gpg$//g')

gpg --quiet --no-tty --batch --yes --decrypt \
	--passphrase="$PASS" --output $OUTPUT $FILE
