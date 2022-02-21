#!/bin/bash
# backup MFS keyring to tar file
if [ $# -ne 1 ]; then
	echo "Usage: $1 <file> - backup MFS keyring to <file>"
	exit 1
fi
[ -d /tmp/keyring.tmp ] && rm -r /tmp/keyring.tmp
mkdir /tmp/keyring.tmp
mfs_dumpobj -r /State/Keyring > /tmp/keyring.tmp/keyring.dump
for x in $(mfs_ls /State/Keyring | awk '/tyDb/ { print $3 }'); do
	fn=$(echo $x | sed s/:/_/)
	mfs_export /State/Keyring/$x > /tmp/keyring.tmp/$fn.export
done
tar -c -f $1 -C /tmp/keyring.tmp .
echo "Backup file is $1"
rm -r /tmp/keyring.tmp
