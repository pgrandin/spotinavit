set(plugin_spotify_LIBS "-lspotify -lasound")
module_add_library(plugin_spotify audio.c spotify.c)
