#!/bin/sh
#
# example_minicom.sh: emulate a modem in the comm program "minicom". You 
# will need to install minicom if you haven't already.
#
# This script is part of modemu2k
# <https://github.com/theimpossibleastronaut/modemu2k> 


TERM=ansi src/modemu2k -e "AT%B0=1%B1=1&W" -c "minicom -l -tansi -con -p %s"
