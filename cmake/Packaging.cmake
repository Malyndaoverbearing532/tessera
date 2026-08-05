# Application bundling and installer generation.
#
# On macOS this turns the plain executable into a tessera.app bundle and wraps it in
# a drag-to-Applications .dmg. Elsewhere it installs a normal binary and builds
# an archive (plus a .deb on Linux).

include(GNUInstallDirs)

# Turns the executable into a macOS .app. Must be called from the directory
# that created the target, because add_custom_command(TARGET) is scoped there.
function(tessera_configure_bundle target)
    # Off by default: a bundle buries the binary at Contents/MacOS/tessera, which is
    # a nuisance during development when the CLI modes are what you are testing.
    # The dmg preset turns it on.
    if(NOT APPLE OR NOT TESSERA_MACOS_BUNDLE)
        return()
    endif()

    set(icon ${CMAKE_SOURCE_DIR}/packaging/tessera.icns)
    if(NOT EXISTS ${icon})
        message(WARNING
            "packaging/tessera.icns is missing; the bundle will use the generic icon. "
            "Regenerate it with: python3 packaging/make_icon.py")
    endif()

    # Consumed by the Info.plist template through configure_file.
    set(MACOSX_BUNDLE_GUI_IDENTIFIER       "com.sarpsoykan.tessera")
    set(MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME      "tessera")
    set(MACOSX_BUNDLE_COPYRIGHT            "MIT licensed")

    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/Info.plist.in)

    # Listing the icon as a source is how it lands in Contents/Resources.
    if(EXISTS ${icon})
        target_sources(${target} PRIVATE ${icon})
        set_source_files_properties(${icon} PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
    endif()

    # Ad-hoc signature. Not a substitute for a Developer ID - it does not get
    # the app past Gatekeeper on someone else's machine - but an unsigned arm64
    # binary will not launch at all, so this is the minimum that works.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND codesign --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:${target}>"
        COMMENT "Ad-hoc signing tessera.app"
        VERBATIM)
endfunction()

# ---------------------------------------------------------------------------
# Install rules. Called from the top level once the target exists.
# ---------------------------------------------------------------------------
function(tessera_add_install_rules)
if(TESSERA_INSTALL)
    if(APPLE AND TESSERA_MACOS_BUNDLE)
        install(TARGETS tessera BUNDLE DESTINATION .)

        # The drag-to-install /Applications symlink is created by the DragNDrop
        # generator itself; staging one here as well makes cpack fail on the
        # collision.

        install(FILES ${CMAKE_SOURCE_DIR}/packaging/dmg-readme.txt
                DESTINATION . RENAME "Read Me.txt" OPTIONAL)
    else()
        install(TARGETS tessera RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES ${CMAKE_SOURCE_DIR}/README.md
                DESTINATION ${CMAKE_INSTALL_DOCDIR} OPTIONAL)
    endif()

    # -----------------------------------------------------------------------
    # CPack
    # -----------------------------------------------------------------------
    set(CPACK_PACKAGE_NAME tessera)
    set(CPACK_PACKAGE_VENDOR "Tessera")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_STRIP_FILES ON)

    if(APPLE AND TESSERA_MACOS_BUNDLE)
        set(CPACK_GENERATOR DragNDrop)
        set(CPACK_DMG_VOLUME_NAME "Tessera ${PROJECT_VERSION}")
        set(CPACK_DMG_FORMAT "UDZO")  # compressed, read-only

        # Name the artefact after what it actually contains, since a universal
        # build and a single-architecture one are not interchangeable.
        if(CMAKE_OSX_ARCHITECTURES MATCHES ";")
            set(TESSERA_ARCH_TAG "universal")
        elseif(CMAKE_OSX_ARCHITECTURES)
            set(TESSERA_ARCH_TAG "${CMAKE_OSX_ARCHITECTURES}")
        else()
            set(TESSERA_ARCH_TAG "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()
        set(CPACK_PACKAGE_FILE_NAME "tessera-${PROJECT_VERSION}-macOS-${TESSERA_ARCH_TAG}")
    elseif(APPLE)
        # A non-bundle macOS build is a plain CLI binary; ship it as an archive
        # rather than falling through to the Linux branch and emitting a .deb.
        set(CPACK_GENERATOR TGZ)
    elseif(WIN32)
        set(CPACK_GENERATOR ZIP)
    else()
        set(CPACK_GENERATOR "TGZ;DEB")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Tessera")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    endif()

    include(CPack)
endif()
endfunction()
