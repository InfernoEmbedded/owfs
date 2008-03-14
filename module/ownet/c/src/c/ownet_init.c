/*
$Id$
    OWFS -- One-Wire filesystem
    OWHTTPD -- One-Wire Web Server
    Written 2003 Paul H Alfille
    email: palfille@earthlink.net
    Released under the GPL
    See the header file: ow.h for full attribution
    1wire/iButton system from Dallas Semiconductor
*/

/* ow_server talks to the server, sending and recieving messages */
/* this is an alternative to direct bus communication */

#include "ownetapi.h"
#include "ow_server.h"

OWNET_HANDLE OWNET_init(const char *owserver_tcp_address_and_port)
{
	OWNET_HANDLE handle;
	struct connection_in *slot_found = NewIn();

	// Could we create or reclaim a slot?
	if (slot_found == NULL) {
		return -ENOMEM;
	}

	if (owserver_tcp_address_and_port == NULL || owserver_tcp_address_and_port[0] == '\0') {
		slot_found->name = strdup("4304");
	} else {
		slot_found->name = strdup(owserver_tcp_address_and_port);
	}
	slot_found->busmode = bus_server;
	handle = Server_detect(slot_found);
	if (handle == 0) {
		slot_found->state = connection_active;
		handle = slot_found->index;
	} else {
		FreeIn(slot_found);
	}

	return handle;
}