#ifdef BUILD_LYNX
#include "netstream.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include <errno.h>
#include <sys/socket.h>
#endif

//#define DEBUG_NETSTREAM

GAME_LIST_T game_list[] = {
	{0x0000, 2, "Bill and Ted's Excellent Adventure"},				// standard redeye games
	{0x0001, 4, "Gauntlet: The Third Encounter"},					// seems to switch baud rate in game mode
	{0x0002, 4, "Zalor Mercenary"},
	{0x0004, 4, "Xenophobe"},
	{0x0005, 8, "Todd's Adventure in Slime World"},
	{0x0006, 2, "Robosquash"},
	{0x0007, 4, "Warbirds"},
	{0x001E, 2, "Turbo Sub"},
	{0x0020, 2, "Basketbrawl"},
	{0x0028, 2, "World Class Soccer"},
	{0x0030, 2, "Hockey"},
	{0x0053, 2, "Shanghai"},
	{0x00C8, 6, "Checkered Flag"},
	{0x00D2, 2, "Rampart"},
	{0x00FF, 2, "Xybots"},
	{0x029A, 2, "Joust"},
	{0x0EFE, 2, "Road Riot 4WD"},
	{0x1313, 2, "Supersqweek"},
	{0x1355, 4, "Rampage"},
	{0x2050, 2, "Baseball Heroes"},
	{0x7000, 6, "Battle Wheels"},
	{0xB0B0, 2, "NFL Football"},
	{0xBABE, 2, "Raiden"},
	{0xDAD0, 4, "Tournament Cyberball"},							// may also switch baud rate?
	// Remapped from 0xFFFF in Fujinet Firmware
	{0xE001, 2, "Double Dragon"},									// remapped from 0xFFFF games
	{0xE002, 2, "European Soccer"},
	{0xE003, 2, "Lynx Casino"},
	{0xE004, 2, "Pit Fighter"},
	{0xE005, 2, "Relief Pitcher"},
	{0xE006, 4, "Super Off-Road"},
	// Non-Redeye Games
	{0xE101, 4, "Awesome Golf"},									// games that don't use redeye (shouldn't ever see these in this server)
	{0xE102, 4, "Battlezone 2000"},									// including for completeness as these IDs are used in the
	{0xE103, 4, "California Games"},								// Fujinet firmware
	{0xE104, 6, "Championship Rally"},
	{0xE105, 2, "Fidelity Ulimate Chess Challenge"},
	{0xE106, 4, "Hyperdrome"},
	{0xE107, 4, "Jimmy Connor's Tennis"},
	{0xE108, 2, "Loopz"},
	{0xE109, 2, "Lynx Othello"},
	{0xE10A, 4, "Malibu Bikini Volleyball"},
	{0xE10B, 2, "Ponx"},
	// Generic ID used in some games (see remap above)
	{0xFFFF, 2, "Generic game ID"}
};


void lynxNetStream::process_redeye_net_packet(uint8_t *buf, size_t len)
{
	// bad packet length?
	if ((len <= 2) || (len >= 10))
		return;

#ifdef DEBUG_NETSTREAM
	Debug_print("Netstream Redeye FROM NET: ");
	util_dump_bytes(buf, len);
#endif

	if (redeye_validate_packet(buf, len))
	{
		if (game.state.logon)
			redeye_process_logon_packet_from_net(buf);
		else
			redeye_process_game_packet_from_net(buf);
	}
}


void lynxNetStream::comlynx_handle_redeye_netstream() {

	redeye_check_logon_state();

	// Get data from network
	int packetSize = 0;
	if (netstreamMode == NetStreamMode::UDP)
	{
		packetSize = netStreamUdp.parsePacket();
		if (packetSize > 0)
		{
			netStreamUdp.read(buf_net, NETSTREAM_BUFFER_SIZE);
			process_redeye_net_packet(buf_net, packetSize);
		}
	}
	else if (ensure_netstream_ready())
	{
		while (buf_net_index < NETSTREAM_BUFFER_SIZE)
		{
			size_t free_space = NETSTREAM_BUFFER_SIZE - buf_net_index;
#ifdef ESP_PLATFORM
			int bytes_read = recv(netStreamTcp.fd(), (char *)&buf_net[buf_net_index], free_space, MSG_DONTWAIT);
			if (bytes_read <= 0)
			{
				if (bytes_read == 0)
					netStreamTcp.stop();
				else if (errno != EWOULDBLOCK && errno != EAGAIN)
					netStreamTcp.stop();
				break;
			}
#else
			size_t available = netStreamTcp.available();
			if (available == 0)
				break;
			size_t to_read = (available > free_space) ? free_space : available;
			int bytes_read = netStreamTcp.read(&buf_net[buf_net_index], to_read);
			if (bytes_read <= 0)
				break;
#endif
			buf_net_index += bytes_read;
			while (buf_net_index > 0)
			{
				uint8_t size = buf_net[0];
				if (size < 1 || size > 6)
				{
					memmove(buf_net, &buf_net[1], buf_net_index - 1);
					buf_net_index--;
					continue;
				}

				size_t packet_len = (size_t)size + 2;
				if (buf_net_index < packet_len)
					break;

				process_redeye_net_packet(buf_net, packet_len);
				memmove(buf_net, &buf_net[packet_len], buf_net_index - packet_len);
				buf_net_index -= packet_len;
			}
		}

		if (buf_net_index >= NETSTREAM_BUFFER_SIZE)
			buf_net_index = 0;
	}

	// Collect data from serial bus
	// serial collect loop, waiting until the serial has been idle for IDLE_TIME (2-3 char time at 62500 baud)
	buf_stream_index = 0;
 	if (SYSTEM_BUS.available() > 0) {											// is there something availabe in FIFO
		uint64_t last_rx = GET_TIMESTAMP();
 		while (true) {
			while (SYSTEM_BUS.available() > 0) { 								// got all data in FIFO
				if (buf_stream_index >= NETSTREAM_BUFFER_SIZE)					// too much data for buffer, just exit (should never hit this)
					break;

				buf_stream[buf_stream_index++] = SYSTEM_BUS.read();				// get byte from FIFO
				last_rx = GET_TIMESTAMP();										// reset idle timer
 			}

			if (buf_stream_index >= NETSTREAM_BUFFER_SIZE)						// too much data for buffer, just exit (should never hit this)
				break;

			if ((GET_TIMESTAMP() - last_rx) > COMLYNX_IDLE_TIME)				// data has paused for 2-3 bytes at 62500 baud, end of packet
				break;
 		}
 	}

	if (buf_stream_index == 0)
		return;

	SYSTEM_BUS.flush();

 	// parse all packets collected from serial bus (should hopefully only be one)
 	uint16_t index = 0;
 	while (index < buf_stream_index) {
		if (buf_stream[index] == 0) {
			index++;
			continue;
		}
		else
			packetSize = buf_stream[index]+2;				// get the redeye packet size (this is 2 more than what the packet payload is)

 		#ifdef DEBUG_NETSTREAM
		Debug_print("Netstream Redeye FROM LYNX: ");
 		util_dump_bytes(&buf_stream[index], packetSize);
 		#endif

 		// validate this is a good redeye packet
 		if (redeye_validate_packet(&buf_stream[index], packetSize)) {
			if (game.state.logon)
				redeye_process_logon_packet_from_lynx(&buf_stream[index]);
 			else {
 				redeye_process_game_packet_from_lynx(&buf_stream[index]);
			}
 		}

 		index += packetSize;
 	}
}


void lynxNetStream::comlynx_enable_redeye()         // also can be used to reset redeye mode
{
    redeye_mode = true;
    redeye_reset_game();

	#ifdef DEBUG
    Debug_println("NETSTREAM redeye mode ENABLED");
	#endif
}


void lynxNetStream::comlynx_disable_redeye()
{
    redeye_mode = false;
    redeye_reset_game();

	#ifdef DEBUG
    	Debug_println("NETSTREAM redeye mode DISABLED");
	#endif
}


 /* Calculate the checksum of incoming from the lynx redeye packets
    Return true if ok, false if not

    typical message:
    05 00 00 01 FF FF F8

    Checksum is calculated on size, plus message bytes.
 */
 bool lynxNetStream::redeye_checksum(uint8_t *buf)
 {
    uint16_t ck;
    uint8_t i;
    uint8_t size;


    size = buf[0];                         // get message size
    if ((size == 0) || (size > 6)) {       // check packets are in range
        //Debug_printf("checksum size %d %d\n", size, buf[0]);
        return false;
    }

    // checksum caculation is 255 - size - message bytes
    ck = 255;
    for (i=0; i < size+1; i++) {
        ck -= buf[i];
    }

    if ((ck & 0xFF) == buf[size+1])
        return true;
    else
        return false;

 }


/* Recalculate the checksum of the lynx redeye packet.
 * We may have to do this if we have changed anything inside the packet (like game ID)
 *
 *  Checksum is calculated on size byte, plus message bytes.
 */
 void lynxNetStream::redeye_recalculate_checksum(uint8_t *buf)
 {
    uint16_t ck;
    uint8_t i;
    uint8_t size;


    size = buf[0];  // get message size

    // checksum caculation is 255 - size - message bytes
    ck = 255;
    for (i=0; i < size+1; i++) {
        ck -= buf[i];
    }

    // set new checksum on packet
    buf[size+1] = (ck & 0xFF);
    return;
}


/* redeye_remap_game_id
 *
 * Remap certain game IDs (based on GUI setting) so that we
 * have a unique game id for each game.
 *
 * 0xFFFF       0xE001  Relief Pitcher
 * 0xFFFF       0xE002  Pit Fighter
 * 0xFFFF       0xE003  Double Dragon
 * 0xFFFF       0xE004  European Soccer
 * 0xFFFF       0xE005  Lynx Casino
 * 0xFFFF       0xE006  Super Off-Road
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_remap_game_id(uint8_t *buf, uint16_t remap)
{
	// Set new game ID
	buf[4] = game.remap_game_id & 0xFF;
	buf[5] = (game.remap_game_id >> 8) & 0xFF;

    // recalculate checksum
    redeye_recalculate_checksum(buf);
    return;
}


/* redeye_find_game
 *
 * Searches the game list for the game id and returns the index into
 * the list, so that we can lookup name and max players.  Returns 255
 * if the game was not found.
 */
uint8_t lynxNetStream::redeye_find_game(uint16_t gid)
{
	uint8_t i;

  	for(i=0; i<NUM_GAMES; i++) {
    	if (game_list[i].game_id == gid)
      	return(i);	// game found
	}
	return(255);	// game not found
}


/* redeye_process_logon_packet_from_net
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_logon_packet_from_net(uint8_t *buf)
{
	uint8_t size, msg, plrs, countdown, pnum;
	uint16_t gid;


   	// is logon ended, and game starting?
	if (!redeye_check_logon_state())
		return;

	// extract info from packet
	size = buf[0];
	msg = buf[1];
	countdown = pnum = buf[2];					// could be pnum or countdown
	plrs = std::popcount(buf[3]);
	gid = (buf[4]+(buf[5]<<8));

	// Not in Logon state, or game ID mismatch, or packet size mismatch?
	if (!game.state.logon || (size != 5) || (gid != game.game_id))
		return;

	// process logon message
	switch(msg) {
		case 0:			// logon annouce packet
			// track logon state
			game.logon_state.player_present[pnum] = true;
			game.logon_state.cached_mask[pnum] = buf[3];
			game.logon_state.logon_rx_timer[pnum] = GET_TIMESTAMP();

			// collision with my player number? Need to relay to lynx for player collision handling
			if (pnum == game.my_player_num) {
				if ((GET_TIMESTAMP() - game.logon_state.collision_timer) < COLLISION_BACKOFF) {
					Debug_printf("REDEYE (net)  %04X %s --> Logon collision with my player number %d, but backoff timer not expired\n", game.game_id, *game.name, pnum);
					return;
				}

				Debug_printf("REDEYE (net)  %04X %s --> Logon collision with my player number %d\n", game.game_id, *game.name, pnum);
				game.logon_state.collision_timer = GET_TIMESTAMP();
				break;		// relay logon packet to lynx for collision handling
			}

			if (game.num_players < plrs) {
				Debug_printf("REDEYE (net)  %04X %s --> Logon new player %d, players:%d old num_players:%d\n", game.game_id, *game.name, pnum, plrs, game.num_players);
				game.num_players = plrs;
			}

			return;			// don't relay logon packet, Fujinet will send logon packets
			break;

		case 2:
			Debug_printf("REDEYE (net)  %04X %s --> Game starting in %d\n", game.game_id, *game.name, countdown);
			game.num_players = plrs;

            if (game.state.logon_timer == 0)
				game.state.logon_timer = GET_TIMESTAMP();
			break;
	}

	// Should we remap the game id? Set it back to 0xFFFF for lynx
	if (game.remap_game_id) {
		redeye_remap_game_id(buf_net, 0xFFFF);
	}

    // Send to Lynx UART
	SYSTEM_BUS.wait_for_idle();
    SYSTEM_BUS.write(buf, size+2);
    //if (!SYSTEM_BUS.isBoIP())
        SYSTEM_BUS.read(buf, size+2); 		// discard physical ComLynx UART echo
}


/* redeye_process_game_packet_from_net
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_game_packet_from_net(uint8_t *buf)
{
	uint8_t size, seq, msg, plr;
	uint16_t gid;


	// In logon state
	if (game.state.logon)
		return;

	// Parse header dataq
	size = buf[0]+2;
	msg = buf[1] & 0x07;
	plr = (buf[1] & 0x78) >> 3;
	seq = (buf[1] & 0x80) ? 1 : 0;

	// process game message
	switch(msg) {
		case 0:		// looks like we're back in logon, someone pressed restart?
			if (buf[0] == 5) {
				redeye_reset_game();
				Debug_printf("REDEYE (net)  %04X %s --> re-entering logon mode\n", game.game_id, *game.name);
				return;
			}
		break;

		case 3: 	// data packet
			Debug_printf("REDEYE (net)  %04X %s --> DATA player %d data for seq %d - header:%02X, data size:%d\n", game.game_id, *game.name, plr, seq, buf[1], size);
			break;

		case 4:		// SendData Req
			Debug_printf("REDEYE (net)  %04X %s --> REQUEST player %d data for seq %d, header:%02X\n", game.game_id, *game.name, plr, seq, buf[1]);
			break;

		case 5:		// Master resend req
			// if I'm not master, just ignore
			if (game.my_player_num != 0)
				return;
			break;
	}

    // Send to Lynx UART
    SYSTEM_BUS.wait_for_idle();
    SYSTEM_BUS.write(buf, size);
    //if (!SYSTEM_BUS.isBoIP())
        SYSTEM_BUS.read(buf, size); 		// discard physical ComLynx UART echo
}


/* redeye_process_logon_packet_from_lynx
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_logon_packet_from_lynx(uint8_t *buf)
{
	uint8_t size, msg, plrs, countdown, pnum;
	uint16_t gid;


    // is logon ended, and game starting?
	if (!redeye_check_logon_state())
		return;

	// extract info from packet
	size = buf[0];
	msg = buf[1];
	countdown = pnum = buf[2];
	plrs = std::popcount(buf[3]);
	gid = (buf[4]+(buf[5]<<8));

	if (size != 5)			// malformed packet
		return;

	// process logon message
	switch(msg) {
		case 0:			// logon annouce packet
			// Set game ID and name if haven't seen yet
			if (gid != game.game_id) {
				game.game_id = gid;
                uint8_t i = redeye_find_game(game.game_id);
				if (i == 255) {
					Debug_printf("REDEYE (lynx) couldn't find game %04X in game list\n", game.game_id);
					return;
				}

				game.max_players = game_list[i].max_players;
				game.name = &game_list[i].name;
                game.num_players = 1;
				Debug_printf("REDEYE (lynx) new game %04X %s\n", game.game_id, *game.name);
			}

			// Set my player number
			if (pnum != game.my_player_num) {
				game.my_player_num = pnum;
				Debug_printf("REDEYE (lynx) %04X %s ---> My player number: %d\n", game.game_id, *game.name, pnum);
			}

			// Set number of players
			if (game.num_players < plrs) {
				Debug_printf("REDEYE (lynx) %04X %s --> Logon new player %d, plrs: %d, old num_players: %d\n", game.game_id, *game.name, pnum, plrs, game.num_players);
				game.num_players = plrs;
			}

			// track logon state
			game.logon_state.player_present[pnum] = 1;
			game.logon_state.cached_mask[pnum] = buf[3];
			game.logon_state.logon_rx_timer[pnum] = GET_TIMESTAMP();
			break;

		case 2:
			Debug_printf("REDEYE (lynx) %04X %s --> Game starting in %d\n", game.game_id, *game.name, countdown);

			game.num_players = plrs;

            if (game.state.logon_timer == 0)
				game.state.logon_timer = GET_TIMESTAMP();
			break;
	}

	// Should we remap the game id for server?
	if (game.remap_game_id) {
		redeye_remap_game_id(buf, game.remap_game_id);
	}

    // Send to network
	send_net_packet(buf, size+2);

	// Send this lynx the rest of the logon packets from other clients
	if (msg == 0) {
		fnSystem.delay(LOGON_PACKET_DELAY);
		redeye_send_logon_packets();
	}
}


/* redeye_process_game_packet_from_lynx
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_game_packet_from_lynx(uint8_t *buf)
{
	uint8_t size, seq, msg, plr;


	// In logon state
	if (game.state.logon)
		return;

	// Parse header dataq
	size = buf[0]+2;
	msg = buf[1] & 0x07;
	plr = (buf[1] & 0x78) >> 3;
	seq = (buf[1] & 0x80) ? 1 : 0;

	// process game message
	switch(msg) {
		case 0:		// looks like we're back in logon, someone pressed restart?
			if (buf[0] == 5) {
				redeye_reset_game();
				Debug_printf("REDEYE (lynx) %04X %s --> re-entering logon mode\n", game.game_id, *game.name);
				return;
			}
		break;

		case 3: 	// data packet
			Debug_printf("REDEYE (lynx) %04X %s --> DATA player %d data for seq %d - header:%02X, data size:%d\n", game.game_id, *game.name, plr, seq, buf[1], size);
			break;

		case 4:		// SendData Req
			Debug_printf("REDEYE (lynx) %04X %s --> REQUEST player %d data for seq %d, header:%02X\n", game.game_id, *game.name, plr, seq, buf[1]);
			break;

		case 5:		// Master resend req
			Debug_printf("REDEYE (lynx) %04X %s --> MASTER RESEND REQUEST, plr_mask:%d, header:%02X\n", game.game_id, *game.name, plr, seq, buf[2], buf[1]);
			break;
	}

    // Send to network
	send_net_packet(buf, size);
}


bool lynxNetStream::redeye_validate_packet(uint8_t *buf, uint8_t bufsize)
{
	// Sanity checks on packet size
	if ((bufsize < 3) || (bufsize > 10) || (buf[0]+2 != bufsize)) {
		#ifdef REDEYE_DEBUG
		Debug_printf("REDEYE bad packet size - bufsize:%d buf[0]:%d\n", bufsize, buf[0]);
		#endif
		return false;
	}

	// validate the checksum
	if (redeye_checksum(buf))
		return true;
	else {
		#ifdef REDEYE_DEBUG
		//Debug_println("REDEYE bad checksum");
		#endif
		return false;
	}
}


/* redeye_reset_game
 *
 * Reset the game state to initial values.
 * */
void lynxNetStream::redeye_reset_game()
{
    uint8_t i;

    // clear game info
    game.game_id = 0;
    game.remap_game_id = 0;
    game.max_players = 0;
    game.num_players = 0;
    game.my_player_num = 255;

    // reset game state
    game.state.logon = true;
    game.logon_state.active_mask = 0;
    for(i=0; i<MAX_PLAYERS; i++) {
		game.logon_state.player_present[i] = 0;
	    game.logon_state.cached_mask[i] = 0;
	    game.logon_state.logon_rx_timer[i] = 0;
    }
}


bool lynxNetStream::redeye_check_logon_state()
{
	// Are we in logon timer countdown mode?
	if (game.state.logon_timer > 0) {
		uint64_t now = GET_TIMESTAMP();
		#ifdef REDEYE_DEBUG
		Debug_printf("REDEYE %04X %s --> game start countdown: %d\n", game.game_id, *game.name, (now - game.state.logon_timer));
		#endif

		if ((now - game.state.logon_timer) > LOGON_DELAY) {
			game.state.logon = false;
			game.state.logon_timer = 0;

			// Change baud rate for Gauntlet3 (0001)
			if (game.game_id == 0001) {
				//SYSTEM_BUS.flush();
				SYSTEM_BUS.change_baud(31250);
			}

		    Debug_printf("REDEYE %04X %s --> Logon ended, players: %d\n", game.game_id, *game.name, game.num_players);
			return(false);
		}
	}

	// Time out any inactive players, but only if we are in logon mode
	if (game.state.logon)
		redeye_check_for_inactive_players();

	return(true);
}


/* redeye_send_logon_to_lynx
 *
 * Send a logon packet to Lynx over serial for this player number
 *
 * DEBUG LOGON PKT: 05 00 00 01 13 13 D3 - Msg=00 Plrs=0 countdown=0
 */
void lynxNetStream::redeye_send_logon_to_lynx(uint8_t pnum)
{
	uint8_t buf[7];		// temporary logon packet buffer


	// Build the logon packet
	buf[0] = 0x05;								// payload size
	buf[1] = 0x00;								// message type - logon packet
	buf[2] = pnum;								// player number
	//buf[2] = redeye_active_players_mask();		// active player mask
	buf[3] = game.logon_state.cached_mask[pnum];
	buf[4] = game.game_id & 0xFF;				// game ID
	buf[5] = (game.game_id >> 8) & 0xFF;
	buf[6] = 0;									// checksum

	// calculate the checksum
	redeye_recalculate_checksum(&buf[0]);
	//if ((redeye_checksum(&buf[0])) == false) {
	//	Debug_printf("REDEYE %04X %s --> ERROR: logon packet re-checksum failed for player %d\n", game.game_id, *game.name, pnum);
	//	return;
	//}

	// Send to Lynx UART
	SYSTEM_BUS.wait_for_idle();
	SYSTEM_BUS.write(&buf[0], 7);
	//if (!SYSTEM_BUS.isBoIP())
		SYSTEM_BUS.read(&buf[0], 7); 				// discard physical ComLynx UART echo
}


/* redeye_active_players_mask
 *
 * Build the active players mask to send in logon packet
 */
uint8_t lynxNetStream::redeye_active_players_mask()
{
	uint8_t mask = 0;

	for (int i = 0; i < 8; i++) {
    	if (game.logon_state.player_present[i])
        	mask |= (1 << i);
	}

	game.logon_state.active_mask = mask;
	return(mask);
}


void lynxNetStream::redeye_send_logon_packets()
{
	uint8_t i;

	// Set the initial player to start sending logon packets from
	i = game.my_player_num + 1;
	if (i >= MAX_PLAYERS)
		i = 0;

	while(i != game.my_player_num) {
		if (game.logon_state.player_present[i]) {
			#ifdef REDEYE_DEBUG
				if (game.num_players > 1)
					Debug_printf("REDEYE (fn) %04X %s --> sending logon packet for player %d, active_mask:%0X cached_mask:%0X\n", game.game_id, *game.name, i, game.logon_state.active_mask, game.logon_state.cached_mask[i]);
			#endif
			redeye_send_logon_to_lynx(i);
			fnSystem.delay(LOGON_PACKET_DELAY);
		}

		i++;
		if (i >= MAX_PLAYERS)
			i = 0;
	}
}


void lynxNetStream::redeye_check_for_inactive_players()
{
	uint8_t i;


	for(i=0; i<MAX_PLAYERS; i++) {
		if (game.logon_state.player_present[i]) {
			if ((GET_TIMESTAMP() - game.logon_state.logon_rx_timer[i]) > PLAYER_INACTIVE) {
				Debug_printf("REDEYE %04X %s --> removing inactive player %d\n", game.game_id, *game.name, i);
				game.logon_state.player_present[i] = 0;
				game.logon_state.cached_mask[i] = 0;
				//game.num_players--;
			}
		}
	}

	game.logon_state.active_mask = redeye_active_players_mask();
}


#endif /* BUILD_LYNX */
