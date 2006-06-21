#!/bin/sh
# the next line restarts using tclsh \
exec wish owsim.tcl "$@"
# $Id$
###########################################################
###########################################################
########## Enough of notebook, lets do Simulant! ##########
###########################################################
###########################################################

###########################################################
########## Simulant! DS2401 functions  ####################
###########################################################

proc Setup01 { addr type } {
    global chip
    set chip($addr.type) $type
    set chip($addr.display) Display01
    
    set chip($addr.page.aggregate) [list ALL numbers 3 ","]
    foreach x { 0 1 2 } {
        set chip($addr.page.$x) $x
    }
    puts [ReadALL $addr.page]
}

proc Display01 { addr fmain } {
    DisplayStandard     $addr $fmain
}



###########################################################
########## Simulant! Do it  ###############################
###########################################################
