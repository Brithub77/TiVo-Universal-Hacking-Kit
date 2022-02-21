#!/bin/bash

# Dialog height and width
H=0
W=0
TITLE="TiVo-S3-Get-Password-Utility-Version-1.0"
DIALOG="dialog --backtitle ${TITLE}"

info() {
	$DIALOG --title "Info" --infobox "$@" $H $W
}

msg() {
	$DIALOG --title "Message" --msgbox "$1" $H $W
}

get_password() {
	unset TIVO_ROOT_PASSWORD
	while true; do
		local pw1=$($DIALOG --no-cancel --title "Enter password" --passwordbox "Enter a new password for root user on TiVo (password will not be displayed)" $H $W 3>&1 1>&2 2>&3) 
		local pw2=$($DIALOG --no-cancel --title "Verify password" --passwordbox "Enter the same password again to verify (password will not be displayed)" $H $W 3>&1 1>&2 2>&3) 
		if [ "x$pw1" != "x$pw2" ]; then
			msg "Passwords do not match"
		elif [ "x$pw1" == "x" ]; then
			msg "Password is blank"
			return 1
		else
			break
		fi
	done
	TIVO_ROOT_PASSWORD=$(openssl passwd -1 ${pw1})
	return 0
}

install_hacks() {
	info "Creating /etc/passwd for ssh server"
	get_password
	if [ $? -eq 0 ] ; then
		info "Root password was set"
		echo "root:${TIVO_ROOT_PASSWORD}:0:0:root:/:/bin/bash" > /tivo/etc/passwd
	else
		info "Root password is blank"
		echo "root::0:0:root:/:/bin/bash" > /tivo/etc/passwd
	fi
	info "Creating /etc/shells for ssh server"
	echo "/bin/bash" > /tivo/etc/shells
	info "Creating /.ssh for ssh server"
	mkdir /tivo/.ssh
	chmod 0600 /tivo/.ssh
}

install_hacks

# End of script
