add_library(asteroid-local-music main.cpp localplayback.cpp speakeramp.cpp)
set_target_properties(asteroid-local-music PROPERTIES PREFIX "")

asteroid_app_qml_module(asteroid-local-music
	main.qml
)

target_link_libraries(
	asteroid-local-music
	PUBLIC
		Asteroid::AsteroidApp6
		Qt6::Core
)

install(
	TARGETS asteroid-local-music
	DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
