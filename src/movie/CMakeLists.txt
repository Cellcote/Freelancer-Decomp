# CMake build of movie.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(movie DLL
  SOURCES
    MediaPlayer.cpp
    MovieSha.cpp
    MovieMod.cpp
    MovieAce.cpp
    MovieExp.cpp
    MovieField.cpp
    MovieFieldHigh.cpp
    MovieFieldMid.cpp
    MovieGraph.cpp
    MovieRuntime.cpp
    MovieCca.cpp
    MovieWindow.cpp
    MovieD4.cpp
    MovieWide.cpp
    MoviePlay.cpp
    MovieObject.cpp
)

fl_link(movie LIBS ole32.lib user32.lib)
