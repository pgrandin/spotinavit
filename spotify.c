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
#include <navit/command.h>
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

struct attr initial_layout, main_layout;

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
    dbg (0, "jukebox: No playlist. Waiting\n");
    // Fixme : g_jukeboxlist is never set to the right value
    //return;

  if (!sp_playlist_num_tracks (g_jukeboxlist))
    {
      dbg (0,"jukebox: No tracks in playlist. Waiting\n");
      return;
    }

  if (sp_playlist_num_tracks (g_jukeboxlist) < g_track_index)
    {
      dbg (0,"jukebox: No more tracks in playlist. Waiting\n");
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

  dbg (0,"jukebox: Now playing \"%s\"...\n", sp_track_name (t));

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
  dbg (0, "jukebox: Rootlist synchronized (%d playlists)\n",
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
      dbg (0, "Error: unable to log in: %s\n",
	       sp_error_message (error));
      exit (1);
    }

  g_logged_in = 1;
  sp_playlistcontainer *pc = sp_session_playlistcontainer (session);
  int i;

  sp_playlistcontainer_add_callbacks (pc, &pc_callbacks, NULL);
  dbg (0, "Got %d playlists\n", sp_playlistcontainer_num_playlists (pc))

  for (i = 0; i < sp_playlistcontainer_num_playlists (pc); ++i)
    {
      sp_playlist *pl = sp_playlistcontainer_playlist (pc, i);

      sp_playlist_add_callbacks (pl, &pl_callbacks, NULL);

      if (!strcasecmp (sp_playlist_name (pl), g_listname))
        {
          dbg (0,"Found the playlist %s\n", g_listname);
          switch (sp_playlist_get_offline_status (session, pl))
            {
            case SP_PLAYLIST_OFFLINE_STATUS_NO:
              dbg (0, "Playlist is not offline enabled.\n");
              sp_playlist_set_offline_mode (session, pl, 1);
              dbg (0, "  %d tracks to sync\n",
                      sp_offline_tracks_to_sync (session));
              break;

            case SP_PLAYLIST_OFFLINE_STATUS_YES:
              dbg (0, "Playlist is synchronized to local storage.\n");
              break;

            case SP_PLAYLIST_OFFLINE_STATUS_DOWNLOADING:
              dbg
                (0, "This playlist is currently downloading. Only one playlist can be in this state any given time.\n");
              break;

            case SP_PLAYLIST_OFFLINE_STATUS_WAITING:
              dbg (0, "Playlist is queued for download.\n");
              break;

            default:
              dbg (0, "unknow state\n");
              break;
            }
          g_jukeboxlist = pl;
          try_jukebox_start ();
        }
    }
  if (!g_jukeboxlist)
    {
      dbg (0, "jukebox: No such playlist. Waiting for one to pop up...\n");
    }
  try_jukebox_start ();


}

static int
on_music_delivered (sp_session * session, const sp_audioformat * format,
                    const void *frames, int num_frames)
{
  audio_fifo_t *af = &g_audiofifo;
  audio_fifo_data_t *afd;
  size_t s;

  s = num_frames * sizeof (int16_t) * format->channels;
  return num_frames;

  if (num_frames == 0)
    return 0;                   // Audio discontinuity, do nothing

  pthread_mutex_lock (&af->mutex);

  /* Buffer one second of audio */
  if (af->qlen > format->sample_rate)
    {
      pthread_mutex_unlock (&af->mutex);

      return 0;
    }

  s = num_frames * sizeof (int16_t) * format->channels;

  afd = malloc (sizeof (*afd) + s);
  memcpy (afd->samples, frames, s);

  afd->nsamples = num_frames;

  afd->rate = format->sample_rate;
  afd->channels = format->channels;

  TAILQ_INSERT_TAIL (&af->q, afd, link);
  af->qlen += num_frames;

  pthread_cond_signal (&af->cond);
  pthread_mutex_unlock (&af->mutex);

  return num_frames;
}

static void
on_end_of_track (sp_session * session)
{
  // g_playback_done = 1; 
  //
  ++g_track_index;
  try_jukebox_start ();
}

static sp_session_callbacks session_callbacks = {
  .logged_in = &on_login,
//  .notify_main_thread = &on_main_thread_notified,
  .music_delivery = &on_music_delivered,
//  .log_message = &on_log,
  .end_of_track = &on_end_of_track,
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
spotify_cmd_spotify_toggle(struct spotify *spotify)
{
  dbg (0,"toggling\n");
}

static struct command_table commands[] = {
	{"spotify_toggle", command_cast(spotify_cmd_spotify_toggle)},
};

static void
osd_spotify_init(struct navit *nav)
{
  struct spotify *spotify=g_new0(struct spotify, 1);  
  struct attr attr;
  spotify->navit=nav;

  if (navit_get_attr(nav, attr_callback_list, &attr, NULL)) {
  	dbg(0,"Adding command\n");
	command_add_table(attr.u.callback_list, commands, sizeof(commands)/sizeof(struct command_table), spotify);
  }
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
  osd_spotify_init(nav);
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

struct marker {
        struct cursor *cursor;
};


void
plugin_init (void)
{
  dbg (0, "spotify init\n");
  struct attr callback, navit;
  struct attr_iter *iter;
//  plugin_register_osd_type("spotify", osd_marker_new);
  callback.type = attr_callback;
  callback.u.callback =
    callback_new_attr_0 (callback_cast (spotify_navit), attr_navit);
  config_add_attr (config, &callback);
  iter = config_attr_iter_new ();
  while (config_get_attr (config, attr_navit, &navit, iter))
    spotify_navit_init (navit.u.navit);
  config_attr_iter_destroy (iter);
}
