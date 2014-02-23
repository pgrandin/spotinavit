set(plugin_spotify_LIBS "-lspotify -lasound -lpthread")
module_add_library(plugin_spotify audio.c spotify.c alsa-audio.c)
