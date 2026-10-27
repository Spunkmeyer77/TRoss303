set_property(DIRECTORY PROPERTY max::external YES)
set(AUTHOR_DOMAIN "com.ticcoross")
set(COPYRIGHT_STRING "Copyright (c) 2026 Ticco Ross. AGPL-3.0. TB-303 voice from Lemus.")
set(GIT_VERSION_TAG "1.0.0")

project(tross303_tilde C CXX)

target_sources(tross303_tilde PRIVATE tross303~.cpp ${TROSS_ENGINE_SOURCES})
target_include_directories(tross303_tilde PRIVATE ${TROSS_ENGINE_INCLUDES})
if(MSVC)
	target_compile_options(tross303_tilde PRIVATE /O2 /utf-8)
else()
	target_compile_options(tross303_tilde PRIVATE -O3)
endif()
