cmake_minimum_required(VERSION 3.16)
project(whisper-echo LANGUAGES C CXX)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_TAG_RESULT
    )
    if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^v?[0-9]")
        string(REGEX REPLACE "^v" "" PROJECT_VERSION "${GIT_TAG}")
    else()
        set(PROJECT_VERSION "0.1.0-dev")
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --long --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if(GIT_DESCRIBE_RESULT EQUAL 0)
        # Format: v0.1.0-5-gabc123 or 0.1.0-5-gabc123
        string(REGEX REPLACE "^v" "" GIT_DESCRIBE "${GIT_DESCRIBE}")
        string(REGEX MATCH "^[^ -]+" GIT_BASE "${GIT_DESCRIBE}")
        string(REGEX MATCH "-([0-9]+)-g" GIT_COMMIT_COUNT "${GIT_DESCRIBE}")
        if(GIT_COMMIT_COUNT)
            string(REGEX REPLACE ".*-([0-9]+)-g.*" "\\1" PROJECT_RELEASE "${GIT_COMMIT_COUNT}")
        else()
            set(PROJECT_RELEASE "1")
        endif()
        # Fallback if no tag
        if(NOT PROJECT_RELEASE)
            set(PROJECT_RELEASE "1")
        endif()
    else()
        set(PROJECT_RELEASE "1")
    endif()
else()
    set(PROJECT_VERSION "0.1.0-dev")
    set(PROJECT_RELEASE "1")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_REQUIRED ON)

find_package(SDL2 REQUIRED)

set(GGML_VULKAN ON CACHE BOOL "" FORCE)
set(GGML_CUDA   OFF CACHE BOOL "" FORCE)
set(GGML_METAL  OFF CACHE BOOL "" FORCE)
set(GGML_HIP    OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/whisper.cpp)

set(TARGET whisper-echo)
add_executable(${TARGET}
    src/stream.cpp
    src/audio_utils.cpp
    src/audio_capture.cpp
    src/commands.cpp
    src/uinput.cpp
)

target_include_directories(${TARGET} PRIVATE
    src
    vendor/whisper.cpp/include
    vendor/whisper.cpp/examples
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(${TARGET} PRIVATE whisper ${SDL2_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS ${TARGET} DESTINATION bin)
install(FILES command.json DESTINATION share/whisper-echo)
install(FILES setup_uinput.sh DESTINATION share/whisper-echo PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES docs/man/whisper-echo.1.gz DESTINATION share/man/man1)
install(FILES docs/uinput.md DESTINATION share/doc/whisper-echo)
install(FILES vendor/whisper.cpp/LICENSE DESTINATION share/licenses/whisper-echo RENAME whisper.cpp-LICENSE)

add_custom_target(dist
  COMMAND ${CMAKE_SOURCE_DIR}/tools/make_dist.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Creating source distribution tarball with submodules"
)

add_custom_target(update-changelog
  COMMAND ${CMAKE_SOURCE_DIR}/tools/update_changelog.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Updating debian/changelog from git tag"
)

set(CPACK_PACKAGE_NAME "whisper-echo")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "whisper-echo")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Real-time streaming speech-to-text with Whisper and VAD")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/description.txt")
set(CPACK_PACKAGE_LICENSE "MIT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/vendor/whisper.cpp/LICENSE")
set(CPACK_PACKAGE_CONTACT "Bryan Nielsen <bnielsen1965@gmail.com>")
set(CPACK_PACKAGE_URL "https://github.com/bnielsen1965/whisper-echo")
set(CPACK_GENERATOR "RPM;DEB")

set(CPACK_RPM_PACKAGE_NAME "whisper-echo")
set(CPACK_RPM_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RPM_PACKAGE_RELEASE "${PROJECT_RELEASE}")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_REQUIRES "sdl2 >= 2.0.0")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_URL "https://github.com/bnielsen1965/whisper-echo")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/usr/share;/usr/share/man;/usr/share/man/man1")

set(CPACK_DEBIAN_PACKAGE_NAME "whisper-echo")
set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${PROJECT_RELEASE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Bryan Nielsen <bnielsen1965@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "multimedia")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl2-2.0-0 (>= 2.0.0)")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_SOURCE_DIR}/packaging/deb/prerm")

include(CPack)
