#!/bin/sh

##
##  User-configurable Variables
##

# Where "nail" is installed on your system.
# We need this to actually send the mail, so make sure it's installed
NAIL=/usr/bin/nail

# REQUIRED CHANGE #1: you must set SMTP_SERVER
# http://www.host45.com/resources/ispsmtps.php has a list of ISP's smtp servers
SMTP_SERVER=your.smtp.server

# REQUIRED CHANGE #2: you must set your email address.
# option A: change "yourname@yourmail.com" here and remove the leading '#' to
# use a real email address
#TO_ADDR=yourname@yourmail.com
#
# option B: for an SMS message, set your phone number here and remove the
# leading '#' on the PHONENUM line and your phone provider's TO_ADDR line
#PHONENUM="1234567890"
#TO_ADDR="$PHONENUM@message.alltel.com"      # SMS: Alltel
#TO_ADDR="$PHONENUM@txt.att.net"             # SMS: AT&T (formerly Cingular)
#TO_ADDR="$PHONENUM@myboostmobile.com"       # SMS: Boost Mobile
#TO_ADDR="$PHONENUM@sms.mycricket.com"       # SMS: Cricket Wireless
#TO_ADDR="$PHONENUM@messaging.nextel.com"    # SMS: Nextel (Sprint Nextel)
#TO_ADDR="$PHONENUM@messaging.sprintpcs.com" # SMS: Sprint (Sprint Nextel)
#TO_ADDR="$PHONENUM@tmomail.net"             # SMS: T-Mobile
#TO_ADDR="$PHONENUM@vtext.com"               # SMS: Verizon
#TO_ADDR="$PHONENUM@vmobl.com"               # SMS: Virgin Mobile USA
#TO_ADDR="$PHONENUM@txt.bellmobility.ca"     # SMS: Bell Canada
#TO_ADDR="$PHONENUM@cwemail.com"             # SMS: Centennial Wireless
#TO_ADDR="$PHONENUM@csouth1.com"             # SMS: Cellular Sout
#TO_ADDR="$PHONENUM@gocbw.com"               # SMS: Cincinnati Bell
#TO_ADDR="$PHONENUM@mymetropcs.com"          # SMS: Metro PCS 1
#TO_ADDR="$PHONENUM@metropcs.sms.us"         # SMS: Metro PCS 2
#TO_ADDR="$PHONENUM@qwestmp.com"             # SMS: Quest
#TO_ADDR="$PHONENUM@pcs.rogers.com"          # SMS: Rogers
#TO_ADDR="$PHONENUM@tms.suncom.com"          # SMS: Suncom
#TO_ADDR="$PHONENUM@msg.telus.com"           # SMS: Telus
#TO_ADDR="$PHONENUM@email.uscc.net"          # SMS: U.S. Cellular

###
###  Send the mail...
###

SUBJECT="Torrent Done!"
FROM_ADDR="transmission@localhost.localdomain"
TMPFILE=$(mktemp -t transmission.XXXXXXXXXX)
echo "Transmission finished downloading \"$TR_TORRENT_NAME\" on $TR_TIME_LOCALTIME" > "$TMPFILE"
$NAIL -v -S from="$FROM_ADDR" -S smtp -s "$SUBJECT" -S smtp=$SMTP_SERVER "$TO_ADDR" < "$TMPFILE"
rm "$TMPFILE"
