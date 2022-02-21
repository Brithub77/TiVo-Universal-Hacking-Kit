##############################################################################
# Start hacks
#############################################################################
TM=/hack/bin/ticketmaster
if [[ $hacks  == *"ticketmaster"* ]] ; then
	if [ -x $TM -a ! -f /var/ticketmaster.block ]; then
		/tvbin/text2osd  --line 24 --message "Loading ticketmaster $($TM -V)"  --xscale 1 --bgcolor 33 33 33
		[ -f /var/log/ticketmaster.log ] &&  mv /var/log/ticketmaster.log /var/log/Oticketmaster.log
		$TM -l /var/log/ticketmaster.log &
		# Prevent run next time (file will be deleted on sucessful boot)
		/hack/bin/touch /var/ticketmaster.block
	else
		/tvbin/text2osd  --line 24 --message "Not loading ticketmaster"  --xscale 2 --bgcolor 128 0 0
	fi
fi
