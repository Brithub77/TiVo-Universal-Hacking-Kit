#!/bin/bash
# OzTiVo update script

source /hack/etc/oztivo.config

# Add some random delay to reduce load on server
if [ "$1" == "-r" ]; then
        T=$(( ($RANDOM / 1000) * 60))
        echo "Sleeping for $T seconds"
        sleep $T
fi

cleanup() {
        rm -rf /tmp/swupdate
}

fatal() {
        echo $@
        cleanup
        exit 1
}

mkdir -p /tmp/swupdate
echo "Checking for update. UpdateUrl is $UpdateUrl"
wget -q -O /tmp/swupdate/update.bnd "$UpdateUrl" || fatal "Nothing downloaded"
cd /tmp/swupdate
echo "Unbundling..."
tar xvf update.bnd || fatal "Failed to unbundle update file"
echo "Checking signature"
crypto  -vfs swupdate.sig swupdate.tar.gz /hack/etc/OzTiVo-SW-Update.pub || fatal "Failed to validate package"
echo "Extracting..."
tar xvzf swupdate.tar.gz || fatal "Failed to extract update"
echo "Running update"
/tmp/swupdate/update.sh || fatal "Failed to run update script"

cleanup
exit 0
