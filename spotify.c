#include "keys.h"
#include <glib.h>
#include <navit/main.h>
#include <navit/debug.h>
#include <navit/point.h>
#include <navit/navit.h>
#include <navit/callback.h>
#include <navit/color.h>
#include <navit/osd.h>
#include <navit/event.h>
#include <navit/config_.h>

#include <libspotify/api.h>
#include "audio.h"

extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;
extern const char *username;
extern const char *password;

/// Non-zero when a track has ended and the jukebox has not yet started a new one
static int g_playback_done;
/// Handle to the playlist currently being played
static sp_playlist *g_jukeboxlist;
/// Name of the playlist currently being played
const char *g_listname;
/// Handle to the current track 
static sp_track *g_currenttrack;
/// Index to the next track
static int g_track_index;
/// The global session handle

static sp_session *g_sess;
int g_logged_in;
static audio_fifo_t g_audiofifo;

int next_timeout = 0;

struct spotify
{
  struct navit *navit;
  struct callback *callback;
  struct event_idle *idle;
};

/**
 * The callbacks we are interested in for individual playlists.
 */
static sp_playlist_callbacks pl_callbacks = {
//        .tracks_added = &tracks_added,
//        .tracks_removed = &tracks_removed,
//        .tracks_moved = &tracks_moved,
//        .playlist_renamed = &playlist_renamed,
};


/**
 * Called on various events to start playback if it hasn't been started already.
 *
 * The function simply starts playing the first track of the playlist.
 */
static void
try_jukebox_start (void)
{
  dbg (0, "Starting the jukebox\n");
  sp_track *t;

  if (!g_jukeboxlist)
    printf ("jukebox: No playlist. Waiting\n");
  // return;

  if (!sp_playlist_num_tracks (g_jukeboxlist))
    {
      printf ("jukebox: No tracks in playlist. Waiting\n");
      return;
    }

  if (sp_playlist_num_tracks (g_jukeboxlist) < g_track_index)
    {
      printf ("jukebox: No more tracks in playlist. Waiting\n");
      return;
    }

  t = sp_playlist_track (g_jukeboxlist, g_track_index);

  if (g_currenttrack && t != g_currenttrack)
    {
      /* Someone changed the current track */
      audio_fifo_flush (&g_audiofifo);
      sp_session_player_unload (g_sess);
      g_currenttrack = NULL;
    }

  if (!t)
    return;

  if (sp_track_error (t) != SP_ERROR_OK)
    return;

  if (g_currenttrack == t)
    return;

  g_currenttrack = t;

  printf ("jukebox: Now playing \"%s\"...\n", sp_track_name (t));
  fflush (stdout);

  sp_session_player_load (g_sess, t);
  sp_session_player_play (g_sess, 1);
}

/* --------------------  PLAYLIST CONTAINER CALLBACKS  --------------------- */
/**
 * Callback from libspotify, telling us a playlist was added to the playlist container.
 *
 * We add our playlist callbacks to the newly added playlist.
 *
 * @param  pc            The playlist container handle
 * @param  pl            The playlist handle
 * @param  position      Index of the added playlist
 * @param  userdata      The opaque pointer
 */
static void
playlist_added (sp_playlistcontainer * pc, sp_playlist * pl,
		int position, void *userdata)
{
  sp_playlist_add_callbacks (pl, &pl_callbacks, NULL);
  dbg (0, "List name: %s\n", sp_playlist_name (pl));

  if (!strcasecmp (sp_playlist_name (pl), g_listname))
    {
      g_jukeboxlist = pl;
      try_jukebox_start ();
    }
}


/**
 * Callback from libspotify, telling us the rootlist is fully synchronized
 * We just print an informational message
 *
 * @param  pc            The playlist container handle
 * @param  userdata      The opaque pointer
 */
static void
container_loaded (sp_playlistcontainer * pc, void *userdata)
{
  fprintf (stderr, "jukebox: Rootlist synchronized (%d playlists)\n",
	   sp_playlistcontainer_num_playlists (pc));
  try_jukebox_start ();
}

/**
 * The playlist container callbacks
 */
static sp_playlistcontainer_callbacks pc_callbacks = {
  .playlist_added = &playlist_added,
//        .playlist_removed = &playlist_removed,
  .container_loaded = &container_loaded,
};

static void
on_login (sp_session * session, sp_error error)
{
  dbg (0, "spotify login\n");
  if (error != SP_ERROR_OK)
    {
      fprintf (stderr, "Error: unable to log in: %s\n",
	       sp_error_message (error));
      exit (1);
    }

  g_logged_in = 1;
  sp_playlistcontainer *pc = sp_session_playlistcontainer (session);
  int i;

  sp_playlistcontainer_add_callbacks (pc, &pc_callbacks, NULL);
dbg (0, "Got %d playlists\n", sp_playlistcontainer_num_playlists (pc))}

static sp_session_callbacks session_callbacks = {
  .logged_in = &on_login,
//  .notify_main_thread = &on_main_thread_notified,
//  .music_delivery = &on_music_delivered,
//  .log_message = &on_log,
//  .end_of_track = &on_end_of_track,
//  .offline_status_updated = &offline_status_updated,
//  .play_token_lost = &play_token_lost,
};

static sp_session_config spconfig = {
  .api_version = SPOTIFY_API_VERSION,
  .cache_location = "tmp",
  .settings_location = "tmp",
  .application_key = g_appkey,
  .application_key_size = 0,	// set in main()
  .user_agent = "spot",
  .callbacks = &session_callbacks,
  NULL
};

static void
spotify_spotify_idle (struct spotify *spotify)
{
  sp_session_process_events (g_sess, &next_timeout);
}

static void
spotify_navit_init (struct navit *nav)
{
  dbg (0, "spotify_navit_init\n");
  sp_error error;
  sp_session *session;

  spconfig.application_key_size = g_appkey_size;
  error = sp_session_create (&spconfig, &session);
  if (error != SP_ERROR_OK)
    {
      dbg (0, "Can't create spotify session :(\n");
      return;
    }
  dbg (0, "Session created successfully :)\n");
  g_sess = session;
  g_logged_in = 0;
  sp_session_login (session, username, password, 0, NULL);
  struct spotify *spotify = g_new0 (struct spotify, 1);
  spotify->navit = nav;
  spotify->callback =
    callback_new_1 (callback_cast (spotify_spotify_idle), spotify);
  event_add_idle (500, spotify->callback);
  dbg (0, "Callback created successfully\n");
}


static void
spotify_navit (struct navit *nav, int add)
{
  struct attr callback;
  if (add)
    {
      dbg (0, "adding callback\n");
      callback.type = attr_callback;
      callback.u.callback =
	callback_new_attr_0 (callback_cast (spotify_navit_init), attr_navit);
      navit_add_attr (nav, &callback);
    }
}


void
plugin_init (void)
{
  dbg (0, "spotify init\n");
  struct attr callback, navit;
  struct attr_iter *iter;
  // plugin_register_osd_type("spotify", osd_button_new);
  callback.type = attr_callback;
  callback.u.callback =
    callback_new_attr_0 (callback_cast (spotify_navit), attr_navit);
  config_add_attr (config, &callback);
  iter = config_attr_iter_new ();
  while (config_get_attr (config, attr_navit, &navit, iter))
    spotify_navit_init (navit.u.navit);
  config_attr_iter_destroy (iter);
}