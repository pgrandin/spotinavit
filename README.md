spotinavit
==========

An (experimental) Spotify plugin for Navit

* Clone the code to navit/plugin/spotify
* patch the CMakeLists.txt to enable the plugin: patch -p0 <CMakeLists.txt.patch 
* patch the plugin code to add attr parsing : patch -p0 < plugin.patch
* copy Findlibspotify.cmake to navit/cmake/
* copy keys.h.local to keys.h  and add your appkey in keys.h
* Enable the plugin in your navit.xml, don't forget to include your credentials:
 `<plugin path="libplugin_spotify.so" active="yes" spotify_login="me" spotify_password="secret" spotify_playlist="my_playlist"/>`

