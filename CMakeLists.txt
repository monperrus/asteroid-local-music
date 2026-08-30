cmake_minimum_required(VERSION 3.6.0)

project(
	asteroid-local-music
	VERSION 2.0
	DESCRIPTION "Local music player for AsteroidOS"
)

find_package(ECM REQUIRED NO_MODULE)
find_package(AsteroidApp6 REQUIRED)

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ASTEROID_MODULE_PATH})

include(FeatureSummary)
include(GNUInstallDirs)
include(ECMFindQmlModule)
include(Asteroid6CMakeSettings)

add_subdirectory(src)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/asteroid-local-music.in
	${CMAKE_BINARY_DIR}/asteroid-local-music
	@ONLY
)

install(
	PROGRAMS ${CMAKE_BINARY_DIR}/asteroid-local-music
	DESTINATION ${CMAKE_INSTALL_BINDIR}
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/asteroid-local-music.desktop.template
	${CMAKE_BINARY_DIR}/asteroid-local-music.desktop
	@ONLY
)

install(
	FILES ${CMAKE_BINARY_DIR}/asteroid-local-music.desktop
	DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
