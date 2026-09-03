#ifndef REDEYE_H
#define REDEYE_H


// RedEye Game handling
#define MAX_PLAYERS	16				      // the maximum redeye players supported
#define RE_BUFSIZE  32        		  // only really need 16 bytes, and in practice games rarely use more than 6
#define LOGON_DELAY 200*1000  		  // logon countdown timer (us to ms)
#define PLAYER_INACTIVE 600*1000	  // tolerance of not receiving a player logon packet before removing, (us to ms)
#define COLLISION_BACKOFF 600*1000  // time to let collision resolve itself
#define LOGON_PACKET_DELAY 7		    // delay between logon packets in ms


typedef struct LOGON_STATE_T
{
		uint8_t active_mask;					        // active player mask we're tracking
		uint8_t player_present[MAX_PLAYERS];	// player is actually present
		uint8_t cached_mask[MAX_PLAYERS];		  // what players actually sent us (for debugging)
		int64_t logon_rx_timer[MAX_PLAYERS];	// time we last heard from a player (in milliseconds)
    int64_t collision_timer;           
} LOGON_STATE_T;

typedef struct GAME_STATE_T
{
	  bool logon;
    int64_t logon_timer;
} GAME_STATE_T;

typedef struct GAME_T
{
  uint16_t game_id;         // game id
  uint16_t remap_game_id;	  // remap game id
  const char **name;		    // pointer to game name in games list
  uint8_t max_players;		  // max players for this game
  uint8_t num_players;      // number of players
  uint8_t my_player_num;	  // my player number
  GAME_STATE_T state;		    // game state
  LOGON_STATE_T logon_state;
} GAME_T;

typedef struct GAME_LIST_T
{
  uint16_t game_id;			// game id
  uint8_t max_players;		// maximum players for ths game
  const char *name;			// pointer to the game name
} GAME_LIST_T;

/* Game List */
#define NUM_GAMES   42

#endif