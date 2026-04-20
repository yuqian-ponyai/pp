cmake_minimum_required(VERSION 3.16)

# macOS deployment target must be set BEFORE project() so the compiler picks
# it up. We pin to 13.0 to match the LSMinimumSystemVersion in
# data/macos/Info.plist.in. Without this, the binary bakes in whatever the
# current SDK targets (e.g. macOS 26 on Tahoe), which makes Text Input
# Services treat the bundle as version-mismatched and silently refuses to
# enumerate it in System Settings → Keyboard → Input Sources.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")
endif()

project(predictable_pinyin VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)
enable_testing()
include(FetchContent)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RIME REQUIRED rime)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.13.0
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(ParseAndAddCatchTests)

add_library(pp_core
  src/frequency_sorter.cc
  src/pinyin_trie.cc
  src/predictable_state_machine.cc
  src/stroke_filter.cc
  src/rime_session.cc
)
set_target_properties(pp_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(pp_core
  PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    ${RIME_INCLUDE_DIRS}
)

target_compile_options(pp_core PRIVATE ${RIME_CFLAGS_OTHER})
target_link_directories(pp_core PUBLIC ${RIME_LIBRARY_DIRS})
target_link_libraries(pp_core PUBLIC ${RIME_LIBRARIES})

add_executable(predictable_pinyin_cli src/main.cc)
target_link_libraries(predictable_pinyin_cli PRIVATE pp_core)
install(TARGETS predictable_pinyin_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT common)

add_executable(pp_core_tests
  tests/pinyin_trie_test.cc
  tests/predictable_state_machine_test.cc
  tests/stroke_filter_test.cc
  tests/frequency_sorter_test.cc
  tests/integration_test.cc
  src/frequency_sorter.cc
  src/predictable_state_machine.cc
  src/pinyin_trie.cc
  src/stroke_filter.cc
)
target_include_directories(pp_core_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  ${RIME_INCLUDE_DIRS})
target_link_directories(pp_core_tests PRIVATE ${RIME_LIBRARY_DIRS})
target_link_libraries(pp_core_tests PRIVATE Catch2::Catch2WithMain ${RIME_LIBRARIES})

if(BUILD_TESTING)
  set(PARSE_CATCH_TESTS_ADD_TARGET_IN_TEST_NAME OFF)
  ParseAndAddCatchTests(pp_core_tests)
endif()

install(FILES
  rime/predictable_pinyin.schema.yaml
  DESTINATION "${CMAKE_INSTALL_DATADIR}/rime-data"
  COMPONENT common)

install(FILES
  data/raw/stroke.dict.yaml
  data/raw/hanzi_db.csv
  data/raw/pinyin_simp.prism.txt
  data/raw/pinyin_simp.dict.yaml
  DESTINATION "${CMAKE_INSTALL_DATADIR}/predictable-pinyin"
  COMPONENT common)

install(FILES data/icons/predictable-pinyin.svg
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
  COMPONENT common)
install(FILES data/icons/predictable-pinyin.svg
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/status"
  COMPONENT common)
install(FILES data/icons/predictable-pinyin.png
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps"
  COMPONENT common)

# --- ibus engine (optional) ------------------------------------------------

pkg_check_modules(IBUS ibus-1.0)

if(IBUS_FOUND)
  set(IBUS_ENGINE_EXEC_PATH
      "${CMAKE_INSTALL_FULL_LIBEXECDIR}/ibus-engine-predictable-pinyin")
  set(IBUS_PP_ICON_PATH
      "${CMAKE_INSTALL_FULL_DATADIR}/predictable-pinyin/icons/predictable-pinyin.svg")

  configure_file(
    data/ibus/predictable-pinyin.xml.in
    "${CMAKE_CURRENT_BINARY_DIR}/predictable-pinyin.xml"
    @ONLY)

  add_executable(ibus-engine-predictable-pinyin src/ibus_plugin.cc)
  target_link_libraries(ibus-engine-predictable-pinyin
    PRIVATE pp_core ${IBUS_LIBRARIES})
  target_include_directories(ibus-engine-predictable-pinyin
    PRIVATE ${IBUS_INCLUDE_DIRS})

  install(TARGETS ibus-engine-predictable-pinyin
          RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}"
          COMPONENT ibus)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/predictable-pinyin.xml"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/ibus/component"
          COMPONENT ibus)
  install(FILES data/icons/predictable-pinyin.svg
          DESTINATION "${CMAKE_INSTALL_DATADIR}/predictable-pinyin/icons"
          COMPONENT ibus)
else()
  message(STATUS "libibus-1.0-dev not found: skipping ibus target")
endif()

# --- macOS input method (optional) ------------------------------------------

if(APPLE)
  find_library(INPUT_METHOD_KIT_FRAMEWORK InputMethodKit REQUIRED)
  find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
  find_library(CARBON_FRAMEWORK Carbon REQUIRED)

  set(MACOS_APP_NAME "PredictablePinyin")
  configure_file(
    data/macos/Info.plist.in
    "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    @ONLY)

  add_executable(${MACOS_APP_NAME} MACOSX_BUNDLE src/macos_plugin.mm)
  set_source_files_properties(src/macos_plugin.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc")
  target_link_libraries(${MACOS_APP_NAME}
    PRIVATE pp_core
      ${INPUT_METHOD_KIT_FRAMEWORK}
      ${APPKIT_FRAMEWORK}
      ${FOUNDATION_FRAMEWORK}
      ${CARBON_FRAMEWORK})
  set_target_properties(${MACOS_APP_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "${MACOS_APP_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "im.predictablepinyin.inputmethod.PredictablePinyin")

  # Stage data files into the bundle at build time so ./build/<app>.app is
  # directly runnable without a separate install step.
  set(_pp_bundle_ss
    "$<TARGET_BUNDLE_CONTENT_DIR:${MACOS_APP_NAME}>/SharedSupport")
  set(_pp_bundle_res
    "$<TARGET_BUNDLE_CONTENT_DIR:${MACOS_APP_NAME}>/Resources")
  add_custom_command(TARGET ${MACOS_APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_pp_bundle_ss}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_pp_bundle_res}"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_SOURCE_DIR}/data/raw/stroke.dict.yaml"
      "${CMAKE_SOURCE_DIR}/data/raw/hanzi_db.csv"
      "${CMAKE_SOURCE_DIR}/data/raw/pinyin_simp.prism.txt"
      "${CMAKE_SOURCE_DIR}/data/raw/pinyin_simp.dict.yaml"
      "${CMAKE_SOURCE_DIR}/rime/predictable_pinyin.schema.yaml"
      "${CMAKE_SOURCE_DIR}/data/macos/default.yaml"
      "${CMAKE_SOURCE_DIR}/data/macos/symbols.yaml"
      "${CMAKE_SOURCE_DIR}/data/macos/stroke.schema.yaml"
      "${_pp_bundle_ss}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/data/macos/PredictablePinyin.icns"
      "${_pp_bundle_res}/PredictablePinyin.icns"
    VERBATIM)
endif()

# --- license/notice files ----------------------------------------------------

# Install LICENSE and THIRD_PARTY_LICENSES under /usr/share/doc/predictable-pinyin/
# so they travel with both the CLI and the IBus engine packages. Both components
# ship the same docs so either .deb can be installed standalone and still
# satisfy Debian policy on copyright.
set(PP_DOC_DIR "${CMAKE_INSTALL_DATADIR}/doc/predictable-pinyin")
foreach(_pp_doc_component common ibus)
  install(FILES LICENSE THIRD_PARTY_LICENSES
          DESTINATION "${PP_DOC_DIR}"
          COMPONENT ${_pp_doc_component})
endforeach()

# --- packaging (see scripts/build-deb.sh) ------------------------------------

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_VENDOR "Pony AI Inc.")
set(CPACK_PACKAGE_CONTACT "Pony AI Inc.")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

include(CPack)
