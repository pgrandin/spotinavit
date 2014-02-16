spotinavit
==========

An (experimental) Spotify plugin for Navit

* Clone the code to navit/plugin/spotify
* patch the CMakeLists.txt using the patch in the plugin folder : patch -p0 <CMakeLists.txt.patch 
* copy Findlibspotify.cmake to navit/cmake/
* put your appkey in keys.h
* put your login/pass into keys.h, until it's configured via navit.xml
* Enable the plugin in your navit.xml

