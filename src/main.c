// https://github.com/dradtke/Spot

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <libspotify/api.h>
#include "audio.h"
#define DEBUG 1
 
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
 
void debug(const char *format, ...)
{
    if (!DEBUG)
        return;
 
    va_list argptr;
    va_start(argptr, format);
    vprintf(format, argptr);
    printf("\n");
}

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
 *
 */
static void SP_CALLCONV offline_status_updated(sp_session *sess)
{
        sp_offline_sync_status status;
        sp_offline_sync_get_status(sess, &status);
        if(status.syncing) {
                printf("Offline status: queued:%d:%zd done:%d:%zd copied:%d:%zd nocopy:%d err:%d\n",
                    status.queued_tracks,
                    (size_t)status.queued_bytes,
                    status.done_tracks,
                    (size_t)status.done_bytes,
                    status.copied_tracks,
                    (size_t)status.copied_bytes,
                    status.willnotcopy_tracks,
                    status.error_tracks);
        } else {
                printf("Offline status: Idle\n");
        }
}

/**
 * Called on various events to start playback if it hasn't been started already.
 *
 * The function simply starts playing the first track of the playlist.
 */
static void try_jukebox_start(void)
{
        sp_track *t;

        if (!g_jukeboxlist)
                printf("jukebox: No playlist. Waiting\n");
                // return;

        if (!sp_playlist_num_tracks(g_jukeboxlist)) {
                printf( "jukebox: No tracks in playlist. Waiting\n");
                return;
        }

        if (sp_playlist_num_tracks(g_jukeboxlist) < g_track_index) {
                printf("jukebox: No more tracks in playlist. Waiting\n");
                return;
        }

        t = sp_playlist_track(g_jukeboxlist, g_track_index);

        if (g_currenttrack && t != g_currenttrack) {
                /* Someone changed the current track */
                audio_fifo_flush(&g_audiofifo);
                sp_session_player_unload(g_sess);
                g_currenttrack = NULL;
        }

        if (!t)
                return;

        if (sp_track_error(t) != SP_ERROR_OK)
                return;

        if (g_currenttrack == t)
                return;

        g_currenttrack = t;

        printf("jukebox: Now playing \"%s\"...\n", sp_track_name(t));
        fflush(stdout);

        sp_session_player_load(g_sess, t);
        sp_session_player_play(g_sess, 1);
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
static void playlist_added(sp_playlistcontainer *pc, sp_playlist *pl,
                           int position, void *userdata)
{
        sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);
	printf("List name: %s\n",  sp_playlist_name(pl));

        if (!strcasecmp(sp_playlist_name(pl), g_listname)) {
                g_jukeboxlist = pl;
                try_jukebox_start();
        }
}

/**
 * Callback from libspotify, telling us the rootlist is fully synchronized
 * We just print an informational message
 *
 * @param  pc            The playlist container handle
 * @param  userdata      The opaque pointer
 */
static void container_loaded(sp_playlistcontainer *pc, void *userdata)
{
        fprintf(stderr, "jukebox: Rootlist synchronized (%d playlists)\n",
            sp_playlistcontainer_num_playlists(pc));
	try_jukebox_start();
}

/**
 * The playlist container callbacks
 */
static sp_playlistcontainer_callbacks pc_callbacks = {
        .playlist_added = &playlist_added,
//        .playlist_removed = &playlist_removed,
        .container_loaded = &container_loaded,
};

 
static void on_login(sp_session *session, sp_error error)
{
    debug("callback: on_login");
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Error: unable to log in: %s\n", sp_error_message(error));
        exit(1);
    }
 
    g_logged_in = 1;
    sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
    int i;

        sp_playlistcontainer_add_callbacks(
                pc,
                &pc_callbacks,
                NULL);

        printf("jukebox: Looking at %d playlists\n", sp_playlistcontainer_num_playlists(pc));

        for (i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
                sp_playlist *pl = sp_playlistcontainer_playlist(pc, i);

                sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);

                if (!strcasecmp(sp_playlist_name(pl), g_listname)) {
			printf("** ");
			switch (sp_playlist_get_offline_status(
				session,
				pl)) {
				case SP_PLAYLIST_OFFLINE_STATUS_NO 	:
				printf("Playlist is not offline enabled.\n");
				sp_playlist_set_offline_mode(session,pl,1);
				printf("  %d tracks to sync\n",
				                    sp_offline_tracks_to_sync(session));
				break;

				case SP_PLAYLIST_OFFLINE_STATUS_YES 	:
				printf("Playlist is synchronized to local storage.\n");
				break;

				case SP_PLAYLIST_OFFLINE_STATUS_DOWNLOADING 	:
				printf("This playlist is currently downloading. Only one playlist can be in this state any given time.\n");
				break;

				case SP_PLAYLIST_OFFLINE_STATUS_WAITING :
				printf("Playlist is queued for download.\n");
				break;

				default:
				printf("unknow state\n");
				break;
			}
                        g_jukeboxlist = pl;
                        try_jukebox_start();
                }
        }
	 if (!g_jukeboxlist) {
                printf("jukebox: No such playlist. Waiting for one to pop up...\n");
                fflush(stdout);
        }
	try_jukebox_start();
   
}

static void on_main_thread_notified(sp_session *session)
{
//    debug("callback: on_main_thread_notified");
}
 
static int on_music_delivered(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames)
{
    audio_fifo_t *af = &g_audiofifo;
    audio_fifo_data_t *afd;
    size_t s;
 
    if (num_frames == 0)
        return 0; // Audio discontinuity, do nothing
 
    pthread_mutex_lock(&af->mutex);
 
    /* Buffer one second of audio */
    if (af->qlen > format->sample_rate) {
        pthread_mutex_unlock(&af->mutex);
 
        return 0;
    }
 
    s = num_frames * sizeof(int16_t) * format->channels;
 
    afd = malloc(sizeof(*afd) + s);
    memcpy(afd->samples, frames, s);
 
    afd->nsamples = num_frames;
 
    afd->rate = format->sample_rate;
    afd->channels = format->channels;
 
    TAILQ_INSERT_TAIL(&af->q, afd, link);
    af->qlen += num_frames;
 
    pthread_cond_signal(&af->cond);
    pthread_mutex_unlock(&af->mutex);
 
    return num_frames;
}
 
static void on_log(sp_session *session, const char *data)
{
    // this method is *very* verbose, so this data should really be written out to a log file
}
 
static void on_end_of_track(sp_session *session)
{
    // g_playback_done = 1; 
    //
    ++g_track_index;
    try_jukebox_start();
} 

/**
 * Notification that some other connection has started playing on this account.
 * Playback has been stopped.
 *
 * @sa sp_session_callbacks#play_token_lost
 */
static void play_token_lost(sp_session *sess)
{
	printf("Playback has started elsewhere, pausing\n");
        audio_fifo_flush(&g_audiofifo);

        if (g_currenttrack != NULL) {
                sp_session_player_unload(g_sess);
                g_currenttrack = NULL;
        }
}

static sp_session_callbacks session_callbacks = {
    .logged_in = &on_login,
    .notify_main_thread = &on_main_thread_notified,
    .music_delivery = &on_music_delivered,
    .log_message = &on_log,
    .end_of_track = &on_end_of_track,
    .offline_status_updated = &offline_status_updated,
    .play_token_lost = &play_token_lost,
};

static sp_session_config spconfig = {
    .api_version = SPOTIFY_API_VERSION,
    .cache_location = "tmp",
    .settings_location = "tmp",
    .application_key = g_appkey,
    .application_key_size = 0, // set in main()
    .user_agent = "spot",
    .callbacks = &session_callbacks,
    NULL
};

 
int main(void)
{
    sp_error error;
    sp_session *session;
 
    // create the spotify session
    spconfig.application_key_size = g_appkey_size;
    error = sp_session_create(&spconfig, &session);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Error: unable to create spotify session: %s\n", sp_error_message(error));
        return 1;
    }
    g_sess = session;
 
    int next_timeout = 0;
 
    g_logged_in = 0;
    sp_session_login(session, username, password, 0, NULL);

    audio_init(&g_audiofifo);
    dbus_init();

    while (1) {
        sp_session_process_events(session, &next_timeout);
	dbus_process();
    }
 
    dbus_shutdown();
    return 0;
}
