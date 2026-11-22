add_library(asteroid-local-music main.cpp playbackclient.cpp)
set_target_properties(asteroid-local-music PROPERTIES PREFIX "")

asteroid_app_qml_module(asteroid-local-music
	main.qml
)

target_link_libraries(
	asteroid-local-music
	PUBLIC
		Asteroid::AsteroidApp6
		Qt6::Core
		Qt6::DBus
)

add_executable(asteroid-local-music-player player-main.cpp localplayback.cpp speakeramp.cpp)
set_target_properties(asteroid-local-music-player PROPERTIES AUTOMOC ON)
target_link_libraries(asteroid-local-music-player PRIVATE Qt6::Core Qt6::DBus)
install(TARGETS asteroid-local-music-player RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

install(
	TARGETS asteroid-local-music
	DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
