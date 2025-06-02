# Add GLFW library

# We first try to find glfw in the system
if(NOT BOTCRAFT_FORCE_LOCAL_GLFW)
    find_package(glfw3 3.4 QUIET)
endif()

# If not found, build from sources
if(NOT TARGET glfw)
    set(GLFW_SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/glfw/")
    set(GLFW_BUILD_PATH "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/glfw")

    file(GLOB RESULT "${GLFW_BUILD_PATH}/install")
    list(LENGTH RESULT RES_LEN)
    if(RES_LEN EQUAL 0)
        message(STATUS "Can't find GLFW, installing it locally...")

        file(GLOB RESULT "${GLFW_SRC_PATH}")
        list(LENGTH RESULT RES_LEN)
        if(RES_LEN EQUAL 0)
            execute_process(COMMAND git submodule update --init -- 3rdparty/glfw WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()

        file(MAKE_DIRECTORY "${GLFW_BUILD_PATH}")

        execute_process(
            COMMAND "${CMAKE_COMMAND}" "${GLFW_SRC_PATH}" "-G" "${CMAKE_GENERATOR}" "-A" "${CMAKE_GENERATOR_PLATFORM}" "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" "-DCMAKE_BUILD_TYPE=Release" "-DGLFW_BUILD_EXAMPLES=OFF" "-DGLFW_BUILD_TESTS=OFF" "-DGLFW_BUILD_DOCS=OFF" "-DGLFW_INSTALL=ON" "-DUSE_MSVC_RUNTIME_LIBRARY_DLL=OFF" "-DCMAKE_INSTALL_PREFIX=install"
            WORKING_DIRECTORY "${GLFW_BUILD_PATH}")

        execute_process(COMMAND "${CMAKE_COMMAND}" "--build" "." "--target" "install" "--parallel" "2" "--config" "Release" WORKING_DIRECTORY "${GLFW_BUILD_PATH}")
    endif()


    # Find the freshly built library
    find_package(glfw3 3.4 REQUIRED PATHS "${GLFW_BUILD_PATH}/install/${CMAKE_INSTALL_LIBDIR}/cmake/glfw3")
endif()
